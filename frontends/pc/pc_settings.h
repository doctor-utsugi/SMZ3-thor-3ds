/* The settings screen of the PC app, drawn over the menu and driven by keyboard, controller or mouse: controller buttons, a file browser
 * to pick the base ROMs (no drag and drop needed), full screen and quit. */
#ifndef PC_SETTINGS_H
#define PC_SETTINGS_H

#include <SDL.h>
#include "smz3_ui.h"

typedef struct {
    const char *(*setup)(const char *path);   /* keeps the ROMs found at a path (file, zip or folder); returns a message */
    void (*note)(const char *msg);
    void (*fullscreen)(void);
    void (*quit)(void);
} SettingsHooks;

void settings_init(const SettingsHooks *hooks);
int settings_active(void);
void settings_open(void);
void settings_open_files(void);
/* A menu button (from the keyboard or the controller). */
void settings_nav(UiButton b);
/* Raw events while a button is being assigned; returns 1 when the event was used. */
int settings_raw(const SDL_Event *e);
/* Draws the screen in a 320 x 240 area scaled by k at (ox, oy). */
void settings_draw(float k, float ox, float oy);

#endif
