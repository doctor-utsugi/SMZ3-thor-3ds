#include <jni.h>
#include <stdio.h>
#include "smz3_state.h"

#define NATIVE(name) Java_com_swordfish_lemuroid_app_shared_game_viewmodel_Smz3Native_##name

/* Returns {game, started, zeldaCleared, zeldaTotal, metroidCleared, metroidTotal}. */
JNIEXPORT jintArray JNICALL NATIVE(summarize)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL);
    jbyte *s = (*env)->GetByteArrayElements(env, sram, NULL);
    smz3_summary sum;
    smz3_summarize((const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl, &sum);
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);

    jint out[6] = {(jint)sum.game, sum.started, (jint)sum.zelda_cleared, (jint)sum.zelda_total,
                   (jint)sum.metroid_cleared, (jint)sum.metroid_total};
    jintArray result = (*env)->NewIntArray(env, 6);
    (*env)->SetIntArrayRegion(env, result, 0, 6, out);
    return result;
}

/* Returns the indices of every cleared location, empty before a file has started. */
JNIEXPORT jintArray JNICALL NATIVE(clearedLocations)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL);
    jbyte *s = (*env)->GetByteArrayElements(env, sram, NULL);
    smz3_game game = smz3_current_game((const uint8_t *)s, (size_t)sl);
    int started = smz3_started((const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl);

    jint cleared[1024];
    jsize count = 0;
    if (started && game != SMZ3_GAME_NONE) {
        for (unsigned i = 0; i < smz3_location_count && count < 1024; i++) {
            if (smz3_location_cleared(&smz3_locations[i], game, (const uint8_t *)w, (size_t)wl,
                                      (const uint8_t *)s, (size_t)sl) > 0)
                cleared[count++] = (jint)i;
        }
    }
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);

    jintArray result = (*env)->NewIntArray(env, count);
    (*env)->SetIntArrayRegion(env, result, 0, count, cleared);
    return result;
}

JNIEXPORT jstring JNICALL NATIVE(locationName)(JNIEnv *env, jclass cls, jint index)
{
    if (index < 0 || (unsigned)index >= smz3_location_count) return (*env)->NewStringUTF(env, "");
    char buf[256];
    snprintf(buf, sizeof buf, "%s: %s", smz3_locations[index].region, smz3_locations[index].name);
    return (*env)->NewStringUTF(env, buf);
}

/* Inventory levels in smz3_inventory_items order; -1 means unknown right now. */
JNIEXPORT jintArray JNICALL NATIVE(inventory)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL);
    jbyte *s = (*env)->GetByteArrayElements(env, sram, NULL);
    jint levels[256];
    int tmp[256];
    unsigned n = smz3_inventory_count < 256 ? smz3_inventory_count : 256;
    smz3_inventory((const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl, tmp);
    for (unsigned i = 0; i < n; i++) levels[i] = tmp[i];
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);
    jintArray result = (*env)->NewIntArray(env, (jsize)n);
    (*env)->SetIntArrayRegion(env, result, 0, (jsize)n, levels);
    return result;
}

/* "game|name|icon" for an inventory item, game being 1 Zelda or 2 Metroid. */
JNIEXPORT jstring JNICALL NATIVE(inventoryInfo)(JNIEnv *env, jclass cls, jint index)
{
    if (index < 0 || (unsigned)index >= smz3_inventory_count) return (*env)->NewStringUTF(env, "");
    const smz3_inv_item *it = &smz3_inventory_items[index];
    char buf[256];
    snprintf(buf, sizeof buf, "%d|%s|%s", (int)it->game, it->name, it->icon);
    return (*env)->NewStringUTF(env, buf);
}

/* "game|region|name|id" for a location. */
JNIEXPORT jstring JNICALL NATIVE(locationInfo)(JNIEnv *env, jclass cls, jint index)
{
    if (index < 0 || (unsigned)index >= smz3_location_count) return (*env)->NewStringUTF(env, "");
    const smz3_location *l = &smz3_locations[index];
    char buf[256];
    snprintf(buf, sizeof buf, "%d|%s|%s|%s", (int)l->game, l->region, l->name, l->id);
    return (*env)->NewStringUTF(env, buf);
}

