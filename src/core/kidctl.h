/* Parental controls for the Kids profile. The rules are set in the Adult profile (Settings > Parental controls,
   and "Kids" in a title's menu) and kept with the Adult profile's settings; the app reads them as it starts,
   before a profile is chosen. In the Kids profile a title is shown and played only when
     - the parent allowed it, or
     - it came from a list addon installed on the Kids account (when lists are on), or
     - its age rating is at or under the limit (a title with no known rating is not shown),
   and never when the parent blocked it. Age ratings come from IMDb (the UK's, then the US's), kept on disk. */
#ifndef SB_KIDCTL_H
#define SB_KIDCTL_H
#include <stddef.h>
void kidctl_capture(const char *baseDir); /* at start, the Adult profile's settings open */
int  kidctl_ok(const char *id);           /* Kids: may this title (or an episode of it) be shown and played; always 1 in Adult */
void kidctl_listed(const char *id);       /* Kids: seen in a list addon's catalogue */
int  kidctl_lists_on(void);
int  kidctl_no_torrents(void);            /* Kids: torrent streams are left out */
int  kidctl_bedtime(char *until, size_t cap); /* 1: it is bedtime now (until: "07:00") */
void kidctl_tick(void);                   /* once a frame: the rating lookups */
/* the Adult profile: a title's mark */
enum { KID_RULE = 0, KID_ALLOWED = 1, KID_BLOCKED = 2 };
int  kidctl_mark(const char *id);
void kidctl_set_mark(const char *id, int mark);
#endif
