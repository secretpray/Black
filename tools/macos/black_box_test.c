/* The Black Box: the rolling window saves beside the settings with a settings
 * copy, its session stream follows BLACK_BLACK_BOX_DIR, and the saved file
 * replays through black_BoxReplay at the recorded times. */
#undef NDEBUG
#include "black_box.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int count_lines(const char *path, int comments)
{
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char line[512];
    int count = 0;
    while (fgets(line, sizeof line, file))
        if (!comments ? line[0] != '#' : line[0] == '#') ++count;
    fclose(file);
    return count;
}

static int contains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    char line[512];
    int found = 0;
    while (!found && fgets(line, sizeof line, file))
        found = strstr(line, needle) != NULL;
    fclose(file);
    return found;
}

/* A saved window may start after poll zero: the replay clock starts at the
 * first sample, not at the beginning of the old run. */
static int replay_probe(const char *path)
{
    setenv("BLACK_INPUT_REPLAY", path, 1);
    uint16_t buttons = 0;
    uint8_t analog[8] = {0};
    int16_t axes[4] = {0};
    if (!black_BoxReplay(&buttons, analog, axes)) return 1;
    return buttons == 0x0002 && axes[0] == 123 ? 0 : 2;
}

int main(int argc, char **argv)
{
    if (argc == 2) return replay_probe(argv[1]);
    char directory[] = "/tmp/black-box-test.XXXXXX";
    if (!mkdtemp(directory)) return 10;
    char settings[1200], path[1200], session[1200] = "";
    snprintf(settings, sizeof settings, "%s/black-settings.ini", directory);
    FILE *ini = fopen(settings, "w");
    if (!ini) return 11;
    fprintf(ini, "render_scale=3\nfps_counter=1\n");
    fclose(ini);
    setenv("BLACK_SETTINGS_FILE", settings, 1);
    setenv("BLACK_BLACK_BOX_DIR", directory, 1);

    int16_t zero[4] = {0, 0, 0, 0};
    uint8_t idle[8] = {0};
    assert(!black_BoxPath()[0]);
    black_BoxRecord(0, idle, zero);
    usleep(20000);
    uint8_t pressed[8] = {200, 0, 0, 0, 0, 0, 0, 0};
    int16_t axes[4] = {-12000, 4000, 0, 0};
    black_BoxRecord(0x0001, pressed, axes);

    /* The session stream is written from the first poll. */
    DIR *dir = opendir(directory);
    assert(dir);
    struct dirent *entry;
    while ((entry = readdir(dir)))
        if (!strncmp(entry->d_name, "black-box-", 10)) {
            snprintf(session, sizeof session, "%s/%s", directory, entry->d_name);
            break;
        }
    closedir(dir);
    if (!session[0]) return 1;
    if (count_lines(session, 1) < 4 || count_lines(session, 0) < 2) return 2;
    if (!contains(session, "render_scale=3")) return 3;

    if (!black_BoxSave()) return 4;
    const char *saved = black_BoxPath();
    if (!saved[0] || strncmp(saved, directory, strlen(directory))) return 5;
    if (count_lines(saved, 1) < 4) return 6;
    /* Two polls, two samples: the second state differs, so no heartbeat hold. */
    if (count_lines(saved, 0) != 2) return 7;

    setenv("BLACK_INPUT_REPLAY", saved, 1);
    uint16_t buttons = 0xFFFF;
    uint8_t analog[8];
    int16_t replay_axes[4] = {7, 7, 7, 7};
    assert(black_BoxReplay(&buttons, analog, replay_axes));
    if (buttons != 0 || analog[0] != 0 || replay_axes[0] != 0) return 8;
    usleep(120000);
    assert(black_BoxReplay(&buttons, analog, replay_axes));
    if (buttons != 0x0001 || analog[0] != 200 || replay_axes[0] != -12000 ||
        replay_axes[1] != 4000) return 9;

    /* A pruned head starts after poll zero and must still replay at once. */
    char pruned[1200];
    snprintf(pruned, sizeof pruned, "%s/pruned.csv", directory);
    FILE *cut = fopen(pruned, "w");
    if (!cut) return 12;
    fprintf(cut, "# t_ms buttons lt rt a b x y black white lx ly rx ry\n"
                 "40000 0x0002 0 0 0 0 0 0 0 0 123 0 0 0\n"
                 "40100 0x0000 0 0 0 0 0 0 0 0 0 0 0 0\n");
    fclose(cut);
    pid_t pid = fork();
    if (pid == 0) { execl(argv[0], argv[0], pruned, (char *)NULL); _exit(127); }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) return 13;

    unsetenv("BLACK_INPUT_REPLAY");
    unlink(session);
    unlink(saved);
    unlink(settings);
    unlink(pruned);
    rmdir(directory);
    puts("Black Box: session stream, settings copy, save beside settings and replay: PASS");
    return 0;
}
