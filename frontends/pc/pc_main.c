/* smz3: the PC frontend. A window with the run menu, the game and the tracker panels; also a small command line. */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <SDL.h>
#include "pc_emu.h"
#include "pc_assets.h"
#include "pc_gfx.h"
#include "pc_map.h"
#include "pc_online.h"
#include "pc_store.h"
#include "smz3_host.h"
#include "smz3_md5.h"
#include "smz3_discovery.h"
#include "smz3_proof.h"
#include "smz3_runlogic.h"
#include "smz3_tracker.h"
#include "smz3_ui_gen.h"

static SmzUi ui;
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_GameController *pad;

/* ---------- generation ---------- */

typedef struct {
    int slot;               /* 0-based */
    int rc;
    char err[160];
    SmzRun run;
    int cli;                /* the command line wrote nothing to the run store */
    const uint8_t *sm, *z3;
    uint8_t *rom;
    uint8_t *link, *samus; size_t link_len, samus_len;
    int32_t seed_if_random;
} GenJob;

static uint8_t *read_sprite(const char *kind, const char *name, size_t *len)
{
    *len = 0;
    if (!name[0] || !strcmp(name, "Original")) return NULL;
    return store_read(store_path("sprites/%s/%s.rdc", kind, name), len);
}

static void *generate_thread(void *arg)
{
    GenJob *j = arg;
    char link[32], samus[32], json[4096], seed[16];
    smz_host_sprites(&ui, link, samus);
    j->link = read_sprite("link", link, &j->link_len);
    j->samus = read_sprite("samus", samus, &j->samus_len);
    j->rom = malloc(SMZ_HOST_ROM_SIZE);
    j->rc = smz_host_generate(&ui, j->seed_if_random, j->sm, j->z3, j->link, j->link_len, j->samus, j->samus_len, store_ips, NULL,
                              j->rom, json, sizeof json, seed, sizeof seed, NULL);
    if (j->rc) { snprintf(j->err, sizeof j->err, "generation failed (%d)", j->rc); return NULL; }
    memset(&j->run, 0, sizeof j->run);
    snprintf(j->run.name, sizeof j->run.name, "%s", ui_run_label(&ui));
    snprintf(j->run.seed, sizeof j->run.seed, "%s", seed);
    snprintf(j->run.settings, sizeof j->run.settings, "%s", json);
    for (unsigned i = 0; i < smz3_location_count; i++)
        if (smz3_locations[i].game == SMZ3_GAME_ZELDA) j->run.zelda_total++; else j->run.metroid_total++;
    return NULL;
}

/* The generator needs more stack than a default thread has. */
static int run_big(void *(*fn)(void *), void *arg)
{
    pthread_attr_t at;
    pthread_t t;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64u << 20);
    if (pthread_create(&t, &at, fn, arg)) return -1;
    pthread_join(t, NULL);
    return 0;
}

static void generate(GenJob *j)
{
    if (run_big(generate_thread, j)) { j->rc = -1; snprintf(j->err, sizeof j->err, "cannot start the generator"); }
}

typedef struct { SmzLogic *logic; const char *json; int rc; } LogicJob;
static void *logic_thread(void *arg) { LogicJob *j = arg; j->rc = smz_logic_build(j->logic, j->json); return NULL; }

static void rom_file(const SmzRun *r, char *out, size_t cap)
{
    char name[64];
    smz_run_rom_name(r->settings, r->seed, name, sizeof name);
    snprintf(out, cap, "%s", store_path("roms/%s", name));
}

static void gen_free(GenJob *j) { free(j->rom); free(j->link); free(j->samus); }

/* ---------- command line ---------- */

static int cmd_gen(int argc, char **argv)
{
    const char *preset = "cozy", *out = NULL, *smp = NULL, *z3p = NULL, *spec = "";
    long seed = 0;
    char specbuf[1024] = "";
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--preset") && i + 1 < argc) preset = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atol(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--sm") && i + 1 < argc) smp = argv[++i];
        else if (!strcmp(argv[i], "--z3") && i + 1 < argc) z3p = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) { size_t n = strlen(specbuf); snprintf(specbuf + n, sizeof specbuf - n, "%s%s", n ? "," : "", argv[++i]); }
        else { fprintf(stderr, "gen: unknown argument %s\n", argv[i]); return 2; }
    }
    spec = specbuf;
    if (!out) { fprintf(stderr, "gen: --out FILE is required\n"); return 2; }
    if (smp || z3p) {
        char a[512], b[512];
        store_roms_get(a, b);
        store_roms_set(smp ? smp : a, z3p ? z3p : b);
    }
    GenJob j;
    memset(&j, 0, sizeof j);
    uint8_t *sm, *z3;
    char err[200];
    if (store_load_base(&sm, &z3, err, sizeof err)) { fprintf(stderr, "gen: %s\n(give --sm and --z3 once, they are remembered)\n", err); return 1; }
    ui_init(&ui);
    int mode = !strcmp(preset, "original") ? 1 : !strcmp(preset, "custom") ? 2 : 0;
    if (strcmp(preset, "cozy") && strcmp(preset, "original") && strcmp(preset, "custom")) { fprintf(stderr, "gen: preset must be cozy, original or custom\n"); return 2; }
    ui_preset(&ui, mode);
    ui_apply_spec(&ui, spec);
    ui.value[0] = (int)seed;
    j.sm = sm; j.z3 = z3;
    j.seed_if_random = (int32_t)((uint32_t)time(NULL) * 2654435761u >> 1) % 2147483646 + 1;
    generate(&j);
    if (j.rc) { fprintf(stderr, "gen: %s\n", j.err); return 1; }
    if (store_write(out, j.rom, SMZ_HOST_ROM_SIZE)) { fprintf(stderr, "gen: cannot write %s\n", out); return 1; }
    printf("%s  seed %s  %s\n", out, j.run.seed, j.run.name);
    gen_free(&j); free(sm); free(z3);
    return 0;
}

