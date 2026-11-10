/* Profiles: Adult (behind a 4-digit PIN, the app as it always was) and Kids (its own sign-in and saved data,
   with the parental controls). Chosen on the screen the app starts with (screens/gate.c). What is common to both
   (the PIN, the parental settings) is kept in profiles.json in the app's data folder; the Adult profile's own data
   stays where it always was, the Kids profile's is in the kids folder under it. */
#ifndef SB_PROFILE_H
#define SB_PROFILE_H

void profile_init(const char *baseDir);
int  profile_has_pin(void);
int  profile_pin_ok(const char *pin4);     /* 1: the PIN */
void profile_pin_set(const char *pin4);
void profile_choose(int kids);             /* at start, before the profile's data is opened */
int  profile_is_kids(void);
const char *profile_dir(void);             /* the chosen profile's data folder (made when missing) */

/* the app left the screen and came back (core/sync.c watches): the Adult profile asks for the PIN again
   when it was away for more than half a minute */
void profile_life(int l);
int  profile_relock_due(void);             /* 1 once, on the frame loop's side */
/* asked from Settings, done by the frame loop between two frames */
enum { PROFILE_ASK_NONE = 0, PROFILE_ASK_PIN, PROFILE_ASK_SWITCH };
void profile_ask(int what);
int  profile_asked(void);                  /* what was asked, once */
#endif
