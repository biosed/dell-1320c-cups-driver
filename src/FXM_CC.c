/*
 * FXM_CC - Color Correction filter for Dell 1320c / FX DocuPrint C525A
 *
 * Passthrough implementation: reads FXRaster data and writes it unchanged.
 *
 * The original i386 binary performs color correction using proprietary CIL
 * (Color Image Library) functions including 3D LUT, LCC, CST, and NTSC
 * conversions. The color correction algorithms are too complex to faithfully
 * reimplement from decompiled code.
 *
 * When printing in color mode, this passthrough means colors may not be
 * perfectly calibrated. For mono mode, this has no effect.
 *
 * In the future, this could be enhanced with open-source ICC profile
 * color management (e.g., using LittleCMS).
 *
 * Usage (CUPS filter):
 *   FXM_CC job-id user title copies options [file]
 *
 * Input:  FXRaster format (from FXM_PR)
 * Output: FXRaster format (to FXM_ALC)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cups/cups.h>
#include <cups/ppd.h>

#define FXRASTER_HDR_SIZE 44
#define BUF_SIZE (1024 * 1024)

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bitsPerPixel;
    uint32_t bytesPerLine;
    uint32_t dataSize;
    uint32_t xRes;
    uint32_t yRes;
    uint32_t reserved1;
    uint32_t pageType;
    uint32_t landscape;
    uint32_t reserved2;
} FXRasterHeader;

static int ReadFXRasterHeader(int fd, FXRasterHeader *hdr)
{
    size_t remaining = sizeof(FXRasterHeader);
    char *p = (char *)hdr;
    while (remaining > 0) {
        ssize_t n = read(fd, p, remaining);
        if (n < 1) return 0;
        remaining -= (size_t)n;
        p += n;
    }
    return 1;
}

static int WriteFXRasterHeader(int fd, const FXRasterHeader *hdr)
{
    size_t remaining = sizeof(FXRasterHeader);
    const char *p = (const char *)hdr;
    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n < 1) return 0;
        remaining -= (size_t)n;
        p += n;
    }
    return 1;
}

/*
 * For mono mode (colorMode=0x11), the original converts 24-bit BGR to
 * 8-bit grayscale. We need to do this conversion to maintain compatibility
 * with the downstream HBPL filter which expects mono data for mono jobs.
 */
static int GetColorModeSetting(ppd_file_t *ppd)
{
    ppd_choice_t *c = ppdFindMarkedChoice(ppd, "FXColorMode");
    if (c) {
        if (strcmp(c->choice, "Mono") == 0 || strcmp(c->choice, "Grayscale") == 0)
            return 0x11; /* mono */
        if (strcmp(c->choice, "Color") == 0)
            return 0x16; /* color */
    }

    /* Also check ColorModel */
    c = ppdFindMarkedChoice(ppd, "ColorModel");
    if (c) {
        if (strcmp(c->choice, "Gray") == 0 || strcmp(c->choice, "Mono") == 0)
            return 0x11;
    }

    return 0x16; /* default: color */
}