static int cmd_new(int argc, char **argv)
{
    const char *preset = "cozy", *name = NULL;
    long seed = 0;
    char specbuf[1024] = "";
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--preset") && i + 1 < argc) preset = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atol(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) { size_t n = strlen(specbuf); snprintf(specbuf + n, sizeof specbuf - n, "%s%s", n ? "," : "", argv[++i]); }
        else { fprintf(stderr, "new: unknown argument %s\n", argv[i]); return 2; }
    }
    int mode = !strcmp(preset, "original") ? 1 : !strcmp(preset, "custom") ? 2 : 0;
    int slot = store_free_slot();
    if (slot < 0) { fprintf(stderr, "new: all %d slots are used\n", UI_SLOTS); return 1; }
    uint8_t *sm, *z3;
    char err[200];
    if (store_load_base(&sm, &z3, err, sizeof err)) { fprintf(stderr, "new: %s\n", err); return 1; }
    static GenJob j;
    memset(&j, 0, sizeof j);
    ui_init(&ui);
    ui_preset(&ui, mode);
    ui_apply_spec(&ui, specbuf);
    ui.value[0] = (int)seed;
    j.sm = sm; j.z3 = z3;
    j.seed_if_random = (int32_t)((uint32_t)time(NULL) * 2654435761u >> 1) % 2147483646 + 1;
    generate(&j);
    if (j.rc) { fprintf(stderr, "new: %s\n", j.err); return 1; }
    if (name) snprintf(j.run.name, sizeof j.run.name, "%s", name);
    rom_file(&j.run, j.run.rom_path, sizeof j.run.rom_path);
    j.run.created = j.run.last_played = (int64_t)time(NULL) * 1000;
    if (store_write(j.run.rom_path, j.rom, SMZ_HOST_ROM_SIZE) || store_run_save(slot, &j.run)) { fprintf(stderr, "new: cannot write the run files\n"); return 1; }
    printf("slot %d  %s  seed %s\n", slot, j.run.name, j.run.seed);
    return 0;
}

static int cmd_online(void)
{
    char label[16];
    int seed;
    if (online_monthly(label, &seed)) printf("monthly seed %d (%s), %d days left\n", seed, label, online_days_left());
    else printf("no monthly seed (no connection, or curl is missing)\n");
    if (online_weekly(label, &seed)) printf("weekly seed %d (%s), %d days left\n", seed, label, online_weekly_days_left());
    for (int i = 1; i <= UI_SLOTS; i++) if (online_slot_kind(i)) printf("slot %d is on the %s ladder\n", i, online_slot_kind(i) == 3 ? "weekly" : online_slot_kind(i) == 2 ? "monthly" : "original");
    return 0;
}

static int cmd_runs(void)
{
    int n = 0;
    for (int i = 1; i <= UI_SLOTS; i++) {
        SmzRun r;
        if (store_run_load(i, &r)) continue;
        char sum[64];
        smz_run_summary(&r, sum, sizeof sum);
        printf("%2d  %-24s seed %-10s %s\n", i, r.name, r.seed, sum);
        n++;
    }
    if (!n) printf("no runs yet (%s)\n", store_dir());
    return 0;
}

static int cmd_config(int argc, char **argv)
{
    char sm[512], z3[512];
    store_roms_get(sm, z3);
    for (int i = 0; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--sm")) snprintf(sm, sizeof sm, "%s", argv[i + 1]);
        else if (!strcmp(argv[i], "--z3")) snprintf(z3, sizeof z3, "%s", argv[i + 1]);
    }
    if (argc) store_roms_set(sm, z3);
    printf("data  %s\nsm    %s\nz3    %s\n", store_dir(), sm, z3);
    uint8_t *a, *b;
    char err[200];
    if (store_load_base(&a, &b, err, sizeof err)) printf("check %s\n", err);
    else { printf("check both base ROMs are the right revision\n"); free(a); free(b); }
    return 0;
}

/* Runs a ROM for some frames without a window and reports what the tracker reads: a quick check of the emulator link. */
static int cmd_selftest(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "selftest ROM [frames]\n"); return 2; }
    size_t n;
    uint8_t *rom = store_read(argv[0], &n);
    if (!rom || emu_open(rom, n)) { fprintf(stderr, "cannot load %s\n", argv[0]); return 1; }
    int frames = argc > 1 ? atoi(argv[1]) : 600;
    static SmzTracker t;
    smz_tracker_reset(&t, 0, 0, 0, 0);
    uint64_t sum = 0;
    size_t audio_total = 0;
    for (int f = 0; f < frames; f++) {
        emu_run_frame();
        size_t a;
        emu_take_audio(&a);
        audio_total += a;
        if (f % 30 == 29) {
            size_t wl, sl;
            const uint8_t *wram = emu_wram(&wl);
            uint8_t *sram = emu_sram(&sl);
            smz_tracker_tick(&t, wram, wl, sram, sl, 500, 1);
        }
    }
    int w, h, pitch;
    const uint32_t *pic = emu_picture(&w, &h, &pitch);
    for (int i = 0; pic && i < w * h; i++) sum = sum * 31 + pic[i];
    size_t wl, sl;
    emu_wram(&wl); emu_sram(&sl);
    printf("frames %d  picture %dx%d  checksum %016llx  audio %zu samples at %d Hz  wram %zu sram %zu  started %d game %d\n", frames, w, h,
           (unsigned long long)sum, audio_total, emu_sample_rate(), wl, sl, t.started, (int)t.game);
    emu_close();
    return pic ? 0 : 1;
}

static void usage(void)
{
    puts("smz3 - Super Metroid + A Link to the Past randomizer\n\n"
         "  smz3                          open the window (menu, game, tracker)\n"
         "  smz3 play N                   open the window and continue run N\n"
         "  smz3 runs                     list the saved runs\n"
         "  smz3 new [--preset P] [--seed N] [--name S] [--set Key=value]   make a run in the first free slot\n"
         "  smz3 online                   show the monthly seed and which runs are on the online ladder\n"
         "  smz3 config [--sm F] [--z3 F] show or set the base ROM files (Super Metroid JU, ALttP JP v1.0)\n"
         "  smz3 gen --out FILE [--preset cozy|original|custom] [--seed N] [--set Key=value ...]\n"
         "                                make a randomized ROM file without opening a window\n\n"
         "Drop the two base ROM files on the window the first time instead of using config.\n"
         "Window keys: arrows move, X A button, Z B, S X, A Y, Q/W shoulders, Enter start, Backspace select,\n"
         "Space fast forward, Tab tracker, Esc leave the run, F11 full screen.");
}

/* ---------- menu helpers ---------- */

static void refill_sprites(void)
{
    static const char *const KIND[2] = {"link", "samus"};
    for (int g = 0; g < 2; g++) {
        static char names[UI_MAX_SPRITES][32];
        const char *list[UI_MAX_SPRITES];
        int n = 0;
        DIR *d = opendir(store_path("sprites/%s/x", KIND[g]) ? store_path("sprites/%s", KIND[g]) : ".");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) && n < UI_MAX_SPRITES - 2) {
                size_t l = strlen(e->d_name);
                if (l > 4 && !strcmp(e->d_name + l - 4, ".rdc") && l - 4 < 32) {
                    snprintf(names[n], 32, "%.*s", (int)(l - 4), e->d_name);
                    list[n] = names[n]; n++;
                }
            }
            closedir(d);
        }
        ui_set_sprites(&ui, g, list, n);
    }
}

