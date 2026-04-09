/*
 * FXM_MF - Main Filter (chain orchestrator) for Dell 1320c / FX DocuPrint C525A
 *
 * Reimplemented from Ghidra decompilation of the original i386 Linux ELF binary.
 *
 * Reads FXFilterChain and FXFilterDir from the PPD file, then forks/execs
 * each filter in sequence, piping stdout->stdin between them.
 * The first filter gets CUPS input on stdin (or file arg), and the last
 * filter's stdout goes to the CUPS backend (our stdout).
 *
 * Usage (invoked by FXM_PF):
 *   FXM_MF job-id user title copies options [file]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#define MAX_FILTERS  16
#define MAX_PATH_LEN 2048
#define MAX_LINE_LEN 2048

/*
 * FXGetPPDAttribute - read a PPD attribute value from the PPD file.
 * Searches for lines matching *key: "value" and extracts value.
 * Returns 0 on success, 1 if key not found, -1 on error.
 */
static int FXGetPPDAttribute(const char *ppdPath, const char *key, char *outBuf, size_t outBufLen)
{
    FILE *fp;
    char line[MAX_LINE_LEN];
    size_t keyLen;

    if (!ppdPath || !key || !outBuf)
        return -1;

    fp = fopen(ppdPath, "r");
    if (!fp)
        return -1;

    keyLen = strlen(key);
    outBuf[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        /* PPD lines start with '*', skip comments (*%) */
        if (line[0] != '*' || line[1] == '%')
            continue;

        /* Check if key matches (after the '*') */
        if (strncmp(line + 1, key, keyLen) != 0)
            continue;

        /* Find the opening quote */
        char *q1 = strchr(line, '"');
        if (!q1) continue;
        q1++; /* skip the quote */

        /* Find the closing quote */
        char *q2 = strchr(q1, '"');
        if (!q2) continue;

        size_t len = (size_t)(q2 - q1);
        if (len >= outBufLen)
            len = outBufLen - 1;
        memcpy(outBuf, q1, len);
        outBuf[len] = '\0';

        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1; /* key not found */
}

int main(int argc, char *argv[])
{
    char filterChain[MAX_LINE_LEN];
    char filterDir[MAX_PATH_LEN];
    char *filterNames[MAX_FILTERS];
    char *filterPaths[MAX_FILTERS];
    int  numFilters = 0;
    int  (*pipes)[2] = NULL;
    pid_t *pids = NULL;
    int  exitCode = 0;
    char *ppdPath;
    int  i, ret;

    setbuf(stderr, NULL);

    if (argc < 6 || argc > 7) {
        fprintf(stderr, "ERROR: fxmainfilter job-id user title copies options [file]\n");
        return 1;
    }

    /* Get PPD path */
    ppdPath = getenv("PPD");
    if (!ppdPath) {
        fprintf(stderr, "ERROR: PPD environment variable not set.\n");
        return 1;
    }

    /* Read FXFilterChain from PPD */
    ret = FXGetPPDAttribute(ppdPath, "FXFilterChain", filterChain, sizeof(filterChain));
    if (ret != 0) {
        fprintf(stderr, "ERROR: FXFilterChain not found in PPD.\n");
        return 1;
    }

    /* Read FXFilterDir from PPD */
    ret = FXGetPPDAttribute(ppdPath, "FXFilterDir", filterDir, sizeof(filterDir));
    if (ret != 0) {
        /* Default to /usr/lib/cups/filter */
        strcpy(filterDir, "/usr/lib/cups/filter");
    }

    /* Parse filter chain - comma-separated list of filter names */
    {
        char *p = filterChain;
        char *tok;
        while ((tok = strsep(&p, ",")) != NULL) {
            /* Skip leading spaces */
            while (*tok == ' ') tok++;
            /* Trim trailing spaces */
            char *end = tok + strlen(tok) - 1;
            while (end > tok && *end == ' ') { *end = '\0'; end--; }
            if (*tok == '\0') continue;

            if (numFilters >= MAX_FILTERS) {
                fprintf(stderr, "ERROR: too many filters in chain.\n");
                return 1;
            }
            filterNames[numFilters] = strdup(tok);
            numFilters++;
        }
    }

    if (numFilters == 0) {
        fprintf(stderr, "ERROR: empty filter chain.\n");
        return 1;
    }

    fprintf(stderr, "DEBUG: FXM_MF: %d filters in chain\n", numFilters);

    /* Build full paths for each filter */
    for (i = 0; i < numFilters; i++) {
        filterPaths[i] = malloc(strlen(filterDir) + strlen(filterNames[i]) + 2);
        if (!filterPaths[i]) {
            fprintf(stderr, "ERROR: memory allocation failed.\n");
            exitCode = 1;
            goto cleanup;
        }
        /* If filter name already starts with '/', use it as-is */
        if (filterNames[i][0] == '/') {
            strcpy(filterPaths[i], filterNames[i]);
        } else {
            sprintf(filterPaths[i], "%s/%s", filterDir, filterNames[i]);
        }
        fprintf(stderr, "DEBUG: FXM_MF: filter[%d] = %s\n", i, filterPaths[i]);
    }

    /* Create pipes between filters.
     * We need numFilters pipe pairs.
     * pipes[0] is the stdin of filter 0 (comes from our stdin, fd 0)
     * pipes[i] between filter i-1 and filter i for i > 0
     * The last filter's stdout goes to our stdout (fd 1)
     *
     * Actually, we need numFilters-1 pipes for inter-filter communication.
     * Filter 0 reads from our stdin (or file arg), filter N-1 writes to our stdout.
     */
    if (numFilters > 1) {
        pipes = calloc(numFilters - 1, sizeof(int[2]));
        if (!pipes) {
            fprintf(stderr, "ERROR: memory allocation failed.\n");
            exitCode = 1;
            goto cleanup;
        }
        for (i = 0; i < numFilters - 1; i++) {
            if (pipe(pipes[i]) != 0) {
                fprintf(stderr, "ERROR: pipe() failed: %s\n", strerror(errno));
                exitCode = 1;
                goto cleanup;
            }
        }
    }

    /* Fork each filter */
    pids = calloc(numFilters, sizeof(pid_t));
    if (!pids) {
        fprintf(stderr, "ERROR: memory allocation failed.\n");
        exitCode = 1;
        goto cleanup;
    }

    for (i = 0; i < numFilters; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            fprintf(stderr, "ERROR: fork() failed: %s\n", strerror(errno));
            exitCode = 1;
            goto cleanup;
        }

        if (pids[i] == 0) {
            /* Child process */

            /* Set up stdin: first filter reads from parent's stdin (or file),
             * others read from previous pipe */
            if (i > 0) {
                dup2(pipes[i-1][0], 0);
            }
            /* else: inherit parent's stdin */

            /* Set up stdout: last filter writes to parent's stdout,
             * others write to next pipe */
            if (i < numFilters - 1) {
                dup2(pipes[i][1], 1);
            }
            /* else: inherit parent's stdout */

            /* Close all pipe fds in child */
            if (pipes) {
                int j;
                for (j = 0; j < numFilters - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }

            /* For filters after the first, clear the file argument
             * (they read from stdin, not a file) */
            if (i > 0 && argc == 7) {
                argv[6] = NULL;
            }

            fprintf(stderr, "DEBUG: FXM_MF: exec filter %s (pid %d)\n",
                    filterPaths[i], getpid());

            /* Execute the filter */
            if (argc == 7 && i == 0) {
                execl(filterPaths[i], argv[0], argv[1], argv[2], argv[3],
                      argv[4], argv[5], argv[6], (char *)NULL);
            } else {
                execl(filterPaths[i], argv[0], argv[1], argv[2], argv[3],
                      argv[4], argv[5], (char *)NULL);
            }

            /* If we get here, execl failed */
            fprintf(stderr, "ERROR: Unable to exec filter %s: %s\n",
                    filterPaths[i], strerror(errno));
            _exit(errno);
        }
    }

    /* Parent: close all pipe fds */
    if (pipes) {
        for (i = 0; i < numFilters - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
    }

    /* Wait for all children */
    for (i = 0; i < numFilters; i++) {
        int status;
        pid_t wpid = wait(&status);

        fprintf(stderr, "DEBUG: FXM_MF: process %d exited\n", wpid);

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "ERROR: FXM_MF: filter exited with status %d\n",
                    WEXITSTATUS(status));
            /* Kill remaining children */
            int j;
            for (j = 0; j < numFilters; j++) {
                if (pids[j] > 0) {
                    kill(pids[j], SIGTERM);
                }
            }
            exitCode = 1;
            break;
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "ERROR: FXM_MF: filter killed by signal %d\n",
                    WTERMSIG(status));
            int j;
            for (j = 0; j < numFilters; j++) {
                if (pids[j] > 0) {
                    kill(pids[j], SIGTERM);
                }
            }
            exitCode = 1;
            break;
        }

        /* Mark this pid as done */
        {
            int j;
            for (j = 0; j < numFilters; j++) {
                if (pids[j] == wpid) {
                    pids[j] = 0;
                    break;
                }
            }
        }
    }

cleanup:
    if (pipes)
        free(pipes);
    if (pids)
        free(pids);
    for (i = 0; i < numFilters; i++) {
        if (filterNames[i]) free(filterNames[i]);
        if (filterPaths[i]) free(filterPaths[i]);
    }

    fprintf(stderr, "DEBUG: FXM_MF: End fxmainfilter\n");
    return exitCode;
}
