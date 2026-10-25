#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include "smz3_ui.h"

#define ROW_H 20
#define LIST_Y 44
#define ROWS_SHOWN 7
#define GRID_COLS 5
#define GRID_ROWS 3
#define GRID_X 10
#define GRID_Y 28
#define GRID_CELL 60

static void put(SmzUi *u, int kind, int x, int y, int w, int h, int color, int align, const char *text)
{
    if (u->cmd_count >= UI_MAX_CMDS) return;
    UiCmd *c = &u->cmd[u->cmd_count++];
    c->kind = kind; c->x = x; c->y = y; c->w = w; c->h = h; c->color = color; c->align = align;
    snprintf(c->text, sizeof c->text, "%s", text ? text : "");
}
static void hit(SmzUi *u, int kind, int idx, int x, int y, int w, int h)
{
    if (u->hit_count >= UI_MAX_HITS) return;
    UiHit *t = &u->hit[u->hit_count++];
    t->x = x; t->y = y; t->w = w; t->h = h; t->kind = kind; t->idx = idx;
}
static void rect(SmzUi *u, int x, int y, int w, int h, int color) { put(u, 0, x, y, w, h, color, 0, NULL); }
static void text(SmzUi *u, int x, int y, int color, int align, const char *t) { put(u, 1, x, y, 0, 0, color, align, t); }

void ui_init(SmzUi *u)
{
    memset(u, 0, sizeof *u);
    for (int i = 0; i < UI_OPT_COUNT; i++) u->value[i] = UI_OPTS[i].def;
    for (int g = 0; g < 4; g++) { strcpy(u->sprites[g][0], g == 2 ? "None" : "Original"); u->sprite_count[g] = 1; }
    strcpy(u->sprites[3][1], "Random"); u->sprite_count[3] = 2;
    u->slot_count = 4;
    u->screen = UI_SLOTS_SCREEN;
    u->rng = (uint32_t)time(NULL) * 2654435761u | 1;
    ui_saved_load(u, NULL);
}

void ui_set_slot_count(SmzUi *u, int n)
{
    u->slot_count = n < 1 ? 1 : n > UI_SLOTS ? UI_SLOTS : n;
    if (u->slot_cursor >= u->slot_count) u->slot_cursor = 0;
}

void ui_set_slot(SmzUi *u, int i, int used, const char *name, const char *summary)
{
    if (i < 0 || i >= UI_SLOTS) return;
    u->slots[i].used = used;
    u->slots[i].stats = 0;
    snprintf(u->slots[i].name, sizeof u->slots[i].name, "%s", name ? name : "");
    snprintf(u->slots[i].summary, sizeof u->slots[i].summary, "%s", summary ? summary : "");
}

void ui_set_slot_stats(SmzUi *u, int i, int zc, int zt, int mc, int mt, int64_t ms, int game, int bosses, int crystals)
{
    if (i < 0 || i >= UI_SLOTS) return;
    UiSlot *t = &u->slots[i];
    t->stats = 1; t->zelda_cleared = zc; t->zelda_total = zt; t->metroid_cleared = mc; t->metroid_total = mt;
    t->play_ms = ms; t->game = game; t->boss_mask = bosses; t->crystal_mask = crystals;
}

void ui_set_sprites(SmzUi *u, int game, const char *const *names, int n)
{
    if (game < 0 || game > 3) return;
    int lead = game == 3 ? 2 : 1;   /* music packages start with "Original" and "Random" */
    u->sprite_count[game] = lead;
    for (int i = 0; i < n && u->sprite_count[game] < UI_MAX_SPRITES; i++)
        snprintf(u->sprites[game][u->sprite_count[game]++], 32, "%s", names[i]);
    /* alphabetical (case-insensitive) after the leading entries */
    for (int i = lead + 1; i < u->sprite_count[game]; i++) {
        char t[32];
        memcpy(t, u->sprites[game][i], 32);
        int j = i - 1;
        while (j >= lead && strcasecmp(u->sprites[game][j], t) > 0) { memcpy(u->sprites[game][j + 1], u->sprites[game][j], 32); j--; }
        memcpy(u->sprites[game][j + 1], t, 32);
    }
}

void ui_busy(SmzUi *u, const char *msg) { u->screen = UI_BUSY; snprintf(u->status, sizeof u->status, "%s", msg); }
const char *ui_run_label(const SmzUi *u) { return u->start_label; }
void ui_idle(SmzUi *u) { u->screen = UI_SLOTS_SCREEN; }

/* indices into UI_OPTS of the options shown at one level */
static int visible(const SmzUi *u, int level, int *idx)
{
    int n = 0;
    for (int k = 0; k < UI_OPT_COUNT; k++) {
        int i = k;
        if (level != LEVEL_ITEMS) {
            const char *key = k < 24 ? UI_TABS[level][k] : NULL;
            if (!key) break;
            for (i = 0; i < UI_OPT_COUNT && strcmp(UI_OPTS[i].key, key); i++) {}
            if (i == UI_OPT_COUNT) continue;
        } else if (UI_OPTS[i].level != LEVEL_ITEMS) continue;
        if (i == UIO_HELPS && u->value[UIO_HELP] != 1) continue;   /* the count only matters in the limited mode */
        if (!strncmp(UI_OPTS[i].key, "Spazers", 7) && u->value[UIO_MODE] != 2) continue;   /* Spazer Hunt only */
        idx[n++] = i;
    }
    return n;
}

static void change(SmzUi *u, int opt, int dir, int big)
{
    const UiOptDef *d = &UI_OPTS[opt];
    int *v = &u->value[opt];
    switch (d->kind) {
    case OPT_BOOL: *v = !*v; break;
    case OPT_SEED: { long n = (long)*v + dir * (big ? 1000 : 1); if (n < 0) n = 0; if (n > d->max) n = d->max; *v = (int)n; break; }
    case OPT_SPRITE: break;
    default: { int n = *v + dir; if (n < d->min) n = d->max; if (n > d->max) n = d->min; *v = n; }
    }
}

static void slot_view(SmzUi *u)
{
    if (u->slot_cursor < u->slot_scroll) u->slot_scroll = u->slot_cursor;
    if (u->slot_cursor >= u->slot_scroll + UI_SLOTS_SHOWN) u->slot_scroll = u->slot_cursor - UI_SLOTS_SHOWN + 1;
    if (u->slot_scroll > u->slot_count - UI_SLOTS_SHOWN) u->slot_scroll = u->slot_count - UI_SLOTS_SHOWN;
    if (u->slot_scroll < 0) u->slot_scroll = 0;
}

