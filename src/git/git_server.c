/*
 * git_server.c - the git server
 *
 * A tiny web server on this computer only, speaking git's "smart HTTP"
 * protocol, so git clone, fetch, pull and push work. Pushing (and reading
 * private repositories) needs a Mini GitHub username and password.
 */

#include "git_server.h"
#include "app_state.h"
#include "byte_buffer.h"
#include "compression.h"
#include "forks.h"
#include "git_pack_read.h"
#include "git_pack_write.h"
#include "git_receive.h"
#include "model.h"
#include "net.h"
#include "passwords.h"
#include "repo.h"
#include "sha1.h"
#include "sha256.h"
#include "tab_code.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"

/* ------------------------------------------------------------------ */
/* Talking to real git                                                */
/*                                                                    */
/* Mini GitHub runs a tiny web server on this computer only           */
/* (http://localhost:7979), speaking git's "smart HTTP" language:     */
/*   git clone http://localhost:7979/jo/hello-world.git               */
/*   git push / git pull / git fetch                                  */
/* Pushing needs a Mini GitHub username and password, and only the    */
/* owner and collaborators may push.                                  */
/*                                                                    */
/* git sends and receives commits as a "pack": every object (commit,  */
/* tree, blob) squeezed with zlib, one after another. Inside a pack,  */
/* an object can also be a "delta": "take that other object, copy     */
/* these bits of it and add these new bytes" - which saves space.     */
/* ------------------------------------------------------------------ */

/* ---------- pkt-lines ----------
   git's messages are "pkt-lines": 4 hex digits giving the length
   (including those 4), then the text. "0000" means "end of this part". */
static void PktWrite(ByteBuffer *out, const char *text, size_t length)
{
    char lengthHex[5];
    snprintf(lengthHex, sizeof lengthHex, "%04x", (unsigned)(length + 4));
    BufferAppend(out, lengthHex, 4);
    BufferAppend(out, text, length);
}
static void PktWriteText(ByteBuffer *out, const char *text) { PktWrite(out, text, strlen(text)); }
static void PktFlush(ByteBuffer *out) { BufferAppend(out, "0000", 4); }

/* Reads the next pkt-line. Returns its length (-1 = a flush "0000", -2 = damaged or the end). */
static int PktRead(const uint8_t *data, size_t length, size_t *at, const char **line)
{
    if (*at + 4 > length) return -2;
    int lineLength = 0;
    for (int i = 0; i < 4; i++) {
        if (!isxdigit(data[*at + i])) return -2;
        lineLength = lineLength * 16 + HexDigitValue((char)data[*at + i]);
    }
    *at += 4;
    if (lineLength == 0) return -1;
    if (lineLength < 4 || *at + (size_t)(lineLength - 4) > length) return -2;
    *line = (const char *)data + *at;
    *at += (size_t)(lineLength - 4);
    lineLength -= 4;
    if (lineLength > 0 && (*line)[lineLength - 1] == '\n') lineLength--;  /* the newline isn't part of it */
    return lineLength;
}

/* ---------- the web server ----------
   It runs inside the normal frame loop: each frame it takes in whatever
   has arrived and sends what it can, without ever waiting. */
#define GIT_PORT  7979
#define MAX_CONNECTIONS 16
typedef struct {
    bool inUse; Socket socket; ByteBuffer request, response; size_t bytesSent; bool replied, sentContinue; double lastActivity;
    int headerLength;                    /* bytes of the request's header, once it's all arrived */
} Connection;
static Connection connections[MAX_CONNECTIONS];
static Socket gitListenSocket = NO_SOCKET;
static int  gitPushes, gitClones;        /* since the program started */

/* Starts listening for git on localhost (port 7979, or the next free one). Sets gitPort, or
   gitProblem if it can't. */
