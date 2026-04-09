/*
 * FXM_PM2FXR - PPM to FXRaster converter for Dell 1320c / FX DocuPrint C525A
 *
 * Reimplemented from Ghidra decompilation of the original i386 Linux ELF binary.
 *
 * Reads raw PPM (P6) output from Ghostscript and converts it to FXRaster format
 * (44-byte header + raster data). Handles page size imageable area cropping.
 *
 * Usage (CUPS filter):
 *   FXM_PM2FXR job-id user title copies options [file]
 *
 * Input:  PPM (ppmraw) format from Ghostscript via FXM_PS2PM
 * Output: FXRaster format (to FXM_SBP)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <cups/cups.h>
#include <cups/ppd.h>

/*
 * FXRaster header: 44 (0x2c) bytes
 * Offsets:
 *   0x00: width (pixels)
 *   0x04: height (pixels)
 *   0x08: bits per pixel (24 for color)
 *   0x0c: bytes per line
 *   0x10: data size (bytesPerLine * height)
 *   0x14: xRes
 *   0x18: yRes
 *   0x1c: orientation flag (vendor emits 1 when width > height)
 *   0x20: reserved / flags (vendor emits 0 here)
 *   0x24: color mode (vendor emits 1 for color input to downstream filters)
 *   0x28: reserved (0)
 */
typedef struct {
    uint32_t width;         /* 0x00 */
    uint32_t height;        /* 0x04 */
    uint32_t bitsPerPixel;  /* 0x08 */
    uint32_t bytesPerLine;  /* 0x0c */
    uint32_t dataSize;      /* 0x10 */
    uint32_t xRes;          /* 0x14 */
    uint32_t yRes;          /* 0x18 */
    uint32_t orientation;   /* 0x1c */
    uint32_t field_20;      /* 0x20 */
    uint32_t field_24;      /* 0x24 */
    uint32_t reserved2;     /* 0x28 */
} FXRasterHeader;

static int fxround(double d)
{
    int i = (int)d;
    if ((d - (double)i) >= 0.5)
        return i + 1;
    return i;
}

/*
 * ReadStringFromPPM - read a token from PPM, skipping comments (#...\n)
 * Returns 0 on success, -1 on error.
 */
static int ReadStringFromPPM(int fd, char *buf, int maxLen)
{
    char c;
    ssize_t n;
    int i;

    /* Read first char */
    n = read(fd, &c, 1);
    if (n < 1) return -1;

    maxLen--;

    /* Skip comment lines */
    while (c == '#') {
        /* Read until end of line */
        do {
            n = read(fd, &c, 1);
            if (n < 1) return -1;
        } while (c != '\n' && c != '\r');

        /* Read next char */
        n = read(fd, &c, 1);
        if (n < 1) return -1;
    }

    /* Skip leading whitespace */
    while (isspace((unsigned char)c)) {
        n = read(fd, &c, 1);
        if (n < 1) return -1;
    }

    /* Read token */
    buf[0] = c;
    i = 1;
    while (i < maxLen) {
        n = read(fd, &c, 1);
        if (n < 1) return -1;
        if (isspace((unsigned char)c)) break;
        buf[i++] = c;
    }
    buf[i] = '\0';

    return 0;
}