static int base_ok(void)
{
    uint8_t *a, *b;
    char err[64];
    if (store_load_base(&a, &b, err, sizeof err)) return 0;
    free(a); free(b);
    return 1;
}

/* Stores a dropped ROM file as the Super Metroid or the ALttP base, judged by its content. Returns a message. */
static const char *accept_rom(const char *path)
{
    size_t n;
    uint8_t *rom = store_read(path, &n);
    if (!rom) return "cannot read that file";
    uint8_t *start = rom;
    if (n % 0x8000 == 0x200) { start += 0x200; n -= 0x200; }
    char hex[33];
    smz_md5_hex(start, n, hex);
    char sm[512], z3[512];
    store_roms_get(sm, z3);
    const char *msg = "that is not Super Metroid (JU) or ALttP (JP v1.0)";
    if (!strcmp(hex, SMZ_MD5_SM)) { snprintf(sm, sizeof sm, "%s", path); msg = "Super Metroid found"; }
    else if (!strcmp(hex, SMZ_MD5_Z3)) { snprintf(z3, sizeof z3, "%s", path); msg = "A Link to the Past found"; }
    free(rom);
    store_roms_set(sm, z3);
    return msg;
}

/* Test hooks: SMZ3_SHOT=file.bmp [SMZ3_SHOT_AFTER=n] saves the window after n presents and exits; SMZ3_QUIT_AFTER=n closes
 * the window normally after n presents; SMZ3_TAB=k opens the tracker on tab k. */
static void present(void)
{
    static int count = -1, after;
    SDL_RenderPresent(ren);
    static int quit_after = -1, frames;
    if (quit_after < 0) { const char *q = getenv("SMZ3_QUIT_AFTER"); quit_after = q ? atoi(q) : 0; }
    if (quit_after && ++frames == quit_after) { SDL_Event q = {.type = SDL_QUIT}; SDL_PushEvent(&q); }
    if (count < 0) { const char *a = getenv("SMZ3_SHOT_AFTER"); after = a ? atoi(a) : 60; count = getenv("SMZ3_SHOT") ? 0 : -2; }
    if (count >= 0 && ++count == after) {
        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        SDL_SaveBMP(s, getenv("SMZ3_SHOT"));
        exit(0);
    }
}

static int g_quit;

/* the monthly seed is fetched in the background; the menu takes it when it arrives */
static char g_month[16];
static int g_month_seed, g_month_ready;
static char g_week[16];
static int g_week_seed, g_week_ready;
static void *monthly_thread(void *arg) { (void)arg; char l[16]; int s; if (online_monthly(l, &s)) { snprintf(g_month, sizeof g_month, "%s", l); g_month_seed = s; g_month_ready = 1; }
    if (online_weekly(l, &s)) { snprintf(g_week, sizeof g_week, "%s", l); g_week_seed = s; g_week_ready = 1; } return NULL; }
static void save_proof(int slot, const SmzProof *p)
{
    static char text[SMZ_PROOF_MAX * 24];
    size_t n = smz_proof_format(p, text, sizeof text);
    store_write(store_path("runs/slot%d.proof", slot), text, n);
}

static void *flush_thread(void *arg) { (void)arg; online_flush(); return NULL; }
static void *token_thread(void *arg) { online_ensure_token((int)(intptr_t)arg); return NULL; }
static void detach(void *(*fn)(void *), void *arg) { pthread_t t; if (!pthread_create(&t, NULL, fn, arg)) pthread_detach(t); }
static void refill_slots(void) { store_fill_slots(&ui); for (int i = 0; i < UI_SLOTS; i++) ui_set_slot_online(&ui, i, online_slot_kind(i + 1)); }
static char g_note[200];
static Uint32 g_note_until;
static void note(const char *msg) { snprintf(g_note, sizeof g_note, "%s", msg); g_note_until = SDL_GetTicks() + 6000; }

/* ---------- playing ---------- */

typedef struct { const char *name; int bit; } Cell;

static int json_flag(const char *json, const char *key, int def)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    return !strncmp(p, "true", 4);
}

static float shape_aspect(const char *json)
{
    if (strstr(json, "\"screenShape\":\"wide\"")) return 16.0f / 9;
    if (strstr(json, "\"screenShape\":\"pixel\"")) return 8.0f / 7;
    return 4.0f / 3;
}

static int pad_bits(const Uint8 *k)
{
    int b = 0;
    if (k[SDL_SCANCODE_UP]) b |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) b |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= PAD_RIGHT;
    if (k[SDL_SCANCODE_X]) b |= PAD_A;
    if (k[SDL_SCANCODE_Z]) b |= PAD_B;
    if (k[SDL_SCANCODE_S]) b |= PAD_X;
    if (k[SDL_SCANCODE_A]) b |= PAD_Y;
    if (k[SDL_SCANCODE_Q]) b |= PAD_L;
    if (k[SDL_SCANCODE_W]) b |= PAD_R;
    if (k[SDL_SCANCODE_RETURN]) b |= PAD_START;
    if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) b |= PAD_SELECT;
    if (pad) {
        #define BTN(c, m) if (SDL_GameControllerGetButton(pad, c)) b |= m
        BTN(SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP); BTN(SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN);
        BTN(SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT); BTN(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT);
        BTN(SDL_CONTROLLER_BUTTON_A, PAD_B); BTN(SDL_CONTROLLER_BUTTON_B, PAD_A);
        BTN(SDL_CONTROLLER_BUTTON_X, PAD_Y); BTN(SDL_CONTROLLER_BUTTON_Y, PAD_X);
        BTN(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_L); BTN(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_R);
        BTN(SDL_CONTROLLER_BUTTON_START, PAD_START); BTN(SDL_CONTROLLER_BUTTON_BACK, PAD_SELECT);
        int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (lx < -16000) b |= PAD_LEFT; if (lx > 16000) b |= PAD_RIGHT;
        if (ly < -16000) b |= PAD_UP; if (ly > 16000) b |= PAD_DOWN;
    }
    return b;
}

static SDL_Color C(int r, int g, int b) { SDL_Color c = {r, g, b, 255}; return c; }

