#ifndef XENITH_CONFIG_H
#define XENITH_CONFIG_H

/* Settings from uda:/xenith/config.txt (one key=value per line, # for comments):
 *   server=203.0.113.10:8795     XENITH Cloud server (IP:port, no DNS on the console)
 *   name=OWEN                    player name for leaderboards (A-Z, 0-9, max 12)
 *   key=...                      console key, lets this console read its inbox
 */
struct xenith_config {
    char server[32];
    char name[16];
    char key[72];
    int loaded;              /* 1 if the file was found */
};

extern struct xenith_config cfg;

void config_load(void);      /* safe to call more than once; needs fatInitDefault() first */

#define XENITH_CONFIG_PATH "uda:/xenith/config.txt"

#endif
