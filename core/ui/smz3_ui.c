#include <stdio.h>
#include <string.h>
#include <strings.h>
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
    for (int g = 0; g < 3; g++) { strcpy(u->sprites[g][0], g == 2 ? "None" : "Default"); u->sprite_count[g] = 1; }
    u->screen = UI_SLOTS_SCREEN;
}

void ui_set_slot(SmzUi *u, int i, int used, const char *name, const char *summary)
{
    if (i < 0 || i >= UI_SLOTS) return;
    u->slots[i].used = used;
    snprintf(u->slots[i].name, sizeof u->slots[i].name, "%s", name ? name : "");
    snprintf(u->slots[i].summary, sizeof u->slots[i].summary, "%s", summary ? summary : "");
}

void ui_set_sprites(SmzUi *u, int game, const char *const *names, int n)
{
    if (game < 0 || game > 2) return;
    u->sprite_count[game] = 1;
    for (int i = 0; i < n && u->sprite_count[game] < UI_MAX_SPRITES; i++)
        snprintf(u->sprites[game][u->sprite_count[game]++], 32, "%s", names[i]);
    /* alphabetical (case-insensitive) after the leading "Default" / "None" */
    for (int i = 2; i < u->sprite_count[game]; i++) {
        char t[32];
        memcpy(t, u->sprites[game][i], 32);
        int j = i - 1;
        while (j >= 1 && strcasecmp(u->sprites[game][j], t) > 0) { memcpy(u->sprites[game][j + 1], u->sprites[game][j], 32); j--; }
        memcpy(u->sprites[game][j + 1], t, 32);
    }
}

void ui_busy(SmzUi *u, const char *msg) { u->screen = UI_BUSY; snprintf(u->status, sizeof u->status, "%s", msg); }
void ui_idle(SmzUi *u) { u->screen = UI_SLOTS_SCREEN; }