void GitServerStart(void)
{
    if (!NetworkInit()) { snprintf(gitProblem, sizeof gitProblem, "this computer's network functions couldn't be loaded"); return; }
    for (int port = GIT_PORT; port < GIT_PORT + 10; port++) {
        gitListenSocket = SocketListen(port);
        if (gitListenSocket != NO_SOCKET) { gitPort = port; return; }
    }
    snprintf(gitProblem, sizeof gitProblem, "ports %d to %d are all in use by other programs", GIT_PORT, GIT_PORT + 9);
}
/* Closes every connection and stops listening. */
void GitServerStop(void)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) if (connections[i].inUse) { SocketClose(connections[i].socket); BufferFree(&connections[i].request); BufferFree(&connections[i].response); }
    if (gitListenSocket != NO_SOCKET) SocketClose(gitListenSocket);
    gitListenSocket = NO_SOCKET;
    gitPort = 0;
}
/* The address git uses for a repository. */
const char *CloneUrl(Repo *repo)
{
    return TextFormat("http://localhost:%d/%s/%s.git", gitPort ? gitPort : GIT_PORT, UserName(repo->owner), repo->name);
}

/* What arrived: just the parts Mini GitHub needs. */
typedef struct {
    char method[8], path[512], query[256];
    long long contentLength;
    bool chunked, gzip, expectsContinue;
    char authorization[512], host[128];
    const uint8_t *body; size_t bodyLength;
    uint8_t *bodyOwned;                  /* if the body had to be put back together */
} HttpRequest;

static const char *HttpStatusText(int code)
{
    switch (code) {
    case 100: return "Continue";        case 200: return "OK";
    case 400: return "Bad Request";     case 401: return "Unauthorized";
    case 403: return "Forbidden";       case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 413: return "Payload Too Large";
    default:  return "Internal Server Error";
    }
}
static void SendReply(Connection *conn, int code, const char *type, const uint8_t *body, size_t length, const char *extra)
{
    char header[640];
    int headerLength = snprintf(header, sizeof header,
                      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\nCache-Control: no-cache\r\n"
                      "Connection: close\r\nServer: Mini-GitHub\r\n%s\r\n",
                      code, HttpStatusText(code), type, (unsigned long long)length, extra ? extra : "");
    BufferAppend(&conn->response, header, (size_t)headerLength);
    BufferAppend(&conn->response, body, length);
    conn->replied = true;
}
static void SendTextReply(Connection *conn, int code, const char *text, const char *extra)
{
    SendReply(conn, code, "text/plain; charset=utf-8", (const uint8_t *)text, strlen(text), extra);
}

/* Header lines are "Name: value"; case doesn't matter in the name. */
static bool HeaderMatches(const char *line, int length, const char *name, const char **value, int *valueLength)
{
    int nameLength = (int)strlen(name);
    if (length <= nameLength || line[nameLength] != ':') return false;
    for (int i = 0; i < nameLength; i++) if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i])) return false;
    int valueStart = nameLength + 1;
    while (valueStart < length && line[valueStart] == ' ') valueStart++;
    *value = line + valueStart;
    *valueLength = length - valueStart;
    return true;
}
static bool HeaderValueHas(const char *value, int length, const char *word)
{
    int wordLength = (int)strlen(word);
    for (int i = 0; i + wordLength <= length; i++) {
        int j = 0;
        while (j < wordLength && tolower((unsigned char)value[i + j]) == word[j]) j++;
        if (j == wordLength) return true;
    }
    return false;
}

