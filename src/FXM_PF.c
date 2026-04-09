#define _POSIX_C_SOURCE 200809L

/*
 * Clean-room FXM_PF implementation.
 *
 * Behavior:
 * - read PostScript from stdin or argv[6]
 * - preserve it verbatim in a temporary file
 * - collect %%BeginFeature: *Key Value directives before page content
 * - merge those feature pairs into the CUPS option string
 * - exec the main filter named by FXMainFilter in the active PPD
 */

#include <cups/cups.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LINE_BUF_SIZE 4096
#define MAX_FEATURES 128

typedef struct {
    char key[256];
    char value[256];
} feature_t;

static int read_line(FILE *fp, char *buf, size_t size)
{
    if (fgets(buf, (int)size, fp) == NULL) {
        return 0;
    }
    return 1;
}

static int get_ppd_value(const char *key, char *value, size_t value_size)
{
    const char *ppd_path = getenv("PPD");
    FILE *fp;
    char line[LINE_BUF_SIZE];

    if (!ppd_path) {
        return -1;
    }

    fp = fopen(ppd_path, "r");
    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p;
        if (line[0] != '*' || line[1] == '%') {
            continue;
        }
        if (strncmp(line + 1, key, strlen(key)) != 0) {
            continue;
        }
        p = strchr(line, '"');
        if (!p) {
            continue;
        }
        p++;
        {
            char *end = strchr(p, '"');
            if (!end) {
                continue;
            }
            *end = '\0';
            snprintf(value, value_size, "%s", p);
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return -1;
}

static int feature_index(feature_t *features, int num_features, const char *key)
{
    int i;
    for (i = 0; i < num_features; i++) {
        if (strcmp(features[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static void add_feature(feature_t *features, int *num_features, const char *key, const char *value)
{
    int idx;
    if (!key[0] || !value[0]) {
        return;
    }
    idx = feature_index(features, *num_features, key);
    if (idx < 0) {
        if (*num_features >= MAX_FEATURES) {
            return;
        }
        idx = (*num_features)++;
    }
    snprintf(features[idx].key, sizeof(features[idx].key), "%s", key);
    snprintf(features[idx].value, sizeof(features[idx].value), "%s", value);
}

static int collect_ps_features(FILE *in, FILE *tmp, feature_t *features, int *num_features)
{
    char line[LINE_BUF_SIZE];
    int in_setup = 1;

    while (read_line(in, line, sizeof(line))) {
        fputs(line, tmp);

        if (in_setup && strncmp(line, "%%BeginFeature:", 15) == 0) {
            char key[256] = {0};
            char value[256] = {0};
            char *p = line + 15;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '*') p++;
            sscanf(p, "%255s %255s", key, value);
            add_feature(features, num_features, key, value);
        }

        if (strncmp(line, "%%EndPageSetup", 14) == 0 ||
            strncmp(line, "%%Page:", 7) == 0 ||
            strncmp(line, "gsave", 5) == 0) {
            in_setup = 0;
        }
    }

    return ferror(in) ? -1 : 0;
}

static void merge_options(char *dst, size_t dst_size, const char *base_opts, feature_t *features, int num_features)
{
    int i;
    dst[0] = '\0';
    if (base_opts && base_opts[0]) {
        snprintf(dst, dst_size, "%s", base_opts);
    }
    for (i = 0; i < num_features; i++) {
        size_t used = strlen(dst);
        if (used + strlen(features[i].key) + strlen(features[i].value) + 3 >= dst_size) {
            break;
        }
        if (used != 0) {
            strcat(dst, " ");
        }
        strcat(dst, features[i].key);
        strcat(dst, "=");
        strcat(dst, features[i].value);
    }
}

int main(int argc, char *argv[])
{
    FILE *in = stdin;
    FILE *tmp;
    char temp_path[1024];
    char main_filter[1024];
    char options_buf[65536];
    feature_t features[MAX_FEATURES];
    int num_features = 0;
    int temp_fd;
    pid_t pid;
    int status = 0;

    if (argc < 6 || argc > 7) {
        fprintf(stderr, "ERROR: FXM_PF job-id user title copies options [file]\n");
        return 1;
    }

    if (argc == 7 && argv[6] && argv[6][0]) {
        in = fopen(argv[6], "rb");
        if (!in) {
            perror(argv[6]);
            return 1;
        }
    }

    if (get_ppd_value("FXMainFilter", main_filter, sizeof(main_filter)) != 0) {
        fprintf(stderr, "ERROR: unable to read FXMainFilter from PPD\n");
        if (in != stdin) fclose(in);
        return 1;
    }

    temp_fd = cupsTempFd(temp_path, sizeof(temp_path));
    if (temp_fd < 0) {
        fprintf(stderr, "ERROR: unable to create temp file\n");
        if (in != stdin) fclose(in);
        return 1;
    }
    tmp = fdopen(temp_fd, "wb");
    if (!tmp) {
        close(temp_fd);
        unlink(temp_path);
        if (in != stdin) fclose(in);
        return 1;
    }

    if (collect_ps_features(in, tmp, features, &num_features) != 0) {
        fclose(tmp);
        unlink(temp_path);
        if (in != stdin) fclose(in);
        return 1;
    }
    fclose(tmp);
    if (in != stdin) {
        fclose(in);
    }

    merge_options(options_buf, sizeof(options_buf), argv[5], features, num_features);

    pid = fork();
    if (pid < 0) {
        unlink(temp_path);
        return 1;
    }
    if (pid == 0) {
        execl(main_filter,
              argv[0], argv[1], argv[2], argv[3], argv[4], options_buf, temp_path,
              (char *)NULL);
        fprintf(stderr, "ERROR: unable to exec %s (errno=%d)\n", main_filter, errno);
        _exit(1);
    }

    waitpid(pid, &status, 0);
    if ((WIFEXITED(status) && WEXITSTATUS(status) != 0) || WIFSIGNALED(status)) {
        kill(pid, SIGTERM);
    }

    unlink(temp_path);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
