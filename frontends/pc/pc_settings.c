#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "pc_compat.h"
#include "pc_gfx.h"
#include "pc_pad.h"
#include "pc_settings.h"

enum { S_OFF, S_MAIN, S_PAD, S_REBIND, S_FILES };
static const char *const MAIN_ROWS[] = {"Controller buttons", "Find the ROM files", "Full screen", "Quit"};
#define MAIN_COUNT 4
#define ROW_H 22
#define SHOWN 8

static SettingsHooks hk;
static int screen, cursor, scroll, rebind_action;
static Uint32 rebind_until;
static int files_direct;   /* the browser was opened from outside the settings: closing it leaves the settings screen too */

typedef struct { char name[200]; int dir; } Entry;
static Entry *entries;
static int entry_count;
static char cwd[1024];

void settings_init(const SettingsHooks *hooks) { hk = *hooks; }
int settings_active(void) { return screen != S_OFF; }
void settings_open(void) { screen = S_MAIN; cursor = 0; scroll = 0; }

/* ---- file browser ---- */
static int is_rom_name(const char *n)
{
    const char *dot = strrchr(n, '.');
    if (!dot) return 0;
    return !strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc") || !strcasecmp(dot, ".zip") || !strcasecmp(dot, ".bin") || !strcasecmp(dot, ".rom");
}

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

static void list_dir(const char *path)
{
    free(entries);
    entries = NULL;
    entry_count = 0;
    int cap = 64;
    entries = malloc(sizeof(Entry) * (size_t)cap);
    snprintf(cwd, sizeof cwd, "%s", path);
    size_t l = strlen(cwd);
    while (l > 1 && (cwd[l - 1] == '/' || cwd[l - 1] == '\\') && !(l == 3 && cwd[1] == ':')) cwd[--l] = 0;
    DIR *d = opendir(cwd);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof entries[0].name) continue;
            char full[1400];
            struct stat st;
            snprintf(full, sizeof full, "%s/%s", cwd, e->d_name);
            if (stat(full, &st)) continue;
            int dir = S_ISDIR(st.st_mode);
            if (!dir && !is_rom_name(e->d_name)) continue;
            if (entry_count == cap) { cap *= 2; entries = realloc(entries, sizeof(Entry) * (size_t)cap); }
            snprintf(entries[entry_count].name, sizeof entries[0].name, "%s", e->d_name);
            entries[entry_count++].dir = dir;
        }
        closedir(d);
    }
    qsort(entries, (size_t)entry_count, sizeof(Entry), entry_cmp);
    cursor = 0;
    scroll = 0;
}

/* rows: 0 "use this folder", 1 "up one level", then the entries */
static int file_rows(void) { return entry_count + 2; }

static void go_up(void)
{
    char up[1024];
    snprintf(up, sizeof up, "%s", cwd);
    char *s = strrchr(up, '/');
    char *b = strrchr(up, '\\');
    if (b && (!s || b > s)) s = b;
    if (!s) return;
    if (s == up) up[1] = 0; else if (s[-1] == ':') s[1] = 0; else *s = 0;
    list_dir(up);
}

void settings_open_files(void)
{
    const char *home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    char start[1024] = ".";
    if (home) {
        snprintf(start, sizeof start, "%s/Downloads", home);
        struct stat st;
        if (stat(start, &st) || !S_ISDIR(st.st_mode)) snprintf(start, sizeof start, "%s", home);
    }
    list_dir(start);
    screen = S_FILES;
    files_direct = 1;
}

static void leave_files(void)
{
    if (files_direct) screen = S_OFF;
    else { screen = S_MAIN; cursor = 1; scroll = 0; }
}

static void file_pick(int row)
{
    if (row == 0) {
        hk.note(hk.setup(cwd));
        leave_files();
    } else if (row == 1) go_up();
    else {
        Entry e = entries[row - 2];
        char full[1400];
        snprintf(full, sizeof full, "%s/%s", cwd, e.name);
        if (e.dir) list_dir(full);
        else { hk.note(hk.setup(full)); leave_files(); }
    }
}

/* ---- input ---- */
static void clamp_scroll(int rows)
{
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + SHOWN) scroll = cursor - SHOWN + 1;
    if (scroll > rows - SHOWN) scroll = rows - SHOWN;
    if (scroll < 0) scroll = 0;
}

