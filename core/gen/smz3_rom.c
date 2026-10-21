#include <string.h>
#include "smz3_rom.h"

void smz_rom_combine(uint8_t *out, const uint8_t *sm, const uint8_t *z3)
{
    memset(out, 0, SMZ_ROM_SIZE);
    size_t pos = 0, used = 0;
    const uint8_t *src = sm;
    /* the 3 MB Super Metroid ROM fills the 0x60 half banks in order; the rest of the layout stays empty */
    for (int i = 0; i < 0x40 && used < SMZ_SM_SIZE; i++) { memcpy(out + pos + 0x8000, src, 0x8000); src += 0x8000; used += 0x8000; pos += 0x10000; }
    pos = 0;
    for (int i = 0; i < 0x40 && used < SMZ_SM_SIZE; i++) { memcpy(out + pos, src, 0x8000); src += 0x8000; used += 0x8000; pos += 0x10000; }
    pos = 0x400000;
    src = z3;
    for (int i = 0; i < 0x20; i++) { memcpy(out + pos + 0x8000, src, 0x8000); src += 0x8000; pos += 0x10000; }
}

static int sm_offset(int offset)
{
    if (offset >= 0x300000) return -1;
    int hi = offset < 0x200000;
    int base = hi ? offset : offset - 0x200000;
    int i = base / 0x8000;
    if (hi) i++;
    return base + i * 0x8000;
}

static int ips_apply(uint8_t *rom, const uint8_t *ips, size_t len, int translate)
{
    size_t p = 5;
    while (p + 3 < len) {
        int offset = (ips[p] << 16) | (ips[p + 1] << 8) | ips[p + 2];
        int size = (ips[p + 3] << 8) | ips[p + 4];
        p += 5;
        int at = translate ? sm_offset(offset) : offset;
        if (at < 0) return -1;
        if (size > 0) {
            if (p + size > len || at + size > SMZ_ROM_SIZE) return -1;
            memcpy(rom + at, ips + p, size);
            p += size;
        } else {
            int rle = (ips[p] << 8) | ips[p + 1];
            uint8_t b = ips[p + 2];
            p += 3;
            if (at + rle > SMZ_ROM_SIZE) return -1;
            memset(rom + at, b, rle);
        }
    }
    return 0;
}

int smz_ips_apply(uint8_t *rom, const uint8_t *ips, size_t len) { return ips_apply(rom, ips, len, 0); }
int smz_ips_apply_sm(uint8_t *rom, const uint8_t *ips, size_t len) { return ips_apply(rom, ips, len, 1); }

static uint16_t sum(const uint8_t *d, size_t n)
{
    uint32_t s = 0;
    for (size_t i = 0; i < n; i++) s += d[i];
    return (uint16_t)s;
}

static uint16_t mirror_sum(const uint8_t *d, size_t *length, size_t mask)
{
    while ((*length & mask) == 0 && mask != 0) mask >>= 1;
    uint16_t part1 = sum(d, mask), part2 = 0;
    size_t next = *length - mask;
    if (next != 0) {
        part2 = mirror_sum(d + mask, &next, mask >> 1);
        while (next < mask) { next += next; part2 = (uint16_t)(part2 + part2); }
        *length = mask + mask;
    }
    return (uint16_t)(part1 + part2);
}

void smz_rom_checksum(uint8_t *rom)
{
    const int header = 0x40FFB0, complement_at = header + 0x2C, checksum_at = header + 0x2E;
    size_t length = SMZ_ROM_SIZE;
    uint16_t checksum = mirror_sum(rom, &length, 0x800000);
    uint16_t complement = checksum ^ 0xFFFF;
    rom[complement_at] = (uint8_t)complement; rom[complement_at + 1] = (uint8_t)(complement >> 8);
    rom[checksum_at] = (uint8_t)checksum; rom[checksum_at + 1] = (uint8_t)(checksum >> 8);
}

const char *const SMZ_DEFAULT_CAS_IPS[] = {
    "spinjumprestart.ips", "nerfed_charge.ips", "refill_before_save.ips", "fast_doors.ips", "elevators_speed.ips",
    "AimAnyButton.ips", "rando_speed.ips", "noflashing.ips", "disable_screen_shake.ips", "EasierWJ.ips", "Celeste.ips",
    "SandPitPlatforms.ips", "maridia_entrance.ips",
    /* soft lock patches */
    "moat.ips", "climb_supers.ips", "early_super_bridge.ips", "dachora.ips", "spospo_save.ips", "mission_impossible.ips",
    "red_tower.ips", "spazer.ips", "kraid_save.ips", "nova_boost_platform.ips", "high_jump.ips",
};
const int SMZ_DEFAULT_CAS_IPS_COUNT = (int)(sizeof SMZ_DEFAULT_CAS_IPS / sizeof SMZ_DEFAULT_CAS_IPS[0]);
