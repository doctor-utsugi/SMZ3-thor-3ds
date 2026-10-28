#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pc_compat.h"
#include "pc_online.h"
#include "pc_store.h"

#define BASE "https://smz3.indieviral.com"

/* Runs curl without a shell. Returns the HTTP status (-1 when curl failed) and the body in out. */
#ifdef _WIN32
static int http(const char *method, const char *path, const char *body, char *out, size_t cap)
{
    char req[600], res[600], cmd[2048], url[256];
    snprintf(req, sizeof req, "%s", store_path("tmp/request.json"));
    snprintf(res, sizeof res, "%s", store_path("tmp/response.txt"));
    snprintf(url, sizeof url, "%s%s", getenv("SMZ3_SERVER") ? getenv("SMZ3_SERVER") : BASE, path);
    out[0] = 0;
    if (body && store_write(req, body, strlen(body))) return -1;
    int n = snprintf(cmd, sizeof cmd, "curl.exe -s -m 8 -X %s -H \"Content-Type: application/json\" ", method);
    if (body) n += snprintf(cmd + n, sizeof cmd - n, "--data-binary @\"%s\" ", req);
    snprintf(cmd + n, sizeof cmd - n, "-w \"\\n%%{http_code}\" \"%s\"", url);
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE h = CreateFileA(res, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    STARTUPINFOA si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h;
    si.hStdError = h;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(h);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    remove(req);
    FILE *f = fopen(res, "rb");
    if (!f) return -1;
    size_t len = fread(out, 1, cap - 1, f);
    fclose(f);
    remove(res);
    out[len] = 0;
    char *nl = strrchr(out, '\n');
    if (!nl || exit_code != 0) return -1;
    int code = atoi(nl + 1);
    *nl = 0;
    return code;
}
#else
static int http(const char *method, const char *path, const char *body, char *out, size_t cap)
{
    int in_pipe[2], out_pipe[2];
    out[0] = 0;
    if (pipe(in_pipe) || pipe(out_pipe)) return -1;
    char url[256];
    snprintf(url, sizeof url, "%s%s", getenv("SMZ3_SERVER") ? getenv("SMZ3_SERVER") : BASE, path);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        dup2(in_pipe[0], 0); dup2(out_pipe[1], 1);
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) dup2(nul, 2);
        close(in_pipe[1]); close(out_pipe[0]);
        if (body) execlp("curl", "curl", "-s", "-m", "8", "-X", method, "-H", "Content-Type: application/json", "--data-binary", "@-", "-w", "\n%{http_code}", url, (char *)NULL);
        execlp("curl", "curl", "-s", "-m", "8", "-w", "\n%{http_code}", url, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]); close(out_pipe[1]);
    if (body) { size_t n = strlen(body); if (write(in_pipe[1], body, n) < 0) {} }
    close(in_pipe[1]);
    size_t len = 0;
    ssize_t r;
    while (len + 1 < cap && (r = read(out_pipe[0], out + len, cap - 1 - len)) > 0) len += (size_t)r;
    out[len] = 0;
    close(out_pipe[0]);
    int status;
    waitpid(pid, &status, 0);
    char *nl = strrchr(out, '\n');
    if (!nl || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    int code = atoi(nl + 1);
    *nl = 0;
    return code;
}

#endif

