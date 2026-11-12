/* The profile screen the app starts with, and the PIN pad. They run between two frames of the app (a small loop
   of their own): nothing of the app is drawn or ticked under them. */
#ifndef SB_GATE_H
#define SB_GATE_H
#include <SDL2/SDL.h>
int gate_start(SDL_Window *win);      /* who is watching: 0 Adult (its PIN given), 1 Kids, -1 the app closes */
int gate_relock(SDL_Window *win);     /* the Adult profile, back after a while away: 1 the PIN was given, -1 the app closes */
void gate_change_pin(SDL_Window *win); /* Settings: the current PIN, then a new one twice */
int gate_verify(SDL_Window *win);     /* Settings: the PIN for the Parental controls: 1 given, 0 Back */
#endif