static int sprite_opt(int game) { return game == 3 ? UIO_MUSIC : game == 2 ? UIO_PLANDO : game == 1 ? UIO_SAMUS : UIO_LINK; }

static void move(int *cur, int *scroll, int n, int dir)
{
    *cur = (*cur + dir + n) % n;
    if (*cur < *scroll) *scroll = *cur;
    if (*cur >= *scroll + ROWS_SHOWN) *scroll = *cur - ROWS_SHOWN + 1;
    if (*cur == 0) *scroll = 0;
}


/* ---- run modes and saved customs */

typedef struct { const char *name, *desc, *spec; } UiPreset;
static const UiPreset CHILL = {"Cozy run", "Zelda keys shuffled, easy wall jumps", "Keysanity=2,WallJumpDifficulty=0,LightDarkRooms=1,SaveStates=1"};
static const UiPreset ORIGINAL = {"Original", "The default randomizer rules", ""};
static const UiPreset BUILTIN[] = {
    {"Keysanity (both games)", "", "Keysanity=1"},
    {"Dungeons + Metroid", "", "ItemPlacement=1"},
    {"Opposite game", "", "ItemPlacement=4"},
    {"All Dungeons", "", "GameMode=1"},
    {"Spazer Hunt", "", "GameMode=2"},
    {"Quick finish", "", "GanonCrystals=4,GtCrystals=4,TourianBosses=2"},
    {"Open doors", "", "PyramidHole=1,TourianBossDoor=1"},
    {"Casual", "", "WallJumpDifficulty=0,LightDarkRooms=1,SaveStates=1"},
    {"Hardcore", "", "Keysanity=1,HelpMode=2,WallJumpDifficulty=3"},
};

static int opt_by_key(const char *key, size_t len)
{
    for (int i = 0; i < UI_OPT_COUNT; i++) if (!strncmp(UI_OPTS[i].key, key, len) && !UI_OPTS[i].key[len]) return i;
    return -1;
}

/* the choices a saved custom does not carry: they depend on the files of this device */
static int spec_skips(int i) { return i == 0 || i == UIO_LINK || i == UIO_SAMUS || i == UIO_PLANDO || i == UIO_MUSIC; }

static void reset_values(SmzUi *u) { for (int i = 0; i < UI_OPT_COUNT; i++) u->value[i] = UI_OPTS[i].def; }

static void apply_spec(SmzUi *u, const char *spec)
{
    while (*spec) {
        const char *eq = strchr(spec, '='), *end = strchr(spec, ',');
        if (!eq) break;
        if (!end) end = spec + strlen(spec);
        if (eq < end) {
            int i = opt_by_key(spec, (size_t)(eq - spec));
            if (i >= 0 && !spec_skips(i)) {
                int v = atoi(eq + 1);
                u->value[i] = v < UI_OPTS[i].min ? UI_OPTS[i].min : v > UI_OPTS[i].max ? UI_OPTS[i].max : v;
            }
        }
        spec = *end ? end + 1 : end;
    }
}

static void make_spec(const SmzUi *u, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < UI_OPT_COUNT; i++) {
        if (spec_skips(i) || u->value[i] == UI_OPTS[i].def) continue;
        int w = snprintf(out + n, cap - n, "%s%s=%d", n ? "," : "", UI_OPTS[i].key, u->value[i]);
        if (w < 0 || n + (size_t)w >= cap) { out[n] = 0; return; }
        n += (size_t)w;
    }
}

static void fmt_value(const SmzUi *u, int opt, int v, char *out, size_t n);

/* "Label: value" for each option of a spec, as much as fits `cap` */
static void spec_summary(const SmzUi *u, const char *spec, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    if (!*spec) { snprintf(out, cap, "Default rules"); return; }
    while (*spec) {
        const char *eq = strchr(spec, '='), *end = strchr(spec, ',');
        if (!eq) break;
        if (!end) end = spec + strlen(spec);
        int i = eq < end ? opt_by_key(spec, (size_t)(eq - spec)) : -1;
        if (i >= 0) {
            char v[44], part[80];
            fmt_value(u, i, atoi(eq + 1), v, sizeof v);
            snprintf(part, sizeof part, "%s%s: %s", n ? ", " : "", UI_OPTS[i].label, v);
            if (n + strlen(part) + 3 >= cap) { snprintf(out + n, cap - n, ".."); return; }
            memcpy(out + n, part, strlen(part) + 1);
            n += strlen(part);
        }
        spec = *end ? end + 1 : end;
    }
}

static void saved_add(SmzUi *u, const char *name, const char *spec)
{
    int i;
    for (i = 0; i < u->saved_count && strcasecmp(u->saved[i].name, name); i++) {}
    if (i == u->saved_count) {
        if (i >= UI_MAX_SAVED) return;
        u->saved_count++;
    }
    snprintf(u->saved[i].name, sizeof u->saved[i].name, "%s", name);
    snprintf(u->saved[i].spec, sizeof u->saved[i].spec, "%s", spec);
    u->saved_dirty = 1;
}

void ui_saved_load(SmzUi *u, const char *text)
{
    u->saved_count = 0;
    u->saved_cursor = u->saved_scroll = 0;
    if (!text) {
        for (size_t i = 0; i < sizeof BUILTIN / sizeof BUILTIN[0]; i++) saved_add(u, BUILTIN[i].name, BUILTIN[i].spec);
        return;
    }
    while (*text && u->saved_count < UI_MAX_SAVED) {
        const char *nl = strchr(text, '\n'), *tab = strchr(text, '\t');
        if (!nl) nl = text + strlen(text);
        if (tab && tab < nl) {
            UiSaved *s = &u->saved[u->saved_count++];
            snprintf(s->name, sizeof s->name, "%.*s", (int)(tab - text), text);
            snprintf(s->spec, sizeof s->spec, "%.*s", (int)(nl - tab - 1), tab + 1);
        }
        text = *nl ? nl + 1 : nl;
    }
    u->saved_dirty = 0;
}