/* Reads the request line and headers. false if they don't make sense. */
static bool ParseRequestHead(const uint8_t *data, int length, HttpRequest *request)
{
    memset(request, 0, sizeof *request);
    request->contentLength = -1;
    int pos = 0, lineNumber = 0;
    while (pos < length) {
        int lineEnd = pos;
        while (lineEnd + 1 < length && !(data[lineEnd] == '\r' && data[lineEnd + 1] == '\n')) lineEnd++;
        const char *lineText = (const char *)data + pos;
        int lineLength = lineEnd - pos;
        if (lineLength == 0) break;
        if (lineNumber++ == 0) {
            /* the request line on its own, ending in a zero: the received bytes don't end
               in one, and sscanf would otherwise read on into the next lines (or past the
               end of what arrived) when the line is malformed */
            char *requestLine = malloc((size_t)lineLength + 1);
            if (!requestLine) return false;
            memcpy(requestLine, lineText, (size_t)lineLength);
            requestLine[lineLength] = 0;
            char target[800];
            int parts = sscanf(requestLine, "%7s %799s", request->method, target);
            free(requestLine);
            if (parts != 2) return false;
            char *questionMark = strchr(target, '?');
            if (questionMark) { snprintf(request->query, sizeof request->query, "%s", questionMark + 1); *questionMark = 0; }
            /* undo %20-style escapes */
            int j = 0;
            for (int i = 0; target[i] && j < (int)sizeof request->path - 1; i++) {
                if (target[i] == '%' && isxdigit((unsigned char)target[i + 1]) && isxdigit((unsigned char)target[i + 2])) {
                    request->path[j++] = (char)(HexDigitValue(target[i + 1]) * 16 + HexDigitValue(target[i + 2])); i += 2;
                } else request->path[j++] = target[i];
            }
            request->path[j] = 0;
        } else {
            const char *value;
            int valueLength;
            if (HeaderMatches(lineText, lineLength, "Content-Length", &value, &valueLength)) {
                char number[32];                                  /* ending in a zero, so strtoll stops at the line's end */
                snprintf(number, sizeof number, "%.*s", valueLength, value);
                request->contentLength = strtoll(number, NULL, 10);
            }
            else if (HeaderMatches(lineText, lineLength, "Transfer-Encoding", &value, &valueLength)) request->chunked = HeaderValueHas(value, valueLength, "chunked");
            else if (HeaderMatches(lineText, lineLength, "Content-Encoding", &value, &valueLength)) request->gzip = HeaderValueHas(value, valueLength, "gzip");
            else if (HeaderMatches(lineText, lineLength, "Expect", &value, &valueLength)) request->expectsContinue = HeaderValueHas(value, valueLength, "100-continue");
            else if (HeaderMatches(lineText, lineLength, "Authorization", &value, &valueLength)) snprintf(request->authorization, sizeof request->authorization, "%.*s", valueLength, value);
            else if (HeaderMatches(lineText, lineLength, "Host", &value, &valueLength)) snprintf(request->host, sizeof request->host, "%.*s", valueLength, value);
        }
        pos = lineEnd + 2;
    }
    return lineNumber > 0;
}

/* "chunked": the body arrives in pieces, each with its size in hex first.
   Returns 1 when it's all there (and puts it together), 0 if not yet, -1 if damaged. */
static int DecodeChunkedBody(const uint8_t *data, size_t length, HttpRequest *request)
{
    size_t pos = 0;
    ByteBuffer body = {0};
    for (;;) {
        size_t lineEnd = pos;
        while (lineEnd + 1 < length && !(data[lineEnd] == '\r' && data[lineEnd + 1] == '\n')) lineEnd++;
        if (lineEnd + 1 >= length) { BufferFree(&body); return 0; }
        unsigned long long chunkSize = 0;
        size_t i = pos;
        if (i == lineEnd || !isxdigit(data[i])) { BufferFree(&body); return -1; }
        while (i < lineEnd && isxdigit(data[i])) { chunkSize = chunkSize * 16 + (unsigned long long)HexDigitValue((char)data[i]); i++; if (chunkSize > MAX_PUSH) { BufferFree(&body); return -1; } }
        pos = lineEnd + 2;
        if (chunkSize == 0) {                                /* the end (skip any trailer lines) */
            for (;;) {
                size_t trailerEnd = pos;
                while (trailerEnd + 1 < length && !(data[trailerEnd] == '\r' && data[trailerEnd + 1] == '\n')) trailerEnd++;
                if (trailerEnd + 1 >= length) { BufferFree(&body); return 0; }
                if (trailerEnd == pos) break;
                pos = trailerEnd + 2;
            }
            BufferAppendByte(&body, 0);
            if (body.outOfMemory) { BufferFree(&body); return -1; }
            request->bodyOwned = body.data;
            request->body = body.data;
            request->bodyLength = body.length - 1;
            return 1;
        }
        if (pos + chunkSize + 2 > length) { BufferFree(&body); return 0; }
        BufferAppend(&body, data + pos, (size_t)chunkSize);
        pos += (size_t)chunkSize + 2;
    }
}

/* ---------- who's asking ----------
   git sends "Authorization: Basic <base64 of name:password>". Checking a
   password takes a moment (that's what makes guessing slow), so a correct
   one is remembered for a few minutes. */