int main(int argc, char *argv[])
{
    int inputFd = 0;
    ppd_file_t *ppd = NULL;
    int numOptions = 0;
    cups_option_t *options = NULL;
    ppd_size_t *pageSize = NULL;
    char tokenBuf[256];
    uint32_t ppmWidth, ppmHeight, ppmMaxVal;
    int bitsPerChannel;
    int xRes, yRes;
    FXRasterHeader hdr;
    uint32_t srcBytesPerLine;
    uint32_t srcDataSize;
    void *lineBuffer = NULL;
    int ret = 0;

    setbuf(stderr, NULL);

    if (argc < 6 || argc > 7) {
        fprintf(stderr, "ERROR: ppmtofxraster job-id user title copies options [file]\n");
        return 1;
    }

    /* Open input */
    if (argc == 7) {
        inputFd = open(argv[6], O_RDONLY);
        if (inputFd == -1) {
            fprintf(stderr, "ERROR: unable to open raster file.\n");
            return 1;
        }
    }

    /* Open PPD and parse options */
    {
        char *ppdPath = getenv("PPD");
        if (ppdPath) ppd = ppdOpenFile(ppdPath);
        if (!ppd) {
            fprintf(stderr, "ERROR: can't open PPD.\n");
            return 1;
        }
    }
    ppdMarkDefaults(ppd);
    numOptions = cupsParseOptions(argv[5], 0, &options);
    if (numOptions > 0) {
        cupsMarkOptions(ppd, numOptions, options);
    }

    /* Get page size */
    pageSize = ppdPageSize(ppd, NULL);
    if (!pageSize) {
        fprintf(stderr, "ERROR: can't get page size from PPD.\n");
        ret = 1;
        goto cleanup;
    }

    /* Process PPM pages (Ghostscript can output multiple pages) */
    while (1) {
        /* Read PPM header: P6 */
        if (ReadStringFromPPM(inputFd, tokenBuf, sizeof(tokenBuf)) != 0)
            break; /* EOF - no more pages */

        /* Check magic */
        if (tokenBuf[0] != 'P' || tokenBuf[1] != '6') {
            /* Not P6 format */
            if (tokenBuf[0] == 'P' && tokenBuf[1] == '3') {
                fprintf(stderr, "ERROR: ASCII PPM (P3) not supported, need binary (P6).\n");
            } else {
                fprintf(stderr, "ERROR: not a PPM file (got '%s').\n", tokenBuf);
            }
            ret = 1;
            goto cleanup;
        }

        /* Read width */
        if (ReadStringFromPPM(inputFd, tokenBuf, sizeof(tokenBuf)) != 0) break;
        ppmWidth = (uint32_t)strtol(tokenBuf, NULL, 10);

        /* Read height */
        if (ReadStringFromPPM(inputFd, tokenBuf, sizeof(tokenBuf)) != 0) break;
        ppmHeight = (uint32_t)strtol(tokenBuf, NULL, 10);

        /* Read maxval */
        if (ReadStringFromPPM(inputFd, tokenBuf, sizeof(tokenBuf)) != 0) break;
        ppmMaxVal = (uint32_t)strtol(tokenBuf, NULL, 10);

        /* Calculate bits per channel */
        bitsPerChannel = 0;
        {
            uint32_t v = ppmMaxVal + 1;
            while (v > 1) { bitsPerChannel++; v /= 2; }
        }

        /* Get resolution from PPD */
        {
            ppd_choice_t *c = ppdFindMarkedChoice(ppd, "FXOutputMode");
            if (c && strcmp(c->choice, "300x300") == 0) {
                xRes = 300; yRes = 300;
            } else {
                xRes = 600; yRes = 600;
            }
        }

        /* Calculate source line dimensions */
        srcBytesPerLine = (uint32_t)((bitsPerChannel * 3 + 7) / 8) * ppmWidth;
        srcDataSize = srcBytesPerLine * ppmHeight;

        /* Calculate imageable area cropping */
        uint32_t cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
        uint32_t outWidth = ppmWidth, outHeight = ppmHeight;
        int needCrop = 0;

        {
            uint32_t imgWidth = (uint32_t)fxround(
                (double)((pageSize->right - pageSize->left) * (float)xRes) / 72.0);
            uint32_t imgHeight = (uint32_t)fxround(
                (double)((pageSize->top - pageSize->bottom) * (float)yRes) / 72.0);

            if (imgWidth < ppmWidth) {
                cropLeft = (ppmWidth - imgWidth) / 2;
                cropRight = (ppmWidth - imgWidth) - cropLeft;
                outWidth = imgWidth;
            }
            if (imgHeight < ppmHeight) {
                cropTop = (ppmHeight - imgHeight) / 2;
                cropBottom = (ppmHeight - imgHeight) - cropTop;
                outHeight = imgHeight;
            }
            if (cropLeft || cropRight || cropTop || cropBottom)
                needCrop = 1;
        }

        /* Build FXRaster header */
        memset(&hdr, 0, sizeof(hdr));
        hdr.width = outWidth;
        hdr.height = outHeight;
        hdr.bitsPerPixel = (uint32_t)(bitsPerChannel * 3);
        hdr.bytesPerLine = (uint32_t)((hdr.bitsPerPixel + 7) / 8) * outWidth;
        hdr.dataSize = hdr.bytesPerLine * outHeight;
        hdr.xRes = (uint32_t)xRes;
        hdr.yRes = (uint32_t)yRes;
        hdr.orientation = (outHeight < outWidth) ? 1 : 0;
        hdr.field_20 = 0;
        hdr.field_24 = 1;
        hdr.reserved2 = 0;

        /* Write FXRaster header */
        fwrite(&hdr, 1, sizeof(FXRasterHeader), stdout);

        /* Allocate line buffer */
        if (!lineBuffer) {
            lineBuffer = malloc(srcBytesPerLine);
            if (!lineBuffer) {
                fprintf(stderr, "ERROR: memory allocation failed.\n");
                ret = 1;
                goto cleanup;
            }
        }

        /* Read and write raster data, applying cropping */
        uint32_t row;
        for (row = 0; row < ppmHeight; row++) {
            /* Read source line */
            size_t remaining = srcBytesPerLine;
            char *p = (char *)lineBuffer;
            while (remaining > 0) {
                ssize_t n = read(inputFd, p, remaining);
                if (n < 1) {
                    fprintf(stderr, "ERROR: short read on PPM data.\n");
                    ret = 1;
                    goto cleanup;
                }
                p += n;
                remaining -= (size_t)n;
            }

            /* Skip rows in crop region */
            if (row < cropTop || row >= (ppmHeight - cropBottom))
                continue;

            /* Write cropped line */
            if (needCrop) {
                size_t bpp = (size_t)((hdr.bitsPerPixel + 7) / 8);
                size_t offset = cropLeft * bpp;
                size_t writeLen = hdr.bytesPerLine;
                if (fwrite((char *)lineBuffer + offset, 1, writeLen, stdout) < writeLen) {
                    fprintf(stderr, "ERROR: write failed.\n");
                    ret = 1;
                    goto cleanup;
                }
            } else {
                if (fwrite(lineBuffer, 1, srcBytesPerLine, stdout) < srcBytesPerLine) {
                    fprintf(stderr, "ERROR: write failed.\n");
                    ret = 1;
                    goto cleanup;
                }
            }
        }

        fflush(stdout);
    }

cleanup:
    if (lineBuffer) free(lineBuffer);
    if (numOptions > 0) cupsFreeOptions(numOptions, options);
    if (ppd) ppdClose(ppd);
    if (inputFd > 0) close(inputFd);
    return ret;
}