static void fmt_time(int64_t ms, char *out, size_t cap)
{
    long long s = ms / 1000;
    snprintf(out, cap, "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
}

static void draw_bar(float x, float y, float w, float h, float frac, SDL_Color fill)
{
    gfx_rect(x, y, w, h, C(0x1D, 0x24, 0x30));
    if (frac > 0) gfx_rect(x, y, w * (frac > 1 ? 1 : frac), h, fill);
}

static void panel_left(const SmzTracker *t, const SmzRun *r, float x, float w, float h)
{
    float u = fminf(h / 720.0f, w / 230.0f), pad = 18 * u, y = 40 * u;
    if (w < 90) return;
    unsigned done = t->zelda_cleared + t->metroid_cleared, total = t->zelda_total + t->metroid_total;
    char s[64];
    snprintf(s, sizeof s, "%u%%", total ? done * 100 / total : 0);
    gfx_text(x + pad, y, 64 * u, C(0xE9, 0xA2, 0x3B), 0, s);
    y += 84 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Zelda");
    snprintf(s, sizeof s, "%u/%u", t->zelda_cleared, t->zelda_total);
    gfx_text(x + w - pad, y, 20 * u, C(0xE8, 0xEC, 0xF2), 2, s);
    y += 26 * u;
    draw_bar(x + pad, y, w - 2 * pad, 10 * u, t->zelda_total ? (float)t->zelda_cleared / t->zelda_total : 0, C(0x4C, 0xC3, 0x8A));
    y += 30 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Metroid");
    snprintf(s, sizeof s, "%u/%u", t->metroid_cleared, t->metroid_total);
    gfx_text(x + w - pad, y, 20 * u, C(0xE8, 0xEC, 0xF2), 2, s);
    y += 26 * u;
    draw_bar(x + pad, y, w - 2 * pad, 10 * u, t->metroid_total ? (float)t->metroid_cleared / t->metroid_total : 0, C(0xE0, 0x5A, 0x4F));
    y += 44 * u;
    fmt_time(t->elapsed_ms ? t->elapsed_ms : r->play_ms, s, sizeof s);
    gfx_text(x + pad, y, 34 * u, C(0xE8, 0xEC, 0xF2), 0, s);
    y += 50 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, r->name);
}

static void panel_right(const SmzTracker *t, float x, float w, float h)
{
    float u = fminf(h / 720.0f, w / 230.0f), pad = 18 * u, y = 40 * u;
    if (w < 90) return;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Crystals");
    y += 28 * u;
    float cell = (w - 2 * pad - 6 * 4 * u) / 7;
    if (cell > 30 * u) cell = 30 * u;
    for (int i = 0; i < 7; i++) {
        float cx = x + pad + i * (cell + 4 * u);
        int has = (t->crystal_mask >> i) & 1;
        gfx_rect(cx, y, cell, cell, has ? C(0x4F, 0xC3, 0xF7) : C(0x1D, 0x24, 0x30));
        char d[2] = {(char)('1' + i), 0};
        gfx_text(cx + cell / 2, y + 4 * u, 22 * u, has ? C(0x10, 0x14, 0x1C) : C(0x87, 0x93, 0xA6), 1, d);
    }
    y += cell + 26 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Bosses");
    y += 28 * u;
    static const char *const B = "KPDR";
    for (int i = 0; i < 4; i++) {
        float cx = x + pad + i * (cell + 4 * u);
        int has = (t->boss_mask >> i) & 1;
        gfx_rect(cx, y, cell, cell, has ? C(0xE0, 0x5A, 0x4F) : C(0x1D, 0x24, 0x30));
        char d[2] = {B[i], 0};
        gfx_text(cx + cell / 2, y + 4 * u, 22 * u, has ? C(0x10, 0x14, 0x1C) : C(0x87, 0x93, 0xA6), 1, d);
    }
    y += cell + 26 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Last items");
    y += 28 * u;
    for (int i = 0; i < t->n_recent_items && i < 6; i++, y += 30 * u) {
        const smz3_inv_item *it = &smz3_inventory_items[t->recent_items[i]];
        SDL_Texture *ic = map_item_icon(ren, it->icon);
        float ix = x + pad;
        if (ic) { SDL_SetTextureAlphaMod(ic, 255); SDL_FRect d = {ix, y - 2 * u, 26 * u, 26 * u}; SDL_RenderCopyF(ren, ic, NULL, &d); ix += 32 * u; }
        gfx_text(ix, y, 18 * u, C(0xE8, 0xEC, 0xF2), 0, it->name);
    }
}

enum { TAB_RUN, TAB_ITEMS, TAB_CHECKS, TAB_MAP, TAB_HELP, TAB_COUNT };
static const char *const TAB_NAMES[TAB_COUNT] = {"RUN", "ITEMS", "CHECKS", "MAP", "HELP"};

