#include "core/kidctl.h"
#include "core/profile.h"
#include "core/store.h"
#include "core/net.h"
#include "core/json.h"
#include "core/app.h"
#include "screens/player.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- a small id -> value table (open addressing; ids cut at the episode part) ---- */
#define TCAP 32768
typedef struct { char id[28]; signed char v; } Ent;
typedef struct { Ent *e; int n; } Tab;
static unsigned hs(const char *s) { unsigned h = 2166136261u; while (*s) h = (h ^ (unsigned char)*s++) * 16777619u; return h; }
static Ent *tab_at(Tab *t, const char *id, int make) {
  if (!t->e) { if (!make) return NULL; t->e = calloc(TCAP, sizeof(Ent)); if (!t->e) return NULL; }
  unsigned i = hs(id) & (TCAP - 1);
  for (int k = 0; k < TCAP; k++, i = (i + 1) & (TCAP - 1)) {
    if (!t->e[i].id[0]) { if (!make || t->n > TCAP * 3 / 4) return NULL; snprintf(t->e[i].id, sizeof t->e[i].id, "%s", id); t->n++; return &t->e[i]; }
    if (!strcmp(t->e[i].id, id)) return &t->e[i]; }
  return NULL;
}
static void base_id(const char *id, char out[28]) { size_t n = 0; while (id && id[n] && n < 27 && !(id[n] == ':' && !strncmp(id, "tt", 2))) { out[n] = id[n]; n++; } out[n] = 0; }

static Tab marks, listed, ages;   /* marks: 1 allowed, 2 blocked; ages: -1 no rating, 0.. the age, -3 asked */
static char baseDir[1024];
static int maxAge = 10, listsOn = 1, torrents, liveOn, bedOn, bedFrom = 19 * 60 + 30, bedTo = 7 * 60, bedExt; static time_t bedExtUntil;
static int schoolOn = 1, skipYmd, adultPinned; static int ymd_now(int *mins, int *wday);
static int hhmm(const char *s, int def) { int h, m; return s && sscanf(s, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60 ? h * 60 + m : def; }

/* ---- ages on disk: "id age", appended as they are learnt ---- */
static void ages_path(char *p, size_t cap) { snprintf(p, cap, "%s/pc-ratings.txt", baseDir); }
static void listed_path(char *p, size_t cap) { snprintf(p, cap, "%s/pc-listed.txt", baseDir); }
static void load_file(Tab *t, const char *path, int withVal) {
  FILE *f = fopen(path, "r"); if (!f) return; char id[64]; int v = 1;
  while (withVal ? fscanf(f, "%27s %d", id, &v) == 2 : fscanf(f, "%27s", id) == 1) { Ent *e = tab_at(t, id, 1); if (e) e->v = (signed char)v; }
  fclose(f);
}
static void append(const char *path, const char *id, int v, int withVal) { FILE *f = fopen(path, "a"); if (!f) return; if (withVal) fprintf(f, "%s %d\n", id, v); else fprintf(f, "%s\n", id); fclose(f); }

void kidctl_capture(const char *dir) {
  snprintf(baseDir, sizeof baseDir, "%s", dir);
  maxAge = atoi(store_get("pcAge", "10")); listsOn = strcmp(store_get("pcLists", "true"), "false") != 0; torrents = !strcmp(store_get("pcTorrents", "false"), "true"); liveOn = !strcmp(store_get("pcLive", "false"), "true");
  bedOn = !strcmp(store_get("pcBed", "false"), "true"); bedFrom = hhmm(store_get("pcBedFrom", "19:30"), 19 * 60 + 30); bedTo = hhmm(store_get("pcBedTo", "07:00"), 7 * 60);
  bedExt = atoi(store_get("pcBedExt", "0")); bedExtUntil = (time_t)atol(store_get("pcBedExtUntil", "0"));
  if (bedExt && time(NULL) >= bedExtUntil) { bedExt = 0; store_set("pcBedExt", "0"); store_set("pcBedExtUntil", "0"); } /* last night's later bedtime is over */
  schoolOn = strcmp(store_get("pcSchool", "true"), "false") != 0; skipYmd = atoi(store_get("pcSchoolSkipDay", "0"));
  if (skipYmd && skipYmd != ymd_now(NULL, NULL)) { skipYmd = 0; store_set("pcSchoolSkipDay", "0"); store_set("pcSchoolSkip", "false"); } /* yesterday's skip is over */
  const cJSON *x; char b[28];
  cJSON_ArrayForEach(x, store_node("pcAllow")) if (cJSON_IsString(x)) { base_id(x->valuestring, b); Ent *e = tab_at(&marks, b, 1); if (e) e->v = KID_ALLOWED; }
  cJSON_ArrayForEach(x, store_node("pcBlock")) if (cJSON_IsString(x)) { base_id(x->valuestring, b); Ent *e = tab_at(&marks, b, 1); if (e) e->v = KID_BLOCKED; }
  char p[1100]; ages_path(p, sizeof p); load_file(&ages, p, 1); listed_path(p, sizeof p); load_file(&listed, p, 0);
  fprintf(stderr, "[kids] rules: age limit %d, lists %s, torrents %s, live TV %s, bedtime %s, school mornings %s, %d marked, %d ratings known\n", maxAge, listsOn ? "on" : "off", torrents ? "on" : "off", liveOn ? "on" : "off", bedOn ? "on" : "off", schoolOn ? (skipYmd ? "skipped today" : "on") : "off", marks.n, ages.n);
}
/* ---- school mornings -------------------------------------------------------------------------------------------
   The school's term dates (its calendar for 2026-27), and England's bank holidays (GOV.UK, 2026-27). Every weekday in a
   term that is not a closure is a school morning. Next year's dates: add them here */
static const struct { int from, to; } TERMS[] = {
  { 20260902, 20261023 }, { 20261102, 20261218 }, { 20270104, 20270212 }, { 20270222, 20270325 }, { 20270412, 20270528 }, { 20270607, 20270716 } };
static const int CLOSED[] = { 20260901, 20270506, 20270719, 20270720, /* INSET days */
  20260101, 20260403, 20260406, 20260504, 20260525, 20260831, 20261225, 20261228, /* bank holidays */
  20270101, 20270326, 20270329, 20270503, 20270531, 20270830, 20271227, 20271228 };
#define M_READY (8 * 60 + 10)
#define M_STOP  (8 * 60 + 25)
#define M_UNTIL (9 * 60)
static int ymd_now(int *mins, int *wday) { time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt); if (mins) *mins = lt.tm_hour * 60 + lt.tm_min; if (wday) *wday = lt.tm_wday; return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday; }
static int school_day(int ymd, int wday) {
  if (wday == 0 || wday == 6) return 0;
  for (size_t i = 0; i < sizeof CLOSED / sizeof *CLOSED; i++) if (CLOSED[i] == ymd) return 0;
  for (size_t i = 0; i < sizeof TERMS / sizeof *TERMS; i++) if (ymd >= TERMS[i].from && ymd <= TERMS[i].to) return 1;
  return 0; }
