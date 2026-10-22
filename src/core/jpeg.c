/*
 * jpeg.c - a small JPEG decoder
 *
 * raylib's standard build only reads PNG, so this reads "baseline" JPEG
 * photos - the kind cameras and phones save - including the EXIF note
 * saying which way up they are.
 */

#include "jpeg.h"

/* ------------------------------------------------------------------ */
/* JPG reader                                                         */
/*                                                                    */
/* raylib's standard build can open PNG but not JPG, and most photos  */
/* are JPG, so here is a small reader for "baseline" JPG files (the   */
/* kind cameras, phones and most programs save).                      */
/*                                                                    */
/* How a JPG works, in short: the picture is cut into 8x8 squares.    */
/* Each square is stored as 64 "frequency" numbers (mostly zeros),    */
/* squeezed with Huffman coding. To read it we undo the squeezing,    */
/* turn the frequencies back into pixels (the IDCT step), and convert */
/* from brightness + colour-difference (YCbCr) to red/green/blue.     */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t counts[17];          /* how many codes of each length 1..16 */
    uint8_t values[256];
    int     minCode[17], maxCode[18], firstValue[17];
    bool    ok;
} JpegHuffman;

typedef struct {
    int id, hSampling, vSampling, quantTable;  /* sampling factors and which quantisation table */
    int dcTable, acTable;                      /* Huffman tables for this scan                  */
    int dcPrediction;                          /* last DC value (each block stores a difference) */
    int blocksWide, blocksHigh;                /* size of its pixel plane, in 8x8 blocks        */
    uint8_t *pixels;
} JpegComponent;

typedef struct {
    const uint8_t *data;
    size_t size, position;
    unsigned bitBuffer; int bitsLeft; bool atMarker;
    uint16_t quantTables[4][64];
    JpegHuffman dcTables[4], acTables[4];
    JpegComponent components[3];
    int componentCount, width, height, maxHSampling, maxVSampling, restartInterval, orientation;
    bool progressive;
    const char *error;
} JpegDecoder;

static const uint8_t JPEG_ZIGZAG[64] = {
     0, 1, 8,16, 9, 2, 3,10, 17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34, 27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36, 29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46, 53,60,61,54,47,55,62,63
};

static int JpegRead16(JpegDecoder *jpeg)
{
    if (jpeg->position + 2 > jpeg->size) { jpeg->error = "the file is cut short"; return 0; }
    int value = jpeg->data[jpeg->position] << 8 | jpeg->data[jpeg->position + 1];
    jpeg->position += 2;
    return value;
}

static unsigned JpegReadBit(JpegDecoder *jpeg)
{
    if (jpeg->bitsLeft == 0) {
        unsigned byte = 0;
        if (!jpeg->atMarker && jpeg->position < jpeg->size) {
            byte = jpeg->data[jpeg->position++];
            if (byte == 0xFF) {
                unsigned nextByte = jpeg->position < jpeg->size ? jpeg->data[jpeg->position] : 0;
                if (nextByte == 0x00) jpeg->position++;                      /* a real 0xFF byte */
                else { jpeg->atMarker = true; jpeg->position--; byte = 0; }  /* a marker: stop here */
            }
        }
        jpeg->bitBuffer = byte;
        jpeg->bitsLeft = 8;
    }
    jpeg->bitsLeft--;
    return (jpeg->bitBuffer >> jpeg->bitsLeft) & 1;
}
static int JpegReadBits(JpegDecoder *jpeg, int count) { int value = 0; while (count--) value = (value << 1) | (int)JpegReadBit(jpeg); return value; }
static int JpegExtendSign(int value, int bits) { return bits == 0 ? 0 : (value < (1 << (bits - 1)) ? value - (1 << bits) + 1 : value); }

static void JpegBuildHuffman(JpegHuffman *table)
{
    int code = 0, valueIndex = 0;
    for (int length = 1; length <= 16; length++) {
        table->firstValue[length]  = valueIndex;
        table->minCode[length] = code;
        code += table->counts[length];
        valueIndex    += table->counts[length];
        table->maxCode[length] = table->counts[length] ? code - 1 : -1;
        code <<= 1;
    }
    table->ok = true;
}
static int JpegDecodeSymbol(JpegDecoder *jpeg, JpegHuffman *table)
{
    int code = 0;
    for (int length = 1; length <= 16; length++) {
        code = (code << 1) | (int)JpegReadBit(jpeg);
        if (table->maxCode[length] >= 0 && code <= table->maxCode[length]) return table->values[table->firstValue[length] + code - table->minCode[length]];
    }
    jpeg->error = "the picture data is damaged";
    return 0;
}

