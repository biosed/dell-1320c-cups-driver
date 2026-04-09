/*
 * fxr2hbpl.c - FXRaster to HBPL wrapper for Dell 1320c
 *
 * Reads pre-compressed FXRaster format from stdin, wraps it in PJL/HBPL
 * protocol, and writes to stdout. Replaces FXM_HBPL in the filter chain.
 *
 * Usage (CUPS filter convention):
 *   fxr2hbpl job-id user title copies options [filename]
 *
 * Compile:
 *   gcc -O2 -Wall -o fxr2hbpl fxr2hbpl.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <cups/cups.h>
#include <cups/ppd.h>

#define UEL "\033%-12345X"

/* ------------------------------------------------------------------ */
/* FXRaster input header (44 bytes, little-endian)                    */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t width;       /* offset  0: page width in points */
    uint32_t height;      /* offset  4: page height in points */
    uint32_t field_08;    /* offset  8: 32 */
    uint32_t field_0c;    /* offset 12: 1785 */
    uint32_t dataSize;    /* offset 16: compressed data size */
    uint32_t field_14;    /* offset 20: width again */
    uint32_t field_18;    /* offset 24: width again */
    uint32_t field_1c;    /* offset 28: 0 */
    uint32_t field_20;    /* offset 32: 0 */
    uint32_t field_24;    /* offset 36: color mode (2=color) */
    uint32_t field_28;    /* offset 40: input slot (3=bypass) */
} FXRasterHeader;

/* ------------------------------------------------------------------ */
/* Paper code mapping (width x height in points -> HBPL paper code)   */
/* ------------------------------------------------------------------ */

