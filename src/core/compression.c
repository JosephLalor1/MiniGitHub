/*
 * compression.c - zlib / DEFLATE in both directions
 *
 * Unpacking everything git sends (and gzip-compressed requests), and
 * squeezing the objects in the packs Mini GitHub sends and the files in
 * zip downloads.
 */

#include "compression.h"
#include "byte_buffer.h"

/* ---------- unpacking (DEFLATE) ----------
   Git squeezes every object with zlib (DEFLATE plus a small header and a
   checksum). This undoes that, and also says how many bytes it used -
   git's packs put objects back to back without saying where each ends. */
typedef struct { uint16_t lookup[1 << 15]; } HuffmanTable;  /* lookup: 15 bits in -> symbol and code length */

typedef struct {
    const uint8_t *input; size_t inputLength, position;
    uint64_t bitBuffer; int bitCount;              /* bits waiting to be used */
    ByteBuffer output; size_t maxOutput;
    bool failed;
} Inflater;

static void InflaterRefill(Inflater *inflater)
{
    while (inflater->bitCount <= 56) {
        uint64_t nextByte = inflater->position < inflater->inputLength ? inflater->input[inflater->position] : 0;  /* past the end: zeros (checked later) */
        inflater->position++;
        inflater->bitBuffer |= nextByte << inflater->bitCount;
        inflater->bitCount += 8;
    }
}
static unsigned InflaterReadBits(Inflater *inflater, int count)
{
    if (count == 0) return 0;
    if (inflater->bitCount < count) InflaterRefill(inflater);
    unsigned value = (unsigned)(inflater->bitBuffer & ((1ull << count) - 1));
    inflater->bitBuffer >>= count; inflater->bitCount -= count;
    return value;
}
/* Builds the lookup table from each symbol's code length. False if the lengths make no sense. */
static bool BuildHuffmanTable(HuffmanTable *table, const uint8_t *codeLengths, int symbolCount)
{
    int lengthCount[16] = {0}, nextCode[16];
    for (int i = 0; i < symbolCount; i++) lengthCount[codeLengths[i]]++;
    lengthCount[0] = 0;
    int codesLeft = 1;
    for (int length = 1; length < 16; length++) { codesLeft <<= 1; codesLeft -= lengthCount[length]; if (codesLeft < 0) return false; }
    int code = 0;
    nextCode[1] = 0;
    for (int length = 1; length < 15; length++) { code = (code + lengthCount[length]) << 1; nextCode[length + 1] = code; }
    memset(table->lookup, 0, sizeof table->lookup);
    for (int sym = 0; sym < symbolCount; sym++) {
        int length = codeLengths[sym];
        if (!length) continue;
        int symbolCode = nextCode[length]++, reversed = 0;
        for (int bit = 0; bit < length; bit++) reversed |= ((symbolCode >> bit) & 1) << (length - 1 - bit);
        for (int i = reversed; i < (1 << 15); i += 1 << length) table->lookup[i] = (uint16_t)(sym | length << 9);
    }
    return true;
}
static int DecodeHuffmanSymbol(Inflater *inflater, const HuffmanTable *table)
{
    if (inflater->bitCount < 15) InflaterRefill(inflater);
    uint16_t entry = table->lookup[inflater->bitBuffer & 0x7FFF];
    int length = entry >> 9;
    if (!length) { inflater->failed = true; return -1; }
    inflater->bitBuffer >>= length; inflater->bitCount -= length;
    return entry & 0x1FF;
}

