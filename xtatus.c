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
    bool first_run;
} Script;

typedef struct {
    Script *items;
    size_t capacity, count;
} Scripts;

typedef struct {
    char *program;
    int window_x, window_y;
    int window_width, window_height;
    bool transparent;
} Options;

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
static pthread_mutex_t scripts_lock;
static Options options = {
    .window_width = 300,
    .window_height = 600,
    .window_x = -1,
    .window_y = -1,
    .transparent = true,
};

void init_assets(void)
{
    font = LoadFontEx("./assets/fonts/SpaceMono-Regular.ttf", FONT_SIZE*5, NULL, 0);
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
        script.first_run = true;

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

    if (pthread_mutex_init(&scripts_lock, NULL) != 0) {
        log_err("scripts_lock init failed\n");
        exit(1);
    }
}

void deinit_scripts(void)
{
    for (int i = 0; i < scripts.count; i++) {
        free((void*)scripts.items[i].path);
        out_lines_free(&scripts.items[i].out_lines);
    }
    list_free(&scripts);

    pthread_mutex_destroy(&scripts_lock);
}

void *do_script(void *arg)
{
    Script *script = (Script *)arg;

    clock_t start = clock();
    for (;;) {
        clock_t end = clock();
        float seconds = (float)(end - start) / CLOCKS_PER_SEC;
        if ((long long)seconds == script->interval || script->first_run) {
            struct subprocess_s sp;
            const char *commandline[] = { script->path, NULL };
            int result = subprocess_create(commandline, 0, &sp);
            if (result != 0) {
                log_err("Could not create subprocess for %s\n", script->path);
            } else {
                int proc_return;
                result = subprocess_join(&sp, &proc_return);
                if (result != 0) {
                    log_err("Could not wait for subprocess %s\n", script->path);
                } else if (proc_return == 0) {
                    pthread_mutex_lock(&scripts_lock);

                    script->out_lines.count = 0;
                    if (script->first_run) script->first_run = false;

                    FILE *pstdout = subprocess_stdout(&sp);
                    char line[MAX_SCRIPT_LINE];
                    while (fgets(line, sizeof(line), pstdout) != NULL) {
                        char *line_copy = malloc(strlen(line)+1);
                        strcpy(line_copy, line);
                        list_append(&script->out_lines, line_copy);
                    }

                    pthread_mutex_unlock(&scripts_lock);
                } else {
                    log_err("Script %s failed\n", script->path);
                }
            }
            
            start = clock();
        }
    }
    return NULL;
}

void usage(void)
{
    log_info("Usage: %s -[OPTIONS]\n", options.program);
    log_info("%-30s %-20s\n", "-wx", "Windwo x position");
    log_info("%-30s %-20s\n", "-wy", "Windwo y position");
    log_info("%-30s %-20s\n", "-ww", "Windwo width");
    log_info("%-30s %-20s\n", "-wh", "Windwo height");
    log_info("%-30s %-20s\n", "-nt", "Make background not transparent");
}

#define shift(argc, argv) \
    (argc <= 0 ? (log_err("Not enough arguments\n"), usage(), exit(1)) : (void)0, --argc, *argv++)

int main(int argc, char ** argv)
{
    options.program = shift(argc, argv);

    while (argc > 0) {
        char *opt_name = shift(argc, argv);

        if (strcmp(opt_name, "-wx") == 0) {
            options.window_x = atoi(shift(argc, argv));
        } else if (strcmp(opt_name, "-wy") == 0) {
            options.window_y = atoi(shift(argc, argv));
        } else if (strcmp(opt_name, "-ww") == 0) {
            options.window_width = atoi(shift(argc, argv));
        } else if (strcmp(opt_name, "-wh") == 0) {
            options.window_height = atoi(shift(argc, argv));
        } else if (strcmp(opt_name, "-nt") == 0) {
            options.transparent = false;
        } else {
            log_err("Unknown option %s\n", opt_name);
            exit(1);
        }
    }

    change_cwd();
    init_scripts();
    
    for (int i = 0; i < scripts.count; i++) {
        pthread_create(&scripts.items[i].thread, NULL, &do_script, &scripts.items[i]);
    }

    unsigned int flags = FLAG_WINDOW_UNDECORATED;
    if (options.transparent) {
        flags |= FLAG_WINDOW_TRANSPARENT;
    }

    SetConfigFlags(flags);
    InitWindow(options.window_width, options.window_height, "xtatus");
    SetExitKey(KEY_NULL);

    init_assets();
    
    int m = GetCurrentMonitor();
    if (options.window_x == -1) {
        options.window_x = (GetMonitorWidth(m)-options.window_width)/2;
    }
    if (options.window_y == -1) {
        options.window_y = (GetMonitorHeight(m)-options.window_height)/2;
    }

    RenderTexture2D target = LoadRenderTexture(options.window_width, options.window_height);

    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        Vector2 win_pos = GetWindowPosition();
        if (win_pos.x != options.window_x || win_pos.y != options.window_y) {
            SetWindowPosition(options.window_x, options.window_y);
        }

        BeginTextureMode(target);

        ClearBackground(BLANK);
        int y = 0;
        pthread_mutex_lock(&scripts_lock);
        for (int i = 0; i < scripts.count; i++) {
            Script *script = &scripts.items[i];
            for (int j = 0; j < script->out_lines.count; j++) {
                char *line = script->out_lines.items[j];
                Vector2 position = { PADDING, PADDING + FONT_SIZE * y };
                DrawTextEx(font, line, position, FONT_SIZE, 0.0f, WHITE);
                y++;
            }
            Vector2 position1 = { PADDING, PADDING + FONT_SIZE * y };
            Vector2 position2 = { options.window_width - PADDING, PADDING + FONT_SIZE * y };
            DrawLineV(position1, position2, ORANGE);
        }
        pthread_mutex_unlock(&scripts_lock);

        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLANK);
        DrawTexturePro(
                target.texture,
                (Rectangle){0, 0, options.window_width, -options.window_height},
                (Rectangle){0, 0, options.window_width, options.window_height},
                (Vector2){0, 0},
                0.0f,
                WHITE
        );
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