static int
paper_code(uint32_t w, uint32_t h)
{
    /* Letter 612x792 or 600x842 */
    if ((w == 612 && h == 792) || (w == 600 && h == 842))
        return 0;
    /* A4 595x842 */
    if (w == 595 && h == 842)
        return 4;
    /* Legal 612x1008 */
    if (w == 612 && h == 1008)
        return 1;
    /* Executive 522x756 */
    if (w == 522 && h == 756)
        return 5;
    /* B5 516x729 */
    if (w == 516 && h == 729)
        return 8;
    /* Default to Letter */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Write little-endian uint32                                         */
/* ------------------------------------------------------------------ */

static void
write_le32(unsigned char *buf, uint32_t val)
{
    buf[0] = (unsigned char)(val & 0xFF);
    buf[1] = (unsigned char)((val >> 8) & 0xFF);
    buf[2] = (unsigned char)((val >> 16) & 0xFF);
    buf[3] = (unsigned char)((val >> 24) & 0xFF);
}

/* ------------------------------------------------------------------ */
/* Write PJL header                                                   */
/* ------------------------------------------------------------------ */

static void
write_pjl(FILE *out, const char *user, const char *title, int copies)
{
    char hostname[256];
    struct hostent *he;
    time_t now;
    struct tm *tm;

    if (gethostname(hostname, sizeof(hostname)) != 0)
        strcpy(hostname, "localhost");

    now = time(NULL);
    tm = localtime(&now);

    fprintf(out, "%s", UEL);
    fprintf(out, "@PJL COMMENT DATE=%02d/%02d/%04d\n",
            tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
    fprintf(out, "@PJL COMMENT TIME=%02d:%02d:%02d\n",
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    fprintf(out, "@PJL COMMENT DNAME=%s\n", title);
    fprintf(out, "@PJL JOB MODE=PRINTER\n");
    fprintf(out, "@PJL SET JOBATTR=\"@LUNA=%s\"\n", user);
    fprintf(out, "@PJL SET JOBATTR=\"@JOAU=%s\"\n", user);
    fprintf(out, "@PJL SET JOBATTR=\"@CNAM=%s\"\n", hostname);
    he = gethostbyname(hostname);
    if (he && he->h_addr_list && he->h_addr_list[0]) {
        fprintf(out, "@PJL SET JOBATTR=\"@HOAD=%02X%02X%02X%02X\"\n",
                (unsigned char)he->h_addr_list[0][0],
                (unsigned char)he->h_addr_list[0][1],
                (unsigned char)he->h_addr_list[0][2],
                (unsigned char)he->h_addr_list[0][3]);
    }
    fprintf(out, "@PJL SET JOBATTR=\"@NLPP=1\"\n");
    fprintf(out, "@PJL SET COPIES=%d\n", copies);
    fprintf(out, "@PJL SET RENDERMODE=COLOR\n");
    fprintf(out, "@PJL SET DUPLEX=OFF\n");
    fprintf(out, "@PJL SET OUTBIN=FACEDOWN\n");
    fprintf(out, "@PJL SET PAPERDIRECTION=SEF\n");
    fprintf(out, "@PJL SET JOBATTR=\"@MSIP=NORMAL\"\n");
    fprintf(out, "@PJL SET RESOLUTION=600\n");
    fprintf(out, "@PJL SET BITSPERPIXEL=8\n");
    fprintf(out, "@PJL SET JOBATTR=\"@TRCH=ON\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@APSP=OFF\"\n");
    fprintf(out, "@PJL SET ECONOMODE=OFF\n");
    fprintf(out, "@PJL SET SLIPSHEET=OFF\n");
    fprintf(out, "@PJL SET JOBATTR=\"@SPSE=AUTO\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@PODR=NORMAL\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@DRDM=RASTER\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@BANR=DEVICE\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@IREC=OFF\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@TCPR=24\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@GCPR=24\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@ICPR=24\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@TUCR=24\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@GUCR=24\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@IUCR=24\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@TTRC=9\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@GTRC=9\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@ITRC=9\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@TSCR=9\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@GSCR=9\"\n");
    fprintf(out, "@PJL SET JOBATTR=\"@ISCR=9\"\n");
    fprintf(out, "@PJL ENTER LANGUAGE=HBPL\n");
}

static int
hbpl_input_slot(const char *options)
{
    ppd_file_t *ppd;
    cups_option_t *opts = NULL;
    int num_opts = 0;
    ppd_choice_t *choice;
    int slot = 0;
    const char *ppd_path = getenv("PPD");

    if (!ppd_path) {
        return 0;
    }

    ppd = ppdOpenFile(ppd_path);
    if (!ppd) {
        return 0;
    }

    ppdMarkDefaults(ppd);
    if (options && options[0]) {
        num_opts = cupsParseOptions(options, 0, &opts);
        if (num_opts > 0) {
            cupsMarkOptions(ppd, num_opts, opts);
        }
    }

    choice = ppdFindMarkedChoice(ppd, "FXInputSlot");
    if (choice != NULL) {
        if (strcmp(choice->choice, "1stTray-S") == 0) {
            slot = 2;
        } else if (strcmp(choice->choice, "1stTray-H") == 0) {
            slot = 2;
        } else if (strcmp(choice->choice, "2ndTray-H") == 0) {
            slot = 3;
        } else {
            slot = 0;
        }
    }

    if (opts) {
        cupsFreeOptions(num_opts, opts);
    }
    ppdClose(ppd);
    return slot;
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    FILE *in = stdin;
    FILE *out = stdout;
    const char *user  = "unknown";
    const char *title = "Untitled";
    const char *options = "";
    int copies = 1;
    int input_slot = 0;
    FXRasterHeader hdr;
    uint32_t page_num = 0;
    unsigned char *data_buf = NULL;
    size_t data_buf_size = 0;

    if (argc < 6) {
        fprintf(stderr, "Usage: %s job-id user title copies options [file]\n",
                argv[0]);
        return 1;
    }

    /* argv[1] = job-id (unused) */
    user  = argv[2];
    title = argv[3];
    copies = atoi(argv[4]);
    if (copies < 1) copies = 1;
    options = argv[5];
    input_slot = hbpl_input_slot(options);

    if (argc >= 7 && argv[6] && argv[6][0]) {
        in = fopen(argv[6], "rb");
        if (!in) {
            perror(argv[6]);
            return 1;
        }
    }

    /* PJL header */
    write_pjl(out, user, title, copies);

    /* Document begin: 41 81 a1 00 82 a2 01 00 83 a2 01 00 */
    {
        static const unsigned char doc_begin[] = {
            0x41, 0x81, 0xa1, 0x00,
            0x82, 0xa2, 0x01, 0x00,
            0x83, 0xa2, 0x01, 0x00
        };
        fwrite(doc_begin, 1, sizeof(doc_begin), out);
    }

    /* Process pages */
    while (fread(&hdr, sizeof(hdr), 1, in) == 1) {
        page_num++;

        /* Ensure data buffer is large enough */
        if (hdr.dataSize > data_buf_size) {
            free(data_buf);
            data_buf_size = hdr.dataSize + 4096;
            data_buf = malloc(data_buf_size);
            if (!data_buf) {
                fprintf(stderr, "ERROR: out of memory\n");
                return 1;
            }
        }

        /* Read compressed raster data */
        if (fread(data_buf, 1, hdr.dataSize, in) != hdr.dataSize) {
            fprintf(stderr, "ERROR: short read on raster data\n");
            return 1;
        }

        /* --- Page header (78 bytes) --- */
        {
            unsigned char ph[78];
            int pc = paper_code(hdr.width, hdr.height);
            int off = 0;

            ph[off++] = 0x43;                          /* page begin */
            ph[off++] = 0x91; ph[off++] = 0xa1;
            ph[off++] = (unsigned char)pc;              /* paper code */
            ph[off++] = 0x92; ph[off++] = 0xa1;
            ph[off++] = 0x02;                           /* vendor page attribute */
            ph[off++] = 0x93; ph[off++] = 0xa1;
            ph[off++] = 0x01;                           /* unknown */
            ph[off++] = 0x94; ph[off++] = 0xa1;
            ph[off++] = 0x02;                           /* color mode */
            ph[off++] = 0x95; ph[off++] = 0xc2;
            ph[off++] = 0x00; ph[off++] = 0x00;
            ph[off++] = 0x00; ph[off++] = 0x00;        /* paper dims zeros */
            ph[off++] = 0x96; ph[off++] = 0xa1;
            ph[off++] = (unsigned char)input_slot;      /* input slot */
            ph[off++] = 0x97; ph[off++] = 0xc3;
            ph[off++] = 0x5e; ph[off++] = 0x00;        /* 600dpi = 0x005e */
            ph[off++] = 0x5e; ph[off++] = 0x00;        /* 600dpi = 0x005e */
            ph[off++] = 0x98; ph[off++] = 0xa1;
            ph[off++] = 0x00;
            ph[off++] = 0x99; ph[off++] = 0xa4;
            write_le32(&ph[off], page_num);  off += 4;  /* page number */
            ph[off++] = 0x9a; ph[off++] = 0xc4;
            write_le32(&ph[off], hdr.width); off += 4;  /* width */
            write_le32(&ph[off], hdr.height); off += 4; /* height */
            ph[off++] = 0x9b; ph[off++] = 0xa1;
            ph[off++] = 0x00;
            ph[off++] = 0x9c; ph[off++] = 0xa1;
            ph[off++] = 0x01;
            ph[off++] = 0x9d; ph[off++] = 0xa1;
            ph[off++] = 0x16;                           /* compression type */
            ph[off++] = 0x9e; ph[off++] = 0xa1;
            ph[off++] = 0x00;
            ph[off++] = 0x9f; ph[off++] = 0xa1;
            ph[off++] = 0x01;
            ph[off++] = 0xa0; ph[off++] = 0xa1;
            ph[off++] = 0x20;                           /* bits = 0x20 */
            ph[off++] = 0xa1; ph[off++] = 0xa1;
            ph[off++] = 0x00;
            ph[off++] = 0xa2; ph[off++] = 0xc4;
            write_le32(&ph[off], hdr.width); off += 4;  /* width again */
            write_le32(&ph[off], hdr.height); off += 4; /* height again */

            fwrite(ph, 1, (size_t)off, out);
        }

        /* --- Band marker --- */
        {
            unsigned char bm[12];
            uint32_t band_blob_size = 24 + hdr.dataSize;

            bm[0] = 0x51;
            bm[1] = 0x52;
            bm[2] = 0xa3;
            bm[3] = 0xa1;
            bm[4] = 0x00;
            bm[5] = 0xa4;
            bm[6] = 0xb1;
            bm[7] = 0xa4;
            write_le32(&bm[8], band_blob_size);

            fwrite(bm, 1, 12, out);
        }

        /* --- Band header (24 bytes) --- */
        {
            static const unsigned char band_hdr[24] = {
                0x18, 0x00, 0x00, 0x00,
                0x00, 0x01, 0x00, 0x00,
                0x10, 0x32, 0x04, 0x00,
                0xa1, 0x42, 0x00, 0x00,
                0x00, 0x00, 0xff, 0x00,
                0x00, 0x00, 0x00, 0x00
            };
            fwrite(band_hdr, 1, 24, out);
        }

        /* --- Compressed raster data (verbatim) --- */
        fwrite(data_buf, 1, hdr.dataSize, out);

        /* --- Page end: "SD" --- */
        fwrite("SD", 1, 2, out);
    }

    /* Document end: "B" + UEL + "@PJL EOJ\n" */
    fprintf(out, "B%s@PJL EOJ\n", UEL);

    free(data_buf);
    if (in != stdin)
        fclose(in);
    fflush(out);

    return 0;
}