static const uint16_t LENGTH_BASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t  LENGTH_EXTRA_BITS[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t DISTANCE_BASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t  DISTANCE_EXTRA_BITS[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static HuffmanTable *fixedLiteralTable, *fixedDistanceTable;

static bool InflateHuffmanBlock(Inflater *inflater, const HuffmanTable *literals, const HuffmanTable *distances)
{
    for (;;) {
        int symbol = DecodeHuffmanSymbol(inflater, literals);
        if (inflater->failed) return false;
        if (symbol < 256) { BufferAppendByte(&inflater->output, (uint8_t)symbol); }
        else if (symbol == 256) return true;
        else {
            symbol -= 257;
            if (symbol >= 29) return false;
            size_t length = LENGTH_BASE[symbol] + InflaterReadBits(inflater, LENGTH_EXTRA_BITS[symbol]);
            int distanceSymbol = DecodeHuffmanSymbol(inflater, distances);
            if (inflater->failed || distanceSymbol < 0 || distanceSymbol >= 30) return false;
            size_t distance = DISTANCE_BASE[distanceSymbol] + InflaterReadBits(inflater, DISTANCE_EXTRA_BITS[distanceSymbol]);
            if (distance > inflater->output.length) return false;
            BufferReserve(&inflater->output, length);
            if (inflater->output.outOfMemory) return false;
            uint8_t *copyTo = inflater->output.data + inflater->output.length;
            for (size_t i = 0; i < length; i++) copyTo[i] = copyTo[(ptrdiff_t)i - (ptrdiff_t)distance];
            inflater->output.length += length;
        }
        if (inflater->output.outOfMemory || inflater->output.length > inflater->maxOutput) return false;
        if (inflater->position > inflater->inputLength + 8) return false;  /* ran off the end of the input */
    }
}

/* Raw DEFLATE. *bytesUsed gets how many input bytes it took. NULL if the data is damaged. */
static uint8_t *InflateRaw(const uint8_t *input, size_t inputLength, size_t *bytesUsed, size_t *outputLength, size_t maxOutput)
{
    static HuffmanTable literals, distances;
    Inflater inflater;
    memset(&inflater, 0, sizeof inflater);
    inflater.input = input; inflater.inputLength = inputLength; inflater.maxOutput = maxOutput;
    if (!fixedLiteralTable) {
        static HuffmanTable fixedLiterals, fixedDistances;
        uint8_t lengths[288];
        for (int i = 0; i < 144; i++) lengths[i] = 8;
        for (int i = 144; i < 256; i++) lengths[i] = 9;
        for (int i = 256; i < 280; i++) lengths[i] = 7;
        for (int i = 280; i < 288; i++) lengths[i] = 8;
        BuildHuffmanTable(&fixedLiterals, lengths, 288);
        for (int i = 0; i < 30; i++) lengths[i] = 5;
        BuildHuffmanTable(&fixedDistances, lengths, 30);
        fixedLiteralTable = &fixedLiterals; fixedDistanceTable = &fixedDistances;
    }
    bool lastBlock = false, ok = true;
    while (!lastBlock && ok) {
        lastBlock = InflaterReadBits(&inflater, 1);
        int blockType = (int)InflaterReadBits(&inflater, 2);
        if (blockType == 0) {                              /* stored: no squeezing */
            InflaterReadBits(&inflater, inflater.bitCount & 7);  /* skip to a whole byte */
            unsigned storedLength = InflaterReadBits(&inflater, 16), storedLengthCheck = InflaterReadBits(&inflater, 16);
            if ((storedLength ^ 0xFFFF) != storedLengthCheck) { ok = false; break; }
            for (unsigned i = 0; i < storedLength && ok; i++) {
                BufferAppendByte(&inflater.output, (uint8_t)InflaterReadBits(&inflater, 8));
                if (inflater.position > inflater.inputLength + 8 || inflater.output.outOfMemory) ok = false;
            }
            if (inflater.output.length > inflater.maxOutput) ok = false;
        } else if (blockType == 1) {
            ok = InflateHuffmanBlock(&inflater, fixedLiteralTable, fixedDistanceTable);
        } else if (blockType == 2) {
            /* a block with its own Huffman codes; hlit, hdist and hclen are RFC 1951's names
               for how many literal/length, distance and code-length codes follow */
            int hlit = (int)InflaterReadBits(&inflater, 5) + 257, hdist = (int)InflaterReadBits(&inflater, 5) + 1, hclen = (int)InflaterReadBits(&inflater, 4) + 4;
            static const uint8_t CODE_LENGTH_ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t codeLengthLengths[19] = {0}, codeLengths[320] = {0};
            for (int i = 0; i < hclen; i++) codeLengthLengths[CODE_LENGTH_ORDER[i]] = (uint8_t)InflaterReadBits(&inflater, 3);
            HuffmanTable *codeLengthTable = &literals;    /* borrow the table for the code-length code */
            if (!BuildHuffmanTable(codeLengthTable, codeLengthLengths, 19)) { ok = false; break; }
            int lengthCount = 0;
            while (lengthCount < hlit + hdist && ok) {
                int symbol = DecodeHuffmanSymbol(&inflater, codeLengthTable);
                if (inflater.failed) { ok = false; break; }
                if (symbol < 16) codeLengths[lengthCount++] = (uint8_t)symbol;
                else {
                    int repeat, repeatedLength = 0;
                    if (symbol == 16) { if (!lengthCount) { ok = false; break; } repeatedLength = codeLengths[lengthCount - 1]; repeat = 3 + (int)InflaterReadBits(&inflater, 2); }
                    else if (symbol == 17) repeat = 3 + (int)InflaterReadBits(&inflater, 3);
                    else repeat = 11 + (int)InflaterReadBits(&inflater, 7);
                    if (lengthCount + repeat > hlit + hdist) { ok = false; break; }
                    while (repeat--) codeLengths[lengthCount++] = (uint8_t)repeatedLength;
                }
            }
            if (!ok || !codeLengths[256]) { ok = false; break; }
            if (!BuildHuffmanTable(&literals, codeLengths, hlit) || !BuildHuffmanTable(&distances, codeLengths + hlit, hdist)) { ok = false; break; }
            ok = InflateHuffmanBlock(&inflater, &literals, &distances);
        } else ok = false;
    }
    size_t bytesConsumed = inflater.position - (size_t)(inflater.bitCount >> 3);
    if (!ok || inflater.output.outOfMemory || bytesConsumed > inputLength) { BufferFree(&inflater.output); return NULL; }
    if (bytesUsed) *bytesUsed = bytesConsumed;
    *outputLength = inflater.output.length;
    BufferAppendByte(&inflater.output, 0);                 /* a zero after it, handy for text */
    if (inflater.output.outOfMemory) { BufferFree(&inflater.output); return NULL; }
    return inflater.output.data;
}

static uint32_t Adler32(const uint8_t *data, size_t length)
{
    uint32_t sumA = 1, sumB = 0;
    while (length) {
        size_t chunk = length < 5000 ? length : 5000;
        length -= chunk;
        while (chunk--) { sumA += *data++; sumB += sumA; }
        sumA %= 65521; sumB %= 65521;
    }
    return sumB << 16 | sumA;
}

/* zlib = 2-byte header + DEFLATE + Adler-32 checksum. */
uint8_t *InflateZlib(const uint8_t *input, size_t inputLength, size_t *bytesUsed, size_t *outputLength, size_t maxOutput)
{
    if (inputLength < 6 || (input[0] & 0x0F) != 8 || ((input[0] << 8) | input[1]) % 31 != 0 || (input[1] & 0x20)) return NULL;
    size_t deflateUsed = 0;
    uint8_t *output = InflateRaw(input + 2, inputLength - 2, &deflateUsed, outputLength, maxOutput);
    if (!output) return NULL;
    if (2 + deflateUsed + 4 > inputLength) { free(output); return NULL; }
    const uint8_t *checksum = input + 2 + deflateUsed;
    uint32_t expected = (uint32_t)checksum[0] << 24 | (uint32_t)checksum[1] << 16 | (uint32_t)checksum[2] << 8 | checksum[3];
    if (expected != Adler32(output, *outputLength)) { free(output); return NULL; }
    if (bytesUsed) *bytesUsed = 2 + deflateUsed + 4;
    return output;
}

/* gzip (used by git for big requests) = a header + DEFLATE + a CRC and the size. */
uint8_t *InflateGzip(const uint8_t *input, size_t inputLength, size_t *outputLength, size_t maxOutput)
{
    if (inputLength < 18 || input[0] != 0x1f || input[1] != 0x8b || input[2] != 8) return NULL;
    int flags = input[3];
    size_t pos = 10;
    if (flags & 4) { if (pos + 2 > inputLength) return NULL; pos += 2 + (size_t)(input[pos] | input[pos + 1] << 8); }
    if (flags & 8)  { while (pos < inputLength && input[pos]) pos++; pos++; }
    if (flags & 16) { while (pos < inputLength && input[pos]) pos++; pos++; }
    if (flags & 2)  pos += 2;
    if (pos >= inputLength) return NULL;
    return InflateRaw(input + pos, inputLength - pos, NULL, outputLength, maxOutput);
}

/* ---------- squeezing (DEFLATE) ----------
   Mini GitHub's own compressor (raylib's one can write past the end of its
   buffer on big files that don't squeeze). It finds repeats with a hash of
   every 3 bytes, and writes them with DEFLATE's fixed codes: simple, quick,
   and it never writes more than "limit" bytes - if the result would be
   bigger than that, it gives up (returns false) and the caller stores the
   data unsqueezed instead. */
typedef struct { ByteBuffer *output; uint64_t pending; int pendingCount; size_t limit, start; bool overflowed; } BitWriter;
static void WriteBits(BitWriter *writer, uint32_t value, int count)
{
    writer->pending |= (uint64_t)value << writer->pendingCount;
    writer->pendingCount += count;
    while (writer->pendingCount >= 8) { BufferAppendByte(writer->output, (uint8_t)writer->pending); writer->pending >>= 8; writer->pendingCount -= 8; }
    if (writer->output->length - writer->start > writer->limit || writer->output->outOfMemory) writer->overflowed = true;
}
/* Huffman codes go in most-significant bit first, so they're reversed. */
static void WriteHuffmanCode(BitWriter *writer, uint32_t code, int length)
{
    uint32_t reversed = 0;
    for (int i = 0; i < length; i++) reversed |= ((code >> i) & 1) << (length - 1 - i);
    WriteBits(writer, reversed, length);
}
static void WriteFixedSymbol(BitWriter *writer, int symbol)  /* DEFLATE's fixed literal / length codes */
{
    if (symbol < 144)      WriteHuffmanCode(writer, 0x30 + (uint32_t)symbol, 8);
    else if (symbol < 256) WriteHuffmanCode(writer, 0x190 + (uint32_t)(symbol - 144), 9);
    else if (symbol < 280) WriteHuffmanCode(writer, (uint32_t)(symbol - 256), 7);
    else                WriteHuffmanCode(writer, 0xC0 + (uint32_t)(symbol - 280), 8);
}
/* Squeezes data with DEFLATE (fixed Huffman codes) onto the end of out. False if the result would
   be bigger than limit. */
bool DeflateRaw(const uint8_t *data, size_t length, ByteBuffer *out, size_t limit)
{
    enum { HASH_BITS = 15, WINDOW = 32768, MAX_CHAIN = 24 };
    BitWriter writer = {out, 0, 0, limit, out->length, false};
    int *chainHead = malloc(sizeof(int) << HASH_BITS), *chainPrev = malloc(sizeof(int) * WINDOW);
    if (!chainHead || !chainPrev) { free(chainHead); free(chainPrev); return false; }
    for (int pos = 0; pos < (1 << HASH_BITS); pos++) chainHead[pos] = -1;
    WriteBits(&writer, 1, 1); WriteBits(&writer, 1, 2);  /* the last block, fixed codes */
    size_t pos = 0;
    size_t nextCheck = 262144;
    while (pos < length && !writer.overflowed) {
        if (pos >= nextCheck) {                              /* not squeezing at all (e.g. a zip or a photo)? stop early */
            if (out->length - writer.start > pos - pos / 64) { writer.overflowed = true; break; }
            nextCheck += 262144;
        }
        int bestLen = 0, bestDist = 0;
        if (pos + 3 <= length) {
            uint32_t hash = ((uint32_t)data[pos] << 16 | (uint32_t)data[pos + 1] << 8 | data[pos + 2]) * 2654435761u >> (32 - HASH_BITS);
            int candidate = chainHead[hash], tries = 0;
            size_t maxLen = length - pos < 258 ? length - pos : 258;
            while (candidate >= 0 && pos - (size_t)candidate <= WINDOW && tries++ < MAX_CHAIN) {
                size_t matchLength = 0;
                while (matchLength < maxLen && data[(size_t)candidate + matchLength] == data[pos + matchLength]) matchLength++;
                if ((int)matchLength > bestLen) { bestLen = (int)matchLength; bestDist = (int)(pos - (size_t)candidate); if (matchLength == maxLen) break; }
                int nextCandidate = chainPrev[candidate % WINDOW];
                if (nextCandidate >= candidate) break;
                candidate = nextCandidate;
            }
            chainPrev[pos % WINDOW] = chainHead[hash]; chainHead[hash] = (int)pos;
        }
        if (bestLen >= 3) {
            int lengthCode = 28;                             /* length code */
            while (lengthCode > 0 && LENGTH_BASE[lengthCode] > bestLen) lengthCode--;
            WriteFixedSymbol(&writer, 257 + lengthCode);
            if (LENGTH_EXTRA_BITS[lengthCode]) WriteBits(&writer, (uint32_t)(bestLen - LENGTH_BASE[lengthCode]), LENGTH_EXTRA_BITS[lengthCode]);
            int distanceCode = 29;                           /* distance code */
            while (distanceCode > 0 && DISTANCE_BASE[distanceCode] > bestDist) distanceCode--;
            WriteHuffmanCode(&writer, (uint32_t)distanceCode, 5);
            if (DISTANCE_EXTRA_BITS[distanceCode]) WriteBits(&writer, (uint32_t)(bestDist - DISTANCE_BASE[distanceCode]), DISTANCE_EXTRA_BITS[distanceCode]);
            for (size_t skip = 1; skip < (size_t)bestLen && pos + skip + 3 <= length; skip++) {  /* remember the bytes inside the match too */
                size_t inside = pos + skip;
                uint32_t hash = ((uint32_t)data[inside] << 16 | (uint32_t)data[inside + 1] << 8 | data[inside + 2]) * 2654435761u >> (32 - HASH_BITS);
                chainPrev[inside % WINDOW] = chainHead[hash]; chainHead[hash] = (int)inside;
            }
            pos += (size_t)bestLen;
        } else WriteFixedSymbol(&writer, data[pos++]);
    }
    WriteFixedSymbol(&writer, 256);                          /* end of block */
    if (writer.pendingCount) WriteBits(&writer, 0, 8 - writer.pendingCount);
    free(chainHead); free(chainPrev);
    if (writer.overflowed) { out->length = writer.start; return false; }
    return true;
}

/* Squeezes data into zlib format: DEFLATE when that makes it smaller,
   otherwise "stored" blocks (no squeezing, still valid). */
void ZlibCompress(ByteBuffer *out, const uint8_t *data, size_t length)
{
    BufferAppendByte(out, 0x78); BufferAppendByte(out, 0x01);
    if (length < 64 || !DeflateRaw(data, length, out, length - 8)) {
        size_t done = 0;
        do {
            size_t chunk = length - done > 65535 ? 65535 : length - done;
            BufferAppendByte(out, done + chunk >= length ? 1 : 0);
            BufferAppendByte(out, (uint8_t)(chunk & 0xFF)); BufferAppendByte(out, (uint8_t)(chunk >> 8));
            BufferAppendByte(out, (uint8_t)(~chunk & 0xFF)); BufferAppendByte(out, (uint8_t)((~chunk >> 8) & 0xFF));
            BufferAppend(out, data + done, chunk);
            done += chunk;
        } while (done < length);
    }
    uint32_t checksum = Adler32(data, length);
    BufferAppendByte(out, (uint8_t)(checksum >> 24)); BufferAppendByte(out, (uint8_t)(checksum >> 16)); BufferAppendByte(out, (uint8_t)(checksum >> 8)); BufferAppendByte(out, (uint8_t)checksum);
}
