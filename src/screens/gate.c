#include "screens/gate.h"
#include "core/profile.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "platform/plat.h"
#include <stdio.h>
#include <string.h>

enum { M_PICK, M_UNLOCK, M_CREATE, M_CONFIRM, M_OLD };
static struct { int mode, pick, pad, n, wrong, fresh; char pin[5], first[5]; const char *msg; Uint32 lockUntil, msgAt; } G;

/* the pad: 1 2 3 / 4 5 6 / 7 8 9 / delete 0 */
#define PAD_N 11
static const char *pad_label(int i) { static const char *L[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "\xe2\x8c\xab", "0" }; return L[i]; }
static void pin_clear(void) { G.n = 0; G.pin[0] = 0; }
static void say(const char *m) { G.msg = m; G.msgAt = SDL_GetTicks(); }

/* a digit (or -1: delete). Returns 1 when four digits made a PIN that the mode accepted and the mode is over */
static int pin_key(int d) {
  if (G.lockUntil && SDL_GetTicks() < G.lockUntil) return 0;
  G.lockUntil = 0;
  if (d < 0) { if (G.n) G.pin[--G.n] = 0; return 0; }
  if (G.n < 4) { G.pin[G.n++] = (char)('0' + d); G.pin[G.n] = 0; }
  if (G.n < 4) return 0;
  if (G.mode == M_UNLOCK || G.mode == M_OLD) {
    if (profile_pin_ok(G.pin)) { G.wrong = 0; pin_clear(); G.msg = NULL; if (G.mode == M_OLD) { G.mode = M_CREATE; return 0; } return 1; }
    pin_clear();
    if (++G.wrong >= 3) { G.wrong = 0; G.lockUntil = SDL_GetTicks() + 30000; say("Too many tries. Wait half a minute"); fprintf(stderr, "[profile] three wrong PINs: locked for 30 s\n"); }
    else say("That\xe2\x80\x99s not the PIN");
    return 0;
  }
  if (G.mode == M_CREATE) { memcpy(G.first, G.pin, 5); pin_clear(); G.mode = M_CONFIRM; G.msg = NULL; return 0; }
  /* M_CONFIRM */
  if (!strcmp(G.first, G.pin)) { profile_pin_set(G.pin); pin_clear(); G.msg = NULL; return 1; }
  pin_clear(); G.mode = M_CREATE; say("The two didn\xe2\x80\x99t match. Try again");
  return 0;
}

static void draw_tile(float x, float y, float s, const char *letter, const char *name, const char *note, int on, uint32_t tint) {
  gfx_rrect(x, y, s, s, 36, on ? tint : WHITE_A(16));
  if (on) gfx_ring_out(x, y, s, s, 36, 5, WHITE_A(255)); else gfx_ring(x, y, s, s, 36, 2, WHITE_A(30));
  text_draw(letter, x + s / 2, y + s / 2 - text_line_height(150) / 2.0f, 150, 700, WHITE_A(on ? 255 : 150), 0, 0, ALIGN_CENTER);
  text_draw(name, x + s / 2, y + s + 26, 40, 700, WHITE_A(on ? 255 : 170), 0, s + 80, ALIGN_CENTER);
  if (note) text_draw(note, x + s / 2, y + s + 26 + text_line_height(40) + 4, 24, 400, WHITE_A(130), 0, s + 120, ALIGN_CENTER);
}
static void draw(void) {
  float cx = SB_W / 2;
  if (G.mode == M_PICK) {
    text_draw(T("Who\xe2\x80\x99s watching?"), cx, 170, 64, 700, WHITE_A(255), -0.5f, 0, ALIGN_CENTER);
    float s = 330, gap = 110, x0 = cx - s - gap / 2, y = 360;
    draw_tile(x0, y, s, "A", T("Adult"), T("PIN needed"), G.pick == 0, RGB(74, 62, 140));
    draw_tile(x0 + s + gap, y, s, "K", T("Kids"), NULL, G.pick == 1, RGB(36, 122, 88));
    text_draw(T("Left / Right to choose, OK to open. Back closes the app"), cx, 960, 24, 400, WHITE_A(120), 0, 0, ALIGN_CENTER);
    return;
  }
  const char *title = G.mode == M_UNLOCK ? T("Enter the PIN") : G.mode == M_OLD ? T("Enter the current PIN") : G.mode == M_CREATE ? T("Choose a parental PIN") : T("Enter it again");
  const char *sub = G.mode == M_CREATE ? T("Four digits. It opens the Adult profile and the parental controls") : G.mode == M_UNLOCK ? T("The Adult profile is locked") : "";
  text_draw(title, cx, 110, 56, 700, WHITE_A(255), -0.5f, 0, ALIGN_CENTER);
  if (sub[0]) text_draw(sub, cx, 110 + text_line_height(56) + 6, 26, 400, WHITE_A(150), 0, 0, ALIGN_CENTER);
  { float d = 30, g = 34, w = 4 * d + 3 * g, x = cx - w / 2, y = 290; /* the four places */
    for (int i = 0; i < 4; i++) { if (i < G.n) gfx_rrect(x + i * (d + g), y, d, d, d / 2, WHITE_A(255)); else gfx_ring(x + i * (d + g), y, d, d, d / 2, 3, WHITE_A(90)); } }
  int locked = G.lockUntil && SDL_GetTicks() < G.lockUntil;
  if (G.msg && (locked || SDL_GetTicks() - G.msgAt < 4000)) text_draw(T(G.msg), cx, 350, 26, 500, RGB(255, 170, 120), 0, 0, ALIGN_CENTER);
  { float b = 118, g = 22, w = 3 * b + 2 * g, x0 = cx - w / 2, y0 = 420;
    for (int i = 0; i < PAD_N; i++) { int c = i == 10 ? 1 : i % 3, r = i == 10 ? 3 : i / 3; float x = x0 + c * (b + g), y = y0 + r * (b + g); int on = G.pad == i;
      gfx_rrect(x, y, b, b, 28, on ? WHITE_A(locked ? 60 : 235) : WHITE_A(18));
      if (!on) gfx_ring(x, y, b, b, 28, 2, WHITE_A(28));
      text_draw(pad_label(i), x + b / 2, y + b / 2 - text_line_height(48) / 2.0f, 48, 600, on ? RGB(12, 11, 17) : WHITE_A(locked ? 80 : 230), 0, 0, ALIGN_CENTER); } }
  text_draw(T("Arrows and OK, or the number keys. Back goes back"), cx, 1000, 24, 400, WHITE_A(120), 0, 0, ALIGN_CENTER);
}

