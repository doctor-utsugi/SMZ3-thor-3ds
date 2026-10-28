#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_gfx.h"
#include "pc_welcome.h"
#include "smz3_md5.h"
#include "smz3_romsetup.h"

enum { EMPTY, VALID, WRONG };
typedef struct { int state; char name[128], md5[33], msg[96]; Uint32 since; } Card;
typedef struct { float x, y, vx, vy, life, max; SDL_Color c; } Spark;

#define SPARKS 220
static WelcomeHooks hk;
static int active, focus;   /* focus 0 browse, 1 play */
static Card card[2];
static Spark spark[SPARKS];
static Uint32 opened, ready_since, closing_since, fade_start;
static int was_ready;
static char last_reject[160];

static const char *const TITLE[2] = {"SUPER METROID", "A LINK TO THE PAST"};
static const char *const NEEDS[2] = {"Super Metroid (JU)", "ALttP, Japan v1.0"};
static const char *const WANT_MD5[2] = {SMZ_MD5_SM, SMZ_MD5_Z3};

void welcome_init(const WelcomeHooks *hooks) { hk = *hooks; }
int welcome_active(void) { return active; }

static float since(Uint32 t) { return (SDL_GetTicks() - t) / 1000.0f; }
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float ease(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }

static void burst(float cx, float cy, int n, SDL_Color base)
{
    for (int i = 0, made = 0; i < SPARKS && made < n; i++) {
        if (spark[i].life > 0) continue;
        float a = (float)(rand() % 6283) / 1000.0f, sp = 30 + rand() % 110;
        spark[i] = (Spark){cx, cy, cosf(a) * sp, sinf(a) * sp - 20, 0, 0.7f + (rand() % 70) / 100.0f, base};
        spark[i].life = spark[i].max;
        made++;
    }
}

static void load_existing(void)
{
    char names[2][128] = {{0}}, md5[2][33] = {{0}};
    hk.existing(names, md5);
    for (int i = 0; i < 2; i++) {
        if (!names[i][0]) { card[i] = (Card){EMPTY}; continue; }
        card[i].state = VALID;
        snprintf(card[i].name, sizeof card[i].name, "%s", names[i]);
        snprintf(card[i].md5, sizeof card[i].md5, "%s", md5[i]);
        card[i].since = SDL_GetTicks() - 2000;
    }
}

void welcome_open(void)
{
    memset(card, 0, sizeof card);
    memset(spark, 0, sizeof spark);
    last_reject[0] = 0;
    active = 1;
    focus = 0;
    opened = SDL_GetTicks();
    closing_since = 0;
    if (hk.ready()) { load_existing(); focus = 1; was_ready = 1; ready_since = opened - 3000; }
    else was_ready = 0;
}

void welcome_report(const char *source, int kind, const char *md5, int kept)
{
    int which = kind == SMZ_ROM_SM || kind == SMZ_ROM_SM_OTHER ? 0 : kind == SMZ_ROM_Z3 || kind == SMZ_ROM_Z3_OTHER ? 1 : -1;
    if (which < 0) {
        snprintf(last_reject, sizeof last_reject, "%s: not Super Metroid or ALttP (md5 %.8s...)", source, md5);
        return;
    }
    if (card[which].state == VALID && !kept) return;   /* a wrong file never replaces a good one */
    card[which].state = kept ? VALID : WRONG;
    snprintf(card[which].name, sizeof card[which].name, "%s", source);
    snprintf(card[which].md5, sizeof card[which].md5, "%s", md5);
    snprintf(card[which].msg, sizeof card[which].msg, "%s", kept ? "" : "Right game, wrong version");
    card[which].since = SDL_GetTicks();
    if (kept) last_reject[0] = 0;
}

