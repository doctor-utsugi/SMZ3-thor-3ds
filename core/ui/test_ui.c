/* Drives the menu with scripted buttons and checks the settings it produces. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "smz3_ui_gen.h"
#include "smz3_run.h"

int main(void)
{
    static SmzUi u;
    ui_init(&u);
    const char *links[] = {"Zelda", "Pink Link"};
    ui_set_sprites(&u, 0, links, 2);
    ui_set_slot(&u, 1, 1, "Run 2", "Zelda 3/216");
    ui_draw(&u);
    assert(u.cmd_count > 4);

    ui_input(&u, BTN_A);                       /* empty slot 1 -> Create */
    assert(u.screen == UI_CREATE && u.active_slot == 0);
    ui_input(&u, BTN_DOWN); ui_input(&u, BTN_DOWN);   /* keysanity (after Game mode) */
    ui_input(&u, BTN_RIGHT);
    ui_input(&u, BTN_RIGHT);                   /* Both -> Zelda only */
    ui_input(&u, BTN_R);                       /* Zelda tab */
    for (int i = 0; i < 3; i++) ui_input(&u, BTN_DOWN);   /* Link sprite */
    ui_input(&u, BTN_A);
    assert(u.screen == UI_SPRITES);
    ui_input(&u, BTN_RIGHT);      /* Pink Link (grid, names are sorted) */
    ui_input(&u, BTN_A);
    assert(u.screen == UI_CREATE);
    ui_input(&u, BTN_R);                       /* Metroid tab */
    for (int i = 0; i < 4; i++) ui_input(&u, BTN_DOWN);   /* wall jump (after the jingle) */
    ui_input(&u, BTN_RIGHT);                   /* Hard */
    ui_draw(&u);

    SmzSettings s; SmzPatchCfg pc; char link[32], samus[32];
    ui_settings(&u, 1234, &s, &pc, link, samus);
    assert(s.seed == 1234 && s.keysanity == KEYS_Zelda);
    assert(!strcmp(link, "Pink Link") && !samus[0]);
    assert(s.cfg.WallJumpDifficulty == 3);

    for (int i = 0; i < 40 && u.screen == UI_CREATE; i++) { ui_input(&u, BTN_DOWN); }
    u.level = LEVEL_RUN; u.cursor[0] = 7;  /* Create button (the "Helps available" row is hidden unless the mode is Limited) */
    ui_input(&u, BTN_A);
    assert(u.action == ACT_GENERATE && u.action_slot == 0);
    {   /* "Helps available" only shows in the Limited mode */
        int shown[2];
        for (int m = 0; m < 3; m++) {
            u.value[UIO_HELP] = m; u.level = LEVEL_RUN; u.screen = UI_CREATE; u.scroll[0] = 0; u.cursor[0] = 0;
            int found = 0;
            for (int pass = 0; pass < 3; pass++) {
                u.scroll[0] = pass * 6; ui_draw(&u);
                for (int i = 0; i < u.cmd_count; i++) if (u.cmd[i].kind == 1 && strstr(u.cmd[i].text, "Helps available")) found = 1;
            }
            shown[m == 1] = found;
            assert(found == (m == 1));
        }
        (void)shown;
        u.value[UIO_HELP] = 0;
    }
    /* touch: tapping an empty slot opens Create, tapping a tab switches level */
    u.action = ACT_NONE; u.screen = UI_SLOTS_SCREEN;
    ui_draw(&u);
    ui_touch(&u, 100, 40 + 2 * 46 + 10);
    assert(u.screen == UI_CREATE && u.active_slot == 2);
    ui_draw(&u);
    ui_touch(&u, 137, 10);
    assert(u.level == LEVEL_ZELDA);
    ui_touch(&u, 188, 10);
    assert(u.level == LEVEL_METROID);
    ui_touch(&u, 239, 10);
    assert(u.level == LEVEL_PLAY);
    ui_touch(&u, 290, 10);
    assert(u.level == LEVEL_ITEMS);
    ui_touch(&u, 10, 10);
    assert(u.screen == UI_SLOTS_SCREEN);
    ui_draw(&u);
    ui_touch(&u, 100, 40 + 2 * 46 + 10);               /* open Create again, then scroll and press the button by touch */
    ui_draw(&u);
    ui_touch(&u, UI_W - 18, 44 + 100);
    assert(u.cursor[u.level] == 3);
    ui_draw(&u);
    ui_touch(&u, UI_W - 40, UI_H - 20);
    assert(u.action == ACT_GENERATE && u.action_slot == 2);
    u.action = ACT_NONE; u.screen = UI_SLOTS_SCREEN;
    char js[2048];
    assert(ui_settings_json(&u, 5, js, sizeof js) > 0);
    assert(strstr(js, "\"keysanity\":\"Zelda\"") && strstr(js, "\"linkSprite\":\"Pink Link\"") && strstr(js, "\"WallJumpDifficulty\":3"));
    printf("%s\n", js);
    SmzRun run = {0}, back;
    snprintf(run.name, sizeof run.name, "Run 1");
    snprintf(run.seed, sizeof run.seed, "5");
    snprintf(run.rom_path, sizeof run.rom_path, "/tmp/x.sfc");
    snprintf(run.settings, sizeof run.settings, "%s", js);
    run.zelda_cleared = 3; run.zelda_total = 216; run.play_ms = 3723000; run.created = 1700000000000LL;
    assert(smz_run_save("/tmp/test_slot.run", &run) == 0 && smz_run_load("/tmp/test_slot.run", &back) == 0);
    assert(!strcmp(back.name, "Run 1") && !strcmp(back.settings, js) && back.play_ms == 3723000 && back.created == run.created);
    char sum[64]; smz_run_summary(&back, sum, sizeof sum);
    assert(strstr(sum, "Zelda 3/216") && strstr(sum, "1:02:03"));
    assert(smz_run_load("/tmp/does_not_exist.run", &back) < 0);
    char n1[96], n2[96];
    smz_run_rom_name(js, "5", n1, sizeof n1);
    ui_settings_json(&u, 5, js, sizeof js);
    u.value[UIO_MAP] = 1; u.value[UIO_FILTER] = 2; u.value[UIO_SHAPE] = 1;      /* tracker-only options must not change the name */
    ui_settings_json(&u, 5, js, sizeof js);
    smz_run_rom_name(js, "5", n2, sizeof n2);
    assert(!strcmp(n1, n2) && !strncmp(n1, "smz3_5_", 7));
    u.value[UIO_HELPS] = 7; ui_settings_json(&u, 5, js, sizeof js);       /* how many helps may be asked is not in the ROM */
    smz_run_rom_name(js, "5", n2, sizeof n2);
    assert(!strcmp(n1, n2));
    u.value[UIO_HELP] = 1; ui_settings_json(&u, 5, js, sizeof js);          /* limited helps: no hint tiles in the ROM */
    smz_run_rom_name(js, "5", n2, sizeof n2);
    assert(strcmp(n1, n2) && strstr(js, "\"helpMode\":\"limited\"") && strstr(js, "\"helpCount\":7"));
    { SmzSettings hs; SmzPatchCfg hp; char l[32], sm[32]; ui_settings(&u, 5, &hs, &hp, l, sm); assert(hp.hint_tiles == 0); }
    u.value[UIO_HELP] = 0;
    { SmzSettings hs; SmzPatchCfg hp; char l[32], sm[32]; ui_settings(&u, 5, &hs, &hp, l, sm); assert(hp.hint_tiles == 15); }
    ui_settings_json(&u, 5, js, sizeof js);
    u.value[UIO_DARK] = 1; ui_settings_json(&u, 5, js, sizeof js);
    smz_run_rom_name(js, "5", n2, sizeof n2);
    assert(strcmp(n1, n2));
    /* plando: the chosen file's config replaces the seed and the settings */
    {
        const char *plandos[] = {"first.yml", "second"};
        ui_set_sprites(&u, 2, plandos, 2);
        u.value[UIO_PLANDO] = 2;
        char name[32], err[256];
        ui_plando_name(&u, name);
        assert(!strcmp(name, "second"));
        const char *yaml = "Seed: \"777\"\nGameModeType: AllDungeons\nGanonCrystalCount: 3\nKeysanityMode: Zelda\nOpenPyramid: true\nLogic:\n  MockBall: true\n  WallJumpDifficulty: Hard\n";
        assert(ui_set_plando(yaml, strlen(yaml), err, sizeof err) == 0 && ui_plando());
        ui_settings_json(&u, 5, js, sizeof js);
        assert(strstr(js, "\"seed\":\"777\"") && strstr(js, "\"gameMode\":\"AllDungeons\"") && strstr(js, "\"plando\":\"second\"") &&
               strstr(js, "\"MockBall\":true") && strstr(js, "\"WallJumpDifficulty\":3") && strstr(js, "\"keysanity\":\"Zelda\""));
        assert(ui_set_plando("Items:\n  Nowhere - Nothing: Missile\n", 36, err, sizeof err) < 0 && !ui_plando() && strstr(err, "Nowhere"));
        ui_set_plando(NULL, 0, err, sizeof err);
        ui_settings_json(&u, 5, js, sizeof js);
        assert(strstr(js, "\"plando\":\"\""));
    }
    {   /* 32 slots, 4 shown, chevrons; deleting asks first */
        SmzUi v; ui_init(&v);
        ui_set_slot_count(&v, 32);
        ui_set_slot(&v, 5, 1, "Run 6", "Zelda 1/216");
        for (int k = 0; k < 6; k++) ui_input(&v, BTN_DOWN);
        assert(v.slot_cursor == 6 && v.slot_scroll == 3);
        ui_draw(&v);
        int chev = 0;
        for (int k = 0; k < v.hit_count; k++) chev += v.hit[k].kind == H_CHEVRON;
        assert(chev == 2);
        v.slot_cursor = 5; ui_input(&v, BTN_A);                       /* the used slot opens its menu */
        assert(v.screen == UI_SLOT_MENU);
        ui_input(&v, BTN_DOWN); ui_input(&v, BTN_A);
        assert(v.screen == UI_CONFIRM && v.action == ACT_NONE);       /* nothing is deleted yet */
        ui_input(&v, BTN_A);                                          /* Cancel is the default */
        assert(v.screen == UI_SLOT_MENU && v.action == ACT_NONE);
        ui_input(&v, BTN_A);                                          /* Continue */
        ui_input(&v, BTN_A);
        v.action = ACT_NONE; v.screen = UI_SLOT_MENU; v.menu_cursor = 1; ui_input(&v, BTN_A);
        ui_input(&v, BTN_DOWN); ui_input(&v, BTN_A);
        assert(v.action == ACT_DELETE && v.action_slot == 5);
    }
    printf("ui ok, %d draw commands\n", u.cmd_count);
    return 0;
}
