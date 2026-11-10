#include "core/profile.h"
#include "cJSON.h"
#include "sbcrypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>

static char base[1024], kidsDir[1100], file[1100];
static cJSON *P;
static int kids;
static volatile int relock, asked;
static Uint32 awayAt;

static void save(void) {
  char *s = cJSON_PrintUnformatted(P); if (!s) return;
  char tmp[1200]; snprintf(tmp, sizeof tmp, "%s.tmp", file);
  FILE *f = fopen(tmp, "wb"); if (f) { size_t n = strlen(s), w = fwrite(s, 1, n, f); fclose(f); if (w == n) { chmod(tmp, 0600); rename(tmp, file); } else remove(tmp); }
  free(s);
}
void profile_init(const char *baseDir) {
  snprintf(base, sizeof base, "%s", baseDir); snprintf(kidsDir, sizeof kidsDir, "%s/kids", base); snprintf(file, sizeof file, "%s/profiles.json", base);
  FILE *f = fopen(file, "rb");
  if (f) { char *b = malloc(65537); size_t n = fread(b, 1, 65536, f); fclose(f); b[n] = 0; P = cJSON_Parse(b); free(b); }
  if (!P) P = cJSON_CreateObject();
}
static const char *str(const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(P, k); return cJSON_IsString(v) ? v->valuestring : ""; }
static void hex(const unsigned char *b, int n, char *out) { for (int i = 0; i < n; i++) snprintf(out + i * 2, 3, "%02x", b[i]); }
static void pin_hash(const char *salt, const char *pin, char out[65]) {
  char in[128]; int n = snprintf(in, sizeof in, "sbpin1:%s:%s", salt, pin); unsigned char h[32];
  for (int i = 0; i < 2000; i++) { sbc_sha256(i ? (const void *)h : (const void *)in, i ? 32 : (size_t)n, h); } /* (slow enough that the four digits are not read off the file at a glance) */
  hex(h, 32, out);
}
int profile_has_pin(void) { return strlen(str("pinHash")) == 64 && str("pinSalt")[0]; }
int profile_pin_ok(const char *pin) {
  if (!profile_has_pin() || !pin || strlen(pin) != 4) return 0;
  char h[65]; pin_hash(str("pinSalt"), pin, h); const char *want = str("pinHash");
  unsigned d = 0; for (int i = 0; i < 64; i++) d |= (unsigned)(h[i] ^ want[i]); return d == 0;
}
void profile_pin_set(const char *pin) {
  unsigned char r[16] = { 0 }; FILE *f = fopen("/dev/urandom", "rb"); if (f) { if (fread(r, 1, 16, f) != 16) r[0] = (unsigned char)SDL_GetTicks(); fclose(f); }
  char salt[33], h[65]; hex(r, 16, salt); pin_hash(salt, pin, h);
  cJSON_DeleteItemFromObjectCaseSensitive(P, "pinSalt"); cJSON_DeleteItemFromObjectCaseSensitive(P, "pinHash");
  cJSON_AddStringToObject(P, "pinSalt", salt); cJSON_AddStringToObject(P, "pinHash", h); save();
  fprintf(stderr, "[profile] the PIN was set\n");
}
void profile_choose(int k) { kids = k ? 1 : 0; fprintf(stderr, "[profile] %s\n", kids ? "kids" : "adult"); }
int profile_is_kids(void) { return kids; }
const char *profile_dir(void) { if (!kids) return base; mkdir(kidsDir, 0755); return kidsDir; }

void profile_life(int l) {
  if (l < 0) { if (!awayAt) awayAt = SDL_GetTicks() | 1; }
  else if (l > 0) { Uint32 a = awayAt; awayAt = 0; if (a && !kids && profile_has_pin() && SDL_GetTicks() - a >= 30000) relock = 1; }
}
int profile_relock_due(void) { if (!relock) return 0; relock = 0; return 1; }
void profile_ask(int what) { asked = what; }
int profile_asked(void) { int a = asked; asked = 0; return a; }