static int morning_mins(void) { /* the minute of a school morning before 9:00, or -1 */
  if (!schoolOn) return -1; int m, w, d = ymd_now(&m, &w);
  if (d == skipYmd || m >= M_UNTIL || !school_day(d, w)) return -1; return m; }
int kidctl_morning(void) { int m = morning_mins(); return m < 0 ? KM_NONE : m < M_READY ? KM_CLOCK : m < M_STOP ? KM_LAST : KM_LOCK; }
int kidctl_morning_amber(void) { int m = morning_mins(); return m >= M_READY - 10 && m < M_STOP; }
int kidctl_morning_left(void) { int m = morning_mins(); return m < 0 ? 0 : M_READY - m; }
void kidctl_skip_today(int on) { skipYmd = on ? ymd_now(NULL, NULL) : 0; char b[16]; snprintf(b, sizeof b, "%d", skipYmd); store_set("pcSchoolSkipDay", b); fprintf(stderr, "[kids] school lock %s today\n", on ? "skipped" : "back on"); }
const char *kidctl_school_until(void) { static char s[24]; int d = TERMS[sizeof TERMS / sizeof *TERMS - 1].to; static const char *M[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" }; snprintf(s, sizeof s, "%d %s %d", d % 100, M[(d / 100) % 100 - 1], d / 10000); return s; }
void kidctl_adult_pinned(int on) { adultPinned = on; }
int kidctl_adult_is_pinned(void) { return adultPinned; }

int kidctl_lists_on(void) { return listsOn; }
int kidctl_no_torrents(void) { return profile_is_kids() && !torrents; }
int kidctl_no_live(void) { return profile_is_kids() && !liveOn; }
int kidctl_bedtime(char *until, size_t cap) {
  if (!bedOn || bedFrom == bedTo) return 0;
  time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt); int m = lt.tm_hour * 60 + lt.tm_min;
  int from = bedExt && t < bedExtUntil ? (bedFrom + bedExt) % 1440 : bedFrom; /* tonight's later bedtime */
  int in = from < bedTo ? (m >= from && m < bedTo) : (m >= from || m < bedTo);
  if (in && until) snprintf(until, cap, "%02d:%02d", bedTo / 60, bedTo % 60);
  return in;
}

/* ---- the Adult profile's marks (kept in its settings) ---- */
int kidctl_mark(const char *id) { char b[28]; base_id(id, b); Ent *e = tab_at(&marks, b, 0); return e ? e->v : KID_RULE; }
static void arr_del(const char *key, const char *id) { cJSON *a = store_node(key); if (!a) return; int i = 0; const cJSON *x; cJSON_ArrayForEach(x, a) { if (cJSON_IsString(x) && !strcmp(x->valuestring, id)) { cJSON_DeleteItemFromArray(a, i); store_set_node(key, a); return; } i++; } }
static void arr_add(const char *key, const char *id) { cJSON *a = store_node(key); if (!cJSON_IsArray(a)) { a = cJSON_CreateArray(); cJSON_AddItemToArray(a, cJSON_CreateString(id)); store_set_node(key, a); return; } cJSON_AddItemToArray(a, cJSON_CreateString(id)); store_set_node(key, a); }
void kidctl_set_mark(const char *id, int mark) {
  if (profile_is_kids()) return;
  char b[28]; base_id(id, b); if (!b[0]) return;
  arr_del("pcAllow", b); arr_del("pcBlock", b);
  if (mark == KID_ALLOWED) arr_add("pcAllow", b); else if (mark == KID_BLOCKED) arr_add("pcBlock", b);
  Ent *e = tab_at(&marks, b, 1); if (e) e->v = (signed char)mark;
  store_flush();
}

/* ---- age ratings ---- */
static int age_of(const char *r) {
  static const struct { const char *r; int a; } M[] = { { "U", 4 }, { "UC", 4 }, { "G", 4 }, { "TV-Y", 4 }, { "TV-G", 4 }, { "TV-Y7", 7 }, { "TV-Y7-FV", 7 }, { "PG", 8 }, { "TV-PG", 8 },
    { "12", 12 }, { "12A", 12 }, { "PG-13", 13 }, { "TV-14", 14 }, { "15", 15 }, { "R", 17 }, { "18", 18 }, { "R18", 18 }, { "TV-MA", 18 }, { "NC-17", 18 }, { "X", 18 } };
  if (!r) return -1;
  for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) if (!strcasecmp(M[i].r, r)) return M[i].a;
  return -1;
}
#define QMAX 600
#define BATCH 40
static char queue[QMAX][28]; static int nq;
typedef struct { char ids[BATCH][28]; int n, phase; } Job;
static unsigned gen_; static int flying, changed; static double holdUntil, refreshAt;
static void send(Job *j);
static void learnt(const char *id, int age) { Ent *e = tab_at(&ages, id, 1); if (!e) return; e->v = (signed char)age; char p[1100]; ages_path(p, sizeof p); append(p, id, age, 1); if (age >= 0 && age <= maxAge) changed = 1; }
static void on_ages(int st, const unsigned char *body, size_t len, void *ud) {
  Job *j = ud; (void)len; cJSON *r = st == 200 ? net_json(body) : NULL; const cJSON *ts = js_arr(js_obj(r, "data"), "titles");
  if (!ts) { /* no answer (or not the one expected): asked again later, the titles stay hidden meanwhile */
    fprintf(stderr, "[kids] ratings: no answer (status %d): %.200s\n", st, st > 0 && body ? (const char *)body : "");
    for (int i = 0; i < j->n; i++) { Ent *e = tab_at(&ages, j->ids[i], 0); if (e && e->v == -3) e->v = -4; } /* -4: not known, may be asked again */
    holdUntil = app_now() + 120; net_json_free(r); free(j); flying = 0; return; }
  Job *again = j->phase == 0 ? calloc(1, sizeof *again) : NULL; if (again) again->phase = 1;
  int got = 0;
  for (int i = 0; i < j->n; i++) { const char *rating = NULL; const cJSON *t;
    cJSON_ArrayForEach(t, ts) if (!strcmp(js_str(t, "id", ""), j->ids[i])) { rating = js_str(js_obj(t, "certificate"), "rating", NULL); break; }
    int a = age_of(rating);
    if (a >= 0) { learnt(j->ids[i], a); got++; }
    else if (again) snprintf(again->ids[again->n++], 28, "%s", j->ids[i]);
    else learnt(j->ids[i], -1); }
  fprintf(stderr, "[kids] ratings (%s): %d of %d\n", j->phase ? "US" : "UK", got, j->n);
  net_json_free(r); free(j);
  if (again && again->n) send(again); else { free(again); flying = 0; if (changed) { changed = 0; gen_++; refreshAt = app_now() + 1.0; } }
}
static void send(Job *j) {
  char q[BATCH * 32 + 96]; int o = snprintf(q, sizeof q, "query{titles(ids:[");
  for (int i = 0; i < j->n; i++) o += snprintf(q + o, sizeof q - o, "%s\"%s\"", i ? "," : "", j->ids[i]);
  snprintf(q + o, sizeof q - o, "]){id certificate{rating}}}");
  cJSON *b = cJSON_CreateObject(); cJSON_AddStringToObject(b, "query", q); char *body = cJSON_PrintUnformatted(b); cJSON_Delete(b);
  flying = 1;
  net_post_hdr_t("https://api.graphql.imdb.com/", body, "application/json", j->phase ? "x-imdb-client-name: imdb-web-next\nUser-Agent: Mozilla/5.0\nx-imdb-user-country: US" : "x-imdb-client-name: imdb-web-next\nUser-Agent: Mozilla/5.0\nx-imdb-user-country: GB", 15000, on_ages, j);
  free(body);
}
static int valid_tt(const char *b) { if (strncmp(b, "tt", 2)) return 0; size_t n = strlen(b); if (n < 7 || n > 12) return 0; for (size_t i = 2; i < n; i++) if (b[i] < '0' || b[i] > '9') return 0; return 1; }
int kidctl_ok(const char *id) {
  if (!profile_is_kids()) return 1;
  char b[28]; base_id(id, b); if (!b[0]) return 0;
  Ent *m = tab_at(&marks, b, 0); if (m && m->v == KID_BLOCKED) return 0; if (m && m->v == KID_ALLOWED) return 1;
  if (listsOn) { Ent *l = tab_at(&listed, b, 0); if (l) return 1; }
  if (maxAge <= 0 || !valid_tt(b)) return 0;
  Ent *a = tab_at(&ages, b, 0);
  if (a && a->v >= 0) return a->v <= maxAge;
  if (a && (a->v == -1 || a->v == -3)) return 0;
  if (app_now() < holdUntil || nq >= QMAX) return 0;
  a = tab_at(&ages, b, 1); if (!a) return 0; a->v = -3; snprintf(queue[nq++], 28, "%s", b);
  return 0;
}
void kidctl_listed(const char *id) {
  if (!profile_is_kids() || !listsOn) return;
  char b[28]; base_id(id, b); if (!b[0] || tab_at(&listed, b, 0)) return;
  if (tab_at(&listed, b, 1)) { char p[1100]; listed_path(p, sizeof p); append(p, b, 1, 0); }
}
void kidctl_tick(void) {
  if (!profile_is_kids()) return;
  if (nq && !flying) { Job *j = calloc(1, sizeof *j); if (j) { while (nq && j->n < BATCH) snprintf(j->ids[j->n++], 28, "%s", queue[--nq]); send(j); } }
  if (refreshAt && app_now() >= refreshAt && app_idle() > 0.7 && !player_open()) { refreshAt = 0; app_refresh_boards(); } /* titles that may now be shown: the rows are made again */
}

unsigned kidctl_gen(void) { return gen_; } /* counts the times new ratings arrived: Search asks again then */

/* Parental controls > Tonight: later bedtime. Kept till the next time bedtime ends (tomorrow morning), then back to
   normal. Written to the Adult profile's settings, where the rules are read from */
void kidctl_extend_tonight(int minutes) {
  char b[24]; time_t now = time(NULL), until = 0;
  if (minutes > 0) { int to = hhmm(store_get("pcBedTo", "07:00"), 7 * 60); struct tm lt; localtime_r(&now, &lt);
    lt.tm_hour = to / 60; lt.tm_min = to % 60; lt.tm_sec = 0; until = mktime(&lt); if (until <= now) { lt.tm_mday += 1; until = mktime(&lt); } }
  snprintf(b, sizeof b, "%ld", (long)until); store_set("pcBedExtUntil", b);
  fprintf(stderr, "[kids] bedtime tonight: %d min later\n", minutes);
}