int ui_saved_text(const SmzUi *u, char *out, size_t cap)
{
    size_t n = 0;
    if (cap) out[0] = 0;
    for (int i = 0; i < u->saved_count; i++) {
        int w = snprintf(out + n, cap - n, "%s\t%s\n", u->saved[i].name, u->saved[i].spec);
        if (w < 0 || n + (size_t)w >= cap) return -1;
        n += (size_t)w;
    }
    return (int)n;
}

int ui_saved_dirty(SmzUi *u) { int d = u->saved_dirty; u->saved_dirty = 0; return d; }

static void saved_remove(SmzUi *u, int i)
{
    for (; i + 1 < u->saved_count; i++) u->saved[i] = u->saved[i + 1];
    u->saved_count--;
    u->saved_dirty = 1;
}

static void goto_start(SmzUi *u)
{
    u->screen = UI_START;
    u->start_cursor = 2;
    u->start_col = 0;
    u->start_from = u->run_mode == 2 ? UI_CREATE : u->run_mode == 3 ? UI_SAVED : UI_MODE;
}

static void begin_run(SmzUi *u, int mode, const char *label, const char *spec)
{
    reset_values(u);
    apply_spec(u, spec);
    u->run_mode = mode;
    snprintf(u->start_label, sizeof u->start_label, "%s", label);
    if (mode == 2) { u->screen = UI_CREATE; u->level = LEVEL_RUN; for (int l = 0; l < LEVEL_COUNT; l++) u->cursor[l] = u->scroll[l] = 0; }
    else goto_start(u);
}

static int mode_rows(const SmzUi *u) { return u->saved_count > 0 ? 4 : 3; }

#define KEY_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.'!"
#define KEY_COLS 10
#define KEY_ROWS 4
#define KEY_COUNT (KEY_COLS * KEY_ROWS)
#define KEY_CTRL 4        /* Space, Delete, Save, Cancel */

static void name_input(SmzUi *u, UiButton b)
{
    int c = u->key_cursor;
    if (b == BTN_B) { u->screen = UI_CREATE; return; }
    if (b == BTN_L) { if (u->name_len) u->name[--u->name_len] = 0; return; }
    if (b == BTN_R) { if (u->name_len < (int)sizeof u->name - 1) { u->name[u->name_len++] = ' '; u->name[u->name_len] = 0; } return; }
    if (b == BTN_LEFT || b == BTN_RIGHT) {
        int d = b == BTN_LEFT ? -1 : 1;
        if (c < KEY_COUNT) u->key_cursor = c / KEY_COLS * KEY_COLS + (c % KEY_COLS + d + KEY_COLS) % KEY_COLS;
        else u->key_cursor = KEY_COUNT + (c - KEY_COUNT + d + KEY_CTRL) % KEY_CTRL;
    } else if (b == BTN_UP || b == BTN_DOWN) {
        if (c < KEY_COUNT) {
            int row = c / KEY_COLS, col = c % KEY_COLS;
            if (b == BTN_UP) u->key_cursor = row ? c - KEY_COLS : KEY_COUNT + col * KEY_CTRL / KEY_COLS;
            else u->key_cursor = row < KEY_ROWS - 1 ? c + KEY_COLS : KEY_COUNT + col * KEY_CTRL / KEY_COLS;
        } else {
            int k = c - KEY_COUNT;
            u->key_cursor = b == BTN_UP ? (KEY_ROWS - 1) * KEY_COLS + k * KEY_COLS / KEY_CTRL : k * KEY_COLS / KEY_CTRL;
        }
    } else if (b == BTN_A) {
        if (c < KEY_COUNT) { if (u->name_len < (int)sizeof u->name - 1) { u->name[u->name_len++] = KEY_CHARS[c]; u->name[u->name_len] = 0; } }
        else switch (c - KEY_COUNT) {
        case 0: if (u->name_len < (int)sizeof u->name - 1) { u->name[u->name_len++] = ' '; u->name[u->name_len] = 0; } break;
        case 1: if (u->name_len) u->name[--u->name_len] = 0; break;
        case 2: {
            char spec[UI_SPEC_LEN];
            int len = u->name_len;
            while (len && u->name[len - 1] == ' ') u->name[--len] = 0;
            if (!len) break;
            make_spec(u, spec, sizeof spec);
            saved_add(u, u->name, spec);
            snprintf(u->notice, sizeof u->notice, "Saved: %.20s", u->name);
            u->screen = UI_CREATE;
            break;
        }
        default: u->screen = UI_CREATE; break;
        }
    }
}

static void saved_input(SmzUi *u, UiButton b)
{
    int n = u->saved_count;
    if (!n) { u->screen = UI_MODE; return; }
    if (b == BTN_UP || b == BTN_DOWN) {
        u->saved_cursor = (u->saved_cursor + (b == BTN_UP ? n - 1 : 1)) % n;
        u->saved_col = u->saved_confirm = 0;
    } else if (b == BTN_LEFT) u->saved_col = u->saved_confirm = 0;
    else if (b == BTN_RIGHT) u->saved_col = 1;
    else if (b == BTN_B) { if (u->saved_confirm) u->saved_confirm = 0; else u->screen = UI_MODE; }
    else if (b == BTN_A) {
        const UiSaved *s = &u->saved[u->saved_cursor];
        if (!u->saved_col) begin_run(u, 3, s->name, s->spec);
        else if (!u->saved_confirm) u->saved_confirm = 1;
        else {
            saved_remove(u, u->saved_cursor);
            u->saved_confirm = u->saved_col = 0;
            if (u->saved_cursor >= u->saved_count) u->saved_cursor = u->saved_count ? u->saved_count - 1 : 0;
            if (!u->saved_count) u->screen = UI_MODE;
        }
    }
    if (u->saved_cursor < u->saved_scroll) u->saved_scroll = u->saved_cursor;
    if (u->saved_cursor >= u->saved_scroll + 5) u->saved_scroll = u->saved_cursor - 4;
}

static void random_sprite(SmzUi *u, int game)
{
    int n = u->sprite_count[game];
    if (n < 2) return;
    u->rng ^= u->rng << 13; u->rng ^= u->rng >> 17; u->rng ^= u->rng << 5;
    u->value[game ? UIO_SAMUS : UIO_LINK] = 1 + (int)(u->rng % (uint32_t)(n - 1));
}