int main(int argc, char *argv[])
{
    int inputFd = 0;
    ppd_file_t *ppd = NULL;
    int numOptions = 0;
    cups_option_t *options = NULL;
    FXRasterHeader hdr;
    FXRasterHeader outHdr;
    void *inBuf = NULL;
    void *outBuf = NULL;
    int colorMode;
    int ret = 0;

    setbuf(stderr, NULL);

    if (argc < 6 || argc > 7) {
        fprintf(stderr, "ERROR: fxcolorconv job-id user title copies options [file]\n");
        return 1;
    }

    /* Open input */
    if (argc == 7) {
        inputFd = open(argv[6], O_RDONLY);
        if (inputFd == -1) {
            fprintf(stderr, "ERROR: fxcolorconv: unable to open raster file.\n");
            return 1;
        }
    }

    /* Open PPD and parse options */
    {
        char *ppdPath = getenv("PPD");
        if (ppdPath) ppd = ppdOpenFile(ppdPath);
    }
    if (ppd) {
        ppdMarkDefaults(ppd);
        numOptions = cupsParseOptions(argv[5], 0, &options);
        if (numOptions > 0)
            cupsMarkOptions(ppd, numOptions, options);
    }

    colorMode = ppd ? GetColorModeSetting(ppd) : 0x16;

    fprintf(stderr, "DEBUG: FXM_CC: colorMode=0x%02x (%s)\n",
            colorMode, (colorMode == 0x11) ? "mono" : "color");

    /* Process pages */
    while (ReadFXRasterHeader(inputFd, &hdr)) {
        /* Copy header for output */
        memcpy(&outHdr, &hdr, sizeof(FXRasterHeader));

        if (colorMode == 0x11 && hdr.bitsPerPixel == 24) {
            /* Mono mode: convert 24-bit color to 8-bit grayscale */
            outHdr.bitsPerPixel = 8;
            outHdr.bytesPerLine = hdr.width; /* 1 byte per pixel */
            outHdr.dataSize = outHdr.bytesPerLine * outHdr.height;
            outHdr.landscape = 0; /* update for mono */

            /* Write output header */
            if (!WriteFXRasterHeader(1, &outHdr)) {
                fprintf(stderr, "ERROR: fxcolorconv: Write header failed.\n");
                ret = 1;
                break;
            }

            /* Process line by line */
            size_t inLineSize = hdr.bytesPerLine;
            size_t outLineSize = outHdr.bytesPerLine;

            if (!inBuf) inBuf = malloc(inLineSize);
            if (!outBuf) outBuf = malloc(outLineSize);
            if (!inBuf || !outBuf) {
                fprintf(stderr, "ERROR: fxcolorconv: Memory not available.\n");
                ret = 1;
                break;
            }

            uint32_t row;
            for (row = 0; row < hdr.height; row++) {
                /* Read input line */
                size_t remaining = inLineSize;
                char *p = (char *)inBuf;
                while (remaining > 0) {
                    ssize_t n = read(inputFd, p, remaining);
                    if (n < 1) {
                        fprintf(stderr, "ERROR: fxcolorconv: File read failed.\n");
                        ret = 1;
                        goto cleanup;
                    }
                    p += n;
                    remaining -= (size_t)n;
                }

                /* Convert BGR to grayscale using NTSC weights */
                const uint8_t *src = (const uint8_t *)inBuf;
                uint8_t *dst = (uint8_t *)outBuf;
                uint32_t px;
                for (px = 0; px < hdr.width; px++) {
                    /* Input is BGR: B=src[0], G=src[1], R=src[2] */
                    uint8_t b = src[0], g = src[1], r = src[2];
                    /* NTSC luminance: Y = 0.299*R + 0.587*G + 0.114*B */
                    uint32_t gray = (299 * r + 587 * g + 114 * b) / 1000;
                    if (gray > 255) gray = 255;
                    *dst = (uint8_t)gray;
                    src += 3;
                    dst++;
                }

                /* Write output line */
                size_t written = 0;
                p = (char *)outBuf;
                while (written < outLineSize) {
                    ssize_t n = write(1, p, outLineSize - written);
                    if (n < 1) {
                        fprintf(stderr, "ERROR: fxcolorconv: File write failed.\n");
                        ret = 1;
                        goto cleanup;
                    }
                    p += n;
                    written += (size_t)n;
                }
            }
        } else {
            /* Color mode or already matching format: pass through */
            if (!WriteFXRasterHeader(1, &hdr)) {
                fprintf(stderr, "ERROR: fxcolorconv: Write header failed.\n");
                ret = 1;
                break;
            }

            /* Read and write data in chunks */
            size_t remaining = hdr.dataSize;
            if (!inBuf) inBuf = malloc(BUF_SIZE);
            if (!inBuf) {
                fprintf(stderr, "ERROR: fxcolorconv: Memory not available.\n");
                ret = 1;
                break;
            }

            while (remaining > 0) {
                size_t toRead = (remaining < BUF_SIZE) ? remaining : BUF_SIZE;
                char *p = (char *)inBuf;
                size_t left = toRead;
                while (left > 0) {
                    ssize_t n = read(inputFd, p, left);
                    if (n < 1) {
                        fprintf(stderr, "ERROR: fxcolorconv: File read failed.\n");
                        ret = 1;
                        goto cleanup;
                    }
                    p += n;
                    left -= (size_t)n;
                }

                p = (char *)inBuf;
                left = toRead;
                while (left > 0) {
                    ssize_t n = write(1, p, left);
                    if (n < 1) {
                        fprintf(stderr, "ERROR: fxcolorconv: File write failed.\n");
                        ret = 1;
                        goto cleanup;
                    }
                    p += n;
                    left -= (size_t)n;
                }

                remaining -= toRead;
            }
        }
    }

cleanup:
    if (inBuf) free(inBuf);
    if (outBuf) free(outBuf);
    if (numOptions > 0) cupsFreeOptions(numOptions, options);
    if (ppd) ppdClose(ppd);
    if (inputFd > 0) close(inputFd);
    return ret;
}