static float idctTable[8][8];   /* idctTable[x][u] = C(u) * cos((2x+1) u pi / 16) / 2 */
static void JpegInitIdct(void)
{
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
            idctTable[x][u] = (u == 0 ? 0.70710678f : 1.0f) * cosf((2 * x + 1) * u * 3.14159265f / 16.0f) / 2.0f;
}

/* Read one 8x8 block and write its pixels into the component's plane. */
static void JpegDecodeBlock(JpegDecoder *jpeg, JpegComponent *component, int blockX, int blockY)
{
    float coefficients[64] = {0}, rowPass[64];
    uint16_t *quant = jpeg->quantTables[component->quantTable];
    int dcBits = JpegDecodeSymbol(jpeg, &jpeg->dcTables[component->dcTable]);
    component->dcPrediction += dcBits ? JpegExtendSign(JpegReadBits(jpeg, dcBits), dcBits) : 0;
    coefficients[0] = (float)(component->dcPrediction * quant[0]);
    for (int index = 1; index < 64; ) {
        int runSize = JpegDecodeSymbol(jpeg, &jpeg->acTables[component->acTable]), run = runSize >> 4, size = runSize & 15;
        if (jpeg->error) return;
        if (size == 0) { if (run == 15) { index += 16; continue; } break; }
        index += run;
        if (index > 63) break;
        coefficients[JPEG_ZIGZAG[index]] = (float)(JpegExtendSign(JpegReadBits(jpeg, size), size) * quant[index]);
        index++;
    }
    /* turn frequencies back into pixels: rows first, then columns */
    for (int v = 0; v < 8; v++)
        for (int x = 0; x < 8; x++) {
            float size = 0;
            for (int u = 0; u < 8; u++) size += coefficients[v * 8 + u] * idctTable[x][u];
            rowPass[v * 8 + x] = size;
        }
    int stride = component->blocksWide * 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float size = 128.0f;
            for (int v = 0; v < 8; v++) size += idctTable[y][v] * rowPass[v * 8 + x];
            int pixelX = blockX * 8 + x, pixelY = blockY * 8 + y;
            if (pixelX < stride && pixelY < component->blocksHigh * 8)
                component->pixels[pixelY * stride + pixelX] = (uint8_t)(size < 0 ? 0 : size > 255 ? 255 : (int)(size + 0.5f));
        }
}

/* After every "restart interval", the file has a marker and the DC differences start over. */
static void JpegRestart(JpegDecoder *jpeg)
{
    jpeg->bitsLeft = 0;
    jpeg->atMarker = false;
    while (jpeg->position + 1 < jpeg->size) {
        if (jpeg->data[jpeg->position] == 0xFF && jpeg->data[jpeg->position + 1] >= 0xD0 && jpeg->data[jpeg->position + 1] <= 0xD7) { jpeg->position += 2; break; }
        jpeg->position++;
    }
    for (int i = 0; i < jpeg->componentCount; i++) jpeg->components[i].dcPrediction = 0;
}