void settings_nav(UiButton b)
{
    int rows = screen == S_MAIN ? MAIN_COUNT : screen == S_PAD ? PA_COUNT + 2 : screen == S_FILES ? file_rows() : 0;
    if (screen == S_REBIND) return;
    if (b == BTN_DOWN && cursor < rows - 1) cursor++;
    else if (b == BTN_UP && cursor > 0) cursor--;
    else if (b == BTN_R && screen == S_FILES) cursor = cursor + SHOWN < rows ? cursor + SHOWN : rows - 1;
    else if (b == BTN_L && screen == S_FILES) cursor = cursor > SHOWN ? cursor - SHOWN : 0;
    else if (b == BTN_B) {
        if (screen == S_MAIN) screen = S_OFF;
        else if (screen == S_FILES) leave_files();
        else { screen = S_MAIN; cursor = 0; scroll = 0; }
    } else if (b == BTN_A) {
        if (screen == S_MAIN) {
            if (cursor == 0) { screen = S_PAD; cursor = 0; scroll = 0; }
            else if (cursor == 1) { settings_open_files(); files_direct = 0; }
            else if (cursor == 2) hk.fullscreen();
            else hk.quit();
        } else if (screen == S_PAD) {
            if (cursor < PA_COUNT) { rebind_action = cursor; rebind_until = SDL_GetTicks() + 6000; screen = S_REBIND; }
            else if (cursor == PA_COUNT) { pad_cfg_reset(); pad_cfg_save(); }
            else { screen = S_MAIN; cursor = 0; scroll = 0; }
        } else if (screen == S_FILES) file_pick(cursor);
    }
    clamp_scroll(rows);
}

int settings_raw(const SDL_Event *e)
{
    if (screen != S_REBIND) return 0;
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) { screen = S_PAD; return 1; }
    int code = pad_event_code(e);
    if (code >= 0) {
        pad_set(rebind_action, code);
        pad_cfg_save();
        screen = S_PAD;
        return 1;
    }
    return e->type == SDL_KEYDOWN;
}

/* ---- drawing ---- */
static SDL_Color col(int i) { return gfx_ui_color(i); }

static void rect(float k, float ox, float oy, float x, float y, float w, float h, int c) { gfx_rect(ox + x * k, oy + y * k, w * k, h * k, col(c)); }
static void text(float k, float ox, float oy, float x, float y, int c, int align, const char *s) { gfx_text(ox + x * k, oy + y * k, 12 * k, col(c), align, s); }

static void row(float k, float ox, float oy, int i, const char *left, const char *right, int selected)
{
    float y = 32 + i * ROW_H;
    rect(k, ox, oy, 8, y, 304, ROW_H - 3, selected ? 2 : 1);
    if (selected) rect(k, ox, oy, 8, y, 4, ROW_H - 3, 5);
    text(k, ox, oy, 20, y + 1, 3, 0, left);
    if (right) text(k, ox, oy, 306, y + 1, selected ? 3 : 4, 2, right);
}

void settings_draw(float k, float ox, float oy)
{
    if (screen == S_OFF) return;
    rect(k, ox, oy, 0, 0, 320, 240, 0);
    rect(k, ox, oy, 0, 0, 320, 28, 1);
    const char *title = screen == S_MAIN ? "Settings" : screen == S_FILES ? "Find the ROM files" : "Controller buttons";
    text(k, ox, oy, 12, 6, 5, 0, title);
    const char *hint = "A choose   B back";
    (void)hint;
    if (screen == S_MAIN) {
        for (int i = 0; i < MAIN_COUNT; i++) row(k, ox, oy, i, MAIN_ROWS[i], NULL, i == cursor);
    } else if (screen == S_PAD || screen == S_REBIND) {
        for (int i = 0; i < SHOWN && scroll + i < PA_COUNT + 2; i++) {
            int r = scroll + i;
            if (r < PA_COUNT) row(k, ox, oy, i, pad_action_name(r), pad_code_name(pad_code(r)), r == cursor);
            else row(k, ox, oy, i, r == PA_COUNT ? "Reset to defaults" : "Back", NULL, r == cursor);
        }
        hint = "A change   B back";
        if (screen == S_REBIND) {
            rect(k, ox, oy, 20, 80, 280, 80, 2);
            rect(k, ox, oy, 20, 80, 280, 3, 5);
            char msg[96];
            snprintf(msg, sizeof msg, "Press the button for %s", pad_action_name(rebind_action));
            text(k, ox, oy, 160, 98, 3, 1, msg);
            text(k, ox, oy, 160, 122, 4, 1, "Esc or wait to cancel");
            int left = (int)(rebind_until - SDL_GetTicks());
            if (left <= 0) screen = S_PAD;
            else rect(k, ox, oy, 20, 154, 280.0f * left / 6000.0f, 6, 5);
        }
    } else {
        char shown[60];
        int n = file_rows();
        for (int i = 0; i < SHOWN && scroll + i < n; i++) {
            int r = scroll + i;
            if (r == 0) row(k, ox, oy, i, "[ Use this folder ]", NULL, r == cursor);
            else if (r == 1) row(k, ox, oy, i, "..", NULL, r == cursor);
            else {
                const Entry *e = &entries[r - 2];
                snprintf(shown, sizeof shown, "%s%s", e->name, e->dir ? "/" : "");
                row(k, ox, oy, i, shown, NULL, r == cursor);
            }
        }
        size_t l = strlen(cwd);
        text(k, ox, oy, 12, 210, 4, 0, l > 44 ? cwd + l - 44 : cwd);
        hint = "A open   B close   L R page";
    }
    text(k, ox, oy, 12, 226, 4, 0, hint);
}
