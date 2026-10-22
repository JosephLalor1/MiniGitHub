/*
 * downloads.c - saving files to the Downloads folder
 *
 * One file as it is, a whole repository as a .zip, or the files of one
 * commit.
 */

#include "downloads.h"
#include "app_state.h"
#include "byte_buffer.h"
#include "compression.h"
#include "model.h"
#include "repo_view.h"
#include "ui_core.h"
#include "users.h"

/* ------------------------------------------------------------------ */
/* Downloading                                                        */
/*                                                                    */
/* Files are saved into your normal Downloads folder (or, if there    */
/* isn't one, a "downloads" folder next to the program). A whole      */
/* repository is saved as one .zip file that any computer can open.   */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

/* One file to download: its name and its bytes. */
typedef struct { const char *name; const char *data; int length; } OutFile;

static bool usingFallbackFolder;          /* true when there was no Downloads folder */

static const char *DownloadFolder(void)
{
    static char dir[1024];
    const char *home = getenv("USERPROFILE");          /* Windows */
    if (!home || !*home) home = getenv("HOME");        /* Linux and macOS */
    if (home && *home) {
        snprintf(dir, sizeof dir, "%s%cDownloads", home, PATH_SEP);
        if (DirectoryExists(dir)) { usingFallbackFolder = false; return dir; }
    }
    snprintf(dir, sizeof dir, "downloads");
    if (!DirectoryExists(dir)) MakeDirectory(dir);
    usingFallbackFolder = true;
    return dir;
}

/* "notes.txt", or "notes (1).txt", "notes (2).txt"... if that's taken - like a web browser. */
static const char *UnusedPath(const char *dir, const char *fileName)
{
    static char path[1200];
    char stem[NAME_LEN * 2], extension[NAME_LEN];
    snprintf(stem, sizeof stem, "%s", fileName);
    extension[0] = 0;
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem) { snprintf(extension, sizeof extension, "%s", dot); *dot = 0; }
    snprintf(path, sizeof path, "%s%c%s%s", dir, PATH_SEP, stem, extension);
    for (int number = 1; FileExists(path) && number < 1000; number++)
        snprintf(path, sizeof path, "%s%c%s (%d)%s", dir, PATH_SEP, stem, number, extension);
    return path;
}

/* Tells you where a download went. */
static void ToastSavedTo(const char *path)
{
    const char *name = GetFileName(path);
    if (usingFallbackFolder) Toast(TextFormat("Saved %.60s in the \"downloads\" folder next to Mini GitHub", name));
    else                     Toast(TextFormat("Saved %.60s to your Downloads folder", name));
}

/* Saves one file's bytes, exactly as they are. */
void DownloadFile(const char *name, const char *data, int length)
{
    const char *slash = strrchr(name, '/');                 /* "src/main.c" is saved as main.c */
    const char *path = UnusedPath(DownloadFolder(), slash ? slash + 1 : name);
    FILE *file = fopen(path, "wb");
    if (!file) { Toast("Couldn't save the file - is the Downloads folder read-only?"); return; }
    bool ok = fwrite(data, 1, (size_t)length, file) == (size_t)length;
    ok = fclose(file) == 0 && ok;
    if (!ok) { remove(path); Toast("Couldn't save the file - is the disk full?"); return; }
    ToastSavedTo(path);
}

/* ---------- .zip files ----------
   A zip is a list of files, each with a small header saying its name, size
   and a checksum (CRC-32) so damage can be spotted, followed by a table of
   contents at the end. Each file is squeezed with DEFLATE (the standard zip
   compression, which raylib can do); if that doesn't make it smaller, it's
   stored as it is. */
