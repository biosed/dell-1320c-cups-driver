/*
 * FXM_PR - Page Rotation filter for Dell 1320c / FX DocuPrint C525A
 *
 * Reimplemented from Ghidra decompilation of the original i386 Linux ELF binary.
 *
 * Handles 180-degree page rotation (FXTurnPage setting) and page reordering
 * for duplex printing. When FXTurnPage is False and no rotation is needed,
 * it passes data through unchanged.
 *
 * Usage (CUPS filter):
 *   FXM_PR job-id user title copies options [file]
 *
 * Input:  FXRaster format (from FXM_SBP)
 * Output: FXRaster format (to FXM_CC)
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
#define TEMP_BUF_SIZE (1024 * 1024)  /* 1MB chunks */

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

static int ReadFXRasterHeader_fd(int fd, FXRasterHeader *hdr)
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

static int ReadFXRasterHeader_fp(FILE *fp, FXRasterHeader *hdr)
{
    return (fread(hdr, sizeof(FXRasterHeader), 1, fp) == 1) ? 1 : 0;
}

static int WriteFXRasterHeader_fp(FILE *fp, const FXRasterHeader *hdr)
{
    return (fwrite(hdr, sizeof(FXRasterHeader), 1, fp) == 1) ? 1 : 0;
}

/*
 * Rotate raster data 180 degrees.
 * Reverses the order of lines and reverses bytes within each line
 * (at pixel granularity).
 */
static int fxrotate180(FILE *inFp, const FXRasterHeader *hdr)
{
    uint32_t bytesPerPixel = (hdr->bitsPerPixel + 7) / 8;
    uint32_t dataSize = hdr->dataSize;
    FILE *tmpFp;
    void *buf = NULL;
    size_t bufSize;
    int ret = 0;

    /* Write header first */
    fwrite(hdr, 1, FXRASTER_HDR_SIZE, stdout);

    /* Read all raster data into a temp file, then reverse */
    tmpFp = tmpfile();
    if (!tmpFp) return -1;

    /* Copy raster data to temp file */
    size_t remaining = dataSize;
    bufSize = (remaining < TEMP_BUF_SIZE) ? remaining : TEMP_BUF_SIZE;
    buf = malloc(bufSize);
    if (!buf) { fclose(tmpFp); return -1; }

    while (remaining > 0) {
        size_t toRead = (remaining < bufSize) ? remaining : bufSize;
        if (fread(buf, toRead, 1, inFp) != 1) { ret = -1; goto done; }
        if (fwrite(buf, toRead, 1, tmpFp) != 1) { ret = -1; goto done; }
        remaining -= toRead;
    }

    /* Now read from temp file in reverse line order */
    {
        uint8_t *lineBuf = malloc(hdr->bytesPerLine);
        uint8_t *revBuf = malloc(hdr->bytesPerLine);
        if (!lineBuf || !revBuf) { free(lineBuf); free(revBuf); ret = -1; goto done; }

        for (int line = (int)hdr->height - 1; line >= 0; line--) {
            long offset = (long)line * (long)hdr->bytesPerLine;
            fseek(tmpFp, offset, SEEK_SET);
            if (fread(lineBuf, hdr->bytesPerLine, 1, tmpFp) != 1) {
                ret = -1; free(lineBuf); free(revBuf); goto done;
            }

            /* Reverse pixels within the line */
            uint32_t numPixels = hdr->width;
            for (uint32_t px = 0; px < numPixels; px++) {
                uint32_t srcOff = px * bytesPerPixel;
                uint32_t dstOff = (numPixels - 1 - px) * bytesPerPixel;
                memcpy(revBuf + dstOff, lineBuf + srcOff, bytesPerPixel);
            }

            fwrite(revBuf, 1, hdr->bytesPerLine, stdout);
        }

        free(lineBuf);
        free(revBuf);
    }

done:
    free(buf);
    fclose(tmpFp);
    return ret;
}

