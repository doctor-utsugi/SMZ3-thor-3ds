#include "smz3_shortcuts.h"
#include "smz3_state.h"

const SmzShortcut smz_shortcuts[SC_COUNT] = {
    {"Bow", "bow.png"}, {"Blue Boomerang", "blue boomerang.png"}, {"Red Boomerang", "red boomerang.png"},
    {"Hookshot", "hookshot.png"}, {"Bombs", "bombs.png"}, {"Mushroom", "mushroom.png"}, {"Powder", "magic powder.png"},
    {"Fire Rod", "fire rod.png"}, {"Ice Rod", "ice rod.png"}, {"Bombos", "bombos.png"}, {"Ether", "ether.png"},
    {"Quake", "quake.png"}, {"Lamp", "lamp.png"}, {"Hammer", "hammer.png"}, {"Shovel", "shovel.png"}, {"Flute", "flute.png"},
    {"Bug Net", "bug catching net.png"}, {"Book", "book of mudora.png"}, {"Bottle 1", "bottle.png"}, {"Bottle 2", "bottle.png"},
    {"Bottle 3", "bottle.png"}, {"Bottle 4", "bottle.png"}, {"Somaria", "cane of somaria.png"}, {"Byrna", "cane of byrna.png"},
    {"Cape", "magic cape.png"}, {"Mirror", "magic mirror.png"},
    {"Wave", "wave beam.png"}, {"Ice", "ice beam.png"}, {"Spazer", "spazer.png"}, {"Plasma", "plasma beam.png"},
};

/* ALttP: the inventory at $7EF340 (one byte per menu slot, in menu order), the randomizer's owned bits at $7EF38C, the menu
 * slot on Y at $0202 (1..20) with bit 7 asking the ROM hook to redraw it. Super Metroid: equipped beams $09A6, collected
 * $09A8, bit 15 of $09A6 asking the hook for the beam tiles. */
enum { INV = 0xF340, SWAP = 0xF38C, Y_SLOT = 0x202, BOTTLES = 0xF35C, BOTTLE_PICK = 0xF34F, BEAMS_ON = 0x9A6, BEAMS_HAVE = 0x9A8 };

static const struct { uint8_t slot, swap_bit, variant; } ZELDA[SC_ZELDA_COUNT] = {
    [SC_BOW] = {1, 0, 0}, [SC_BLUE_BOOMERANG] = {2, 0x80, 1}, [SC_RED_BOOMERANG] = {2, 0x40, 2}, [SC_HOOKSHOT] = {3, 0, 0},
    [SC_BOMBS] = {4, 0, 0}, [SC_MUSHROOM] = {5, 0x20, 1}, [SC_POWDER] = {5, 0x10, 2}, [SC_FIRE_ROD] = {6, 0, 0},
    [SC_ICE_ROD] = {7, 0, 0}, [SC_BOMBOS] = {8, 0, 0}, [SC_ETHER] = {9, 0, 0}, [SC_QUAKE] = {10, 0, 0}, [SC_LAMP] = {11, 0, 0},
    [SC_HAMMER] = {12, 0, 0}, [SC_SHOVEL] = {13, 0x04, 1}, [SC_FLUTE] = {13, 0x03, 2}, [SC_NET] = {14, 0, 0},
    [SC_BOOK] = {15, 0, 0}, [SC_BOTTLE1] = {16, 0, 1}, [SC_BOTTLE2] = {16, 0, 2}, [SC_BOTTLE3] = {16, 0, 3},
    [SC_BOTTLE4] = {16, 0, 4}, [SC_SOMARIA] = {17, 0, 0}, [SC_BYRNA] = {18, 0, 0}, [SC_CAPE] = {19, 0, 0}, [SC_MIRROR] = {20, 0, 0},
};
static const uint8_t BEAM[4] = {0x01, 0x02, 0x04, 0x08};   /* wave, ice, spazer, plasma */

static int is_bottle(int id) { return id >= SC_BOTTLE1 && id <= SC_BOTTLE4; }
static int wram_ok(size_t len) { return len > 0xF400; }

int smz_shortcut_ready(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len)
{
    if (!wram_ok(wram_len)) return 0;
    switch (smz3_current_game(sram, sram_len)) {
    case SMZ3_GAME_ZELDA: return (wram[0x10] == 0x07 || wram[0x10] == 0x09) && wram[0x11] == 0;
    case SMZ3_GAME_METROID: return (wram[0x998] | wram[0x999] << 8) == 8;
    default: return 0;
    }
}

int smz_shortcut_owned(int id, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len)
{
    if (id < 0 || id >= SC_COUNT || !wram_ok(wram_len)) return 0;
    smz3_game g = smz3_current_game(sram, sram_len);
    if (id >= SC_ZELDA_COUNT) return g == SMZ3_GAME_METROID && (wram[BEAMS_HAVE] & BEAM[id - SC_WAVE]) != 0;
    if (g != SMZ3_GAME_ZELDA) return 0;
    if (is_bottle(id)) return wram[BOTTLES + ZELDA[id].variant - 1] != 0;
    if (ZELDA[id].swap_bit) return (wram[SWAP] & ZELDA[id].swap_bit) != 0;
    return wram[INV + ZELDA[id].slot - 1] != 0;
}

int smz_shortcut_active(int id, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len)
{
    if (!smz_shortcut_owned(id, wram, wram_len, sram, sram_len)) return 0;
    if (id >= SC_ZELDA_COUNT) return (wram[BEAMS_ON] & BEAM[id - SC_WAVE]) != 0;
    if ((wram[Y_SLOT] & 0x7F) != ZELDA[id].slot) return 0;
    if (is_bottle(id)) return wram[BOTTLE_PICK] == ZELDA[id].variant;
    uint8_t now = wram[INV + ZELDA[id].slot - 1];
    if (id == SC_FLUTE) return now >= 2;
    return !ZELDA[id].variant || now == ZELDA[id].variant;
}

int smz_shortcut_pokes(int id, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, SmzPoke *out, int cap)
{
    if (cap < 3 || !smz_shortcut_ready(wram, wram_len, sram, sram_len) || !smz_shortcut_owned(id, wram, wram_len, sram, sram_len)) return 0;
    int n = 0;
    if (id >= SC_ZELDA_COUNT) {
        uint8_t on = wram[BEAMS_ON] ^ BEAM[id - SC_WAVE];
        if (id == SC_SPAZER && (on & 0x04)) on &= (uint8_t)~0x08;   /* Spazer and Plasma never fire together */
        if (id == SC_PLASMA && (on & 0x08)) on &= (uint8_t)~0x04;
        out[n++] = (SmzPoke){BEAMS_ON, on};
        out[n++] = (SmzPoke){BEAMS_ON + 1, (uint8_t)(wram[BEAMS_ON + 1] | 0x80)};
        return n;
    }
    if (is_bottle(id)) out[n++] = (SmzPoke){BOTTLE_PICK, ZELDA[id].variant};
    else if (id == SC_FLUTE) out[n++] = (SmzPoke){INV + 12, (uint8_t)(wram[SWAP] & 0x01 ? 3 : 2)};   /* 3: the bird is free */
    else if (ZELDA[id].variant) out[n++] = (SmzPoke){INV + ZELDA[id].slot - 1, ZELDA[id].variant};
    out[n++] = (SmzPoke){Y_SLOT, (uint8_t)(ZELDA[id].slot | 0x80)};
    return n;
}