static int Base64DigitValue(int ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+' || ch == '-') return 62;
    if (ch == '/' || ch == '_') return 63;
    return -1;
}
static struct { uint8_t credentialHash[32]; int userId; double validUntil; } recentLogins[8];
static double authBlockedUntil;
static int authFailures;

/* The user id if the request carries a right name + password, else -1. */
static int Authenticate(HttpRequest *request)
{
    if (strncmp(request->authorization, "Basic ", 6) != 0) return -1;
    uint8_t credentialHash[32];
    Sha256 sha;
    Sha256Init(&sha); Sha256Update(&sha, request->authorization, strlen(request->authorization)); Sha256Final(&sha, credentialHash);
    double now = GetTime();
    for (int i = 0; i < 8; i++)
        if (recentLogins[i].validUntil > now && memcmp(recentLogins[i].credentialHash, credentialHash, 32) == 0 && FindUserById(recentLogins[i].userId)) return recentLogins[i].userId;
    if (now < authBlockedUntil) return -1;                  /* too many wrong passwords: wait a little */

    char decoded[400];
    int decodedLength = 0;
    unsigned accumulated = 0;
    int bitCount = 0;
    for (const char *in = request->authorization + 6; *in && decodedLength < (int)sizeof decoded - 1; in++) {
        int value = Base64DigitValue((unsigned char)*in);
        if (value < 0) continue;
        accumulated = accumulated << 6 | (unsigned)value; bitCount += 6;
        if (bitCount >= 8) { bitCount -= 8; decoded[decodedLength++] = (char)((accumulated >> bitCount) & 0xFF); }
    }
    decoded[decodedLength] = 0;
    char *colon = strchr(decoded, ':');
    int userId = -1;
    if (colon) {
        *colon = 0;
        User *user = FindUserByName(decoded);
        if (user && CheckPassword(user, colon + 1)) userId = user->id;
    }
    memset(decoded, 0, sizeof decoded);
    if (userId < 0) {
        if (++authFailures >= 5) { authBlockedUntil = now + 10; authFailures = 0; }
        return -1;
    }
    authFailures = 0;
    int slot = 0;
    for (int i = 1; i < 8; i++) if (recentLogins[i].validUntil < recentLogins[slot].validUntil) slot = i;
    memcpy(recentLogins[slot].credentialHash, credentialHash, 32);
    recentLogins[slot].userId = userId;
    recentLogins[slot].validUntil = now + 300;
    return userId;
}
static void AskForPassword(Connection *conn)
{
    SendTextReply(conn, 401, "Mini GitHub: sign in with your Mini GitHub username and password.\n",
              "WWW-Authenticate: Basic realm=\"Mini GitHub\"\r\n");
}

/* ---------- git's requests ---------- */
#define GIT_AGENT "agent=mini-github/1.0"

/* "GET /owner/repo.git/info/refs?service=git-upload-pack": the list of branches. */
static void SendRefList(Connection *conn, HttpRequest *request, Repo *repo, bool forPush)
{
    ByteBuffer reply = {0};
    char hex[41], line[512];
    PktWriteText(&reply, forPush ? "# service=git-receive-pack\n" : "# service=git-upload-pack\n");
    PktFlush(&reply);
    char capabilities[256];
    if (forPush) snprintf(capabilities, sizeof capabilities, "report-status delete-refs no-thin ofs-delta %s", GIT_AGENT);
    else      snprintf(capabilities, sizeof capabilities, "multi_ack_detailed symref=HEAD:refs/heads/%s %s", DefaultBranchName(repo), GIT_AGENT);
    if (!repo->branchCount) {
        int length = snprintf(line, sizeof line, "%040d capabilities^{}%c%s\n", 0, 0, capabilities);
        PktWrite(&reply, line, (size_t)length);
    } else {
        /* the default branch as HEAD (for clone), then every branch, in name order */
        int order[MAX_BRANCHES];
        for (int i = 0; i < repo->branchCount; i++) order[i] = i;
        for (int i = 0; i < repo->branchCount; i++)
            for (int j = i + 1; j < repo->branchCount; j++)
                if (strcmp(repo->branches[order[j]].name, repo->branches[order[i]].name) < 0) { int swap = order[i]; order[i] = order[j]; order[j] = swap; }
        bool isFirst = true;
        if (!forPush) {
            ShaToHex(repo->commits[DefaultTip(repo)].sha, hex);
            int length = snprintf(line, sizeof line, "%s HEAD%c%s\n", hex, 0, capabilities);
            PktWrite(&reply, line, (size_t)length);
            isFirst = false;
        }
        for (int i = 0; i < repo->branchCount; i++) {
            Branch *branch = &repo->branches[order[i]];
            ShaToHex(repo->commits[branch->tip].sha, hex);
            int length = isFirst ? snprintf(line, sizeof line, "%s refs/heads/%s%c%s\n", hex, branch->name, 0, capabilities)
                          : snprintf(line, sizeof line, "%s refs/heads/%s\n", hex, branch->name);
            PktWrite(&reply, line, (size_t)length);
            isFirst = false;
        }
    }
    PktFlush(&reply);
    (void)request;
    SendReply(conn, 200, forPush ? "application/x-git-receive-pack-advertisement" : "application/x-git-upload-pack-advertisement", reply.data, reply.length, NULL);
    BufferFree(&reply);
}