/* the pad's focus: 0-8 the digits in rows of three, 9 delete (under 7), 10 zero (under 8) */
static void pad_move(int k) {
  int i = G.pad, c = i == 10 ? 1 : i % 3, r = i == 10 ? 3 : i / 3;
  if (k == K_LEFT && c > 0) c--; else if (k == K_RIGHT && c < 2) c++; else if (k == K_UP && r > 0) r--; else if (k == K_DOWN && r < 3) r++;
  if (r == 3) { if (c > 1) c = 1; G.pad = c == 0 ? 9 : 10; } else G.pad = r * 3 + c;
}
static int digit_of(const SDL_Keysym *ks) {
  if (ks->sym >= SDLK_0 && ks->sym <= SDLK_9) return (int)(ks->sym - SDLK_0);
  if (ks->sym >= SDLK_KP_1 && ks->sym <= SDLK_KP_9) return (int)(ks->sym - SDLK_KP_1) + 1;
  if (ks->sym == SDLK_KP_0) return 0;
  return -1;
}
/* the loop: 0 Adult (or the PIN given / changed), 1 Kids, -1 close the app, -2 Back out of a PIN change */
static int run(SDL_Window *win, int mode, int pickFirst) {
  memset(&G, 0, sizeof G); G.mode = mode; G.pad = 4;
  int startMode = mode;
  for (;;) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) return -1;
      if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) { int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh); gfx_resize(dw, dh); }
      if (e.type != SDL_KEYDOWN) continue;
      int k = plat_map_key(&e.key), d = digit_of(&e.key.keysym);
      if (G.mode == M_PICK) {
        if (k == K_LEFT) G.pick = 0; else if (k == K_RIGHT) G.pick = 1;
        else if (k == K_BACK || k == K_EXIT) return -1;
        else if (k == K_OK) { if (G.pick == 1) return 1; if (G.fresh) return 0; G.mode = M_UNLOCK; G.pad = 4; pin_clear(); G.msg = NULL; }
        continue;
      }
      int done = 0;
      if (d >= 0) done = pin_key(d);
      else if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN) pad_move(k);
      else if (k == K_OK) done = pin_key(G.pad == 9 ? -1 : G.pad == 10 ? 0 : G.pad + 1);
      else if (k == K_BACK || k == K_EXIT) {
        if (G.n) pin_clear();
        else if (G.mode == M_CONFIRM) { G.mode = M_CREATE; G.msg = NULL; }
        else if (G.mode == M_UNLOCK && pickFirst) { G.mode = M_PICK; G.msg = NULL; }
        else if (startMode == M_OLD) return -2;
        else if (G.mode == M_UNLOCK || k == K_EXIT) return -1;
        else if (G.mode == M_CREATE && startMode == M_CREATE) return -1; /* no PIN yet and none wanted: the app closes */
      }
      if (done) {
        if (startMode == M_CREATE && pickFirst) { G.mode = M_PICK; G.pick = 0; G.fresh = 1; startMode = M_PICK; continue; } /* the PIN is made: now who is watching */
        return 0;
      }
    }
    text_frame_begin();
    gfx_begin(PAGE);
    draw();
    gfx_end();
    SDL_GL_SwapWindow(win);
    SDL_Delay(16);
  }
}
int gate_start(SDL_Window *win) { return run(win, profile_has_pin() ? M_PICK : M_CREATE, 1); }
int gate_relock(SDL_Window *win) { int r = run(win, M_UNLOCK, 0); return r == 0 ? 1 : -1; }
void gate_change_pin(SDL_Window *win) { run(win, M_OLD, 0); }