/* Room number of an ALttP chest location (its flag lives in the room table) with its flag bit number in bits 16 and up, or -1. */
JNIEXPORT jint JNICALL NATIVE(zeldaLocationRoom)(JNIEnv *env, jclass cls, jint index)
{
    if (index < 0 || (unsigned)index >= smz3_location_count) return -1;
    const smz3_location *l = &smz3_locations[index];
    return l->game == SMZ3_GAME_ZELDA && !l->zelda_misc ? (jint)(l->addr | (l->flag << 16)) : -1;
}

JNIEXPORT jint JNICALL NATIVE(zeldaRoomLocation)(JNIEnv *env, jclass cls, jint room)
{
    return (jint)smz3_zelda_room_location((unsigned)room);
}

JNIEXPORT jint JNICALL NATIVE(locationCount)(JNIEnv *env, jclass cls)
{
    return (jint)smz3_location_count;
}

JNIEXPORT jint JNICALL NATIVE(inventoryCount)(JNIEnv *env, jclass cls)
{
    return (jint)smz3_inventory_count;
}

/* ---- offline generator (core/gen) */
#include <stdlib.h>
#include <string.h>
#include "smz3_generate.h"
#include "smz3_text.h"
#include "smz3_itemnames.h"
#include "smz3_plando.h"

static char g_error[128];

typedef struct { const char *name; const uint8_t *data; size_t len; } IpsEntry;
typedef struct { IpsEntry *e; int n; } IpsSet;

static const uint8_t *ips_lookup(void *user, const char *name, size_t *len)
{
    IpsSet *set = user;
    for (int i = 0; i < set->n; i++)
        if (!strcmp(set->e[i].name, name)) { *len = set->e[i].len; return set->e[i].data; }
    return NULL;
}

/* params: seed, keysanity, placement rule, ganon crystals, GT crystals, Tourian bosses, open pyramid, open Tourian door, light dark rooms, hint tiles, Etecoons jingle, nerfed charge, quick morph */
static void fill_settings(JNIEnv *env, jintArray params, jobjectArray logicNames, jintArray logicValues, SmzSettings *s, SmzPatchCfg *cfg)
{
    jint p[13] = {0};
    jsize np = (*env)->GetArrayLength(env, params);
    (*env)->GetIntArrayRegion(env, params, 0, np < 13 ? np : 13, p);
    smz_settings_defaults(s);
    s->seed = p[0]; s->keysanity = p[1]; s->placement_rule = p[2];
    s->ganon_crystals = p[3]; s->gt_crystals = p[4]; s->tourian_bosses = p[5];
    jsize n = (*env)->GetArrayLength(env, logicNames);
    jint *vals = (*env)->GetIntArrayElements(env, logicValues, NULL);
    for (jsize i = 0; i < n; i++) {
        jstring js = (*env)->GetObjectArrayElement(env, logicNames, i);
        const char *name = (*env)->GetStringUTFChars(env, js, NULL);
        smz_setting_set(s, name, vals[i]);
        (*env)->ReleaseStringUTFChars(env, js, name);
        (*env)->DeleteLocalRef(env, js);
    }
    (*env)->ReleaseIntArrayElements(env, logicValues, vals, JNI_ABORT);
    if (cfg) {
        smz_patch_cfg_defaults(cfg, s);
        cfg->pyramid_open = p[6];
        cfg->tourian_door_open = p[7];
        cfg->light_dark_rooms = p[8];
        cfg->hint_tiles = p[9];
        cfg->etecoons_jingle = p[10];
        cfg->nerfed_charge = p[11];
        cfg->quick_morph = p[12];
        cfg->sm_default_controls = 1;
    }
}

