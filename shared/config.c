#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

struct xenith_config cfg = {"", "PLAYER", "", 0};

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
}

void config_load(void)
{
    char line[128];
    FILE *f = fopen(XENITH_CONFIG_PATH, "r");
    if (!f) return;
    cfg.loaded = 1;

    while (fgets(line, sizeof line, f)) {
        char *eq, *key = line, *val;
        while (isspace((unsigned char)*key)) key++;
        if (*key == '#' || !*key) continue;
        eq = strchr(key, '=');
        if (!eq) continue;
        *eq = 0;
        val = eq + 1;
        trim(key);
        trim(val);
        while (isspace((unsigned char)*val)) val++;

        if (!strcmp(key, "server")) {
            snprintf(cfg.server, sizeof cfg.server, "%s", val);
        } else if (!strcmp(key, "key")) {
            int o = 0;
            for (; *val && o < (int)sizeof cfg.key - 1; val++)
                if (isalnum((unsigned char)*val)) cfg.key[o++] = *val;
            cfg.key[o] = 0;
        } else if (!strcmp(key, "name")) {
            int o = 0;
            for (; *val && o < 12; val++)
                if (isalnum((unsigned char)*val)) cfg.name[o++] = toupper((unsigned char)*val);
            if (o) cfg.name[o] = 0;
        }
    }
    fclose(f);
}
