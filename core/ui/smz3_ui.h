/* The start menu, the Create run options and the sprite pickers as a platform-free state machine. A frontend feeds it
 * button events and draws the rectangles and texts it produces on a 320x240 canvas. */
#ifndef SMZ3_UI_H
#define SMZ3_UI_H

#include <stdint.h>
#include <stddef.h>

#define UI_W 320
#define UI_H 240
#define UI_SLOTS 32           /* most slots a frontend can show; each one sets how many it keeps with ui_set_slot_count */
#define UI_SLOTS_SHOWN 4
#define UI_MAX_SPRITES 640
#define UI_MAX_OPTS 128
#define UI_MAX_CMDS 512
#define UI_MAX_HITS 64
#define UI_MAX_SAVED 16
#define UI_SPEC_LEN 512

typedef enum { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_L, BTN_R } UiButton;
typedef enum { UI_SLOTS_SCREEN, UI_SLOT_MENU, UI_CREATE, UI_SPRITES, UI_BUSY, UI_CONFIRM, UI_MODE, UI_SAVED, UI_NAME, UI_START, UI_COUNTRY, UI_ONLINE, UI_LOADCHOICE } UiScreen;
typedef enum { OPT_ENUM, OPT_INT, OPT_BOOL, OPT_SEED, OPT_SPRITE } OptKind;
/* Tabs of the Create run screen. Only LEVEL_ITEMS is read from UiOptDef.level; the other tabs list their keys in UI_TABS. */
typedef enum { LEVEL_RUN, LEVEL_ZELDA, LEVEL_METROID, LEVEL_PLAY, LEVEL_ITEMS, LEVEL_COUNT } OptLevel;
typedef enum { ACT_NONE, ACT_CONTINUE, ACT_DELETE, ACT_GENERATE, ACT_LOAD_SAVE, ACT_LOAD_STATE } UiAction;

typedef struct {
    const char *key;             /* C# property name for logic options */
    const char *label;
    const char *tip;
    OptKind kind;
    OptLevel level;
    int min, max, def;
    const char *const *names;    /* OPT_ENUM labels, max-min+1 entries */
} UiOptDef;

typedef struct {
    int used; char name[24]; char summary[40];
    int stats, zelda_cleared, zelda_total, metroid_cleared, metroid_total, game, boss_mask, crystal_mask;   /* stats: the numbers below are set; game 0 new, 1 Zelda, 2 Metroid, 3 finished */
    int64_t play_ms;
    int online;                  /* 0 offline run, 1 original-rules run on the online ladder, 2 monthly seed run, 3 weekly run */
} UiSlot;
/* A saved custom: a name and its options that differ from the defaults as "Key=value,Key=value" (option keys, so the list survives new options). */
typedef struct { char name[24]; char spec[UI_SPEC_LEN]; } UiSaved;

/* kind 0 rectangle, 1 text, 2 sprite preview: draw the picture of text ("link/<name>" or "samus/<name>", file <name>.pv beside the
 * .rdc: u8 width, u8 height, then width*height RGBA bytes) centred and scaled to fit the x, y, w, h box. */
typedef struct { uint8_t kind; int16_t x, y, w, h; uint8_t color; uint8_t align; char text[64]; } UiCmd;
enum { C_BG, C_PANEL, C_SEL, C_TEXT, C_DIM, C_ACCENT, C_WARN, C_GREEN, C_CYAN, C_TEAL, C_NAVY, C_VIOLET, C_GOLD };

typedef struct { int16_t x, y, w, h; uint8_t kind; int16_t idx; } UiHit;
enum { H_SLOT, H_MENU, H_TAB, H_ROW, H_SPRITE, H_SCROLL, H_CREATE, H_BACK, H_CHEVRON, H_CONFIRM, H_MODE, H_SAVED, H_SAVED_DEL, H_KEY, H_START, H_COUNTRY, H_ONLINE, H_LOADCHOICE };

typedef struct {
    UiScreen screen, back;
    UiSlot slots[UI_SLOTS];
    int slot_cursor, menu_cursor, active_slot, slot_count, slot_scroll, confirm_cursor;
    int level;                   /* LEVEL_* shown in the options screen */
    int cursor[LEVEL_COUNT], scroll[LEVEL_COUNT];
    int value[UI_MAX_OPTS];
    char sprites[4][UI_MAX_SPRITES][32];  /* 0 Link, 1 Samus, 2 plando files, 3 music packages; index 0 is "Original" ("None" for plandos, "Original" then "Random" for music) */
    int sprite_count[4], sprite_game, sprite_cursor;
    int mode_cursor, run_mode;   /* run_mode: 0 Chill run, 1 Original, 2 Custom, 3 Saved custom */
    UiSaved saved[UI_MAX_SAVED];
    int saved_count, saved_cursor, saved_scroll, saved_col, saved_confirm, saved_dirty;
    char name[24];               /* the name being typed for a new saved custom or for the player */
    int name_len, key_cursor;
    int name_for;                /* 0 saved custom, 1 player name */
    int player_on, player_dirty; /* the host shows the Player row (it has an online ladder) / the profile changed */
    char player[24], country[3]; /* the profile sent with finished runs; country is an ISO 3166 code or empty */
    int country_cursor, country_scroll;
    char monthly_label[16]; int monthly_seed;  /* the month's fixed seed (0 until the host knows it) and its label, e.g. 2026-10 */
    int monthly_days;                   /* days left until the month's seed changes */
    char weekly_label[16]; int weekly_seed, weekly_days;   /* the week's fixed seed, its label (e.g. 2026-W44) and the days left */
    struct { int64_t save_ms, state_ms; int save_z, save_m, state_z, state_m, zt, mt, cursor; } lc;   /* the load choice shown by UI_LOADCHOICE */
    int online_mode, online_cursor, online_gen; /* 0 ask before each run, 1 upload, 2 never; the prompt's row; 1 when the prompt came from Start */
    char start_label[24], notice[32];
    int start_cursor, start_col;  /* sprite screen: row (Link, Samus, Start) and column (sprite, Random) */
    UiScreen start_from;
    uint32_t rng;
    UiAction action;             /* set by an input event, read and cleared by the host */
    int action_slot;
    char status[44];
    UiCmd cmd[UI_MAX_CMDS];
    int cmd_count;
    UiHit hit[UI_MAX_HITS];
    int hit_count;
} SmzUi;