static void start_input(SmzUi *u, UiButton b)
{
    if (b == BTN_B) { u->screen = u->start_from; return; }
    if (b == BTN_UP) u->start_cursor = (u->start_cursor + 2) % 3;
    else if (b == BTN_DOWN) u->start_cursor = (u->start_cursor + 1) % 3;
    else if (b == BTN_LEFT) u->start_col = 0;
    else if (b == BTN_RIGHT) u->start_col = u->start_cursor < 2;
    else if (b == BTN_A) {
        if (u->start_cursor == 2) { u->action = ACT_GENERATE; u->action_slot = u->active_slot; }
        else if (u->start_col) random_sprite(u, u->start_cursor);
        else {
            u->sprite_game = u->start_cursor;
            u->sprite_cursor = u->value[u->start_cursor ? UIO_SAMUS : UIO_LINK];
            u->back = UI_START; u->screen = UI_SPRITES;
        }
    }
    if (u->start_cursor == 2) u->start_col = 0;
}

static void mode_input(SmzUi *u, UiButton b)
{
    int n = mode_rows(u);
    if (b == BTN_UP) u->mode_cursor = (u->mode_cursor + n - 1) % n;
    else if (b == BTN_DOWN) u->mode_cursor = (u->mode_cursor + 1) % n;
    else if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
    else if (b == BTN_A) switch (u->mode_cursor) {
    case 0: begin_run(u, 0, CHILL.name, CHILL.spec); break;
    case 1: begin_run(u, 1, ORIGINAL.name, ORIGINAL.spec); break;
    case 2: begin_run(u, 2, "Custom", ""); break;
    default: u->screen = UI_SAVED; u->saved_cursor = u->saved_scroll = u->saved_col = u->saved_confirm = 0; break;
    }
}

void ui_input(SmzUi *u, UiButton b)
{
    u->notice[0] = 0;
    switch (u->screen) {
    case UI_SLOTS_SCREEN:
        if (b == BTN_UP || b == BTN_DOWN) { u->slot_cursor = (u->slot_cursor + (b == BTN_UP ? u->slot_count - 1 : 1)) % u->slot_count; slot_view(u); }
        else if (b == BTN_L || b == BTN_R) {
            int c = u->slot_cursor + (b == BTN_L ? -UI_SLOTS_SHOWN : UI_SLOTS_SHOWN);
            u->slot_cursor = c < 0 ? 0 : c >= u->slot_count ? u->slot_count - 1 : c;
            slot_view(u);
        }
        else if (b == BTN_A) {
            u->active_slot = u->slot_cursor;
            if (u->slots[u->slot_cursor].used) { u->screen = UI_SLOT_MENU; u->menu_cursor = 0; }
            else { u->screen = UI_MODE; u->mode_cursor = 0; }
        }
        break;
    case UI_SLOT_MENU:
        if (b == BTN_UP || b == BTN_DOWN) u->menu_cursor = !u->menu_cursor;
        else if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
        else if (b == BTN_A) {
            if (u->menu_cursor) { u->screen = UI_CONFIRM; u->confirm_cursor = 0; }   /* Cancel is the default */
            else { u->action = ACT_CONTINUE; u->action_slot = u->active_slot; u->screen = UI_SLOTS_SCREEN; }
        }
        break;
    case UI_CONFIRM:
        if (b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT) u->confirm_cursor = !u->confirm_cursor;
        else if (b == BTN_B) u->screen = UI_SLOT_MENU;
        else if (b == BTN_A) {
            if (u->confirm_cursor) { u->action = ACT_DELETE; u->action_slot = u->active_slot; u->screen = UI_SLOTS_SCREEN; }
            else u->screen = UI_SLOT_MENU;
        }
        break;
    case UI_CREATE: {
        int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
        int *cur = &u->cursor[u->level], *scr = &u->scroll[u->level];
        int rows = n + 2;                                   /* then Save as custom and Next */
        if (b == BTN_UP) move(cur, scr, rows, -1);
        else if (b == BTN_DOWN) move(cur, scr, rows, 1);
        else if (b == BTN_B) u->screen = UI_MODE;
        else if (b == BTN_L) { if (u->level > LEVEL_RUN) u->level--; }
        else if (b == BTN_R) { if (u->level < LEVEL_ITEMS) u->level++; }
        else if (*cur == n) {
            if (b == BTN_A) {
                snprintf(u->name, sizeof u->name, "Custom %d", u->saved_count + 1);
                u->name_len = (int)strlen(u->name);
                u->key_cursor = 0;
                u->screen = UI_NAME;
            }
        } else if (*cur == n + 1) {
            if (b == BTN_A) { u->run_mode = 2; goto_start(u); }
        } else {
            const UiOptDef *d = &UI_OPTS[idx[*cur]];
            if (d->kind == OPT_SPRITE && b == BTN_A) {
                u->sprite_game = idx[*cur] == UIO_MUSIC ? 3 : idx[*cur] == UIO_PLANDO ? 2 : idx[*cur] == UIO_SAMUS ? 1 : 0;
                u->sprite_cursor = u->value[idx[*cur]];
                u->back = UI_CREATE; u->screen = UI_SPRITES;
            } else if (b == BTN_LEFT) change(u, idx[*cur], -1, 0);
            else if (b == BTN_RIGHT) change(u, idx[*cur], 1, 0);
            else if (b == BTN_A) change(u, idx[*cur], 1, d->kind == OPT_SEED);
        }
        break;
    }
    case UI_SPRITES: {
        int n = u->sprite_count[u->sprite_game];
        if (u->sprite_game >= 2) {   /* plandos and music packages: a plain list */
            if (b == BTN_UP) u->sprite_cursor = (u->sprite_cursor + n - 1) % n;
            else if (b == BTN_DOWN) u->sprite_cursor = (u->sprite_cursor + 1) % n;
        } else {                     /* sprites: a grid, the d-pad moves by cell, L/R by page */
            int step = b == BTN_UP ? -GRID_COLS : b == BTN_DOWN ? GRID_COLS : b == BTN_LEFT ? -1 : b == BTN_RIGHT ? 1
                     : b == BTN_L ? -GRID_COLS * GRID_ROWS : b == BTN_R ? GRID_COLS * GRID_ROWS : 0;
            if (step) {
                int c = u->sprite_cursor + step;
                if (c < 0) c = (b == BTN_UP || b == BTN_L) ? 0 : n - 1;
                if (c > n - 1) c = (b == BTN_DOWN || b == BTN_R) ? n - 1 : 0;
                u->sprite_cursor = c;
            }
        }
        if (b == BTN_B) u->screen = u->back;
        else if (b == BTN_A) {
            u->value[sprite_opt(u->sprite_game)] = u->sprite_cursor;
            u->screen = u->back;
        }
        break;
    }
    case UI_MODE: mode_input(u, b); break;
    case UI_SAVED: saved_input(u, b); break;
    case UI_NAME: name_input(u, b); break;
    case UI_START: start_input(u, b); break;
    case UI_BUSY: break;
    }
}