/* "POST /owner/repo.git/git-upload-pack": clone, fetch and pull.
   git says which commits it wants, then (in rounds) which it already has;
   Mini GitHub says "ACK" to each one it knows too, and once git says
   "done", answers with a pack of everything missing. This is git's
   "multi_ack_detailed" way of talking, which is what git expects over HTTP. */
static void HandleUploadPack(Connection *conn, HttpRequest *request, Repo *repo)
{
    const uint8_t *data = request->body;
    size_t length = request->bodyLength, pos = 0, unzippedLength = 0;
    uint8_t *unzipped = NULL;
    if (request->gzip) {
        unzipped = InflateGzip(data, length, &unzippedLength, MAX_PUSH);
        if (!unzipped) { SendTextReply(conn, 400, "couldn't unpack the request\n", NULL); return; }
        data = unzipped; length = unzippedLength;
    }
    int *wants = NULL, wantCount = 0, wantCapacity = 0, *haves = NULL, haveCount = 0, haveCapacity = 0;
    const char *line;
    int lineLength;
    bool gotDone = false, unknownWant = false;
    ByteBuffer reply = {0};
    /* the wants, up to a flush */
    while ((lineLength = PktRead(data, length, &pos, &line)) >= 0) {
        uint8_t sha[20];
        if (lineLength >= 45 && strncmp(line, "want ", 5) == 0 && ParseShaHex(line + 5, sha)) {
            int commitIndex = CommitBySha(repo, sha);
            if (commitIndex < 0) { unknownWant = true; break; }
            wants = GrowArray(wants, &wantCapacity, wantCount + 1, sizeof(int));
            if (wantCount < wantCapacity) wants[wantCount++] = commitIndex;
        }
    }
    if (unknownWant) { PktWriteText(&reply, TextFormat("ERR upload-pack: not our ref\n")); }
    else if (lineLength == -1 && wantCount) {
        /* then what they have */
        char lastHex[41] = "";
        while ((lineLength = PktRead(data, length, &pos, &line)) != -2) {
            if (lineLength == -1) { PktWriteText(&reply, "NAK\n"); break; }                   /* the end of a round, without "done" */
            uint8_t sha[20];
            if (lineLength >= 45 && strncmp(line, "have ", 5) == 0 && ParseShaHex(line + 5, sha)) {
                int commitIndex = CommitBySha(repo, sha);
                if (commitIndex < 0) continue;
                haves = GrowArray(haves, &haveCapacity, haveCount + 1, sizeof(int));
                if (haveCount < haveCapacity) haves[haveCount++] = commitIndex;
                ShaToHex(sha, lastHex);
                PktWriteText(&reply, TextFormat("ACK %s common\n", lastHex));
            } else if (lineLength == 4 && strncmp(line, "done", 4) == 0) { gotDone = true; break; }
        }
        if (gotDone) {
            if (haveCount) PktWriteText(&reply, TextFormat("ACK %s\n", lastHex));
            else    PktWriteText(&reply, "NAK\n");
            if (!BuildPack(repo, wants, wantCount, haves, haveCount, &reply)) { BufferFree(&reply); SendTextReply(conn, 500, "out of memory\n", NULL); goto cleanup; }
            gitClones++;
        }
    }
    SendReply(conn, 200, "application/x-git-upload-pack-result", reply.data ? reply.data : (const uint8_t *)"", reply.length, NULL);
    BufferFree(&reply);
cleanup:
    free(wants); free(haves); free(unzipped);
}