/* Option indices the host reads from u->value (see ui_settings for the rest). */
enum { UIO_LINK = 6, UIO_SAMUS = 7, UIO_PLANDO = 18, UIO_JINGLE = 19, UIO_MAP = 8, UIO_MODE = 17, UIO_DARK = 9, UIO_FILTER = 10, UIO_SHAPE = 11, UIO_HELP = 12, UIO_HELPS = 13, UIO_NERF = 22, UIO_SAVES = 23, UIO_MORPH = 24, UIO_MUSIC = 25 };

extern const UiOptDef UI_OPTS[];
extern const int UI_OPT_COUNT;
extern const char *const UI_TABS[LEVEL_ITEMS][24];   /* option keys per tab, in display order, NULL ended */

void ui_init(SmzUi *u);
void ui_set_slot(SmzUi *u, int i, int used, const char *name, const char *summary);
void ui_set_slot_count(SmzUi *u, int n);
/* The numbers a used slot shows (percent, counters, time); without them the summary text is drawn. */
void ui_set_slot_stats(SmzUi *u, int i, int zelda_cleared, int zelda_total, int metroid_cleared, int metroid_total, int64_t play_ms, int game, int boss_mask, int crystal_mask);
/* Marks a used slot as an online run (see UiSlot.online): it is drawn on a gradient with the player's name. */
void ui_set_slot_online(SmzUi *u, int i, int kind);
/* Text of an option's current value ("On", "Medium", a sprite name ...). */
void ui_value_text(const SmzUi *u, int opt, char *out, size_t n);
/* game 0 Link sprites, 1 Samus sprites, 2 plando files, 3 music packages */
void ui_set_sprites(SmzUi *u, int game, const char *const *names, int n);
void ui_input(SmzUi *u, UiButton b);
/* Touch at canvas coordinates (as drawn by the last ui_draw): selects and activates the row under the point. */
void ui_touch(SmzUi *u, int x, int y);
/* Saved customs: text is the file the host stored ("name<TAB>spec" lines); NULL (no file yet) installs the built-in ones.
 * ui_saved_dirty returns 1 once after the list changed, then the host writes ui_saved_text again. */
void ui_saved_load(SmzUi *u, const char *text);
int ui_saved_text(const SmzUi *u, char *out, size_t cap);
int ui_saved_dirty(SmzUi *u);
/* Player profile: the Player row appears in the start screen once the host enables it. text is "name<TAB>CC<TAB>mode" (NULL: empty). */
void ui_player_enable(SmzUi *u, int on);
void ui_player_load(SmzUi *u, const char *text);
int ui_player_text(const SmzUi *u, char *out, size_t cap);
int ui_player_dirty(SmzUi *u);
/* 1 when the run being started uploads to the online ladder (read when the host starts generating). */
int ui_online_now(const SmzUi *u);
/* The month's fixed seed; once set, the run mode list offers "Monthly seed" (Original rules with that seed). */
void ui_set_monthly(SmzUi *u, const char *label, int seed, int days_left);
/* Asks which to load for a slot whose save state is ahead of its game save (times in play-time ms, counts of checks done);
 * the answer comes back as ACT_LOAD_SAVE or ACT_LOAD_STATE with the slot in action_slot. */
void ui_load_choice(SmzUi *u, int slot, int64_t save_ms, int save_z, int save_m, int64_t state_ms, int state_z, int state_m, int zelda_total, int metroid_total);
/* Sets every option as the given way of starting a run does (0 Cozy, 1 Original, 2 Custom defaults), without opening any screen. */
void ui_preset(SmzUi *u, int mode);
/* Applies "Key=value,Key=value" on top of the current options (the format of saved customs; Seed is set through value[0]). */
void ui_apply_spec(SmzUi *u, const char *spec);
/* The week's fixed seed; once set, the run mode list offers the Weekly run (limited goals, own ladder). */
void ui_set_weekly(SmzUi *u, const char *label, int seed, int days_left);
int ui_run_weekly(const SmzUi *u);
/* The option values a run of the current mode must keep to count as rules-unchanged (defaults, except what the weekly run limits). */
void ui_expected_values(const SmzUi *u, int *want);
/* 1 when the run being started is the monthly seed run. */
int ui_run_monthly(const SmzUi *u);
/* The name of the way the run was set up ("Cozy run", "Original", a saved custom's name), for naming the run being created. */
const char *ui_run_label(const SmzUi *u);
void ui_busy(SmzUi *u, const char *msg);
void ui_idle(SmzUi *u);
/* Rebuilds u->cmd. */
void ui_draw(SmzUi *u);

/* Writes the settings JSON the frontends store with a run; returns its length or -1 when cap is too small. */
int ui_settings_json(const SmzUi *u, int32_t seed_if_random, char *out, size_t cap);

#endif
