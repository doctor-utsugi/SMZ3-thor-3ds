/* Run tracker shared by every frontend: reads the emulator memory every tick and keeps what the tracker screens show. */
#ifndef SMZ3_TRACKER_H
#define SMZ3_TRACKER_H

#include <stddef.h>
#include <stdint.h>
#include "smz3_state.h"

#define SMZ_TRK_RECENT 8
#define SMZ_TRK_MAX_LOCS 512
#define SMZ_TRK_MAX_INV 64
#define SMZ_TRK_MAX_ITEMS 400

typedef struct {
    smz3_game game;
    int started;
    unsigned zelda_cleared, zelda_total, metroid_cleared, metroid_total;
    uint8_t cleared[SMZ_TRK_MAX_LOCS];       /* per location index */
    int inventory[SMZ_TRK_MAX_INV];          /* last known level per smz3_inventory_items entry */
    int recent_checks[SMZ_TRK_RECENT];       /* location indices, newest first; n_recent_checks used */
    int n_recent_checks;
    int recent_items[SMZ_TRK_RECENT];        /* inventory indices, newest first */
    int n_recent_items;
    int boss_mask;                           /* bit 0 Kraid, 1 Phantoon, 2 Draygon, 3 Ridley */
    int crystal_mask;                        /* bit n-1 is crystal n */
    int64_t elapsed_ms;                      /* run play time including this session */
    int64_t save_ms; int save_zelda, save_metroid;   /* the game's own save: valid when save_ms > 0 */
    int save_changed;                        /* set when the save RAM changed during the last tick */
    /* internals */
    int64_t base_ms, pending_ms, live_at_ms, now_ms;
    int have_live, last_frame, have_frame, running;
    uint32_t sram_hash; int have_hash;
    smz3_summary live;
    uint8_t live_cleared[SMZ_TRK_MAX_LOCS];
    int seen_valid, inv_baseline;
} SmzTracker;

/* Starts (or restarts) tracking a run that already has base_ms of play time and the given last save. */
void smz_tracker_reset(SmzTracker *t, int64_t base_ms, int64_t save_ms, int save_zelda, int save_metroid);

/* One reading. dt_ms is the real time since the previous tick; speed is 1 or the fast-forward factor. */
void smz_tracker_tick(SmzTracker *t, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len,
                      int dt_ms, int speed);

/* Play time gained since the last smz_tracker_commit; commit folds it into the base (call when the run is saved). */
int64_t smz_tracker_pending(const SmzTracker *t);
void smz_tracker_commit(SmzTracker *t);

/* ItemType ids for the inventory read from the game plus keysanity keys and cards (keysanity: 0 None, 1 Zelda,
 * 2 SuperMetroid, 3 Both), ready for smz_reachable. Returns how many were written to out. */
int smz_tracker_logic_items(const SmzTracker *t, const uint8_t *wram, size_t wram_len, const uint8_t *sram,
                            size_t sram_len, int keysanity, int *out, int cap);

/* Which Super Metroid bosses (bit 0 Kraid, 1 Phantoon, 2 Draygon, 3 Ridley) and ALttP crystals are done. */
int smz_boss_mask(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, int game);
int smz_crystal_mask(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, int game);

#endif
