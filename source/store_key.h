#ifndef XENITH_STORE_KEY_H
#define XENITH_STORE_KEY_H

/* XenithOS Cloud's Store signing keys: the public halves, 64 hex digits each (source/store_sig.c).
 *
 * With a key here, the Store only opens with lists signed by it, and every download is checked
 * against the SHA-256 in that list. With none, nothing is checked: for a XenithOS Cloud of your
 * own that has no key yet. `python3 server/xenith_cloud.py store-key new <file>` makes a key and
 * prints the line for here; its private half goes to XenithOS Cloud (XENITH_STORE_KEY), never in
 * this repository. Two keys here (old and new) let consoles carry on through a change of key. */
static const char *const store_keys[] = {
    "6ac2e8d5bd25ccc7478843b2270d572ab7490ed9c9ab2bbdecda2e50a171026b",   /* XenithOS Cloud, 2026-11-17 */
    NULL
};

#endif