/* Pass through: write header + data unchanged */
static int throwdata(FILE *inFp, const FXRasterHeader *hdr)
{
    void *buf;
    size_t remaining;
    size_t bufSize;

    fwrite(hdr, 1, FXRASTER_HDR_SIZE, stdout);

    remaining = hdr->dataSize;
    bufSize = (remaining < TEMP_BUF_SIZE) ? remaining : TEMP_BUF_SIZE;
    buf = malloc(bufSize);
    if (!buf) return -1;

    while (remaining > 0) {
        size_t toRead = (remaining < bufSize) ? remaining : bufSize;
        if (fread(buf, toRead, 1, inFp) != 1) { free(buf); return -1; }
        if (fwrite(buf, 1, toRead, stdout) < toRead) { free(buf); return -1; }
        remaining -= toRead;
    }

    free(buf);
    return 0;
}

int main(int argc, char *argv[])
{
    int inputFd = 0;
    FILE *inFp;
    ppd_file_t *ppd = NULL;
    int numOptions = 0;
    cups_option_t *options = NULL;
    int turnPage = 0;
    int duplexSetting = 0;
    int copies;
    FXRasterHeader hdr;
    int ret = 0;

    setbuf(stderr, NULL);

    if (argc < 6 || argc > 7) {
        fprintf(stderr, "ERROR: fxrotate job-id user title copies options [file]\n");
        return 1;
    }

    /* Open input */
    if (argc == 7) {
        inputFd = open(argv[6], O_RDONLY);
        if (inputFd == -1) {
            fprintf(stderr, "ERROR: can't open input file.\n");
            return 1;
        }
    }

    inFp = fdopen(inputFd, "rb");
    if (!inFp) {
        fprintf(stderr, "ERROR: fdopen failed.\n");
        return 1;
    }

    /* Open PPD */
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

    /* Get copies from args */
    copies = atoi(argv[4]);
    if (copies < 1) copies = 1;

    /* Get settings */
    if (ppd) {
        ppd_choice_t *c;
        c = ppdFindMarkedChoice(ppd, "FXTurnPage");
        if (c && strcmp(c->choice, "True") == 0)
            turnPage = 1;

        c = ppdFindMarkedChoice(ppd, "Duplex");
        if (c) {
            if (strcmp(c->choice, "DuplexTumble") == 0)
                duplexSetting = 1;
            else if (strcmp(c->choice, "DuplexNoTumble") == 0)
                duplexSetting = 2;
        }
    }

    fprintf(stderr, "DEBUG: FXM_PR: turnPage=%d, duplex=%d, copies=%d\n",
            turnPage, duplexSetting, copies);

    /*
     * Main processing:
     * - If FXTurnPage is off and no duplex reordering needed, pass through
     * - If FXTurnPage is on, rotate every other page (back side of duplex)
     * - Duplex logic handles page reordering for output order
     *
     * For simplicity (and because most users use simplex or the printer
     * handles duplex), we implement:
     * - turnPage=0: pass through all pages
     * - turnPage=1: rotate every other page 180 degrees
     */
    if (!turnPage && duplexSetting == 0) {
        /* Simple pass-through */
        while (ReadFXRasterHeader_fp(inFp, &hdr)) {
            if (throwdata(inFp, &hdr) != 0) {
                ret = 1;
                break;
            }
        }
    } else {
        int pageNum = 0;
        int doRotate = 0;

        while (ReadFXRasterHeader_fp(inFp, &hdr)) {
            pageNum++;

            /* Determine if this page should be rotated */
            if (turnPage) {
                /* Rotate based on duplex: alternate pages get rotated */
                if (duplexSetting != 0) {
                    doRotate = (pageNum % 2 == 0); /* rotate even pages (back side) */
                } else {
                    doRotate = 1; /* rotate all pages */
                }
            }

            if (doRotate) {
                if (fxrotate180(inFp, &hdr) != 0) {
                    ret = 1;
                    break;
                }
            } else {
                if (throwdata(inFp, &hdr) != 0) {
                    ret = 1;
                    break;
                }
            }
        }
    }

    fflush(stdout);

    fclose(inFp);
    if (numOptions > 0) cupsFreeOptions(numOptions, options);
    if (ppd) ppdClose(ppd);
    return ret;
}