void ui_touch(SmzUi *u, int x, int y)
{
    for (int i = 0; i < u->hit_count; i++) {
        const UiHit *t = &u->hit[i];
        if (x < t->x || y < t->y || x >= t->x + t->w || y >= t->y + t->h) continue;
        switch (t->kind) {
        case H_SLOT: u->slot_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_CHEVRON:
            u->slot_scroll += t->idx;
            if (u->slot_scroll > u->slot_count - UI_SLOTS_SHOWN) u->slot_scroll = u->slot_count - UI_SLOTS_SHOWN;
            if (u->slot_scroll < 0) u->slot_scroll = 0;
            if (u->slot_cursor < u->slot_scroll) u->slot_cursor = u->slot_scroll;
            if (u->slot_cursor >= u->slot_scroll + UI_SLOTS_SHOWN) u->slot_cursor = u->slot_scroll + UI_SLOTS_SHOWN - 1;
            break;
        case H_CONFIRM: u->confirm_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_MENU: u->menu_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_TAB: u->level = t->idx; break;
        case H_ROW: u->cursor[u->level] = t->idx; ui_input(u, BTN_A); break;
        case H_SCROLL: {
            int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
            for (int k = 0; k < 3; k++) move(&u->cursor[u->level], &u->scroll[u->level], n + 2, t->idx);
            break;
        }
        case H_CREATE: u->run_mode = 2; goto_start(u); break;
        case H_MODE: u->mode_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_SAVED: u->saved_cursor = t->idx; u->saved_col = 0; u->saved_confirm = 0; ui_input(u, BTN_A); break;
        case H_SAVED_DEL: u->saved_cursor = t->idx; u->saved_col = 1; ui_input(u, BTN_A); break;
        case H_KEY: u->key_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_START: {   /* 0, 1 sprite rows, 2, 3 their Random chips, 4 Start */
            u->start_cursor = t->idx == 4 ? 2 : t->idx & 1;
            u->start_col = t->idx == 2 || t->idx == 3;
            ui_input(u, BTN_A);
            break;
        }
        case H_SPRITE: u->sprite_cursor = t->idx; ui_input(u, BTN_A); break;
        }
        return;
    }
    if (y < 28 && u->screen != UI_SLOTS_SCREEN && u->screen != UI_BUSY && x < 100) ui_input(u, BTN_B);
}

static void title(SmzUi *u, const char *t)
{
    rect(u, 0, 0, UI_W, 28, C_PANEL);
    text(u, 8, 8, C_ACCENT, 0, t);
}

static void fmt_value(const SmzUi *u, int opt, int v, char *out, size_t n)
{
    const UiOptDef *d = &UI_OPTS[opt];
    switch (d->kind) {
    case OPT_BOOL: snprintf(out, n, "%s", v ? "On" : "Off"); break;
    case OPT_INT: snprintf(out, n, "%d", v); break;
    case OPT_SEED: if (v) snprintf(out, n, "%d", v); else snprintf(out, n, "Random"); break;
    case OPT_ENUM: snprintf(out, n, "%s", v >= d->min && v <= d->max ? d->names[v - d->min] : "?"); break;
    case OPT_SPRITE: { int g = opt == UIO_MUSIC ? 3 : opt == UIO_PLANDO ? 2 : opt == UIO_SAMUS; snprintf(out, n, "%s", v < u->sprite_count[g] ? u->sprites[g][v] : u->sprites[g][0]); break; }
    }
}

void ui_value_text(const SmzUi *u, int opt, char *out, size_t n) { fmt_value(u, opt, u->value[opt], out, n); }


/* ---- the run list rows: a big percent, Link and Samus counters, name and time */

static const uint16_t DIGIT[10] = {   /* 3x5 pixels, one bit per pixel, row-major from the top-left */
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};

static void big_digit(SmzUi *u, int x, int y, int d, int scale, int color)
{
    for (int r = 0; r < 5; r++) {
        int bits = (DIGIT[d] >> (12 - r * 3)) & 7;
        for (int c = 0; c < 3;) {
            if (!(bits & (4 >> c))) { c++; continue; }
            int e = c;
            while (e < 3 && (bits & (4 >> e))) e++;
            rect(u, x + c * scale, y + r * scale, (e - c) * scale, scale, color);
            c = e;
        }
    }
}

static void bar(SmzUi *u, int x, int y, int w, int num, int den, int color)
{
    int fill = den > 0 && num > 0 ? (int)((long long)w * (num > den ? den : num) / den) : 0;
    rect(u, x, y, w, 3, C_BG);
    if (num > 0 && fill < 1) fill = 1;
    if (fill) rect(u, x, y, fill, 3, color);
}

/* a crystal about 9x9 pixels; collected ones are blue (red for the two dark ones) with a highlight, missing ones dark */
static void gem(SmzUi *u, int x, int y, int have, int red)
{
    int c = !have ? C_BG : red ? C_WARN : C_CYAN;
    rect(u, x + 3, y, 3, 1, c);
    rect(u, x + 1, y + 1, 7, 1, c);
    rect(u, x, y + 2, 9, 3, c);
    rect(u, x + 1, y + 5, 7, 1, c);
    rect(u, x + 2, y + 6, 5, 1, c);
    rect(u, x + 3, y + 7, 3, 1, c);
    rect(u, x + 4, y + 8, 1, 1, c);
    if (have) rect(u, x + 2, y + 2, 2, 1, C_TEXT);
}