static uint32_t Crc32(const unsigned char *data, int length)
{
    static uint32_t table[256];
    static bool ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (int bit = 0; bit < 8; bit++) crc = crc & 1 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
            table[i] = crc;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < length; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void WriteLE16(FILE *file, unsigned value) { fputc((int)(value & 0xFF), file); fputc((int)((value >> 8) & 0xFF), file); }
static void WriteLE32(FILE *file, uint32_t value) { WriteLE16(file, value & 0xFFFF); WriteLE16(file, value >> 16); }

/* Writes files into a .zip, all inside one folder called folder/.
   Returns NULL if it worked, or a message saying why not. */
static const char *WriteZip(const char *path, const char *folder, OutFile *files, int fileCount)
{
    FILE *file = fopen(path, "wb");
    if (!file) return "Couldn't save the zip - is the Downloads folder read-only?";

    /* zip files store dates the old MS-DOS way: packed into two 16-bit numbers */
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    unsigned dosTime = (unsigned)(local->tm_hour << 11 | local->tm_min << 5 | local->tm_sec / 2);
    unsigned dosDate = (unsigned)((local->tm_year - 80) << 9 | (local->tm_mon + 1) << 5 | local->tm_mday);

    typedef struct { uint32_t crc, compressedSize, offset; int method; char name[PATH_LEN + NAME_LEN + 24]; int size; } ZipEntry;
    ZipEntry *entries = malloc((size_t)(fileCount + 1) * sizeof(ZipEntry));
    if (!entries) { fclose(file); remove(path); return "Out of memory"; }
    int count = 0;
    bool ok = true;

    /* the folder itself comes first, so it opens as a neat folder */
    snprintf(entries[count].name, sizeof entries[count].name, "%s/", folder);
    entries[count].crc = 0; entries[count].compressedSize = 0; entries[count].size = 0; entries[count].method = 0;
    entries[count].offset = (uint32_t)ftell(file);
    count++;

    for (int i = 0; i <= fileCount && ok; i++) {
        bool isFolder = i == 0;
        const unsigned char *data = isFolder ? NULL : (const unsigned char *)files[i - 1].data;
        int size = isFolder ? 0 : files[i - 1].length;
        int entry = isFolder ? 0 : count;
        if (!isFolder) {
            snprintf(entries[entry].name, sizeof entries[entry].name, "%s/%s", folder, files[i - 1].name);
            entries[entry].offset = (uint32_t)ftell(file);
            entries[entry].size = size;
            entries[entry].crc = Crc32(data, size);
        }
        /* try to squeeze it */
        ByteBuffer packedBuffer = {0};
        bool compress = size > 64 && DeflateRaw(data, (size_t)size, &packedBuffer, (size_t)size - 8);
        const unsigned char *packedBytes = packedBuffer.data;
        int packedLength = (int)packedBuffer.length;
        entries[entry].method = compress ? 8 : 0;
        entries[entry].compressedSize = (uint32_t)(compress ? packedLength : size);

        int nameLength = (int)strlen(entries[entry].name);
        WriteLE32(file, 0x04034b50);              /* "local file header" marker */
        WriteLE16(file, 20); WriteLE16(file, 0);  /* zip version needed, flags  */
        WriteLE16(file, (unsigned)entries[entry].method);
        WriteLE16(file, dosTime); WriteLE16(file, dosDate);
        WriteLE32(file, entries[entry].crc); WriteLE32(file, entries[entry].compressedSize); WriteLE32(file, (uint32_t)entries[entry].size);
        WriteLE16(file, (unsigned)nameLength); WriteLE16(file, 0);
        fwrite(entries[entry].name, 1, (size_t)nameLength, file);
        if (entries[entry].compressedSize) ok = fwrite(compress ? packedBytes : data, 1, entries[entry].compressedSize, file) == entries[entry].compressedSize;
        BufferFree(&packedBuffer);
        if (!isFolder) count++;
    }

    /* the table of contents */
    uint32_t directoryStart = (uint32_t)ftell(file);
    for (int entry = 0; entry < count; entry++) {
        int nameLength = (int)strlen(entries[entry].name);
        bool isFolder = entries[entry].name[nameLength - 1] == '/';
        WriteLE32(file, 0x02014b50);                 /* "central directory" marker */
        WriteLE16(file, 20); WriteLE16(file, 20); WriteLE16(file, 0);
        WriteLE16(file, (unsigned)entries[entry].method);
        WriteLE16(file, dosTime); WriteLE16(file, dosDate);
        WriteLE32(file, entries[entry].crc); WriteLE32(file, entries[entry].compressedSize); WriteLE32(file, (uint32_t)entries[entry].size);
        WriteLE16(file, (unsigned)nameLength); WriteLE16(file, 0); WriteLE16(file, 0);
        WriteLE16(file, 0); WriteLE16(file, 0);
        WriteLE32(file, isFolder ? 0x10 : 0);        /* "this is a folder" flag */
        WriteLE32(file, entries[entry].offset);
        fwrite(entries[entry].name, 1, (size_t)nameLength, file);
    }
    uint32_t directorySize = (uint32_t)ftell(file) - directoryStart;
    WriteLE32(file, 0x06054b50);                     /* "end of central directory" marker */
    WriteLE16(file, 0); WriteLE16(file, 0);
    WriteLE16(file, (unsigned)count); WriteLE16(file, (unsigned)count);
    WriteLE32(file, directorySize); WriteLE32(file, directoryStart);
    WriteLE16(file, 0);
    free(entries);
    ok = !ferror(file) && ok;
    ok = fclose(file) == 0 && ok;
    if (!ok) { remove(path); return "Couldn't save the zip - is the disk full?"; }
    return NULL;
}

static void DownloadZip(const char *zipName, const char *folder, OutFile *files, int fileCount)
{
    const char *path = UnusedPath(DownloadFolder(), zipName);
    char pathCopy[1200];
    snprintf(pathCopy, sizeof pathCopy, "%s", path);
    const char *error = WriteZip(pathCopy, folder, files, fileCount);
    if (error) Toast(error);
    else ToastSavedTo(pathCopy);
}

/* The whole repository as you see it: the latest commit, plus your own
   uncommitted changes if you can edit it. */
void DownloadRepo(Repo *repo)
{
    static OutFile files[MAX_FILES + MAX_DRAFT];
    BuildView(repo, CanEdit(repo) ? currentUserId : -1);
    int count = 0;
    bool hasDrafts = false;
    for (int i = 0; i < viewFileCount; i++) {
        if (viewFiles[i].status) hasDrafts = true;
        if (ViewDeleted(&viewFiles[i])) continue;
        files[count++] = (OutFile){viewFiles[i].name, viewFiles[i].data, viewFiles[i].length};
    }
    if (!count) { Toast("There are no files to download yet"); return; }
    DownloadZip(TextFormat("%s.zip", repo->name), repo->name, files, count);
    if (hasDrafts && toastText[0] && strncmp(toastText, "Saved", 5) == 0)
        Toast(TextFormat("%.90s (with your uncommitted changes)", toastText));
}

/* The files exactly as they were in one commit. */
void DownloadCommit(Repo *repo, int commitIndex)
{
    static OutFile files[MAX_FILES];
    Commit *commit = &repo->commits[commitIndex];
    for (int index = 0; index < commit->fileCount; index++)
        files[index] = (OutFile){commit->names[index], repo->blobs[commit->blobs[index]].data, repo->blobs[commit->blobs[index]].length};
    if (!commit->fileCount) { Toast("This commit has no files"); return; }
    char folder[NAME_LEN + 16];
    snprintf(folder, sizeof folder, "%s-%s", repo->name, commit->id);
    DownloadZip(TextFormat("%s.zip", folder), folder, files, commit->fileCount);
}
