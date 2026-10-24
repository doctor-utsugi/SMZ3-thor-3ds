/* The start menu, the Create run options and the sprite pickers as a platform-free state machine. A frontend feeds it
 * button events and draws the rectangles and texts it produces on a 320x240 canvas. */
#ifndef SMZ3_UI_H
#define SMZ3_UI_H

#include <stdint.h>

#define UI_W 320
#define UI_H 240
#define UI_SLOTS 4
#define UI_MAX_SPRITES 640
#define UI_MAX_OPTS 128
#define UI_MAX_CMDS 160
#define UI_MAX_HITS 32

typedef enum { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_L, BTN_R } UiButton;
typedef enum { UI_SLOTS_SCREEN, UI_SLOT_MENU, UI_CREATE, UI_SPRITES, UI_BUSY } UiScreen;
typedef enum { OPT_ENUM, OPT_INT, OPT_BOOL, OPT_SEED, OPT_SPRITE } OptKind;
typedef enum { LEVEL_SIMPLE, LEVEL_ADVANCED, LEVEL_QOL, LEVEL_ITEMS, LEVEL_COUNT } OptLevel;
typedef enum { ACT_NONE, ACT_CONTINUE, ACT_DELETE, ACT_GENERATE } UiAction;

typedef struct {
    const char *key;             /* C# property name for logic options */
    const char *label;
    const char *tip;
    OptKind kind;
    OptLevel level;
    int min, max, def;
    const char *const *names;    /* OPT_ENUM labels, max-min+1 entries */
} UiOptDef;

typedef struct { int used; char name[24]; char summary[40]; } UiSlot;

/* kind 0 rectangle, 1 text, 2 sprite preview: draw the picture of text ("link/<name>" or "samus/<name>", file <name>.pv beside the
 * .rdc: u8 width, u8 height, then width*height RGBA bytes) centred and scaled to fit the x, y, w, h box. */
typedef struct { uint8_t kind; int16_t x, y, w, h; uint8_t color; uint8_t align; char text[64]; } UiCmd;
enum { C_BG, C_PANEL, C_SEL, C_TEXT, C_DIM, C_ACCENT, C_WARN };

typedef struct { int16_t x, y, w, h; uint8_t kind; int16_t idx; } UiHit;
enum { H_SLOT, H_MENU, H_TAB, H_ROW, H_SPRITE, H_SCROLL, H_CREATE, H_BACK };

typedef struct {
    UiScreen screen, back;
    UiSlot slots[UI_SLOTS];
    int slot_cursor, menu_cursor, active_slot;
    int level;                   /* LEVEL_* shown in the options screen */
    int cursor[LEVEL_COUNT], scroll[LEVEL_COUNT];
    int value[UI_MAX_OPTS];
    char sprites[3][UI_MAX_SPRITES][32];  /* 0 Link, 1 Samus, 2 plando files; index 0 is "Default" ("None" for plandos) */
    int sprite_count[3], sprite_game, sprite_cursor;
    UiAction action;             /* set by an input event, read and cleared by the host */
    int action_slot;
    char status[44];
    UiCmd cmd[UI_MAX_CMDS];
    int cmd_count;
    UiHit hit[UI_MAX_HITS];
    int hit_count;
} SmzUi;

/* Option indices the host reads from u->value (see ui_settings for the rest). */
enum { UIO_LINK = 6, UIO_SAMUS = 7, UIO_PLANDO = 18, UIO_JINGLE = 19, UIO_MAP = 8, UIO_DARK = 9, UIO_FILTER = 10, UIO_SHAPE = 11, UIO_HELP = 12, UIO_HELPS = 13, UIO_NERF = 22, UIO_SAVES = 23, UIO_MORPH = 24, UIO_MUSIC = 25 };

extern const UiOptDef UI_OPTS[];
extern const int UI_OPT_COUNT;

void ui_init(SmzUi *u);
void ui_set_slot(SmzUi *u, int i, int used, const char *name, const char *summary);
/* game 0 Link sprites, 1 Samus sprites, 2 plando files */
void ui_set_sprites(SmzUi *u, int game, const char *const *names, int n);
void ui_input(SmzUi *u, UiButton b);
/* Touch at canvas coordinates (as drawn by the last ui_draw): selects and activates the row under the point. */
void ui_touch(SmzUi *u, int x, int y);
void ui_busy(SmzUi *u, const char *msg);
void ui_idle(SmzUi *u);
/* Rebuilds u->cmd. */
void ui_draw(SmzUi *u);

/* Writes the settings JSON the frontends store with a run; returns its length or -1 when cap is too small. */
int ui_settings_json(const SmzUi *u, int32_t seed_if_random, char *out, size_t cap);

#endif