JNIEXPORT jbyteArray JNICALL NATIVE(generate)(JNIEnv *env, jclass cls, jbyteArray sm, jbyteArray z3, jobjectArray ipsNames,
                                              jobjectArray ipsData, jintArray params, jobjectArray logicNames, jintArray logicValues,
                                              jbyteArray linkRdc, jbyteArray samusRdc, jbyteArray plandoYaml)
{
    g_error[0] = 0;
    jsize sml = (*env)->GetArrayLength(env, sm), z3l = (*env)->GetArrayLength(env, z3);
    if (sml != 0x300000 || z3l != 0x100000) { snprintf(g_error, sizeof g_error, "base ROMs have the wrong size"); return NULL; }
    SmzSettings *s = calloc(1, sizeof *s);
    SmzPatchCfg *cfg = calloc(1, sizeof *cfg);
    fill_settings(env, params, logicNames, logicValues, s, cfg);

    jsize nips = (*env)->GetArrayLength(env, ipsNames);
    IpsSet set = { calloc(nips ? nips : 1, sizeof(IpsEntry)), nips };
    jstring *names = calloc(nips ? nips : 1, sizeof(jstring));
    jbyteArray *datas = calloc(nips ? nips : 1, sizeof(jbyteArray));
    for (jsize i = 0; i < nips; i++) {
        names[i] = (*env)->GetObjectArrayElement(env, ipsNames, i);
        datas[i] = (*env)->GetObjectArrayElement(env, ipsData, i);
        set.e[i].name = (*env)->GetStringUTFChars(env, names[i], NULL);
        set.e[i].data = (const uint8_t *)(*env)->GetByteArrayElements(env, datas[i], NULL);
        set.e[i].len = (size_t)(*env)->GetArrayLength(env, datas[i]);
    }
    jbyte *lnk = linkRdc ? (*env)->GetByteArrayElements(env, linkRdc, NULL) : NULL;
    jbyte *sam = samusRdc ? (*env)->GetByteArrayElements(env, samusRdc, NULL) : NULL;
    if (lnk) { cfg->link_rdc = (const uint8_t *)lnk; cfg->link_rdc_len = (size_t)(*env)->GetArrayLength(env, linkRdc); }
    if (sam) { cfg->samus_rdc = (const uint8_t *)sam; cfg->samus_rdc_len = (size_t)(*env)->GetArrayLength(env, samusRdc); }
    jbyte *smb = (*env)->GetByteArrayElements(env, sm, NULL);
    jbyte *z3b = (*env)->GetByteArrayElements(env, z3, NULL);

    SmzSeed *seed = calloc(1, sizeof *seed);
    uint8_t *rom = malloc(SMZ_ROM_SIZE);
    int rc;
    if (plandoYaml) {
        SmzPlando *pl = calloc(1, sizeof *pl);
        jbyte *yaml = (*env)->GetByteArrayElements(env, plandoYaml, NULL);
        char err[256];
        if (smz_plando_parse(pl, (const char *)yaml, (size_t)(*env)->GetArrayLength(env, plandoYaml), err, sizeof err)) {
            snprintf(g_error, sizeof g_error, "plando: %s", err);
            rc = -9;
        } else {
            rc = smz_generate_plando(rom, seed, (const uint8_t *)smb, (const uint8_t *)z3b, pl, cfg, ips_lookup, &set, NULL, NULL);
        }
        (*env)->ReleaseByteArrayElements(env, plandoYaml, yaml, JNI_ABORT);
        free(pl);   /* the patcher only read it during the call */
    } else {
        rc = smz_generate(rom, seed, (const uint8_t *)smb, (const uint8_t *)z3b, s, cfg, ips_lookup, &set, NULL, NULL);
    }
    jbyteArray result = NULL;
    if (rc == -9) {}   /* g_error holds the plando message */
    else if (rc) snprintf(g_error, sizeof g_error, rc == -6 ? "the sprite file is not valid" : rc == -8 ? "the plando config is incomplete" : "generation failed (%d)", rc);
    else {
        result = (*env)->NewByteArray(env, SMZ_ROM_SIZE);
        (*env)->SetByteArrayRegion(env, result, 0, SMZ_ROM_SIZE, (const jbyte *)rom);
    }

    if (lnk) (*env)->ReleaseByteArrayElements(env, linkRdc, lnk, JNI_ABORT);
    if (sam) (*env)->ReleaseByteArrayElements(env, samusRdc, sam, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sm, smb, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, z3, z3b, JNI_ABORT);
    for (jsize i = 0; i < nips; i++) {
        (*env)->ReleaseStringUTFChars(env, names[i], set.e[i].name);
        (*env)->ReleaseByteArrayElements(env, datas[i], (jbyte *)set.e[i].data, JNI_ABORT);
        (*env)->DeleteLocalRef(env, names[i]);
        (*env)->DeleteLocalRef(env, datas[i]);
    }
    free(names); free(datas); free(set.e); free(rom); free(seed); free(s); free(cfg);
    return result;
}

JNIEXPORT jstring JNICALL NATIVE(lastError)(JNIEnv *env, jclass cls)
{
    return (*env)->NewStringUTF(env, g_error);
}

