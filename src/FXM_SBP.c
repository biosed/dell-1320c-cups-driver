/*
 * FXM_SBP - Skip Blank Pages filter for Dell 1320c / FX DocuPrint C525A
 *
 * Reimplemented from Ghidra decompilation of the original i386 Linux ELF binary.
 *
 * Reads FXRaster data and optionally skips blank (all-white) pages.
 * If SkipBlankPages is not enabled, passes data through unchanged.
 *
 * Usage (CUPS filter):
 *   FXM_SBP job-id user title copies options [file]
 *
 * Input:  FXRaster format (from FXM_PM2FXR)
 * Output: FXRaster format (to FXM_PR)
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

/* Check if a block of data is all 0xFF (white) */
static int CheckIfWhiteData(const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < size; i++) {
        if (p[i] != 0xFF)
            return 0; /* not white */
    }
    return 1; /* all white */
}

static int GetSkipBlankPagesSetting(ppd_file_t *ppd)
{
    ppd_choice_t *c = ppdFindMarkedChoice(ppd, "FXSkipBlankPages");
    if (!c) return 0; /* default: don't skip */
    if (strcmp(c->choice, "True") == 0)
        return 1;
    return 0;
}

int main(int argc, char *argv[])
{
    int inputFd = 0;
    ppd_file_t *ppd = NULL;
    int numOptions = 0;
    cups_option_t *options = NULL;
    int skipBlanks;
    FXRasterHeader hdr;
    void *buffer = NULL;
    int bufLines = 0;
    int ret = 0;

    setbuf(stderr, NULL);

    if (argc < 6 || argc > 7) {
        fprintf(stderr, "ERROR: (filtername) job-id user title copies options [file]\n");
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
    }
    if (ppd) {
        ppdMarkDefaults(ppd);
        numOptions = cupsParseOptions(argv[5], 0, &options);
        if (numOptions > 0)
            cupsMarkOptions(ppd, numOptions, options);
    }

    skipBlanks = ppd ? GetSkipBlankPagesSetting(ppd) : 0;

    fprintf(stderr, "DEBUG: FXM_SBP: skipBlanks=%d\n", skipBlanks);

    /* Process pages */
    while (ReadFXRasterHeader(inputFd, &hdr)) {
        uint32_t skippedBytes = 0;
        int pageIsBlank = 1; /* assume blank until proven otherwise */
        int headerWritten = 0;

        if (!skipBlanks) {
            /* Not skipping blanks: write header immediately */
            fwrite(&hdr, 1, FXRASTER_HDR_SIZE, stdout);
            headerWritten = 1;
        }

        /* Allocate buffer if needed */
        if (!buffer) {
            /* Read in chunks: up to 1MB at a time */
            bufLines = (int)(0x100000 / hdr.bytesPerLine);
            if (bufLines < 1) bufLines = 1;
            buffer = malloc((size_t)hdr.bytesPerLine * (size_t)bufLines);
            if (!buffer) {
                fprintf(stderr, "ERROR: memory allocation failed.\n");
                ret = 1;
                break;
            }
        }

        /* Read and process raster data */
        uint32_t linesRemaining = hdr.height;
        while (linesRemaining > 0) {
            uint32_t linesToRead = (linesRemaining > (uint32_t)bufLines) ?
                                   (uint32_t)bufLines : linesRemaining;
            size_t bytesToRead = (size_t)linesToRead * hdr.bytesPerLine;

            /* Read chunk */
            size_t remaining = bytesToRead;
            char *p = (char *)buffer;
            while (remaining > 0) {
                ssize_t n = read(inputFd, p, remaining);
                if (n < 1) {
                    fprintf(stderr, "ERROR: short read on raster data.\n");
                    ret = 1;
                    goto cleanup;
                }
                p += n;
                remaining -= (size_t)n;
            }

            if (!skipBlanks || !headerWritten) {
                if (skipBlanks && pageIsBlank) {
                    /* Check if this chunk is white */
                    if (CheckIfWhiteData(buffer, bytesToRead)) {
                        skippedBytes += (uint32_t)bytesToRead;
                        linesRemaining -= linesToRead;
                        continue;
                    }
                    /* Not blank - write header and any skipped white data */
                    pageIsBlank = 0;
                    fwrite(&hdr, 1, FXRASTER_HDR_SIZE, stdout);
                    headerWritten = 1;

                    /* Write skipped white bytes */
                    if (skippedBytes > 0) {
                        uint32_t i;
                        for (i = 0; i < skippedBytes; i++) {
                            fputc(0xFF, stdout);
                        }
                    }
                }
                /* Write this chunk */
                if (fwrite(buffer, 1, bytesToRead, stdout) < bytesToRead) {
                    ret = 1;
                    goto cleanup;
                }
            } else {
                /* Header already written (not skipping or already decided not blank) */
                if (fwrite(buffer, 1, bytesToRead, stdout) < bytesToRead) {
                    ret = 1;
                    goto cleanup;
                }
            }

            linesRemaining -= linesToRead;
        }

        /* If skipBlanks and page was entirely blank, don't output anything for it */
        /* (header was never written, data was never written) */

        fflush(stdout);
    }

cleanup:
    if (buffer) free(buffer);
    if (numOptions > 0) cupsFreeOptions(numOptions, options);
    if (ppd) ppdClose(ppd);
    if (inputFd > 0) close(inputFd);
    return ret;
}