void welcome_added(void)
{
    int now = hk.ready();
    for (int i = 0; i < 2; i++)
        if (card[i].state == VALID && since(card[i].since) < 0.2f) burst(i ? 236 : 84, 112, 26, (SDL_Color){0x4C, 0xC3, 0x8A, 255});
    if (now && !was_ready) {
        ready_since = SDL_GetTicks();
        focus = 1;
        burst(160, 204, 90, (SDL_Color){0xE9, 0xA2, 0x3B, 255});
        burst(84, 112, 40, (SDL_Color){0x4F, 0xC3, 0xF7, 255});
        burst(236, 112, 40, (SDL_Color){0x4F, 0xC3, 0xF7, 255});
    }
    was_ready = now;
}

static void close_screen(void) { closing_since = SDL_GetTicks(); }

void welcome_nav(UiButton b)
{
    if (!active || closing_since) return;
    int ready = hk.ready();
    if (b == BTN_LEFT) focus = 0;
    else if (b == BTN_RIGHT && ready) focus = 1;
    else if (b == BTN_A) { if (focus == 1 && ready) close_screen(); else hk.browse(); }
    else if (b == BTN_B && ready) close_screen();
}

void welcome_click(int x, int y)
{
    if (!active || closing_since) return;
    if (y >= 180 && y < 226) {
        if (x >= 12 && x < 100) { focus = 0; hk.browse(); }
        else if (x >= 108 && x < 308 && hk.ready()) { focus = 1; close_screen(); }
    } else if (y >= 40 && y < 176) hk.browse();
}

static SDL_Color rgba(int r, int g, int b, float a) { return (SDL_Color){(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)(255 * clamp01(a))}; }

static void rect(float k, float ox, float oy, float x, float y, float w, float h, SDL_Color c) { gfx_rect(ox + x * k, oy + y * k, w * k, h * k, c); }
static void text(float k, float ox, float oy, float x, float y, float size, SDL_Color c, int align, const char *s) { gfx_text(ox + x * k, oy + y * k, size * k, c, align, s); }

static void border(float k, float ox, float oy, float x, float y, float w, float h, float t, SDL_Color c)
{
    rect(k, ox, oy, x, y, w, t, c); rect(k, ox, oy, x, y + h - t, w, t, c);
    rect(k, ox, oy, x, y, t, h, c); rect(k, ox, oy, x + w - t, y, t, h, c);
}

static void short_md5(const char *m, char out[24]) { snprintf(out, 24, "%.8s...%s", m, m + 28); }

