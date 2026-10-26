/* The combined SMZ3 map as a pannable, zoomable view with check dots, boss marks and the player. */
#ifndef PC_MAP_H
#define PC_MAP_H

#include <SDL.h>
#include "smz3_discovery.h"
#include "smz3_tracker.h"

typedef struct { float cx, cy, zoom; int follow; } MapView;

void map_view_init(MapView *v);
/* dx, dy in map pixels per call; dz multiplies the zoom. Any pan turns follow off. */
void map_view_move(MapView *v, float dx, float dy, float dz);
void map_draw(SDL_Renderer *r, MapView *v, SDL_Rect area, const SmzTracker *t, const SmzDiscovery *d, int discovered_only,
              const uint8_t *in_logic, const uint8_t *wram, size_t wram_len, Uint32 ticks);

#endif