static void slot_rich(SmzUi *u, int i, int y, int w)
{
    const UiSlot *t = &u->slots[i];
    int total = t->zelda_total + t->metroid_total, done = t->zelda_cleared + t->metroid_cleared;
    int pct = total > 0 ? (int)((long long)done * 100 / total) : 0;
    char s[48];
    int digits = pct >= 100 ? 3 : pct >= 10 ? 2 : 1, x = 22;
    int color = pct >= 100 ? C_GREEN : C_ACCENT;
    for (int d = 0, div = digits == 3 ? 100 : digits == 2 ? 10 : 1; d < digits; d++, div /= 10) { big_digit(u, x, y + 9, pct / div % 10, 5, color); x += 18; }
    text(u, x, y + 24, C_DIM, 0, "%");
    snprintf(s, sizeof s, "%.20s", t->name);
    text(u, 88, y + 2, C_TEXT, 0, s);
    long long sec = t->play_ms / 1000;
    snprintf(s, sizeof s, "%lld:%02lld:%02lld", sec / 3600, sec / 60 % 60, sec % 60);
    text(u, 8 + w - 8, y + 2, C_DIM, 2, s);
    /* Link with the seven crystals, Samus with the four bosses; what is missing is dark (the pictures are dimmed) */
    put(u, 2, 88, y + 15, 10, 14, C_TEXT, 0, "link/Original");     /* the games' own standing sprites (tools/gen_original_icons.py) */
    for (int n = 1; n <= 7; n++) gem(u, 106 + (n - 1) * 14, y + 17, t->crystal_mask >> (n - 1) & 1, n == 5 || n == 6);
    put(u, 2, 88, y + 29, 10, 14, C_TEXT, 0, "samus/Original");
    static const char *const BOSS[4] = {"reward/kraid", "reward/phantoon", "reward/draygon", "reward/ridley"};
    for (int b = 0; b < 4; b++) put(u, 2, 106 + b * 20, y + 30, 13, 13, t->boss_mask >> b & 1 ? C_TEXT : C_DIM, 0, BOSS[b]);
    bar(u, 214, y + 22, w - 214, t->zelda_cleared, t->zelda_total, C_GREEN);
    bar(u, 214, y + 32, w - 214, t->metroid_cleared, t->metroid_total, C_ACCENT);
}

