/*
 * FXM_ALC - Compression filter for Dell 1320c / FX DocuPrint C525A
 *
 * Reads uncompressed FXRaster (RGB, 3 bytes/pixel) from stdin,
 * compresses using the sq21 algorithm, and writes compressed FXRaster to stdout.
 *
 * Usage: FXM_ALC job-id user title copies options [file]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "sq21_simple.h"

/* ------------------------------------------------------------------ */
/* FXRaster header (44 bytes)                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t width;       /* 0: page width (points/pixels) */
    uint32_t height;      /* 4: page height */
    uint32_t field_08;    /* 8: depth/format info */
    uint32_t lineSize;    /* 12: bytes per line */
    uint32_t dataSize;    /* 16: data size (updated on output) */
    uint32_t field_14;    /* 20 */
    uint32_t field_18;    /* 24 */
    uint32_t field_1c;    /* 28 */
    uint32_t field_20;    /* 32 */
    uint32_t field_24;    /* 36: color mode */
    uint32_t field_28;    /* 40: tray/slot */
} FXRasterHeader;

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    FXRasterHeader hdr;
    int fd_in = 0;
    ssize_t n;

    (void)argc; (void)argv;

    if (argc >= 7) {
        fd_in = open(argv[6], O_RDONLY);
        if (fd_in < 0) { perror("open"); return 1; }
    }

    /* Process pages */
    while ((n = read(fd_in, &hdr, sizeof(hdr))) == sizeof(hdr)) {
        uint32_t pixel_width = hdr.lineSize / 3;  /* RGB: 3 bytes/pixel */
        uint32_t height = hdr.height;
        uint32_t dpi = hdr.field_14 > hdr.field_18 ? hdr.field_14 : hdr.field_18;

        /* Read all RGB data */
        size_t rgb_size = (size_t)height * hdr.lineSize;
        uint8_t *rgb_data = malloc(rgb_size);
        if (!rgb_data) { 
            fprintf(stderr, "ERROR: OOM\n"); 
            return 1; 
        }

        size_t total_read = 0;
        while (total_read < rgb_size) {
            n = read(fd_in, rgb_data + total_read, rgb_size - total_read);
            if (n <= 0) break;
            total_read += n;
        }

        /* Compress using sq21 */
        uint8_t *compressed = NULL;
        size_t compressed_size = 0;
        
        if ((hdr.field_24 == 0)
                ? (sq21_compress_8(rgb_data, hdr.lineSize, height, dpi, &compressed, &compressed_size) != 0)
                : (sq21_compress_888(rgb_data, pixel_width, height, dpi, &compressed, &compressed_size) != 0)) {
            fprintf(stderr, "ERROR: sq21 compression failed\n");
            free(rgb_data);
            if (fd_in != 0) close(fd_in);
            return 1;
        }
        free(rgb_data);

        FXRasterHeader out_hdr = hdr;
        out_hdr.dataSize = (uint32_t)compressed_size;
        out_hdr.field_28 = 3;
        if (hdr.field_24 == 0) {
            out_hdr.width = (hdr.lineSize + 7) & ~7u;
            out_hdr.field_08 = 8;
            out_hdr.field_24 = 0;
        } else {
            out_hdr.width = (pixel_width + 7) & ~7u;
            out_hdr.field_08 = 32;
            out_hdr.field_24 = 2;
        }
        write(STDOUT_FILENO, &out_hdr, sizeof(out_hdr));

        /* Write compressed data */
        write(STDOUT_FILENO, compressed, compressed_size);
        free(compressed);
    }

    if (fd_in != 0) close(fd_in);
    return 0;
}
