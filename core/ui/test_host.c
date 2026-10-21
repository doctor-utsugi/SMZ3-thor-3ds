/* The menu path (smz_host_generate with default options) must build the same ROM as the plain generator for the same seed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_host.h"
#include "smz3_generate.h"
#include "smz3_plando.h"

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    fread(b, 1, *n, f); fclose(f);
    return b;
}
static const char *dir;
static const uint8_t *ips(void *u, const char *name, size_t *len)
{
    static uint8_t *b; free(b);
    char p[256]; snprintf(p, sizeof p, "%s/%s", dir, name);
    return b = slurp(p, len);
}

int main(int argc, char **argv)
{
    size_t n;
    uint8_t *sm = slurp(argv[1], &n), *z3 = slurp(argv[2], &n);
    dir = argv[3];
    static SmzUi ui;
    ui_init(&ui);
    static uint8_t a[SMZ_HOST_ROM_SIZE], b[SMZ_HOST_ROM_SIZE];
    char json[4096], seedText[16];
    if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, a, json, sizeof json, seedText, sizeof seedText, NULL)) return 2;
    SmzSettings s; smz_settings_defaults(&s); s.seed = 777;
    SmzPatchCfg cfg; smz_patch_cfg_defaults(&cfg, &s);
    static SmzSeed seed;
    if (smz_generate(b, &seed, sm, z3, &s, &cfg, ips, NULL, NULL, NULL)) return 3;
    int bad = memcmp(a, b, sizeof a) != 0;
    printf(bad ? "DIFFERENT\n" : "host path identical\n");
    /* the jingle row: choosing jingle 3 must give the generator's etecoons_jingle = 3 patch, and differ from the default ROM */
    {
        static uint8_t j[SMZ_HOST_ROM_SIZE];
        ui.value[UIO_JINGLE] = 3;
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, j, json, sizeof json, seedText, sizeof seedText, NULL)) return 7;
        cfg.etecoons_jingle = 3;
        smz_patch_cfg_defaults(&cfg, &s); cfg.etecoons_jingle = 3;
        if (smz_generate(b, &seed, sm, z3, &s, &cfg, ips, NULL, NULL, NULL)) return 8;
        int jbad = memcmp(j, b, sizeof j) != 0;
        printf(jbad ? "jingle DIFFERENT\n" : "jingle row reaches the ROM\n");
        if (!jbad && !memcmp(j, a, sizeof j)) { printf("jingle changed nothing\n"); jbad = 1; }
        bad |= jbad;
        ui.value[UIO_JINGLE] = 0;
    }
    {   /* the "Nerfed charge" row: off by default (as on the web), on applies nerfed_charge.ips at $90:B81E */
        static const uint8_t VANILLA[5] = {0x89, 0x00, 0x10, 0xD0, 0x0A}, NERFED[5] = {0x89, 0x00, 0x00, 0x80, 0x0A};
        static uint8_t c[SMZ_HOST_ROM_SIZE];
        const size_t at = 0x10B81E;
        int nbad = memcmp(a + at, VANILLA, 5) != 0;
        ui.value[UIO_NERF] = 1;
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, c, json, sizeof json, seedText, sizeof seedText, NULL)) return 9;
        nbad |= memcmp(c + at, NERFED, 5) != 0 || !strstr(json, "\"nerfedCharge\":true");
        printf(nbad ? "nerfed charge row DIFFERENT\n" : "nerfed charge: off by default, on patches the ROM\n");
        bad |= nbad;
        ui.value[UIO_NERF] = 0;
    }
    if (argc > 4) {   /* a plando file: the host path with it set must equal smz_generate_plando */
        size_t yl;
        uint8_t *yaml = slurp(argv[4], &yl);
        char err[256];
        if (smz_host_set_plando((const char *)yaml, yl, err, sizeof err)) { printf("plando: %s\n", err); return 4; }
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, a, json, sizeof json, seedText, sizeof seedText, NULL)) return 5;
        static SmzPlando pl;
        smz_plando_parse(&pl, (const char *)yaml, yl, err, sizeof err);
        smz_plando_settings(&pl, &s);
        smz_patch_cfg_defaults(&cfg, &s);
        if (smz_generate_plando(b, &seed, sm, z3, &pl, &cfg, ips, NULL, NULL, NULL)) return 6;
        int pbad = memcmp(a, b, sizeof a) != 0;
        printf(pbad ? "plando DIFFERENT\n" : "plando host path identical\n");
        bad |= pbad;
    }
    return bad;
}
