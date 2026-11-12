#include "platform/plat.h"
#include "core/store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <SDL2/SDL.h>

static char dir[1024], path[1100];
static cJSON *root;
static int dirty; static Uint32 dirtyAt;

static char *read_all(const char *p, size_t *len) {
  FILE *f = fopen(p, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc(n + 1); size_t got = fread(b, 1, n, f); fclose(f); b[got] = 0; if (len) *len = got; return b;
}
static int write_all(const char *p, const void *data, size_t len) {
  static unsigned seq; char tmp[1240]; snprintf(tmp, sizeof tmp, "%s.%u.tmp", p, __sync_add_and_fetch(&seq, 1)); /* the writer thread and the exit flush never share one */
  FILE *f = fopen(tmp, "wb"); if (!f) return 0;
  size_t w = fwrite(data, 1, len, f); fclose(f);
  if (w != len) { remove(tmp); return 0; }
  return rename(tmp, p) == 0;
}
void store_init(const char *dataDir) {
  snprintf(dir, sizeof dir, "%s", dataDir); snprintf(path, sizeof path, "%s/state.json", dir);
  char *t = read_all(path, NULL); root = t ? cJSON_Parse(t) : NULL; free(t);
  if (!root) root = cJSON_CreateObject();
  char c[1100]; snprintf(c, sizeof c, "%s/cache", dir); mkdir(c, 0755);
}
const char *store_data_dir(void) { return dir; }
/* settings are read while drawing (every frame, in places): a hash index over the root, rebuilt after any change of
   its keys (the root is a list walked with strcmp, and the library and progress entries make it long) */
static cJSON **ix; static unsigned ixCap; static int ixStale = 1;
static unsigned hstr(const char *s) { unsigned h = 2166136261u; while (*s) h = (h ^ (unsigned char)*s++) * 16777619u; return h; }
static cJSON *find(const char *k) {
  if (!root || !k) return NULL;
  if (ixStale) { ixStale = 0; unsigned n = (unsigned)cJSON_GetArraySize(root), cap = 64; while (cap < n * 2) cap <<= 1;
    if (cap != ixCap) { free(ix); ix = malloc(cap * sizeof *ix); ixCap = cap; } memset(ix, 0, ixCap * sizeof *ix);
    cJSON *it; cJSON_ArrayForEach(it, root) if (it->string) { unsigned i = hstr(it->string) & (ixCap - 1); while (ix[i] && strcmp(ix[i]->string, it->string)) i = (i + 1) & (ixCap - 1); if (!ix[i]) ix[i] = it; } }
  unsigned i = hstr(k) & (ixCap - 1); cJSON *v;
  while ((v = ix[i]) && strcmp(v->string, k)) i = (i + 1) & (ixCap - 1);
  return v;
}
static unsigned serial = 1;
unsigned store_serial(void) { return serial; }
static void touch(void) { dirty = 1; dirtyAt = SDL_GetTicks(); ixStale = 1; serial++; }
const char *store_get(const char *k, const char *def) { cJSON *v = find(k); return cJSON_IsString(v) && v->valuestring ? v->valuestring : def; }
void store_set(const char *k, const char *val) {
  const char *cur = store_get(k, NULL); if (cur && val && !strcmp(cur, val)) return;
  cJSON_DeleteItemFromObjectCaseSensitive(root, k); if (val) cJSON_AddStringToObject(root, k, val); touch(); }
cJSON *store_node(const char *k) { return find(k); }
void store_set_node(const char *k, cJSON *n) { if (n && find(k) == n) { touch(); return; } /* the store's own node, changed in place */ cJSON_DeleteItemFromObjectCaseSensitive(root, k); if (n) cJSON_AddItemToObject(root, k, n); touch(); }
void store_remove(const char *k) { if (find(k)) { cJSON_DeleteItemFromObjectCaseSensitive(root, k); touch(); } }
/* cache files are written on a thread of their own: a disk write in the middle of a frame was a long frame on the TV
   (its flash can take tens of ms) */
#include <pthread.h>
typedef struct CW { char path[1200]; unsigned char *data; size_t len; struct CW *next; } CW;
static CW *cwHead, *cwTail; static pthread_mutex_t cwMu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cwCv = PTHREAD_COND_INITIALIZER; static int cwThread;
static void *cw_run(void *a) { plat_thread_background(15); (void)a;
  for (;;) { pthread_mutex_lock(&cwMu); while (!cwHead) pthread_cond_wait(&cwCv, &cwMu); CW *w = cwHead; cwHead = w->next; if (!cwHead) cwTail = NULL; pthread_mutex_unlock(&cwMu);
    write_all(w->path, w->data, w->len); free(w->data); free(w); }
  return NULL; }
static void write_later(const char *path, unsigned char *own, size_t len) { /* own: malloc'd, taken */
  CW *w = malloc(sizeof *w); snprintf(w->path, sizeof w->path, "%s", path); w->data = own; w->len = len; w->next = NULL;
  pthread_mutex_lock(&cwMu);
  if (!cwThread) { pthread_t t; if (pthread_create(&t, NULL, cw_run, NULL) == 0) { pthread_detach(t); cwThread = 1; } }
  if (!cwThread) { pthread_mutex_unlock(&cwMu); write_all(w->path, w->data, w->len); free(w->data); free(w); return; }
  if (cwTail) cwTail->next = w; else cwHead = w; cwTail = w; pthread_cond_signal(&cwCv); pthread_mutex_unlock(&cwMu);
}
static void cache_path(const char *key, char *out, size_t cap);
void cache_write(const char *key, const unsigned char *data, size_t len) { char p[1200]; cache_path(key, p, sizeof p); unsigned char *c = malloc(len ? len : 1); memcpy(c, data, len); write_later(p, c, len); }
void store_flush(void) {
  if (!dirty) return; dirty = 0;
  char *t = cJSON_PrintUnformatted(root); if (!t) return;
  if (!write_all(path, t, strlen(t))) fprintf(stderr, "[store] could not write %s\n", path);
  free(t);
}
/* while running, the state is printed here and written by the writer thread (the file is a few hundred KB with a
   library); store_flush() at exit writes it at once */
void store_pump(void) {
  if (!dirty || SDL_GetTicks() - dirtyAt <= 1500) return; dirty = 0;
  char *t = cJSON_PrintUnformatted(root); if (t) write_later(path, (unsigned char *)t, strlen(t));
}

/* ---- file cache ---------------------------------------------------------------------------- */
/* the cache's folder: <data>/cache on the TV, or one on a USB drive, spread over 256 folders there (a FAT32 folder
   holds about 65,000 files). Swapped by pointer so the worker threads never read a half-written name */
static char usbBuf[2][1100]; static const char *volatile usbRoot; static int usbIx;
int cache_on_usb(void) { return usbRoot != NULL; }
static void cache_path(const char *key, char *out, size_t cap) {
  unsigned long long h1 = 1469598103934665603ULL; unsigned h2 = 2166136261u; for (const unsigned char *p = (const unsigned char *)key; *p; p++) { h1 = (h1 ^ *p) * 1099511628211ULL; h2 = (h2 ^ *p) * 16777619u; }
  const char *u = usbRoot;
  if (u) snprintf(out, cap, "%s/%02x/%016llx%08x", u, h2 & 0xff, h1, h2);
  else snprintf(out, cap, "%s/cache/%016llx%08x", dir, h1, h2);
}
unsigned char *cache_read(const char *key, long maxAge, size_t *len) {
  char p[1200]; cache_path(key, p, sizeof p);
  struct stat st; if (stat(p, &st)) return NULL;
  if (maxAge > 0 && time(NULL) - st.st_mtime > maxAge) return NULL;
  unsigned char *b = (unsigned char *)read_all(p, len);
  if (b) { struct timespec ts[2] = { { 0, UTIME_NOW }, { 0, UTIME_OMIT } }; utimensat(AT_FDCWD, p, ts, 0); } /* last used: the access time (the change time says how fresh it is) */
  return b;
}
/* the cache folder is kept under a cap, the least recently used files going first (the TV's app partition has
   little room: a full one breaks installs). Run once at start, on a thread of its own */
typedef struct { char name[40]; time_t at; long size; } CEnt;
static int cent_cmp(const void *a, const void *b) { time_t x = ((const CEnt *)a)->at, y = ((const CEnt *)b)->at; return x < y ? -1 : x > y; }
static long capBytes, keptBytes = -1;
long cache_cap_bytes(void) { return capBytes; }
long cache_kept_bytes(void) { return __atomic_load_n(&keptBytes, __ATOMIC_RELAXED); }
static void *prune_run(void *a) { (void)a; plat_thread_background(15);
  const char *u = usbRoot; char d[1100]; if (u) snprintf(d, sizeof d, "%s", u); else snprintf(d, sizeof d, "%s/cache", dir);
  int n = 0, cap = 4096; CEnt *e = malloc(cap * sizeof *e); long total = 0; struct dirent *de;
  for (int sh = u ? 0 : -1; sh < (u ? 256 : 0); sh++) { /* the one folder on the TV, the 256 on a drive */
    char sd[1200]; if (sh < 0) snprintf(sd, sizeof sd, "%s", d); else snprintf(sd, sizeof sd, "%s/%02x", d, sh);
    DIR *dd = opendir(sd); if (!dd) continue;
    while ((de = readdir(dd))) { if (de->d_name[0] == '.' || strlen(de->d_name) >= sizeof e[0].name - 3) continue; char p[1300]; snprintf(p, sizeof p, "%s/%s", sd, de->d_name); struct stat st; if (stat(p, &st) || !S_ISREG(st.st_mode)) continue;
      if (n == cap) { cap *= 2; e = realloc(e, cap * sizeof *e); }
      if (sh < 0) snprintf(e[n].name, sizeof e[n].name, "%s", de->d_name); else snprintf(e[n].name, sizeof e[n].name, "%02x/%s", sh, de->d_name);
      e[n].at = st.st_atime > st.st_mtime ? st.st_atime : st.st_mtime; e[n].size = st.st_size; total += st.st_size; n++; }
    closedir(dd); }
  if (total > capBytes) { qsort(e, n, sizeof *e, cent_cmp); long goal = capBytes * 3 / 4; int del = 0;
    for (int i = 0; i < n && total > goal; i++) { char p[1200]; snprintf(p, sizeof p, "%s/%s", d, e[i].name); if (!remove(p)) { total -= e[i].size; del++; } }
    fprintf(stderr, "[cache] %d files removed, %ld KB kept\n", del, total / 1024); }
  __atomic_store_n(&keptBytes, total, __ATOMIC_RELAXED);
  if (u) { char st[1200]; snprintf(st, sizeof st, "%s/.pruned", d); FILE *f = fopen(st, "wb"); if (f) fclose(f); } /* when the drive was last tidied */
  free(e); return NULL;
}
void cache_prune_start(int capMb) { capBytes = (long)capMb * 1024 * 1024; pthread_t t; if (!pthread_create(&t, NULL, prune_run, NULL)) pthread_detach(t); }
/* the cache moves to a folder on a USB drive (root), or back to the TV (NULL); capMb: the size kept there. 0: the drive
   can't be written */
int cache_use_usb(const char *root, int capMb) {
  if (!root) { if (usbRoot) { usbRoot = NULL; cache_prune_start(capMb); } return 1; }
  if (usbRoot && !strcmp(usbRoot, root)) return 1;
  mkdir(root, 0755); char t[1200]; snprintf(t, sizeof t, "%s/.write-test", root);
  FILE *f = fopen(t, "wb"); if (!f) return 0; int ok = fputs("ok", f) >= 0; if (fclose(f)) ok = 0; remove(t); if (!ok) return 0;
  for (int i = 0; i < 256; i++) { snprintf(t, sizeof t, "%s/%02x", root, i); mkdir(t, 0755); }
  usbIx ^= 1; snprintf(usbBuf[usbIx], sizeof usbBuf[0], "%s", root); usbRoot = usbBuf[usbIx];
  /* tidied at most once a day: on a full drive that is tens of thousands of files to look at, and it would compete with
     the pictures loading after every start (and every profile switch) */
  snprintf(t, sizeof t, "%s/.pruned", root); struct stat sp;
  if (!stat(t, &sp) && time(NULL) - sp.st_mtime < 86400) { capBytes = (long)capMb * 1024 * 1024; fprintf(stderr, "[cache] the USB drive was tidied in the last day\n"); return 1; }
  cache_prune_start(capMb); return 1;
}
int cache_file(const char *key, long maxAge, char *p, size_t cap) {
  cache_path(key, p, cap); struct stat st; if (stat(p, &st)) return 0;
  return !(maxAge > 0 && time(NULL) - st.st_mtime > maxAge);
}


/* another profile's data, before anything of the first was handed out (screens/gate.c chose Kids) */
void store_switch(const char *dataDir) {
  store_flush(); if (root) cJSON_Delete(root); root = NULL; ixStale = 1; serial++; dirty = 0;
  mkdir(dataDir, 0755); store_init(dataDir);
}
