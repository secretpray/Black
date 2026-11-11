#ifndef BLACK_TEST_TEMP_INCLUDED
#define BLACK_TEST_TEMP_INCLUDED
/* A test's own directory under /tmp: mkdtemp, then removed with everything in
 * it when the test process exits, a failing return included. A test that
 * returns early on a failure (or a baseline built to fail) no longer leaves its
 * files behind. An abort or a crash still leaves the directory, as evidence.
 * Only the process that made it removes it, so a forked child's exit does not
 * pull it from under the parent. */
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { BLACK_TEST_TEMP_MAX = 4 };
static char black_test_temp_paths[BLACK_TEST_TEMP_MAX][256];
static int black_test_temp_count;
static pid_t black_test_temp_owner;

static int black_test_temp_remove(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st; (void)flag; (void)ftw;
    remove(path);
    return 0;
}

static void black_test_temp_cleanup(void)
{
    if (getpid() != black_test_temp_owner) return;
    for (int i = 0; i < black_test_temp_count; ++i)
        nftw(black_test_temp_paths[i], black_test_temp_remove, 16, FTW_DEPTH | FTW_PHYS);
}

/* mkdtemp(template), and its removal at exit; NULL as mkdtemp fails. */
static inline char *black_test_temp_dir(char *template_path)
{
    if (!mkdtemp(template_path)) return NULL;
    if (black_test_temp_count < BLACK_TEST_TEMP_MAX &&
        strlen(template_path) < sizeof black_test_temp_paths[0]) {
        if (!black_test_temp_count) {
            black_test_temp_owner = getpid();
            atexit(black_test_temp_cleanup);
        }
        strcpy(black_test_temp_paths[black_test_temp_count++], template_path);
    }
    return template_path;
}
#endif /* BLACK_TEST_TEMP_INCLUDED */