/* "POST /owner/repo.git/git-receive-pack": git push.
   First the branch updates ("old new refs/heads/main"), then a pack. */
static void HandleReceivePack(Connection *conn, HttpRequest *request, Repo *repo, int userId)
{
    const uint8_t *data = request->body;
    size_t length = request->bodyLength, pos = 0;
    typedef struct { uint8_t oldSha[20], newSha[20]; char refName[256]; } RefUpdate;
    RefUpdate updates[16];
    int updateCount = 0;
    const char *line;
    int lineLength;
    while ((lineLength = PktRead(data, length, &pos, &line)) >= 0) {
        if (updateCount >= 16) continue;
        int lineEnd = 0;
        while (lineEnd < lineLength && line[lineEnd]) lineEnd++;  /* the first line has extra settings after a zero byte */
        if (lineEnd < 83 || line[40] != ' ' || line[81] != ' ') continue;
        if (!ParseShaHex(line, updates[updateCount].oldSha) || !ParseShaHex(line + 41, updates[updateCount].newSha)) continue;
        snprintf(updates[updateCount].refName, sizeof updates[updateCount].refName, "%.*s", lineEnd - 82, line + 82);
        updateCount++;
    }
    if (!updateCount) { SendReply(conn, 200, "application/x-git-receive-pack-result", (const uint8_t *)"", 0, NULL); return; }

    ByteBuffer reply = {0};
    PackContents pack;
    memset(&pack, 0, sizeof pack);
    const char *unpackError = NULL;
    if (pos < length) unpackError = ReadPack(data + pos, length - pos, &pack, repo);
    if (unpackError) {
        PktWriteText(&reply, TextFormat("unpack %s\n", unpackError));
        for (int i = 0; i < updateCount; i++) PktWriteText(&reply, TextFormat("ng %s unpacker error\n", updates[i].refName));
    } else {
        PktWriteText(&reply, "unpack ok\n");
        int totalAdded = 0;
        bool anyChanged = false;
        for (int i = 0; i < updateCount; i++) {
            const char *refusal = NULL;
            int added = 0;
            if (!CanEditBy(repo, userId))
                refusal = TextFormat("%s can't push here - only %s and invited collaborators can", UserName(userId), UserName(repo->owner));
            if (!refusal) refusal = ApplyPush(repo, userId, updates[i].oldSha, updates[i].newSha, updates[i].refName, &pack, &added);
            if (refusal) PktWriteText(&reply, TextFormat("ng %s %s\n", updates[i].refName, refusal));
            else     PktWriteText(&reply, TextFormat("ok %s\n", updates[i].refName));
            if (added) anyChanged = true;
            if (added > 0) totalAdded += added;
        }
        if (anyChanged) {
            MarkDirty();
            ForgetScratchFile();
            gitPushes++;
            if (totalAdded) Toast(TextFormat("%s pushed %d commit%s to %s", UserName(userId), totalAdded, totalAdded == 1 ? "" : "s", repo->name));
            else       Toast(TextFormat("%s updated the branches of %s", UserName(userId), repo->name));
        }
    }
    PktFlush(&reply);
    PackFree(&pack);
    SendReply(conn, 200, "application/x-git-receive-pack-result", reply.data, reply.length, NULL);
    BufferFree(&reply);
}