static void overlay(const SmzTracker *t, const SmzRun *r, int tab, int scroll, int pause, int win_w, int win_h, MapView *mv,
                    const SmzDiscovery *disc, int discovered_only, const uint8_t *in_logic, const uint8_t *wram, size_t wram_len)
{
    float u = win_h / 720.0f, x0 = win_w * 0.08f, w = win_w * 0.84f, y0 = 40 * u;
    SDL_Color back = {0x10, 0x14, 0x1C, 235};
    gfx_rect(x0 - 12 * u, y0 - 16 * u, w + 24 * u, win_h - 2 * y0 + 32 * u, back);
    float x = x0;
    for (int i = 0; i < TAB_COUNT; i++) {
        float tw = 130 * u;
        gfx_rect(x, y0, tw - 6 * u, 36 * u, i == tab ? C(0x2E, 0x4A, 0x6B) : C(0x1D, 0x24, 0x30));
        gfx_text(x + (tw - 6 * u) / 2, y0 + 5 * u, 24 * u, i == tab ? C(0xE9, 0xA2, 0x3B) : C(0x87, 0x93, 0xA6), 1, TAB_NAMES[i]);
        x += tw;
    }
    gfx_text(x0 + w, y0 + 7 * u, 18 * u, C(0x87, 0x93, 0xA6), 2, pause ? "Q/E tabs   P: game paused" : "Q/E tabs   P: game running");
    float y = y0 + 56 * u, line = 26 * u, bottom = win_h - y0 - 10 * u;
    char s[96];
    if (tab == TAB_RUN) {
        fmt_time(t->elapsed_ms ? t->elapsed_ms : r->play_ms, s, sizeof s);
        gfx_text(x0, y, 30 * u, C(0xE8, 0xEC, 0xF2), 0, r->name); y += 44 * u;
        char row[160];
        snprintf(row, sizeof row, "Time %s    Zelda %u/%u    Metroid %u/%u", s, t->zelda_cleared, t->zelda_total, t->metroid_cleared, t->metroid_total);
        gfx_text(x0, y, 22 * u, C(0x87, 0x93, 0xA6), 0, row); y += 44 * u;
        gfx_text(x0, y, 22 * u, C(0xE9, 0xA2, 0x3B), 0, "Last checks"); y += 32 * u;
        for (int i = 0; i < t->n_recent_checks; i++, y += line) {
            const smz3_location *l = &smz3_locations[t->recent_checks[i]];
            snprintf(row, sizeof row, "%s  -  %s", l->region, l->name);
            gfx_text(x0, y, 20 * u, C(0xE8, 0xEC, 0xF2), 0, row);
        }
        if (t->game == 3) gfx_text(x0, y + 10 * u, 24 * u, C(0x4C, 0xC3, 0x8A), 0, "Run finished");
    } else if (tab == TAB_ITEMS) {
        float cell = 56 * u, gap = 10 * u;
        int per = (int)((w + gap) / (cell + gap)), k = 0, last_game = 0;
        if (per < 1) per = 1;
        for (unsigned i = 0; i < smz3_inventory_count && i < SMZ_TRK_MAX_INV; i++) {
            const smz3_inv_item *it = &smz3_inventory_items[i];
            if (it->game != last_game) { if (last_game) y += (k + per - 1) / per * (cell + gap) + 14 * u; k = 0; last_game = it->game; }
            float cx = x0 + (k % per) * (cell + gap), cy = y + (k / per) * (cell + gap);
            int level = t->inventory[i];
            gfx_rect(cx, cy, cell, cell, C(0x1D, 0x24, 0x30));
            SDL_Texture *ic = map_item_icon(ren, it->icon);
            if (ic) {
                SDL_SetTextureAlphaMod(ic, level > 0 ? 255 : 70);
                SDL_FRect d = {cx + 4 * u, cy + 4 * u, cell - 8 * u, cell - 8 * u};
                SDL_RenderCopyF(ren, ic, NULL, &d);
            } else gfx_text(cx + cell / 2, cy + cell / 2 - 8 * u, 12 * u, level > 0 ? C(0xE8, 0xEC, 0xF2) : C(0x4A, 0x55, 0x66), 1, it->name);
            if (level > 1) {
                snprintf(s, sizeof s, "%d", level);
                gfx_text(cx + cell - 3 * u, cy + cell - 20 * u, 16 * u, C(0xE9, 0xA2, 0x3B), 2, s);
            }
            k++;
        }
    } else if (tab == TAB_CHECKS) {
        int rows = (int)((bottom - y) / line), first = scroll;
        if (first > (int)smz3_location_count - rows) first = (int)smz3_location_count - rows;
        if (first < 0) first = 0;
        for (int i = 0; i < rows && first + i < (int)smz3_location_count && first + i < SMZ_TRK_MAX_LOCS; i++) {
            const smz3_location *l = &smz3_locations[first + i];
            int done = t->cleared[first + i];
            snprintf(s, sizeof s, "%s %s  -  %s", done ? "[x]" : "[ ]", l->game == SMZ3_GAME_ZELDA ? "Z" : "M", l->name);
            gfx_text(x0, y + i * line, 19 * u, done ? C(0x4C, 0xC3, 0x8A) : in_logic && !in_logic[first + i] ? C(0x5A, 0x66, 0x78) : C(0xE8, 0xEC, 0xF2), 0, s);
        }
        snprintf(s, sizeof s, "%d-%d of %u   Up/Down PgUp/PgDn", first + 1, first + rows, smz3_location_count);
        gfx_text(x0 + w, bottom, 18 * u, C(0x87, 0x93, 0xA6), 2, s);
    } else if (tab == TAB_MAP) {
        SDL_Rect area = {(int)x0, (int)y, (int)w, (int)(bottom - y - 30 * u)};
        map_draw(ren, mv, area, t, disc, discovered_only, in_logic, wram, wram_len, SDL_GetTicks());
        gfx_text(x0 + w - 6, bottom - 24 * u, 18 * u, C(0xE8, 0xEC, 0xF2), 2, mv->follow ? "Arrows pan   +/- zoom   F: following Samus/Link" : "Arrows pan   +/- zoom   F: follow Samus/Link");
    } else {
        static const char *const HELP[] = {
            "Tab / right stick   tracker on and off", "Q / E or shoulders   change tab", "P   pause or keep playing while the tracker is open",
            "Map tab: arrows pan, +/- or triggers zoom, F follows the player",
            "Space / right trigger   fast forward (play time counts at that speed)", "Esc   leave the run (progress is saved)", "F11   full screen",
            "Arrows move, X = A, Z = B, S = X, A = Y, Q/W = L/R, Enter = Start, Backspace = Select",
        };
        for (unsigned i = 0; i < sizeof HELP / sizeof HELP[0]; i++, y += 34 * u) gfx_text(x0, y, 22 * u, C(0xE8, 0xEC, 0xF2), 0, HELP[i]);
    }
}

static void snapshot_run(SmzRun *r, SmzTracker *t, int finished)
{
    if (!finished) r->play_ms = t->base_ms + t->pending_ms;
    if (t->started) {
        r->game = (int)t->game;
        r->zelda_cleared = (int)t->zelda_cleared; r->metroid_cleared = (int)t->metroid_cleared;
        r->boss_mask = t->boss_mask; r->crystal_mask = t->crystal_mask;
    }
    r->zelda_total = (int)t->zelda_total; r->metroid_total = (int)t->metroid_total;
    r->save_ms = t->save_ms; r->save_zelda = t->save_zelda; r->save_metroid = t->save_metroid;
    r->last_played = (int64_t)time(NULL) * 1000;
}

