#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>

#include "raylib.h"
#include "subprocess.h"

#define log_err(fmt, ...) fprintf(stderr, "Error: " fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) fprintf(stdout, "Info: " fmt, ##__VA_ARGS__)

#define list_append(list, item) \
    do { \
        if ((list)->items == NULL) { \
            (list)->capacity = 1; \
            (list)->items = malloc(sizeof(*(list)->items) * (list)->capacity); \
        } else { \
            if ((list)->count == (list)->capacity) { \
                (list)->capacity *= 2; \
                (list)->items = realloc((list)->items, sizeof(*(list)->items) * (list)->capacity); \
            } \
        } \
        (list)->items[(list)->count++] = (item); \
    } while(0)

#define list_free(list) \
    do { \
        if ((list)->items != NULL) { \
            free((list)->items); \
        } \
    } while(0)

typedef struct {
    char **items;
    size_t count, capacity;
} Out_Lines;

#define out_lines_free(ol) \
    do { \
        for (int i = 0; i < (ol)->count; i++) { \
            free((void*)(ol)->items[i]); \
        } \
        list_free((ol)); \
    } while (0)

typedef struct {
    long long interval;
    char *path;
    pthread_t thread;
    Out_Lines out_lines;
} Script;

typedef struct {
    Script *items;
    size_t capacity, count;
} Scripts;

#define WINDOW_WIDTH    400
#define WINDOW_HEIGHT   600

bool change_cwd(void)
{
    char buf[PATH_MAX];
    if (readlink("/proc/self/exe", buf, sizeof(buf)) < 0) {
        log_err("Could not read /proc/self/exe: %s\n", strerror(errno));
        return false;
    }
    char *dir = dirname(buf);

    if (chdir(dir) < 0) {
        log_err("Could not change CWD to %s: %s\n", dir, strerror(errno));
        return false;
    }

    memset(buf, 0, sizeof(buf));
    if (getcwd(buf, sizeof(buf)) == NULL) {
        log_err("Could not get CWD: %s\n", strerror(errno));
        return false;
    }

    log_info("CWD: %s\n", buf);

    return true;
}

#define MAX_SCRIPT_LINE 500
#define FONT_SIZE       20
#define PADDING         30

static Font font;
static Scripts scripts = {0};

void init_assets(void)
{
    font = LoadFontEx("./assets/fonts/SpaceMono-Regular.ttf", FONT_SIZE, NULL, 0);
}

void deinit_assets(void)
{
    UnloadFont(font);
}

void init_scripts(void)
{
    FILE *scripts_config = fopen("./scripts.config", "r");
    if (scripts_config == NULL) {
        log_err("Could not open ./scripts.config: %s\n", strerror(errno));
        exit(1);
    }

    fseek(scripts_config, 0L, SEEK_END);
    long size = ftell(scripts_config);
    rewind(scripts_config);

    char *buf = malloc(size);
    fread(buf, size, 1, scripts_config);

    char *saveptr1, *saveptr2;

    char *p = strtok_r(buf, "\n", &saveptr1);
    while (p != NULL) {
        char *line = malloc(strlen(p)+1);
        strcpy(line, p);
        Script script;

        char *interval_string = strtok_r(line, " ", &saveptr2);
        script.interval = atoll(interval_string);
        char *path = strtok_r(NULL, " ", &saveptr2);
        script.path = malloc(strlen(path)+1);
        strcpy(script.path, path);

        free(line);
        list_append(&scripts, script);
        p = strtok_r(NULL, "\n", &saveptr1);
    }

    free(buf);
    fclose(scripts_config);

    for (int i = 0; i < scripts.count; i++) {
        log_info("Script %d %s\n", scripts.items[i].interval, scripts.items[i].path);
    }
}

void deinit_scripts(void)
{
    for (int i = 0; i < scripts.count; i++) {
        free((void*)scripts.items[i].path);
        out_lines_free(&scripts.items[i].out_lines);
    }
    list_free(&scripts);
}

void *do_script(void *arg)
{
    Script *script = (Script *)arg;

    clock_t start = clock();
    for (;;) {
        clock_t end = clock();
        float seconds = (float)(end - start) / CLOCKS_PER_SEC;
        if ((long long)seconds == script->interval) {
            struct subprocess_s sp;
            const char *commandline[] = { script->path, NULL };
            int result = subprocess_create(commandline, 0, &sp);
            if (result != 0) {
                log_err("Script %s failed\n", script->path);
            } else {
                script->out_lines.count = 0;
                FILE *pstdout = subprocess_stdout(&sp);
                char line[MAX_SCRIPT_LINE];
                while (fgets(line, sizeof(line), pstdout) != NULL) {
                    char *line_copy = malloc(strlen(line)+1);
                    strcpy(line_copy, line);
                    list_append(&script->out_lines, line_copy);
                }
            }
            
            start = clock();
        }
    }
    return NULL;
}

int main(void)
{
    change_cwd();
    init_scripts();
    
    for (int i = 0; i < scripts.count; i++) {
        pthread_create(&scripts.items[i].thread, NULL, &do_script, &scripts.items[i]);
    }

    SetConfigFlags(FLAG_WINDOW_UNDECORATED);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "xtatus");
    SetExitKey(KEY_NULL);

    init_assets();

    while (!WindowShouldClose()) {
        BeginDrawing();

        ClearBackground(BLACK);
        int y = 0;
        for (int i = 0; i < scripts.count; i++) {
            Script *script = &scripts.items[i];
            for (int j = 0; j < script->out_lines.count; j++) {
                char *line = script->out_lines.items[j];
                Vector2 position = { PADDING, PADDING + FONT_SIZE * y };
                DrawTextEx(font, line, position, FONT_SIZE, 0.0f, WHITE);
                y++;
            }
        }

        EndDrawing();
    }
    
    for (int i = 0; i < scripts.count; i++) {
        pthread_join(scripts.items[i].thread, NULL);
    }

    deinit_scripts();
    deinit_assets();
    CloseWindow();
    return 0;
}