/* Works out what a complete request is for, and answers it. */
static void HandleRequest(Connection *conn, HttpRequest *request)
{
    /* Only requests addressed to this computer: a web page can't trick your browser into reaching it */
    char hostName[128];
    snprintf(hostName, sizeof hostName, "%s", request->host);
    char *colon = hostName[0] == '[' ? strchr(hostName, ']') : strchr(hostName, ':');
    if (colon) colon[hostName[0] == '['] = 0;
    if (request->host[0] && !EqualsIgnoreCase(hostName, "localhost") && strcmp(hostName, "127.0.0.1") != 0 && strcmp(hostName, "[::1]") != 0) {
        SendTextReply(conn, 403, "Mini GitHub only answers requests to localhost\n", NULL);
        return;
    }
    if (strcmp(request->path, "/") == 0) {
        SendTextReply(conn, 200, "This is Mini GitHub's git server.\n"
                          "Use it with git, for example:  git clone http://localhost:7979/<user>/<repository>.git\n", NULL);
        return;
    }
    /* /owner/repo(.git)/rest */
    char owner[NAME_LEN], name[NAME_LEN + 8], rest[128];
    const char *pos = request->path;
    if (*pos++ != '/') { SendTextReply(conn, 404, "Not found\n", NULL); return; }
    const char *ownerEnd = strchr(pos, '/');
    if (!ownerEnd || ownerEnd - pos >= NAME_LEN) { SendTextReply(conn, 404, "Not found\n", NULL); return; }
    snprintf(owner, sizeof owner, "%.*s", (int)(ownerEnd - pos), pos);
    pos = ownerEnd + 1;
    const char *nameEnd = strchr(pos, '/');
    if (!nameEnd || nameEnd - pos >= NAME_LEN + 4) { SendTextReply(conn, 404, "Not found\n", NULL); return; }
    snprintf(name, sizeof name, "%.*s", (int)(nameEnd - pos), pos);
    snprintf(rest, sizeof rest, "%s", nameEnd + 1);
    size_t nameLength = strlen(name);
    if (nameLength > 4 && strcmp(name + nameLength - 4, ".git") == 0) name[nameLength - 4] = 0;

    User *ownerUser = FindUserByName(owner);
    int repoIndex = ownerUser ? FindRepoByOwner(ownerUser->id, name) : -1;
    if (repoIndex < 0) {
        SendTextReply(conn, 404, TextFormat("Mini GitHub has no repository called %s/%s\n", owner, name), NULL);
        return;
    }
    Repo *repo = &repos[repoIndex];
    bool wantPush = strcmp(rest, "git-receive-pack") == 0 || (strcmp(rest, "info/refs") == 0 && strstr(request->query, "service=git-receive-pack"));
    bool wantPull = strcmp(rest, "git-upload-pack") == 0 || (strcmp(rest, "info/refs") == 0 && strstr(request->query, "service=git-upload-pack"));
    if (!wantPush && !wantPull) {
        SendTextReply(conn, 404, "Mini GitHub only speaks git's \"smart\" protocol - please use a recent version of git.\n", NULL);
        return;
    }
    int userId = -1;
    if (wantPush || repo->isPrivate) {            /* private repositories: even reading needs a password */
        userId = Authenticate(request);
        if (userId < 0) { AskForPassword(conn); return; }
        if (!CanSee(repo, userId)) {               /* don't even admit it exists */
            SendTextReply(conn, 404, TextFormat("Mini GitHub has no repository called %s/%s\n", owner, name), NULL);
            return;
        }
    }
    if (strcmp(rest, "info/refs") == 0) {
        if (strcmp(request->method, "GET") != 0) { SendTextReply(conn, 405, "Use GET\n", NULL); return; }
        SendRefList(conn, request, repo, wantPush);
        return;
    }
    if (strcmp(request->method, "POST") != 0) { SendTextReply(conn, 405, "Use POST\n", NULL); return; }
    if (wantPull) HandleUploadPack(conn, request, repo);
    else          HandleReceivePack(conn, request, repo, userId);
}

static void CloseConnection(Connection *conn)
{
    SocketClose(conn->socket);
    BufferFree(&conn->request); BufferFree(&conn->response);
    memset(conn, 0, sizeof *conn);
}