/* mode 0: the autosave state (or the game save when there is none), 1: the game save only */
static void play(int slot, int game_save_only)
{
    SmzRun run;
    if (store_run_load(slot + 1, &run)) { note("that run cannot be read"); return; }
    char romp[512];
    if (!run.rom_path[0]) rom_file(&run, romp, sizeof romp); else snprintf(romp, sizeof romp, "%s", run.rom_path);
    size_t rl;
    uint8_t *rom = store_read(romp, &rl);
    if (!rom || rl != SMZ_HOST_ROM_SIZE) { free(rom); note("the ROM of this run is missing; make a new run"); return; }
    int ok = emu_open(rom, rl) == 0;
    free(rom);
    if (!ok) { note("the emulator could not load the ROM"); return; }

    int states = json_flag(run.settings, "saveStates", 0);
    size_t sl;
    uint8_t *sram = emu_sram(&sl);
    size_t fl;
    uint8_t *saved = store_read(store_path("runs/slot%d.srm", slot + 1), &fl);
    if (saved && sram) memcpy(sram, saved, fl < sl ? fl : sl);
    free(saved);
    if (states && !game_save_only) {
        size_t n;
        uint8_t *st = store_read(store_path("runs/slot%d.state", slot + 1), &n);
        if (st) { emu_state_load(st, n); free(st); }
    }

    SDL_AudioSpec want = {0}, have;
    want.freq = emu_sample_rate(); want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);

    static SmzDiscovery disc;
    smz_disc_init(&disc, &PC_MAP_DATA);
    {
        size_t n;
        char *text = (char *)store_read(store_path("runs/slot%d.discovery", slot + 1), &n);
        if (text) { text[n] = 0; smz_disc_load(&disc, text); free(text); }
    }
    int discovered_only = strstr(run.settings, "\"mapMode\":\"discovered\"") != NULL;
    MapView mv;
    map_view_init(&mv);
    /* checks in logic: only when the run lets the tracker show them (all helps) and its seed can be rebuilt */
    static SmzLogic logic;
    uint8_t in_logic[512];
    int have_logic = 0, keysanity = strstr(run.settings, "\"keysanity\":\"Both\"") ? 3 : strstr(run.settings, "\"keysanity\":\"SuperMetroid\"") ? 2 : strstr(run.settings, "\"keysanity\":\"Zelda\"") ? 1 : 0;
    if (strstr(run.settings, "\"helpMode\":\"all\"")) {
        LogicJob lj = {&logic, run.settings, -1};
        run_big(logic_thread, &lj);
        have_logic = lj.rc == 0;
    }
    uint32_t logic_key = 0;
    Uint32 next_token = 0;
    int is_default = json_flag(run.settings, "defaultRules", 0), queued = 0;
    memset(in_logic, 0, sizeof in_logic);
    static SmzTracker trk;
    smz_tracker_reset(&trk, run.play_ms, run.save_ms, run.save_zelda, run.save_metroid);
    static SmzProof proof;
    {
        size_t pn;
        char *pt = (char *)store_read(store_path("runs/slot%d.proof", slot + 1), &pn);
        if (pt) pt[pn] = 0;
        smz_proof_parse(&proof, pt ? pt : "");
        free(pt);
    }
    int proof_dirty = 0;
    float aspect = shape_aspect(run.settings);
    SDL_Texture *tex = NULL;
    int tw = 0, th = 0, tab = getenv("SMZ3_TAB") ? atoi(getenv("SMZ3_TAB")) % TAB_COUNT : TAB_RUN, scroll = 0, show = getenv("SMZ3_TAB") != NULL, pause_on_overlay = 1, leaving = 0, running = 1, finished = 0, fast = 0;
    Uint64 freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter(), last_tick = next, last_save = next;
    int64_t last_tick_ms = 0;
    (void)last_tick_ms;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; g_quit = 1; }
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if (leaving) { if (k == SDLK_RETURN) running = 0; else leaving = 0; continue; }
                if (k == SDLK_TAB) show = !show;
                else if (k == SDLK_ESCAPE) { if (show) show = 0; else leaving = 1; }
                else if (k == SDLK_F11) SDL_SetWindowFullscreen(win, SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                else if (show) {
                    if (k == SDLK_e || (k == SDLK_RIGHT && tab != TAB_MAP)) tab = (tab + 1) % TAB_COUNT;
                    else if (k == SDLK_q || (k == SDLK_LEFT && tab != TAB_MAP)) tab = (tab + TAB_COUNT - 1) % TAB_COUNT;
                    else if (k == SDLK_p) pause_on_overlay = !pause_on_overlay;
                    else if (k == SDLK_f && tab == TAB_MAP) mv.follow = !mv.follow;
                    else if (k == SDLK_DOWN && tab != TAB_MAP) scroll++;
                    else if (k == SDLK_UP && scroll > 0 && tab != TAB_MAP) scroll--;
                    else if (k == SDLK_PAGEDOWN && tab != TAB_MAP) scroll += 15;
                    else if (k == SDLK_PAGEUP && tab != TAB_MAP) scroll = scroll > 15 ? scroll - 15 : 0;
                }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                int b = e.cbutton.button;
                if (leaving) { if (b == SDL_CONTROLLER_BUTTON_A) running = 0; else leaving = 0; continue; }
                if (b == SDL_CONTROLLER_BUTTON_RIGHTSTICK) show = !show;
                else if (b == SDL_CONTROLLER_BUTTON_LEFTSTICK) leaving = 1;
                else if (show && b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) tab = (tab + 1) % TAB_COUNT;
                else if (show && b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) tab = (tab + TAB_COUNT - 1) % TAB_COUNT;
                else if (show && b == SDL_CONTROLLER_BUTTON_DPAD_DOWN && tab != TAB_MAP) scroll++;
                else if (show && b == SDL_CONTROLLER_BUTTON_DPAD_UP && scroll > 0 && tab != TAB_MAP) scroll--;
                else if (show && b == SDL_CONTROLLER_BUTTON_A && tab == TAB_MAP) mv.follow = !mv.follow;
            }
        }
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (show && tab == TAB_MAP) {
            float sp = 14, dx = 0, dy = 0, dz = 1;
            if (keys[SDL_SCANCODE_LEFT]) dx -= sp; if (keys[SDL_SCANCODE_RIGHT]) dx += sp;
            if (keys[SDL_SCANCODE_UP]) dy -= sp; if (keys[SDL_SCANCODE_DOWN]) dy += sp;
            if (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS] || keys[SDL_SCANCODE_PAGEUP]) dz = 1.03f;
            if (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS] || keys[SDL_SCANCODE_PAGEDOWN]) dz = 1 / 1.03f;
            if (pad) {
                int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
                if (abs(lx) > 8000) dx += sp * lx / 32767.0f; if (abs(ly) > 8000) dy += sp * ly / 32767.0f;
                if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) dz = 1.03f;
                if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000) dz = 1 / 1.03f;
            }
            map_view_move(&mv, dx, dy, dz);
        }
        int input = show || leaving ? 0 : pad_bits(keys);
        fast = (keys[SDL_SCANCODE_SPACE] && !show) || (pad && SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000);
        int frames = fast ? 4 : 1;
        int hold = (show && pause_on_overlay) || leaving;
        if (!hold) {
            emu_set_pad(input);
            for (int f = 0; f < frames; f++) {
                emu_run_frame();
                size_t n;
                const int16_t *a = emu_take_audio(&n);
                if (audio && n && !fast) SDL_QueueAudio(audio, a, (Uint32)(n * 4));
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        if ((now - last_tick) * 1000 / freq >= 500) {
            int dt = (int)((now - last_tick) * 1000 / freq);
            last_tick = now;
            size_t wl;
            const uint8_t *wram = emu_wram(&wl);
            sram = emu_sram(&sl);
            if (wram && sram) {
                smz_tracker_tick(&trk, wram, wl, sram, sl, dt, fast ? 4 : 1);
                if (trk.save_changed) store_write(store_path("runs/slot%d.srm", slot + 1), sram, sl);
                if (trk.game == 3) finished = 1;
                if (trk.started && smz_proof_note(&proof, trk.cleared, SMZ_TRK_MAX_LOCS, trk.base_ms + trk.pending_ms)) proof_dirty = 1;
                if (trk.started && trk.running && SDL_GetTicks() >= next_token && online_slot_kind(slot + 1)) { next_token = SDL_GetTicks() + 60000; detach(token_thread, (void *)(intptr_t)(slot + 1)); }
                if (finished && !queued && is_default && online_slot_kind(slot + 1)) {
                    queued = 1;
                    save_proof(slot + 1, &proof);
                    online_queue_finish(slot + 1, trk.base_ms + trk.pending_ms, atoi(run.seed));
                    detach(flush_thread, NULL);
                }
                if (trk.started && !hold) smz_disc_update(&disc, wram, wl, (int)trk.game);
                if (have_logic && (trk.started || getenv("SMZ3_LOGIC_ALWAYS"))) {
                    int items[SMZ_TRK_MAX_ITEMS];
                    int ni = smz_tracker_logic_items(&trk, wram, wl, sram, sl, keysanity, items, SMZ_TRK_MAX_ITEMS), key = 17;
                    uint32_t h = 2166136261u;
                    for (int q = 0; q < ni; q++) h = (h ^ (uint32_t)items[q]) * 16777619u;
                    (void)key;
                    if (h != logic_key) { logic_key = h; smz_logic_reachable(&logic, items, ni, in_logic, 512); }
                }
            }
        }
        if ((now - last_save) * 1000 / freq >= 5000) {
            last_save = now;
            snapshot_run(&run, &trk, finished);
            store_run_save(slot + 1, &run);
            if (proof_dirty) { save_proof(slot + 1, &proof); proof_dirty = 0; }
            if (disc.dirty) {
                char text[8192];
                if (smz_disc_save(&disc, text, sizeof text) > 0) store_write(store_path("runs/slot%d.discovery", slot + 1), text, strlen(text));
                disc.dirty = 0;
            }
        }

        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        SDL_SetRenderDrawColor(ren, 0x08, 0x0A, 0x10, 255);
        SDL_RenderClear(ren);
        int pw, ph, pitch;
        const uint32_t *pic = emu_picture(&pw, &ph, &pitch);
        float gh = (float)wh, gw = gh * aspect;
        if (gw > ww) { gw = (float)ww; gh = gw / aspect; }
        float gx = (ww - gw) / 2, gy = (wh - gh) / 2;
        if (pic && pw > 0) {
            if (!tex || tw != pw || th != ph) {
                if (tex) SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pw, ph);
                SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
                tw = pw; th = ph;
            }
            SDL_UpdateTexture(tex, NULL, pic, pitch * 4);
            SDL_FRect dst = {gx, gy, gw, gh};
            SDL_RenderCopyF(ren, tex, NULL, &dst);
        }
        panel_left(&trk, &run, 0, gx, (float)wh);
        panel_right(&trk, gx + gw, ww - (gx + gw), (float)wh);
        if (show) {
            size_t owl;
            const uint8_t *ow = emu_wram(&owl);
            overlay(&trk, &run, tab, scroll, pause_on_overlay, ww, wh, &mv, &disc, discovered_only, have_logic ? in_logic : NULL, ow, owl);
        }
        if (leaving) {
            gfx_rect(0, 0, (float)ww, (float)wh, (SDL_Color){0, 0, 0, 170});
            gfx_text(ww / 2.0f, wh / 2.0f - 30, 34, C(0xE8, 0xEC, 0xF2), 1, "Leave the run?");
            gfx_text(ww / 2.0f, wh / 2.0f + 20, 22, C(0x87, 0x93, 0xA6), 1, "Enter: leave and save    any other key: stay");
        }
        present();

        if (audio && !fast) {
            while (running && SDL_GetQueuedAudioSize(audio) > (Uint32)(have.freq / 60 * 4 * 3)) SDL_Delay(1);
        } else if (!fast) {
            next += freq * 1000 / 60988;
            Uint64 t2 = SDL_GetPerformanceCounter();
            if (next > t2) SDL_Delay((Uint32)((next - t2) * 1000 / freq)); else next = t2;
        }
    }

    size_t wl;
    emu_wram(&wl);
    sram = emu_sram(&sl);
    if (sram) store_write(store_path("runs/slot%d.srm", slot + 1), sram, sl);
    if (states) {
        size_t n = emu_state_size();
        uint8_t *buf = malloc(n);
        if (buf && emu_state_save(buf, n) == 0) store_write(store_path("runs/slot%d.state", slot + 1), buf, n);
        free(buf);
    }
    snapshot_run(&run, &trk, finished);
    store_run_save(slot + 1, &run);
    if (audio) SDL_CloseAudioDevice(audio);
    if (tex) SDL_DestroyTexture(tex);
    emu_close();
}