/* indices into UI_OPTS of the options shown at one level */
static int visible(const SmzUi *u, int level, int *idx)
{
    int n = 0;
    for (int i = 0; i < UI_OPT_COUNT; i++) {
        if (UI_OPTS[i].level != level) continue;
        if (i == UIO_HELPS && u->value[UIO_HELP] != 1) continue;   /* the count only matters in the limited mode */
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

static void move(int *cur, int *scroll, int n, int dir)
{
    *cur = (*cur + dir + n) % n;
    if (*cur < *scroll) *scroll = *cur;
    if (*cur >= *scroll + ROWS_SHOWN) *scroll = *cur - ROWS_SHOWN + 1;
    if (*cur == 0) *scroll = 0;
}

void ui_input(SmzUi *u, UiButton b)
{
    switch (u->screen) {
    case UI_SLOTS_SCREEN:
        if (b == BTN_UP || b == BTN_DOWN) u->slot_cursor = (u->slot_cursor + (b == BTN_UP ? UI_SLOTS - 1 : 1)) % UI_SLOTS;
        else if (b == BTN_A) {
            u->active_slot = u->slot_cursor;
            if (u->slots[u->slot_cursor].used) { u->screen = UI_SLOT_MENU; u->menu_cursor = 0; }
            else { u->screen = UI_CREATE; u->level = LEVEL_SIMPLE; for (int l = 0; l < LEVEL_COUNT; l++) u->cursor[l] = u->scroll[l] = 0; }
        }
        break;
    case UI_SLOT_MENU:
        if (b == BTN_UP || b == BTN_DOWN) u->menu_cursor = !u->menu_cursor;
        else if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
        else if (b == BTN_A) {
            u->action = u->menu_cursor ? ACT_DELETE : ACT_CONTINUE;
            u->action_slot = u->active_slot;
            u->screen = UI_SLOTS_SCREEN;
        }
        break;
    case UI_CREATE: {
        int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
        int *cur = &u->cursor[u->level], *scr = &u->scroll[u->level];
        int rows = n + 1;                                   /* last row is the Create button */
        if (b == BTN_UP) move(cur, scr, rows, -1);
        else if (b == BTN_DOWN) move(cur, scr, rows, 1);
        else if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
        else if (b == BTN_L) { if (u->level > LEVEL_SIMPLE) u->level--; }
        else if (b == BTN_R) { if (u->level < LEVEL_ITEMS) u->level++; }
        else if (*cur == n) {
            if (b == BTN_A) { u->action = ACT_GENERATE; u->action_slot = u->active_slot; }
        } else {
            const UiOptDef *d = &UI_OPTS[idx[*cur]];
            if (d->kind == OPT_SPRITE && b == BTN_A) {
                u->sprite_game = idx[*cur] == UIO_PLANDO ? 2 : idx[*cur] == UIO_SAMUS ? 1 : 0;
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
        if (u->sprite_game == 2) {   /* plandos: a plain list */
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
        if (b == BTN_B) u->screen = UI_CREATE;
        else if (b == BTN_A) {
            u->value[u->sprite_game == 2 ? UIO_PLANDO : u->sprite_game ? UIO_SAMUS : UIO_LINK] = u->sprite_cursor;
            u->screen = UI_CREATE;
        }
        break;
    }
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
        case H_MENU: u->menu_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_TAB: u->level = t->idx; break;
        case H_ROW: u->cursor[u->level] = t->idx; ui_input(u, BTN_A); break;
        case H_SCROLL: {
            int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
            for (int k = 0; k < 3; k++) move(&u->cursor[u->level], &u->scroll[u->level], n + 1, t->idx);
            break;
        }
        case H_CREATE: u->action = ACT_GENERATE; u->action_slot = u->active_slot; break;
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

static void value_text(const SmzUi *u, int opt, char *out, size_t n)
{
    const UiOptDef *d = &UI_OPTS[opt];
    int v = u->value[opt];
    switch (d->kind) {
    case OPT_BOOL: snprintf(out, n, "%s", v ? "On" : "Off"); break;
    case OPT_INT: snprintf(out, n, "%d", v); break;
    case OPT_SEED: if (v) snprintf(out, n, "%d", v); else snprintf(out, n, "Random"); break;
    case OPT_ENUM: snprintf(out, n, "%s", d->names[v - d->min]); break;
    case OPT_SPRITE: { int g = opt == UIO_PLANDO ? 2 : opt == UIO_SAMUS; snprintf(out, n, "%s", v < u->sprite_count[g] ? u->sprites[g][v] : g == 2 ? "None" : "Default"); break; }
    }
}

void ui_draw(SmzUi *u)
{
    char buf[64], val[44];
    u->cmd_count = 0;
    u->hit_count = 0;
    rect(u, 0, 0, UI_W, UI_H, C_BG);
    switch (u->screen) {
    case UI_SLOTS_SCREEN:
        title(u, "SMZ3  -  Select a run");
        for (int i = 0; i < UI_SLOTS; i++) {
            int y = 40 + i * 46;
            rect(u, 8, y, UI_W - 16, 40, i == u->slot_cursor ? C_SEL : C_PANEL);
            hit(u, H_SLOT, i, 8, y, UI_W - 16, 40);
            snprintf(buf, sizeof buf, "Slot %d  %s", i + 1, u->slots[i].used ? u->slots[i].name : "Empty");
            text(u, 16, y + 6, C_TEXT, 0, buf);
            text(u, 16, y + 22, C_DIM, 0, u->slots[i].used ? u->slots[i].summary : "Press A to create a run");
        }
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
        title(u, "Create run");
        {
            static const char *const TABS[LEVEL_COUNT] = { "Simple", "Advanced", "QoL", "Items" };
            for (int t = 0; t < LEVEL_COUNT; t++) {
                int x = 88 + t * 58;
                hit(u, H_TAB, t, x, 4, 56, 20);
                rect(u, x, 4, 56, 20, u->level == t ? C_SEL : C_BG);
                text(u, x + 28, 8, C_TEXT, 1, TABS[t]);
            }
        }
        for (int r = 0; r < ROWS_SHOWN && scr + r <= n; r++) {
            int row = scr + r, y = LIST_Y + r * ROW_H;
            rect(u, 4, y, UI_W - 40, ROW_H - 2, row == cur ? C_SEL : C_PANEL);
            hit(u, H_ROW, row, 4, y, UI_W - 40, ROW_H - 2);
            if (row == n) { text(u, (UI_W - 40) / 2, y + 4, C_ACCENT, 1, "Create run"); continue; }
            value_text(u, idx[row], val, sizeof val);
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
        text(u, UI_W - 48, UI_H - 30, C_ACCENT, 1, "Create run");
        hit(u, H_CREATE, 0, UI_W - 92, UI_H - 44, 88, 38);
        {
            const char *tip = cur < n ? UI_OPTS[idx[cur]].tip : "Generates the seed on this device.";
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
        title(u, g == 2 ? "Plando" : g ? "Samus sprite" : "Link sprite");
        if (g == 2) {
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
                if (i == 0) text(u, x + (GRID_CELL - 2) / 2, y + GRID_CELL / 2 - 8, C_DIM, 1, "Default");
                else {
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
    case UI_BUSY:
        title(u, "Generating");
        text(u, UI_W / 2, UI_H / 2 - 6, C_TEXT, 1, u->status);
        break;
    }
}