/* Called once per frame: new connections, incoming data, replies going out. */
void GitServerPoll(void)
{
    if (gitListenSocket == NO_SOCKET) return;
    double now = GetTime();
    for (;;) {
        Socket newSocket = SocketAccept(gitListenSocket);
        if (newSocket == NO_SOCKET) break;
        int freeSlot = -1;
        for (int i = 0; i < MAX_CONNECTIONS; i++) if (!connections[i].inUse) { freeSlot = i; break; }
        if (freeSlot < 0) { SocketClose(newSocket); continue; }
        memset(&connections[freeSlot], 0, sizeof connections[freeSlot]);
        connections[freeSlot].inUse = true; connections[freeSlot].socket = newSocket; connections[freeSlot].lastActivity = now;
    }
    static uint8_t chunk[256 * 1024];
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        Connection *conn = &connections[i];
        if (!conn->inUse) continue;
        if (!conn->replied) {
            size_t received = 0;
            bool closed = false;
            while (received < 64u * 1024 * 1024) {          /* take up to 64 MB a frame */
                int bytes = SocketReceive(conn->socket, chunk, (int)sizeof chunk);
                if (bytes == -1) break;
                if (bytes <= 0) { closed = true; break; }
                BufferAppend(&conn->request, chunk, (size_t)bytes);
                received += (size_t)bytes;
                conn->lastActivity = now;
            }
            if (conn->request.outOfMemory || conn->request.length > MAX_PUSH + 65536) { SendTextReply(conn, 413, "That's too big for Mini GitHub\n", NULL); }
            else if (conn->request.length) {
                /* has the whole request arrived? */
                if (!conn->headerLength) {
                    for (size_t freeSlot = 3; freeSlot < conn->request.length; freeSlot++)
                        if (conn->request.data[freeSlot - 3] == '\r' && conn->request.data[freeSlot - 2] == '\n' && conn->request.data[freeSlot - 1] == '\r' && conn->request.data[freeSlot] == '\n') { conn->headerLength = (int)freeSlot + 1; break; }
                    if (!conn->headerLength && conn->request.length > 16384) SendTextReply(conn, 400, "Bad request\n", NULL);
                }
                if (conn->headerLength && !conn->replied) {
                    HttpRequest request;
                    if (!ParseRequestHead(conn->request.data, conn->headerLength, &request)) SendTextReply(conn, 400, "Bad request\n", NULL);
                    else {
                        const uint8_t *body = conn->request.data + conn->headerLength;
                        size_t bodyBytes = conn->request.length - (size_t)conn->headerLength;
                        int ready = 0;
                        if (request.chunked) ready = DecodeChunkedBody(body, bodyBytes, &request);
                        else if (request.contentLength > (long long)MAX_PUSH) ready = -2;
                        else if (request.contentLength <= 0 || bodyBytes >= (size_t)request.contentLength) {
                            ready = 1;
                            request.body = body;
                            request.bodyLength = request.contentLength > 0 ? (size_t)request.contentLength : 0;
                        }
                        if (ready == 0 && request.expectsContinue && !conn->sentContinue) {          /* "go ahead and send the rest" */
                            const char *continueReply = "HTTP/1.1 100 Continue\r\n\r\n";
                            SocketSend(conn->socket, (const uint8_t *)continueReply, (int)strlen(continueReply));
                            conn->sentContinue = true;
                        }
                        if (ready == 1) HandleRequest(conn, &request);
                        else if (ready == -1) SendTextReply(conn, 400, "Bad request\n", NULL);
                        else if (ready == -2) SendTextReply(conn, 413, "That's too big for Mini GitHub\n", NULL);
                        free(request.bodyOwned);
                    }
                }
            }
            if (closed && !conn->replied) { CloseConnection(conn); continue; }
        }
        if (conn->replied) {
            while (conn->bytesSent < conn->response.length) {
                size_t left = conn->response.length - conn->bytesSent;
                int bytes = SocketSend(conn->socket, conn->response.data + conn->bytesSent, (int)(left > (1u << 20) ? (1u << 20) : left));
                if (bytes == -1) break;
                if (bytes < 0) { conn->bytesSent = conn->response.length; break; }
                conn->bytesSent += (size_t)bytes;
                conn->lastActivity = now;
            }
            if (conn->bytesSent >= conn->response.length) { CloseConnection(conn); continue; }
        }
        if (now - conn->lastActivity > 120) CloseConnection(conn);  /* given up on */
    }
}
