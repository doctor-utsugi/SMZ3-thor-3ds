/* The online ladder (https://smz3.indieviral.com), the PC twin of OnlineLadder.kt. Requests go through the curl program, so
 * nothing is linked; without curl or a connection the run simply stays off the ladder. Slots are the 1-based numbers of the run files. */
#ifndef PC_ONLINE_H
#define PC_ONLINE_H

#include <stdint.h>
#include "smz3_ui.h"

/* Loads the stored profile into the menu and enables its Player row. */
void online_init(SmzUi *u);
/* Writes the profile when the menu changed it. */
void online_save_profile(SmzUi *u);
/* 0 offline, 1 original-rules ladder, 2 monthly seed. */
int online_slot_kind(int slot);
/* Records what the menu answered for the run just generated. */
void online_mark_run(int slot, int online, int monthly);
/* This month's label and seed (fetched, or the copy of the same month); 0 on none. Blocks up to a few seconds. */
int online_monthly(char label[16], int *seed);
int online_days_left(void);
/* Asks for the token of an online run once; 1 when the slot has one (or is not online). Blocks. */
int online_ensure_token(int slot);
void online_queue_finish(int slot, int64_t time_ms);
/* Sends the finished runs waiting. Blocks. */
void online_flush(void);

#endif