/* ---------- the window ---------- */

static UiButton map_key(SDL_Keycode k, int *ok)
{
    *ok = 1;
    switch (k) {
    case SDLK_UP: return BTN_UP; case SDLK_DOWN: return BTN_DOWN; case SDLK_LEFT: return BTN_LEFT; case SDLK_RIGHT: return BTN_RIGHT;
    case SDLK_RETURN: case SDLK_x: case SDLK_SPACE: return BTN_A;
    case SDLK_ESCAPE: case SDLK_z: case SDLK_BACKSPACE: return BTN_B;
    case SDLK_q: case SDLK_PAGEUP: return BTN_L;
    case SDLK_w: case SDLK_PAGEDOWN: return BTN_R;
    default: *ok = 0; return BTN_A;
    }
}

static void window_main(int start_slot)
{
    char err[200];
    SDL_Window *w = win; (void)w;
    ui_init(&ui);
    refill_sprites();
    refill_slots();
    online_init(&ui);
    if (getenv("SMZ3_MODE_MENU")) ui.screen = UI_MODE;   /* test hook: open the run mode list */
    detach(monthly_thread, NULL);
    detach(flush_thread, NULL);
    {
        size_t n;
        char *saved = (char *)store_read(store_path("saved.txt"), &n);
        if (saved) { saved[n] = 0; ui_saved_load(&ui, saved); free(saved); }
    }
    int running = 1, pending_play = start_slot, pending_state_only = 0;
    while (running) {
        if (pending_play >= 0) {
            play(pending_play, pending_state_only);
            pending_play = -1;
            refill_slots();
            if (g_quit) break;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                int ok;
                UiButton b = map_key(e.key.keysym.sym, &ok);
                if (e.key.keysym.sym == SDLK_F11) SDL_SetWindowFullscreen(win, SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                else if (ok) { if (b == BTN_B && ui.screen == UI_SLOTS_SCREEN) continue; ui_input(&ui, b); }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: ui_input(&ui, BTN_UP); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: ui_input(&ui, BTN_DOWN); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: ui_input(&ui, BTN_LEFT); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: ui_input(&ui, BTN_RIGHT); break;
                case SDL_CONTROLLER_BUTTON_A: ui_input(&ui, BTN_A); break;
                case SDL_CONTROLLER_BUTTON_B: ui_input(&ui, BTN_B); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: ui_input(&ui, BTN_L); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: ui_input(&ui, BTN_R); break;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int ww, wh;
                SDL_GetRendererOutputSize(ren, &ww, &wh);
                float k = (float)ww / UI_W < (float)wh / UI_H ? (float)ww / UI_W : (float)wh / UI_H;
                ui_touch(&ui, (int)((e.button.x - (ww - UI_W * k) / 2) / k), (int)((e.button.y - (wh - UI_H * k) / 2) / k));
            } else if (e.type == SDL_DROPFILE) {
                note(accept_rom(e.drop.file));
                SDL_free(e.drop.file);
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
        }

        if (g_month_ready) { g_month_ready = 0; ui_set_monthly(&ui, g_month, g_month_seed, online_days_left()); }
        if (g_week_ready) { g_week_ready = 0; ui_set_weekly(&ui, g_week, g_week_seed, online_weekly_days_left()); }
        online_save_profile(&ui);
        if (ui_saved_dirty(&ui)) {
            char text[8192];
            if (ui_saved_text(&ui, text, sizeof text) > 0) store_write(store_path("saved.txt"), text, strlen(text));
        }
        if (ui.action != ACT_NONE) {
            UiAction act = ui.action;
            int slot = ui.action_slot;
            ui.action = ACT_NONE;
            if (act == ACT_DELETE) { store_run_delete(slot + 1); refill_slots(); }
            else if (act == ACT_CONTINUE) {
                SmzRun r;
                if (store_run_load(slot + 1, &r) == 0 && json_flag(r.settings, "saveStates", 0) && r.save_ms > 0 && r.play_ms - r.save_ms > 10000
                    && access(store_path("runs/slot%d.state", slot + 1), F_OK) == 0)
                    ui_load_choice(&ui, slot, r.save_ms, r.save_zelda, r.save_metroid, r.play_ms, r.zelda_cleared, r.metroid_cleared, r.zelda_total, r.metroid_total);
                else { pending_play = slot; pending_state_only = 0; }
            } else if (act == ACT_LOAD_SAVE) { pending_play = slot; pending_state_only = 1; }
            else if (act == ACT_LOAD_STATE) { pending_play = slot; pending_state_only = 0; }
            else if (act == ACT_GENERATE) {
                uint8_t *sm, *z3;
                if (store_load_base(&sm, &z3, err, sizeof err)) { ui_idle(&ui); note(err); }
                else {
                    ui_busy(&ui, "Generating...");
                    ui_draw(&ui);
                    float ox, oy; int ww, wh;
                    SDL_GetRendererOutputSize(ren, &ww, &wh);
                    gfx_ui(&ui, ww, wh, &ox, &oy);
                    SDL_RenderPresent(ren);
                    static GenJob j;
                    memset(&j, 0, sizeof j);
                    j.sm = sm; j.z3 = z3; j.slot = slot;
                    j.seed_if_random = (int32_t)(SDL_GetTicks() * 2654435761u >> 1) % 2147483646 + 1;
                    generate(&j);
                    ui_idle(&ui);
                    if (j.rc) note(j.err);
                    else {
                        int on = ui_online_now(&ui), kind = ui_run_weekly(&ui) ? 3 : ui_run_monthly(&ui) ? 2 : 1;
                        rom_file(&j.run, j.run.rom_path, sizeof j.run.rom_path);
                        j.run.created = j.run.last_played = (int64_t)time(NULL) * 1000;
                        if (store_write(j.run.rom_path, j.rom, SMZ_HOST_ROM_SIZE) == 0 && store_run_save(slot + 1, &j.run) == 0) {
                            online_mark_run(slot + 1, on, kind);
                            refill_slots();
                            pending_play = slot; pending_state_only = 0;
                        } else note("cannot write the run files");
                    }
                    gen_free(&j); free(sm); free(z3);
                }
            }
        }

        ui_draw(&ui);
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        float ox, oy;
        float k = gfx_ui(&ui, ww, wh, &ox, &oy);
        if (!base_ok() && ui.screen == UI_SLOTS_SCREEN)
            gfx_text(ww / 2.0f, wh - 28 * k / 2 - 8, 11 * k, C(0xE9, 0xA2, 0x3B), 1, "Drop your Super Metroid (JU) and ALttP (JP v1.0) ROM files here");
        if (SDL_GetTicks() < g_note_until) {
            gfx_rect(0, 0, (float)ww, 34 * k / 2 + 8, (SDL_Color){0, 0, 0, 200});
            gfx_text(ww / 2.0f, 6, 13 * k, C(0xE8, 0xEC, 0xF2), 1, g_note);
        }
        present();
        SDL_Delay(8);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "gen")) return cmd_gen(argc - 2, argv + 2);
        if (!strcmp(argv[1], "runs")) return cmd_runs();
        if (!strcmp(argv[1], "online")) return cmd_online();
        if (!strcmp(argv[1], "new")) return cmd_new(argc - 2, argv + 2);
        if (!strcmp(argv[1], "selftest")) return cmd_selftest(argc - 2, argv + 2);
        if (!strcmp(argv[1], "config")) return cmd_config(argc - 2, argv + 2);
        if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "help")) { usage(); return 0; }
        if (strcmp(argv[1], "play") || argc < 3) { usage(); return 2; }
    }
    int start = argc > 2 ? atoi(argv[2]) - 1 : -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    win = SDL_CreateWindow("SMZ3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!ren && win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    gfx_init(ren);
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    window_main(start);
    SDL_Quit();
    return 0;
}