static void JpegDecodeScan(JpegDecoder *jpeg)
{
    int headerLength = JpegRead16(jpeg), scanComponentCount = jpeg->position < jpeg->size ? jpeg->data[jpeg->position++] : 0;
    JpegComponent *scanComponents[3];
    if (scanComponentCount < 1 || scanComponentCount > 3) { jpeg->error = "the picture data is damaged"; return; }
    if (jpeg->position + (size_t)scanComponentCount * 2 + 3 > jpeg->size) { jpeg->error = "the file is cut short"; return; }
    for (int i = 0; i < scanComponentCount; i++) {
        int componentId = jpeg->data[jpeg->position++], tableIds = jpeg->data[jpeg->position++];
        scanComponents[i] = NULL;
        for (int k = 0; k < jpeg->componentCount; k++) if (jpeg->components[k].id == componentId) scanComponents[i] = &jpeg->components[k];
        if (!scanComponents[i]) { jpeg->error = "the picture data is damaged"; return; }
        scanComponents[i]->dcTable = (tableIds >> 4) & 3;
        scanComponents[i]->acTable = tableIds & 3;
        if (!jpeg->dcTables[scanComponents[i]->dcTable].ok || !jpeg->acTables[scanComponents[i]->acTable].ok) { jpeg->error = "the picture data is damaged"; return; }
    }
    jpeg->position += 3;                           /* spectral selection: always 0..63 in baseline */
    (void)headerLength;
    for (int i = 0; i < jpeg->componentCount; i++) jpeg->components[i].dcPrediction = 0;
    jpeg->bitsLeft = 0;
    jpeg->atMarker = false;

    int mcuCount = 0;
    if (scanComponentCount == 1) {                 /* one component on its own: blocks in plain order */
        JpegComponent *component = scanComponents[0];
        int componentWidth = (jpeg->width * component->hSampling + jpeg->maxHSampling - 1) / jpeg->maxHSampling, componentHeight = (jpeg->height * component->vSampling + jpeg->maxVSampling - 1) / jpeg->maxVSampling;
        int blocksAcross = (componentWidth + 7) / 8, blocksDown = (componentHeight + 7) / 8;
        for (int blockY = 0; blockY < blocksDown && !jpeg->error; blockY++)
            for (int blockX = 0; blockX < blocksAcross && !jpeg->error; blockX++) {
                if (jpeg->restartInterval && mcuCount && mcuCount % jpeg->restartInterval == 0) JpegRestart(jpeg);
                JpegDecodeBlock(jpeg, component, blockX, blockY);
                mcuCount++;
            }
    } else {                                       /* interleaved: each "MCU" holds h*v blocks of every component */
        int mcusAcross = (jpeg->width + 8 * jpeg->maxHSampling - 1) / (8 * jpeg->maxHSampling), mcusDown = (jpeg->height + 8 * jpeg->maxVSampling - 1) / (8 * jpeg->maxVSampling);
        for (int y = 0; y < mcusDown && !jpeg->error; y++)
            for (int x = 0; x < mcusAcross && !jpeg->error; x++) {
                if (jpeg->restartInterval && mcuCount && mcuCount % jpeg->restartInterval == 0) JpegRestart(jpeg);
                for (int i = 0; i < scanComponentCount; i++)
                    for (int blockRow = 0; blockRow < scanComponents[i]->vSampling; blockRow++)
                        for (int blockCol = 0; blockCol < scanComponents[i]->hSampling; blockCol++)
                            JpegDecodeBlock(jpeg, scanComponents[i], x * scanComponents[i]->hSampling + blockCol, y * scanComponents[i]->vSampling + blockRow);
                mcuCount++;
            }
    }
    /* move on to the next marker */
    jpeg->atMarker = false;
    while (jpeg->position + 1 < jpeg->size && !(jpeg->data[jpeg->position] == 0xFF && jpeg->data[jpeg->position + 1] != 0x00 &&
                                  !(jpeg->data[jpeg->position + 1] >= 0xD0 && jpeg->data[jpeg->position + 1] <= 0xD7))) jpeg->position++;
}

/* Phones store photos sideways plus a note (EXIF "orientation") saying how to turn them. */
static void JpegReadExif(JpegDecoder *jpeg, size_t start, size_t length)
{
    const uint8_t *segment = jpeg->data + start;
    if (length < 14 || memcmp(segment, "Exif\0\0", 6) != 0) return;
    const uint8_t *tiff = segment + 6;
    size_t tiffLength = length - 6;
    bool littleEndian = tiff[0] == 'I';
#define READ16(offset) ((offset) + 2 <= tiffLength ? (littleEndian ? (tiff[offset] | tiff[(offset)+1] << 8) : (tiff[offset] << 8 | tiff[(offset)+1])) : 0)
#define READ32(offset) ((offset) + 4 <= tiffLength ? (littleEndian ? (uint32_t)(tiff[offset] | tiff[(offset)+1] << 8 | tiff[(offset)+2] << 16 | (uint32_t)tiff[(offset)+3] << 24) \
                                     : (uint32_t)((uint32_t)tiff[offset] << 24 | tiff[(offset)+1] << 16 | tiff[(offset)+2] << 8 | tiff[(offset)+3])) : 0)
    uint32_t firstDirectory = READ32(4);
    int entryCount = READ16(firstDirectory);
    for (int entry = 0; entry < entryCount && entry < 200; entry++) {
        size_t offset = firstDirectory + 2 + (size_t)entry * 12;
        if (READ16(offset) == 0x0112) { jpeg->orientation = READ16(offset + 8); break; }
    }
#undef READ16
#undef READ32
}

/* Decodes a baseline JPG into an RGB image, turned the right way up. On failure, returns an empty
   image and points *error at the reason. */