void ui_draw(SmzUi *u)
{
    char buf[64], val[44];
    u->cmd_count = 0;
    u->hit_count = 0;
    rect(u, 0, 0, UI_W, UI_H, C_BG);
    switch (u->screen) {
    case UI_SLOTS_SCREEN: {
        int more = u->slot_count > UI_SLOTS_SHOWN;
        snprintf(buf, sizeof buf, more ? "SMZ3  -  Select a run  (%d/%d)" : "SMZ3  -  Select a run", u->slot_cursor + 1, u->slot_count);
        title(u, buf);
        int w = UI_W - 16 - (more ? 26 : 0);
        for (int r = 0; r < UI_SLOTS_SHOWN && u->slot_scroll + r < u->slot_count; r++) {
            int i = u->slot_scroll + r, y = 36 + r * 50, used = u->slots[i].used;
            rect(u, 8, y, w, 44, i == u->slot_cursor ? C_SEL : C_PANEL);
            if (used) rect(u, 8, y, 5, 44, C_ACCENT);   /* a run with data has an orange bar on the left */
            hit(u, H_SLOT, i, 8, y, w, 44);
            if (used && u->slots[i].stats) slot_rich(u, i, y, w);
            else if (used) {
                snprintf(buf, sizeof buf, "Slot %d  %s", i + 1, u->slots[i].name);
                text(u, 20, y + 7, C_TEXT, 0, buf);
                text(u, 20, y + 25, C_DIM, 0, u->slots[i].summary);
            } else {   /* an empty slot: a plus where the percent goes and an invitation */
                rect(u, 8, y, 5, 44, i == u->slot_cursor ? C_ACCENT : C_DIM);
                rect(u, 24, y + 20, 26, 5, C_DIM);
                rect(u, 34, y + 10, 5, 25, C_DIM);
                text(u, 88, y + 16, C_ACCENT, 0, "New run");
            }
        }
        if (more) {
            int x = UI_W - 26;
            if (u->slot_scroll > 0) { rect(u, x, 36, 20, 44, C_PANEL); text(u, x + 10, 50, C_ACCENT, 1, "^"); hit(u, H_CHEVRON, -1, x, 36, 20, 44); }
            if (u->slot_scroll + UI_SLOTS_SHOWN < u->slot_count) { rect(u, x, 36 + 3 * 50, 20, 44, C_PANEL); text(u, x + 10, 36 + 3 * 50 + 14, C_ACCENT, 1, "v"); hit(u, H_CHEVRON, 1, x, 36 + 3 * 50, 20, 44); }
        }
        break;
    }
    case UI_CONFIRM:
        snprintf(buf, sizeof buf, "Delete Slot %d?", u->active_slot + 1);
        title(u, buf);
        text(u, 16, 40, C_TEXT, 0, u->slots[u->active_slot].name);
        text(u, 16, 58, C_DIM, 0, u->slots[u->active_slot].summary);
        text(u, 16, 84, C_WARN, 0, "This erases the run and its save.");
        hit(u, H_CONFIRM, 0, 8, 110, UI_W - 16, 30);
        hit(u, H_CONFIRM, 1, 8, 146, UI_W - 16, 30);
        rect(u, 8, 110, UI_W - 16, 30, u->confirm_cursor == 0 ? C_SEL : C_PANEL);
        text(u, 16, 118, C_TEXT, 0, "Cancel");
        rect(u, 8, 146, UI_W - 16, 30, u->confirm_cursor == 1 ? C_SEL : C_PANEL);
        text(u, 16, 154, C_WARN, 0, "Yes, delete it");
        text(u, 8, UI_H - 16, C_DIM, 0, "B: back");
        break;
    case UI_SLOT_MENU:
        snprintf(buf, sizeof buf, "Slot %d  %s", u->active_slot + 1, u->slots[u->active_slot].name);
        title(u, buf);
        text(u, 16, 40, C_DIM, 0, u->slots[u->active_slot].summary);
        hit(u, H_MENU, 0, 8, 70, UI_W - 16, 30);
        hit(u, H_MENU, 1, 8, 106, UI_W - 16, 30);
        rect(u, 8, 70, UI_W - 16, 30, u->menu_cursor == 0 ? C_SEL : C_PANEL);
        text(u, 16, 78, C_TEXT, 0, "Continue");
        rect(u, 8, 106, UI_W - 16, 30, u->menu_cursor == 1 ? C_SEL : C_PANEL);
        text(u, 16, 114, C_WARN, 0, "Delete");
        text(u, 8, UI_H - 16, C_DIM, 0, "B: back");
        break;
    case UI_CREATE: {
        int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
        int cur = u->cursor[u->level], scr = u->scroll[u->level];
        title(u, "New run");
        {
            static const char *const TABS[LEVEL_COUNT] = { "Run", "Zelda", "Metroid", "Play", "Items" };
            for (int t = 0; t < LEVEL_COUNT; t++) {
                int x = 62 + t * 51;
                hit(u, H_TAB, t, x, 4, 49, 20);
                rect(u, x, 4, 49, 20, u->level == t ? C_SEL : C_BG);
                text(u, x + 24, 8, C_TEXT, 1, TABS[t]);
            }
        }
        for (int r = 0; r < ROWS_SHOWN && scr + r <= n + 1; r++) {
            int row = scr + r, y = LIST_Y + r * ROW_H;
            rect(u, 4, y, UI_W - 40, ROW_H - 2, row == cur ? C_SEL : C_PANEL);
            hit(u, H_ROW, row, 4, y, UI_W - 40, ROW_H - 2);
            if (row == n) { text(u, (UI_W - 40) / 2, y + 4, C_ACCENT, 1, "Save as custom..."); continue; }
            if (row == n + 1) { text(u, (UI_W - 40) / 2, y + 4, C_ACCENT, 1, "Next: sprites and start"); continue; }
            ui_value_text(u, idx[row], val, sizeof val);
            text(u, 10, y + 4, C_TEXT, 0, UI_OPTS[idx[row]].label);
            text(u, UI_W - 46, y + 4, C_ACCENT, 2, val);
            if ((idx[row] == UIO_LINK || idx[row] == UIO_SAMUS) && u->value[idx[row]] > 0 && u->value[idx[row]] < u->sprite_count[idx[row] == UIO_SAMUS]) {
                /* the chosen sprite beside its row, not only in the picker */
                char key[64];
                int g = idx[row] == UIO_SAMUS;
                snprintf(key, sizeof key, "%s/%s", g ? "samus" : "link", u->sprites[g][u->value[idx[row]]]);
                put(u, 2, 150, y, ROW_H - 2, ROW_H - 2, C_TEXT, 0, key);
            }
        }
        rect(u, UI_W - 32, LIST_Y, 28, 66, C_PANEL);
        text(u, UI_W - 18, LIST_Y + 24, C_ACCENT, 1, "^");
        hit(u, H_SCROLL, -1, UI_W - 32, LIST_Y, 28, 66);
        rect(u, UI_W - 32, LIST_Y + 74, 28, 66, C_PANEL);
        text(u, UI_W - 18, LIST_Y + 98, C_ACCENT, 1, "v");
        hit(u, H_SCROLL, 1, UI_W - 32, LIST_Y + 74, 28, 66);
        rect(u, UI_W - 92, UI_H - 44, 88, 38, C_SEL);
        text(u, UI_W - 48, UI_H - 30, C_ACCENT, 1, "Next");
        hit(u, H_CREATE, 0, UI_W - 92, UI_H - 44, 88, 38);
        {
            const char *tip = u->notice[0] ? u->notice : cur < n ? UI_OPTS[idx[cur]].tip : cur == n ? "Saves these options under a name." : "Choose the sprites, then start.";
            char line[44];
            size_t len = strlen(tip), cut = len;
            if (len > 28) { cut = 28; while (cut > 0 && tip[cut] != 0x20) cut--; if (!cut) cut = 28; }
            snprintf(line, sizeof line, "%.*s", (int)cut, tip);
            text(u, 6, UI_H - 44, C_DIM, 0, line);
            if (cut < len) {
                const char *rest = tip + cut + (tip[cut] == ' ');
                size_t rl = strlen(rest), c2 = rl > 28 ? 28 : rl;
                if (rl > 28) { while (c2 > 0 && rest[c2] != ' ') c2--; if (!c2) c2 = 28; }
                char l2[44];
                snprintf(l2, sizeof l2, "%.*s", (int)c2, rest);
                text(u, 6, UI_H - 30, C_DIM, 0, l2);
                if (c2 < rl) text(u, 6, UI_H - 18, C_DIM, 0, rest + c2 + (rest[c2] == ' '));
            }
        }
        text(u, 6, UI_H - 6, C_DIM, 0, "L/R tabs  B back");
        break;
    }
    case UI_SPRITES: {
        int g = u->sprite_game, n = u->sprite_count[g];
        title(u, g == 3 ? "Music package" : g == 2 ? "Plando" : g ? "Samus sprite" : "Link sprite");
        if (g >= 2) {
            int first = u->sprite_cursor - ROWS_SHOWN / 2;
            if (first > n - ROWS_SHOWN) first = n - ROWS_SHOWN;
            if (first < 0) first = 0;
            for (int r = 0; r < ROWS_SHOWN && first + r < n; r++) {
                int y = LIST_Y + r * ROW_H;
                rect(u, 4, y, UI_W - 8, ROW_H - 2, first + r == u->sprite_cursor ? C_SEL : C_PANEL);
                hit(u, H_SPRITE, first + r, 4, y, UI_W - 8, ROW_H - 2);
                text(u, 10, y + 4, C_TEXT, 0, u->sprites[g][first + r]);
            }
            break;
        }
        int rows = (n + GRID_COLS - 1) / GRID_COLS, row = u->sprite_cursor / GRID_COLS;
        int first = row - GRID_ROWS / 2;
        if (first > rows - GRID_ROWS) first = rows - GRID_ROWS;
        if (first < 0) first = 0;
        int chosen = u->value[g ? UIO_SAMUS : UIO_LINK];
        for (int r = 0; r < GRID_ROWS; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                int i = (first + r) * GRID_COLS + c;
                if (i >= n) break;
                int x = GRID_X + c * GRID_CELL, y = GRID_Y + r * GRID_CELL;
                rect(u, x, y, GRID_CELL - 2, GRID_CELL - 2, i == u->sprite_cursor ? C_SEL : C_PANEL);
                if (i == chosen) rect(u, x, y, GRID_CELL - 2, 3, C_ACCENT);
                hit(u, H_SPRITE, i, x, y, GRID_CELL - 2, GRID_CELL - 2);
                {
                    char key[64];
                    snprintf(key, sizeof key, "%s/%s", g ? "samus" : "link", u->sprites[g][i]);
                    put(u, 2, x + 2, y + 4, GRID_CELL - 6, GRID_CELL - 8, C_TEXT, 0, key);
                }
            }
        }
        char info[48];
        snprintf(info, sizeof info, "%d/%d  %s", u->sprite_cursor + 1, n, u->sprites[g][u->sprite_cursor]);
        text(u, 6, UI_H - 18, C_TEXT, 0, info);
        text(u, 6, UI_H - 6, C_DIM, 0, "A pick  B back  L/R page");
        break;
    }
    case UI_MODE: {
        snprintf(buf, sizeof buf, "New run  -  Slot %d", u->active_slot + 1);
        title(u, buf);
        for (int r = 0; r < mode_rows(u); r++) {
            int y = 36 + r * 50;
            const char *name = r == 0 ? CHILL.name : r == 1 ? ORIGINAL.name : r == 2 ? "Custom" : "Saved customs";
            char desc[48];
            if (r == 0) snprintf(desc, sizeof desc, "%s", CHILL.desc);
            else if (r == 1) snprintf(desc, sizeof desc, "%s", ORIGINAL.desc);
            else if (r == 2) snprintf(desc, sizeof desc, "Pick every option yourself");
            else snprintf(desc, sizeof desc, "%d saved", u->saved_count);
            rect(u, 8, y, UI_W - 16, 44, r == u->mode_cursor ? C_SEL : C_PANEL);
            rect(u, 8, y, 5, 44, C_ACCENT);
            hit(u, H_MODE, r, 8, y, UI_W - 16, 44);
            text(u, 20, y + 7, C_TEXT, 0, name);
            text(u, 20, y + 25, C_DIM, 0, desc);
        }
        text(u, 6, UI_H - 6, C_DIM, 0, "A choose  B back");
        break;
    }
    case UI_SAVED: {
        snprintf(buf, sizeof buf, "Saved customs  (%d/%d)", u->saved_cursor + 1, u->saved_count);
        title(u, buf);
        for (int r = 0; r < 5 && u->saved_scroll + r < u->saved_count; r++) {
            int i = u->saved_scroll + r, y = 34 + r * 38;
            char sum[48];
            spec_summary(u, u->saved[i].spec, sum, 33);
            rect(u, 6, y, UI_W - 70, 34, i == u->saved_cursor && !u->saved_col ? C_SEL : C_PANEL);
            hit(u, H_SAVED, i, 6, y, UI_W - 70, 34);
            rect(u, UI_W - 60, y, 54, 34, i == u->saved_cursor && u->saved_col ? C_SEL : C_PANEL);
            hit(u, H_SAVED_DEL, i, UI_W - 60, y, 54, 34);
            if (i == u->saved_cursor && u->saved_confirm) {
                text(u, 12, y + 4, C_WARN, 0, "Delete this custom?");
                text(u, 12, y + 19, C_DIM, 0, "A yes   B no");
                text(u, UI_W - 33, y + 11, C_WARN, 1, "Sure?");
            } else {
                text(u, 12, y + 4, C_TEXT, 0, u->saved[i].name);
                text(u, 12, y + 19, C_DIM, 0, sum);
                text(u, UI_W - 33, y + 11, C_WARN, 1, "Delete");
            }
        }
        text(u, 6, UI_H - 6, C_DIM, 0, "A choose  Right: delete  B back");
        break;
    }
    case UI_NAME: {
        title(u, "Save custom as");
        char field[40];
        snprintf(field, sizeof field, "%s_", u->name);
        rect(u, 10, 32, UI_W - 20, 24, C_PANEL);
        text(u, 16, 38, C_TEXT, 0, field);
        for (int k = 0; k < KEY_COUNT; k++) {
            int x = 10 + (k % KEY_COLS) * 30, y = 62 + (k / KEY_COLS) * 30;
            char ch[2] = {KEY_CHARS[k], 0};
            rect(u, x, y, 28, 28, k == u->key_cursor ? C_SEL : C_PANEL);
            hit(u, H_KEY, k, x, y, 28, 28);
            text(u, x + 14, y + 8, C_TEXT, 1, ch);
        }
        static const char *const CTRL[KEY_CTRL] = {"Space", "Delete", "Save", "Cancel"};
        for (int k = 0; k < KEY_CTRL; k++) {
            int x = 10 + k * 75;
            rect(u, x, 186, 73, 26, KEY_COUNT + k == u->key_cursor ? C_SEL : C_PANEL);
            hit(u, H_KEY, KEY_COUNT + k, x, 186, 73, 26);
            text(u, x + 36, 192, k == 2 ? C_ACCENT : C_TEXT, 1, CTRL[k]);
        }
        text(u, 6, UI_H - 6, C_DIM, 0, "A type  L delete  R space  B back");
        break;
    }
    case UI_START: {
        snprintf(buf, sizeof buf, "%s  -  Sprites", u->start_label);
        title(u, buf);
        for (int g = 0; g < 2; g++) {
            int y = 34 + g * 62, v = u->value[g ? UIO_SAMUS : UIO_LINK];
            if (v >= u->sprite_count[g]) v = 0;
            rect(u, 8, y, 218, 56, u->start_cursor == g && !u->start_col ? C_SEL : C_PANEL);
            hit(u, H_START, g, 8, y, 218, 56);
            rect(u, 232, y, 80, 56, u->start_cursor == g && u->start_col ? C_SEL : C_PANEL);
            hit(u, H_START, 2 + g, 232, y, 80, 56);
            {
                char key[64];
                snprintf(key, sizeof key, "%s/%s", g ? "samus" : "link", u->sprites[g][v]);
                put(u, 2, 14, y + 4, 48, 48, C_TEXT, 0, key);
            }
            text(u, 72, y + 10, C_DIM, 0, g ? "Samus" : "Link");
            text(u, 72, y + 30, C_ACCENT, 0, u->sprites[g][v]);
            text(u, 272, y + 22, C_ACCENT, 1, "Random");
        }
        rect(u, 8, 164, UI_W - 16, 44, u->start_cursor == 2 ? C_SEL : C_PANEL);
        hit(u, H_START, 4, 8, 164, UI_W - 16, 44);
        text(u, UI_W / 2, 180, C_ACCENT, 1, "Start");
        text(u, 6, UI_H - 6, C_DIM, 0, "A choose  Right: Random  B back");
        break;
    }
    case UI_BUSY:
        title(u, "Generating");
        text(u, UI_W / 2, UI_H / 2 - 6, C_TEXT, 1, u->status);
        break;
    }
}