JNIEXPORT jint JNICALL NATIVE(itemByName)(JNIEnv *env, jclass cls, jstring name)
{
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    int id = smz_item_by_name(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return id;
}

static SmzSeed g_seed;
static SmzSettings g_seed_settings;
static int g_seed_valid;
static SmzPlando g_seed_plando;
static jsize g_seed_plando_len = -1;

/* (Re)builds the cached seed of a run: filled from its settings, or taken from its plando YAML. */
static void ensure_seed(JNIEnv *env, const SmzSettings *s, jbyteArray plandoYaml)
{
    jsize plen = plandoYaml ? (*env)->GetArrayLength(env, plandoYaml) : 0;
    SmzPlando pl;
    int have_pl = 0;
    if (plandoYaml) {
        jbyte *yaml = (*env)->GetByteArrayElements(env, plandoYaml, NULL);
        char err[256];
        have_pl = smz_plando_parse(&pl, (const char *)yaml, (size_t)plen, err, sizeof err) == 0;
        (*env)->ReleaseByteArrayElements(env, plandoYaml, yaml, JNI_ABORT);
        if (!have_pl) { g_seed_valid = 0; return; }
    }
    if (g_seed_valid && !memcmp(s, &g_seed_settings, sizeof *s) && plen == g_seed_plando_len && (!have_pl || !memcmp(&pl, &g_seed_plando, sizeof pl))) return;
    if (have_pl) {
        char err[256];
        g_seed_valid = smz_plando_seed(&g_seed, &pl, err, sizeof err) == 0;
        g_seed_plando = pl;
    } else {
        SmzSettings eff;
        g_seed_valid = smz_fill_valid(&g_seed, s, &eff) == 0;
    }
    g_seed_settings = *s;
    g_seed_plando_len = plen;
}

/* Indices (into the tracker's location table) of the locations in logic for this run's seed and these item types. */
JNIEXPORT jintArray JNICALL NATIVE(reachable)(JNIEnv *env, jclass cls, jintArray params, jobjectArray logicNames, jintArray logicValues,
                                              jintArray items, jbyteArray plandoYaml)
{
    SmzSettings s;
    fill_settings(env, params, logicNames, logicValues, &s, NULL);
    ensure_seed(env, &s, plandoYaml);
    if (!g_seed_valid) return (*env)->NewIntArray(env, 0);
    jsize n = (*env)->GetArrayLength(env, items);
    jint *it = (*env)->GetIntArrayElements(env, items, NULL);
    int ids[512];
    int all[1100], na = smz_assumed_items(&g_seed, all, 600);
    for (jsize i = 0; i < n && na < 1100; i++) all[na++] = it[i];
    int k = smz_reachable(&g_seed, all, na, ids, 512);
    (*env)->ReleaseIntArrayElements(env, items, it, JNI_ABORT);
    jint out[512];
    int count = 0;
    for (int i = 0; i < k; i++)
        for (unsigned j = 0; j < smz3_location_count; j++) {
            const char *id = smz3_locations[j].id;
            for (int q = 0; q < SMZ_LOC_NAME_COUNT; q++)
                if (SMZ_LOC_NAMES[q].id == ids[i]) {
                    if (!strcmp(SMZ_LOC_NAMES[q].name, id)) out[count++] = (jint)j;
                    break;
                }
        }
    jintArray result = (*env)->NewIntArray(env, count);
    (*env)->SetIntArrayRegion(env, result, 0, count, out);
    return result;
}

/* The hints a player may ask for in the limited help mode (the same ones the hint tiles show), for this run's seed. */
JNIEXPORT jobjectArray JNICALL NATIVE(helpTexts)(JNIEnv *env, jclass cls, jintArray params, jobjectArray logicNames, jintArray logicValues, jbyteArray plandoYaml)
{
    SmzSettings s;
    SmzPatchCfg cfg;
    fill_settings(env, params, logicNames, logicValues, &s, &cfg);
    ensure_seed(env, &s, plandoYaml);
    jclass str = (*env)->FindClass(env, "java/lang/String");
    jobjectArray result = (*env)->NewObjectArray(env, SMZ_HELP_COUNT, str, NULL);
    if (!g_seed_valid) return result;
    static char texts[SMZ_HELP_COUNT][SMZ_HELP_TEXT];
    smz_help_texts(&g_seed, &cfg, texts);
    for (int i = 0; i < SMZ_HELP_COUNT; i++) {
        jstring js = (*env)->NewStringUTF(env, texts[i]);
        (*env)->SetObjectArrayElement(env, result, i, js);
        (*env)->DeleteLocalRef(env, js);
    }
    return result;
}

/* ---- shared menu (core/ui): the Kotlin side only forwards input and draws the commands */
#include "smz3_ui.h"
#include "smz3_run.h"
#include "smz3_ui_gen.h"

static SmzUi g_ui;

JNIEXPORT void JNICALL NATIVE(uiInit)(JNIEnv *env, jclass cls) { ui_init(&g_ui); }

JNIEXPORT void JNICALL NATIVE(uiSetSlot)(JNIEnv *env, jclass cls, jint i, jboolean used, jstring name, jstring summary)
{
    const char *n = (*env)->GetStringUTFChars(env, name, NULL), *s = (*env)->GetStringUTFChars(env, summary, NULL);
    ui_set_slot(&g_ui, i, used, n, s);
    (*env)->ReleaseStringUTFChars(env, name, n);
    (*env)->ReleaseStringUTFChars(env, summary, s);
}

JNIEXPORT void JNICALL NATIVE(uiSetSlotStats)(JNIEnv *env, jclass cls, jint i, jint zc, jint zt, jint mc, jint mt, jlong ms, jint game, jint bosses, jint crystals)
{
    ui_set_slot_stats(&g_ui, i, zc, zt, mc, mt, ms, game, bosses, crystals);
}

JNIEXPORT void JNICALL NATIVE(uiSetSprites)(JNIEnv *env, jclass cls, jint game, jobjectArray names)
{
    jsize n = (*env)->GetArrayLength(env, names);
    const char **list = calloc(n ? n : 1, sizeof *list);
    jstring *js = calloc(n ? n : 1, sizeof *js);
    for (jsize i = 0; i < n; i++) {
        js[i] = (*env)->GetObjectArrayElement(env, names, i);
        list[i] = (*env)->GetStringUTFChars(env, js[i], NULL);
    }
    ui_set_sprites(&g_ui, game, list, n);
    for (jsize i = 0; i < n; i++) { (*env)->ReleaseStringUTFChars(env, js[i], list[i]); (*env)->DeleteLocalRef(env, js[i]); }
    free(list); free(js);
}

JNIEXPORT void JNICALL NATIVE(uiInput)(JNIEnv *env, jclass cls, jint button) { ui_input(&g_ui, (UiButton)button); }
JNIEXPORT void JNICALL NATIVE(uiTouch)(JNIEnv *env, jclass cls, jint x, jint y) { ui_touch(&g_ui, x, y); }
JNIEXPORT void JNICALL NATIVE(uiIdle)(JNIEnv *env, jclass cls) { ui_idle(&g_ui); }

JNIEXPORT void JNICALL NATIVE(uiBusy)(JNIEnv *env, jclass cls, jstring msg)
{
    const char *m = (*env)->GetStringUTFChars(env, msg, NULL);
    ui_busy(&g_ui, m);
    (*env)->ReleaseStringUTFChars(env, msg, m);
}

/* One string per command: "kind,x,y,w,h,color,align,text". */
JNIEXPORT jobjectArray JNICALL NATIVE(uiDraw)(JNIEnv *env, jclass cls)
{
    ui_draw(&g_ui);
    jclass str = (*env)->FindClass(env, "java/lang/String");
    jobjectArray out = (*env)->NewObjectArray(env, g_ui.cmd_count, str, NULL);
    for (int i = 0; i < g_ui.cmd_count; i++) {
        const UiCmd *c = &g_ui.cmd[i];
        char line[128];
        snprintf(line, sizeof line, "%d,%d,%d,%d,%d,%d,%d,%s", c->kind, c->x, c->y, c->w, c->h, c->color, c->align, c->text);
        jstring js = (*env)->NewStringUTF(env, line);
        (*env)->SetObjectArrayElement(env, out, i, js);
        (*env)->DeleteLocalRef(env, js);
    }
    return out;
}

/* [action, slot] of a pending host action, or [0, 0]; clears it. */
JNIEXPORT jintArray JNICALL NATIVE(uiAction)(JNIEnv *env, jclass cls)
{
    jint v[2] = { g_ui.action, g_ui.action_slot };
    g_ui.action = ACT_NONE;
    jintArray out = (*env)->NewIntArray(env, 2);
    (*env)->SetIntArrayRegion(env, out, 0, 2, v);
    return out;
}

/* The plando YAML of the file chosen in the menu (null clears it); returns the error text, empty on success. */
JNIEXPORT jstring JNICALL NATIVE(uiSetPlando)(JNIEnv *env, jclass cls, jbyteArray yaml)
{
    char err[256] = "";
    if (!yaml) ui_set_plando(NULL, 0, err, sizeof err);
    else {
        jbyte *b = (*env)->GetByteArrayElements(env, yaml, NULL);
        if (ui_set_plando((const char *)b, (size_t)(*env)->GetArrayLength(env, yaml), err, sizeof err)) {}
        (*env)->ReleaseByteArrayElements(env, yaml, b, JNI_ABORT);
    }
    return (*env)->NewStringUTF(env, err);
}

JNIEXPORT jstring JNICALL NATIVE(uiPlandoName)(JNIEnv *env, jclass cls)
{
    char name[32];
    ui_plando_name(&g_ui, name);
    return (*env)->NewStringUTF(env, name);
}

JNIEXPORT jstring JNICALL NATIVE(uiSettingsJson)(JNIEnv *env, jclass cls, jint seedIfRandom)
{
    char buf[4096];
    if (ui_settings_json(&g_ui, seedIfRandom, buf, sizeof buf) < 0) buf[0] = 0;
    return (*env)->NewStringUTF(env, buf);
}

/* Saved customs: the host stores the text in a file; null (no file yet) installs the built-in ones. */
JNIEXPORT void JNICALL NATIVE(uiSavedLoad)(JNIEnv *env, jclass cls, jstring text)
{
    const char *t = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
    ui_saved_load(&g_ui, t);
    if (t) (*env)->ReleaseStringUTFChars(env, text, t);
}

/* The saved customs as the file text when they changed since the last call, else null. */
JNIEXPORT jstring JNICALL NATIVE(uiSavedChanged)(JNIEnv *env, jclass cls)
{
    static char buf[UI_MAX_SAVED * (UI_SPEC_LEN + 40)];
    if (!ui_saved_dirty(&g_ui) || ui_saved_text(&g_ui, buf, sizeof buf) < 0) return NULL;
    return (*env)->NewStringUTF(env, buf);
}

/* Player profile ("name<TAB>CC"): shows the Player row in the start screen; same file handling as the saved customs. */
JNIEXPORT void JNICALL NATIVE(uiPlayerLoad)(JNIEnv *env, jclass cls, jstring text)
{
    const char *t = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
    ui_player_enable(&g_ui, 1);
    ui_player_load(&g_ui, t);
    if (t) (*env)->ReleaseStringUTFChars(env, text, t);
}

JNIEXPORT jint JNICALL NATIVE(uiOnline)(JNIEnv *env, jclass cls) { return ui_online_now(&g_ui); }

JNIEXPORT jstring JNICALL NATIVE(uiPlayerChanged)(JNIEnv *env, jclass cls)
{
    char buf[64];
    if (!ui_player_dirty(&g_ui) || ui_player_text(&g_ui, buf, sizeof buf) < 0) return NULL;
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL NATIVE(uiRunLabel)(JNIEnv *env, jclass cls) { return (*env)->NewStringUTF(env, ui_run_label(&g_ui)); }

JNIEXPORT jint JNICALL NATIVE(uiOption)(JNIEnv *env, jclass cls, jint opt) { return opt >= 0 && opt < UI_OPT_COUNT ? g_ui.value[opt] : 0; }
JNIEXPORT void JNICALL NATIVE(uiSetSlotCount)(JNIEnv *env, jclass cls, jint n) { ui_set_slot_count(&g_ui, n); }
/* The slot the menu is on: the highlighted one in the list, the opened one in its menus. */
JNIEXPORT jint JNICALL NATIVE(uiSlot)(JNIEnv *env, jclass cls) { return g_ui.screen == UI_SLOTS_SCREEN ? g_ui.slot_cursor : g_ui.screen == UI_SLOT_MENU || g_ui.screen == UI_CONFIRM ? g_ui.active_slot : -1; }
JNIEXPORT jint JNICALL NATIVE(uiScreen)(JNIEnv *env, jclass cls) { return g_ui.screen; }

JNIEXPORT jstring JNICALL NATIVE(romName)(JNIEnv *env, jclass cls, jstring json, jstring seed)
{
    const char *j = (*env)->GetStringUTFChars(env, json, NULL), *sd = (*env)->GetStringUTFChars(env, seed, NULL);
    char buf[96];
    if (smz_run_rom_name(j, sd, buf, sizeof buf) < 0) buf[0] = 0;
    (*env)->ReleaseStringUTFChars(env, json, j);
    (*env)->ReleaseStringUTFChars(env, seed, sd);
    return (*env)->NewStringUTF(env, buf);
}