Image DecodeJpg(const uint8_t *data, int size, const char **error)
{
    Image image = {0};
    JpegDecoder *jpeg = calloc(1, sizeof(JpegDecoder));
    if (!jpeg) { *error = "out of memory"; return image; }
    jpeg->data = data; jpeg->size = (size_t)size; jpeg->orientation = 1;
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) { *error = "it isn't a JPG file"; free(jpeg); return image; }
    jpeg->position = 2;
    JpegInitIdct();
    bool sawFrame = false, sawScan = false;

    while (!jpeg->error && jpeg->position + 4 <= jpeg->size) {
        if (jpeg->data[jpeg->position] != 0xFF) { jpeg->position++; continue; }
        int marker = jpeg->data[jpeg->position + 1];
        jpeg->position += 2;
        if (marker == 0xFF || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { if (marker == 0xFF) jpeg->position--; continue; }
        if (marker == 0xD9) break;                              /* end of picture */
        if (marker == 0xDA) {                                   /* start of scan */
            if (!sawFrame) { jpeg->error = "the picture data is damaged"; break; }
            JpegDecodeScan(jpeg);
            sawScan = true;
            continue;
        }
        size_t segStart = jpeg->position;
        int segmentLength = JpegRead16(jpeg);
        if (segmentLength < 2 || segStart + (size_t)segmentLength > jpeg->size) { jpeg->error = "the file is cut short"; break; }
        size_t segEnd = segStart + (size_t)segmentLength;

        if (marker == 0xDB) {                                   /* quantisation tables */
            while (jpeg->position < segEnd) {
                int precision = jpeg->data[jpeg->position] >> 4, quantTableId = jpeg->data[jpeg->position] & 3;
                jpeg->position++;
                if (jpeg->position + (precision ? 128 : 64) > segEnd) { jpeg->error = "the picture data is damaged"; break; }
                for (int index = 0; index < 64; index++) {
                    if (precision) { jpeg->quantTables[quantTableId][index] = (uint16_t)(jpeg->data[jpeg->position] << 8 | jpeg->data[jpeg->position + 1]); jpeg->position += 2; }
                    else      jpeg->quantTables[quantTableId][index] = jpeg->data[jpeg->position++];
                }
            }
        } else if (marker == 0xC4) {                            /* Huffman tables */
            while (jpeg->position < segEnd) {
                int tableClass = jpeg->data[jpeg->position] >> 4, huffmanTableId = jpeg->data[jpeg->position] & 3, valueCount = 0;
                jpeg->position++;
                JpegHuffman *table = tableClass ? &jpeg->acTables[huffmanTableId] : &jpeg->dcTables[huffmanTableId];
                if (jpeg->position + 16 > segEnd) { jpeg->error = "the picture data is damaged"; break; }
                for (int length = 1; length <= 16; length++) { table->counts[length] = jpeg->data[jpeg->position++]; valueCount += table->counts[length]; }
                if (valueCount > 256 || jpeg->position + (size_t)valueCount > segEnd) { jpeg->error = "the picture data is damaged"; break; }
                memcpy(table->values, jpeg->data + jpeg->position, (size_t)valueCount);
                jpeg->position += (size_t)valueCount;
                JpegBuildHuffman(table);
            }
        } else if (marker == 0xC0 || marker == 0xC1) {          /* frame header: size and colour layout */
            if (segEnd - jpeg->position < 6) { jpeg->error = "the picture data is damaged"; break; }
            jpeg->position++;                                   /* bits per sample: always 8 here */
            jpeg->height = JpegRead16(jpeg);
            jpeg->width = JpegRead16(jpeg);
            jpeg->componentCount = jpeg->data[jpeg->position++];
            if (jpeg->componentCount != 1 && jpeg->componentCount != 3) { jpeg->error = "CMYK JPG files aren't supported"; break; }
            if (jpeg->width <= 0 || jpeg->height <= 0 || jpeg->width > 16384 || jpeg->height > 16384) { jpeg->error = "the picture is too big"; break; }
            if (jpeg->position + (size_t)jpeg->componentCount * 3 > segEnd) { jpeg->error = "the picture data is damaged"; break; }
            jpeg->maxHSampling = jpeg->maxVSampling = 1;
            for (int i = 0; i < jpeg->componentCount; i++) {
                JpegComponent *component = &jpeg->components[i];
                component->id = jpeg->data[jpeg->position++];
                component->hSampling = jpeg->data[jpeg->position] >> 4; component->vSampling = jpeg->data[jpeg->position] & 15; jpeg->position++;
                component->quantTable = jpeg->data[jpeg->position++] & 3;
                if (component->hSampling < 1 || component->hSampling > 4 || component->vSampling < 1 || component->vSampling > 4) { jpeg->error = "the picture data is damaged"; break; }
                if (component->hSampling > jpeg->maxHSampling) jpeg->maxHSampling = component->hSampling;
                if (component->vSampling > jpeg->maxVSampling) jpeg->maxVSampling = component->vSampling;
            }
            int mcusAcross = (jpeg->width + 8 * jpeg->maxHSampling - 1) / (8 * jpeg->maxHSampling), mcusDown = (jpeg->height + 8 * jpeg->maxVSampling - 1) / (8 * jpeg->maxVSampling);
            for (int i = 0; i < jpeg->componentCount && !jpeg->error; i++) {
                JpegComponent *component = &jpeg->components[i];
                component->blocksWide = mcusAcross * component->hSampling;
                component->blocksHigh = mcusDown * component->vSampling;
                free(component->pixels);                        /* (a second frame header: the first one's pixels go) */
                component->pixels = calloc((size_t)component->blocksWide * 8 * (size_t)component->blocksHigh * 8, 1);
                if (!component->pixels) jpeg->error = "out of memory";
            }
            sawFrame = true;
        } else if (marker == 0xC2 || marker == 0xC6 || marker == 0xCA) {
            jpeg->error = "\"progressive\" JPG files aren't supported - try saving it again as PNG";
        } else if (marker >= 0xC3 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            jpeg->error = "this kind of JPG isn't supported - try saving it again as PNG";
        } else if (marker == 0xDD) {                            /* restart interval */
            jpeg->restartInterval = JpegRead16(jpeg);
        } else if (marker == 0xE1) {
            JpegReadExif(jpeg, jpeg->position, segEnd - jpeg->position);
        }
        jpeg->position = segEnd;
    }
    if (!jpeg->error && !sawScan) jpeg->error = "the picture data is damaged";

    if (!jpeg->error) {
        uint8_t *pixels = malloc((size_t)jpeg->width * (size_t)jpeg->height * 3);
        if (!pixels) jpeg->error = "out of memory";
        else {
            for (int y = 0; y < jpeg->height; y++)
                for (int x = 0; x < jpeg->width; x++) {
                    float samples[3];
                    for (int i = 0; i < jpeg->componentCount; i++) {
                        JpegComponent *component = &jpeg->components[i];
                        int sampleX = x * component->hSampling / jpeg->maxHSampling, sampleY = y * component->vSampling / jpeg->maxVSampling;
                        samples[i] = component->pixels[sampleY * component->blocksWide * 8 + sampleX];
                    }
                    uint8_t *pixel = pixels + ((size_t)y * jpeg->width + x) * 3;
                    if (jpeg->componentCount == 1) { pixel[0] = pixel[1] = pixel[2] = (uint8_t)samples[0]; continue; }
                    /* brightness (Y) and colour differences (Cb, Cr) to red, green and blue */
                    float Y = samples[0], cb = samples[1] - 128, cr = samples[2] - 128;
                    float rgb[3] = {Y + 1.402f * cr, Y - 0.344136f * cb - 0.714136f * cr, Y + 1.772f * cb};
                    for (int index = 0; index < 3; index++) pixel[index] = (uint8_t)(rgb[index] < 0 ? 0 : rgb[index] > 255 ? 255 : (int)(rgb[index] + 0.5f));
                }
            image.data = pixels;
            image.width = jpeg->width;
            image.height = jpeg->height;
            image.mipmaps = 1;
            image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
            /* turn it the right way up */
            switch (jpeg->orientation) {
            case 2: ImageFlipHorizontal(&image); break;
            case 3: ImageRotate(&image, 180); break;
            case 4: ImageFlipVertical(&image); break;
            case 5: ImageRotateCW(&image); ImageFlipHorizontal(&image); break;
            case 6: ImageRotateCW(&image); break;
            case 7: ImageRotateCCW(&image); ImageFlipHorizontal(&image); break;
            case 8: ImageRotateCCW(&image); break;
            default: break;
            }
        }
    }
    for (int i = 0; i < 3; i++) free(jpeg->components[i].pixels);
    *error = jpeg->error;
    free(jpeg);
    return image;
}