/* The string value of "key":"..." (no escapes expected from the server), 0 when absent. */
static int json_str(const char *json, const char *key, char *out, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    size_t n = strcspn(p, "\"");
    if (n >= cap) return 0;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

static char *read_text(const char *path)
{
    size_t n;
    char *t = (char *)store_read(path, &n);
    if (t) t[n] = 0;
    return t;
}

static void install_id(char out[48])
{
    char *t = read_text(store_path("runs/install_id"));
    if (t && strlen(t) >= 16 && strlen(t) <= 40) { snprintf(out, 48, "%s", t); out[strcspn(out, "\r\n ")] = 0; free(t); return; }
    free(t);
    FILE *f = fopen("/dev/urandom", "rb");
    unsigned char b[16] = {0};
    if (f) { if (fread(b, 1, 16, f) != 16) {} fclose(f); }
    snprintf(out, 48, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    store_write(store_path("runs/install_id"), out, strlen(out));
}

void online_init(SmzUi *u)
{
    char *t = read_text(store_path("runs/player.txt"));
    ui_player_enable(u, 1);
    ui_player_load(u, t);
    free(t);
}

void online_save_profile(SmzUi *u)
{
    if (!ui_player_dirty(u)) return;
    char text[128];
    if (ui_player_text(u, text, sizeof text) > 0) store_write(store_path("runs/player.txt"), text, strlen(text));
}

static void profile(char name[32], char country[4])
{
    char *t = read_text(store_path("runs/player.txt"));
    name[0] = country[0] = 0;
    if (t) {
        char *tab = strchr(t, '\t');
        if (tab) {
            *tab = 0;
            char *t2 = strchr(tab + 1, '\t');
            if (t2) *t2 = 0;
            snprintf(country, 4, "%s", tab + 1);
        }
        snprintf(name, 32, "%s", t);
        free(t);
    }
    if (!name[0]) { char id[48]; install_id(id); snprintf(name, 32, "Player-%.4s", id); }
    for (char *c = name; *c; c++) if (*c == '"' || *c == '\\' || (unsigned char)*c < 32) *c = '_';
    if (strlen(country) != 2 || !isupper((unsigned char)country[0]) || !isupper((unsigned char)country[1])) country[0] = 0;
}

int online_slot_kind(int slot)
{
    char *t = read_text(store_path("runs/slot%d.online", slot));
    int k = !t ? 0 : !strncmp(t, "original", 8) ? 1 : !strncmp(t, "monthly", 7) ? 2 : !strncmp(t, "weekly", 6) ? 3 : 0;
    free(t);
    return k;
}

void online_mark_run(int slot, int online, int kind)
{
    remove(store_path("runs/slot%d.token", slot));
    remove(store_path("runs/slot%d.finish", slot));
    remove(store_path("runs/slot%d.proof", slot));
    if (online) {
        const char *k = kind == 3 ? "weekly" : kind == 2 ? "monthly" : "original";
        store_write(store_path("runs/slot%d.online", slot), k, strlen(k));
    }
    else remove(store_path("runs/slot%d.online", slot));
}

int online_days_left(void)
{
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    struct tm e = {0};
    e.tm_year = t.tm_year; e.tm_mon = t.tm_mon + 1; e.tm_mday = 1;
    time_t end = timegm(&e);
    return (int)((end - now) / 86400);
}

/* The fixed seed of this period: fetched, or the copy saved for the same period. */
static int fetch_period(const char *path, const char *file, const char *key, int weekly, char label[16], int *seed)
{
    char body[512], s[32], m[16];
    int code = http("GET", path, NULL, body, sizeof body);
    if (code == 200 && json_str(body, "seed", s, sizeof s) && json_str(body, key, m, sizeof m) && atoi(s) > 0)
        store_write(store_path("runs/%s", file), body, strlen(body));
    char *t = read_text(store_path("runs/%s", file));
    int ok = 0;
    if (t && json_str(t, "seed", s, sizeof s) && json_str(t, key, m, sizeof m)) {
        time_t now = time(NULL);
        struct tm g;
        gmtime_r(&now, &g);
        char cur[16];
        if (weekly) { strftime(cur, sizeof cur, "%G-W%V", &g); }
        else strftime(cur, sizeof cur, "%Y-%m", &g);
        if (!strcmp(cur, m) && atoi(s) > 0) { snprintf(label, 16, "%s", m); *seed = atoi(s); ok = 1; }
    }
    free(t);
    return ok;
}

int online_monthly(char label[16], int *seed) { return fetch_period("/api/monthly", "monthly.json", "month", 0, label, seed); }
int online_weekly(char label[16], int *seed) { return fetch_period("/api/weekly", "weekly.json", "week", 1, label, seed); }

int online_weekly_days_left(void)
{
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    int to_monday = 8 - (t.tm_wday ? t.tm_wday : 7);           /* days to the next Monday 00:00 UTC, counting today */
    return (int)(to_monday * 86400L - (t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec)) / 86400;
}

int online_ensure_token(int slot)
{
    char *kind = read_text(store_path("runs/slot%d.online", slot));
    if (!kind) return 1;
    if (access(store_path("runs/slot%d.token", slot), F_OK) == 0) { free(kind); return 1; }
    kind[strcspn(kind, "\r\n ")] = 0;
    char id[48], req[200], reply[1024], token[512];
    install_id(id);
    snprintf(req, sizeof req, "{\"install\":\"%s\",\"kind\":\"%s\"}", id, kind);
    free(kind);
    if (http("POST", "/api/start", req, reply, sizeof reply) != 200 || !json_str(reply, "token", token, sizeof token)) return 0;
    store_write(store_path("runs/slot%d.token", slot), token, strlen(token));
    return 1;
}

int online_ladder_json(int tab, char *out, size_t cap)
{
    static const char *const PATH[3] = {"/api/ladder?kind=original", "/api/ladder?kind=monthly", "/api/ladder?kind=weekly"};
    return tab >= 0 && tab <= 2 && http("GET", PATH[tab], NULL, out, cap) == 200;
}

void online_queue_finish(int slot, int64_t time_ms, int seed)
{
    if (access(store_path("runs/slot%d.token", slot), F_OK) != 0) return;
    char t[48];
    int n = snprintf(t, sizeof t, "%lld %d", (long long)time_ms, seed);
    store_write(store_path("runs/slot%d.finish", slot), t, (size_t)n);
}

void online_flush(void)
{
    for (int slot = 1; slot <= UI_SLOTS; slot++) {
        char *ft = read_text(store_path("runs/slot%d.finish", slot));
        char *tk = read_text(store_path("runs/slot%d.token", slot));
        if (ft && tk) {
            tk[strcspn(tk, "\r\n ")] = 0;
            char name[32], country[4], reply[1024];
            static char body[16384];
            long long ms = 0;
            int seed = 0;
            sscanf(ft, "%lld %d", &ms, &seed);
            profile(name, country);
            int n = snprintf(body, sizeof body, "{\"token\":\"%s\",\"timeMs\":%lld,\"player\":\"%s\"", tk, ms, name);
            if (country[0]) n += snprintf(body + n, sizeof body - n, ",\"country\":\"%s\"", country);
            if (seed > 0) n += snprintf(body + n, sizeof body - n, ",\"seed\":\"%d\"", seed);
            n += snprintf(body + n, sizeof body - n, ",\"proof\":\"");
            char *pf = read_text(store_path("runs/slot%d.proof", slot));
            for (const char *c = pf ? pf : ""; *c && n < (int)sizeof body - 8; c++) {
                if (*c == '\n') { body[n++] = '\\'; body[n++] = 'n'; }
                else if (*c >= '0' && *c <= '9') body[n++] = *c;
                else if (*c == ' ') body[n++] = ' ';
            }
            free(pf);
            snprintf(body + n, sizeof body - n, "\"}");
            int code = http("POST", "/api/finish", body, reply, sizeof reply);
            if (code == 400) {
                char msg[1100];
                int m = snprintf(msg, sizeof msg, "slot %d: %s", slot, reply);
                store_write(store_path("runs/last_online_error.txt"), msg, (size_t)m);
            }
            if (code == 200 || code == 400) {
                remove(store_path("runs/slot%d.finish", slot));
                remove(store_path("runs/slot%d.proof", slot));
                remove(store_path("runs/slot%d.token", slot));
            }
        }
        free(ft); free(tk);
    }
}