void welcome_draw(float k, float ox, float oy)
{
    if (!active) return;
    float t = since(opened), fade_in = ease(t / 0.5f), out = closing_since ? clamp01(since(closing_since) / 0.45f) : 0;
    if (out >= 1) { active = 0; fx_screen_changed(); return; }
    int ready = hk.ready();
    rect(k, ox, oy, 0, 0, 320, 240, rgba(0x10, 0x14, 0x1C, 1));
    /* drifting stars */
    for (int i = 0; i < 46; i++) {
        float sx = fmodf(i * 53.7f + t * (4 + i % 5) * 1.5f, 320), sy = fmodf(i * 91.3f, 240), tw = 0.35f + 0.35f * sinf(t * (1 + i % 4) + i);
        rect(k, ox, oy, sx, sy, 1.2f, 1.2f, rgba(0xB8, 0xC6, 0xE0, tw * fade_in));
    }
    float glow = ready ? 0.5f + 0.5f * sinf(since(ready_since) * 3.2f) : 0;
    float slide = (1 - fade_in) * 14;
    text(k, ox, oy, 160, 6 - slide, 26, rgba(0xE9, 0xA2, 0x3B, fade_in), 1, "SMZ3");
    text(k, ox, oy, 160, 34 - slide, 11, rgba(0x87, 0x93, 0xA6, fade_in), 1, ready ? "Both ROMs are ready" : "Drop the two ROM files on the window, or browse for them");

    for (int i = 0; i < 2; i++) {
        float x = i ? 166 : 12, y = 52, w = 142, h = 120;
        float appear = ease((t - 0.15f - 0.12f * i) / 0.45f), dy = (1 - appear) * 24;
        float st = since(card[i].since), pop = card[i].state ? ease(st / 0.35f) : 0;
        SDL_Color edge = card[i].state == VALID ? rgba(0x4C, 0xC3, 0x8A, 1) : card[i].state == WRONG ? rgba(0xE0, 0x5A, 0x4F, 1) : rgba(0x2E, 0x4A, 0x6B, 1);
        rect(k, ox, oy, x, y + dy, w, h, rgba(0x1D, 0x24, 0x30, appear));
        if (card[i].state == VALID) {
            float g = 0.18f + 0.12f * sinf(t * 2.2f + i) + (1 - ease(st / 0.9f)) * 0.5f;
            rect(k, ox, oy, x, y + dy, w, h, rgba(0x4C, 0xC3, 0x8A, g * appear));
        }
        if (card[i].state == EMPTY) {   /* dashed, breathing outline */
            float a = 0.5f + 0.4f * sinf(t * 2.4f + i * 1.7f);
            for (int d = 0; d < 142; d += 12) {
                rect(k, ox, oy, x + d, y + dy, 7, 2, rgba(0x4F, 0xC3, 0xF7, a * appear));
                rect(k, ox, oy, x + d, y + dy + h - 2, 7, 2, rgba(0x4F, 0xC3, 0xF7, a * appear));
            }
            for (int d = 0; d < 120; d += 12) {
                rect(k, ox, oy, x, y + dy + d, 2, 7, rgba(0x4F, 0xC3, 0xF7, a * appear));
                rect(k, ox, oy, x + w - 2, y + dy + d, 2, 7, rgba(0x4F, 0xC3, 0xF7, a * appear));
            }
        } else border(k, ox, oy, x, y + dy, w, h, 2, edge);
        text(k, ox, oy, x + w / 2, y + 6 + dy, 11, rgba(0xE8, 0xEC, 0xF2, appear), 1, TITLE[i]);
        char line[64], m[24];
        snprintf(line, sizeof line, "Needs: %s", NEEDS[i]);
        text(k, ox, oy, x + w / 2, y + 22 + dy, 9, rgba(0x87, 0x93, 0xA6, appear), 1, line);
        short_md5(WANT_MD5[i], m);
        snprintf(line, sizeof line, "md5 %s", m);
        text(k, ox, oy, x + w / 2, y + 33 + dy, 9, rgba(0x87, 0x93, 0xA6, appear), 1, line);
        if (card[i].state == EMPTY) {
            text(k, ox, oy, x + w / 2, y + 66 + dy, 13, rgba(0x4F, 0xC3, 0xF7, appear * (0.6f + 0.4f * sinf(t * 2.4f + i))), 1, "Drop it here");
        } else {
            float s = 0.4f + 0.6f * pop;
            if (card[i].state == VALID) {   /* a check mark that pops in */
                float cx = x + w / 2, cy = y + 66 + dy;
                for (int p = 0; p < 6; p++) rect(k, ox, oy, cx - 10 * s + p * 1.3f * s, cy - 1 * s + p * 1.3f * s, 3.4f * s, 3.4f * s, rgba(0x4C, 0xC3, 0x8A, pop));
                for (int p = 0; p < 10; p++) rect(k, ox, oy, cx - 3 * s + p * 1.5f * s, cy + 6 * s - p * 1.4f * s, 3.4f * s, 3.4f * s, rgba(0x4C, 0xC3, 0x8A, pop));
            } else text(k, ox, oy, x + w / 2, y + 56 + dy, 26 * s, rgba(0xE0, 0x5A, 0x4F, pop), 1, "!");
            char nm[40];
            snprintf(nm, sizeof nm, "%.30s", card[i].name);
            text(k, ox, oy, x + w / 2, y + 84 + dy, 9, rgba(0xE8, 0xEC, 0xF2, pop), 1, nm);
            short_md5(card[i].md5, m);
            snprintf(line, sizeof line, "md5 %s", m);
            text(k, ox, oy, x + w / 2, y + 96 + dy, 9, card[i].state == VALID ? rgba(0x4C, 0xC3, 0x8A, pop) : rgba(0xE0, 0x5A, 0x4F, pop), 1, line);
            text(k, ox, oy, x + w / 2, y + 107 + dy, 9, card[i].state == VALID ? rgba(0x4C, 0xC3, 0x8A, pop) : rgba(0xE0, 0x5A, 0x4F, pop), 1, card[i].state == VALID ? "Checksum matches, saved" : card[i].msg);
        }
    }

    if (last_reject[0]) text(k, ox, oy, 160, 175, 9, rgba(0xE0, 0x5A, 0x4F, fade_in), 1, last_reject);

    /* the buttons: Browse, and the Play! panel that lights up */
    float by = 186;
    if (ready) {
        float lift = 1 + 0.03f * glow;
        float w = 200 * lift, x = 208 - w / 2, pulse = 0.25f + 0.25f * glow;
        for (int g = 3; g >= 1; g--) rect(k, ox, oy, x - g * 3, by - g * 3, w + g * 6, 36 + g * 6, rgba(0xE9, 0xA2, 0x3B, pulse / g * 0.6f));
        rect(k, ox, oy, x, by, w, 36, rgba(0xE9, 0xA2, 0x3B, 1));
        rect(k, ox, oy, x, by + 18, w, 18, rgba(0xC9, 0x80, 0x20, 1));
        if (focus == 1) border(k, ox, oy, x - 1, by - 1, w + 2, 38, 2, rgba(0xFF, 0xFF, 0xFF, 0.8f + 0.2f * glow));
        float sheen = fmodf(since(ready_since) * 0.9f, 2.2f) - 0.4f;   /* a light sweeps across */
        if (sheen > 0 && sheen < 1) rect(k, ox, oy, x + sheen * w - 6, by, 12, 36, rgba(0xFF, 0xF2, 0xC8, 0.45f));
        text(k, ox, oy, 208, by + 5, 24, rgba(0x2A, 0x1A, 0x08, 1), 1, "Play!");
    } else {
        rect(k, ox, oy, 108, by, 200, 36, rgba(0x1D, 0x24, 0x30, fade_in));
        border(k, ox, oy, 108, by, 200, 36, 1, rgba(0x2E, 0x4A, 0x6B, fade_in));
        text(k, ox, oy, 208, by + 11, 12, rgba(0x5A, 0x66, 0x78, fade_in), 1, "Add both ROMs to play");
    }
    rect(k, ox, oy, 12, by, 88, 36, rgba(focus == 0 ? 0x2E : 0x1D, focus == 0 ? 0x4A : 0x24, focus == 0 ? 0x6B : 0x30, fade_in));
    text(k, ox, oy, 56, by + 11, 13, rgba(0xE8, 0xEC, 0xF2, fade_in), 1, "Browse...");
    text(k, ox, oy, 160, 228, 9, rgba(0x87, 0x93, 0xA6, fade_in), 1, ready ? "A or B: play   the ROMs are kept in the data folder" : "A browse   or drop files, a zip or a folder");

    /* sparkles */
    static Uint32 last;
    Uint32 now = SDL_GetTicks();
    float dt = last ? (now - last) / 1000.0f : 0;
    last = now;
    for (int i = 0; i < SPARKS; i++) {
        Spark *s = &spark[i];
        if (s->life <= 0) continue;
        s->life -= dt; s->x += s->vx * dt; s->y += s->vy * dt; s->vy += 90 * dt;
        float a = clamp01(s->life / s->max);
        rect(k, ox, oy, s->x, s->y, 2.2f, 2.2f, rgba(s->c.r, s->c.g, s->c.b, a));
    }
    if (out > 0) rect(k, ox, oy, 0, 0, 320, 240, rgba(0x10, 0x14, 0x1C, out));
}

void fx_screen_changed(void) { fade_start = SDL_GetTicks(); }

void fx_draw(int win_w, int win_h)
{
    float a = 1 - clamp01(since(fade_start) / 0.22f);
    if (a > 0.01f) gfx_rect(0, 0, (float)win_w, (float)win_h, rgba(0x10, 0x14, 0x1C, a * 0.8f));
}
