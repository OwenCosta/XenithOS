#ifndef XENITH_AVATAR_H
#define XENITH_AVATAR_H

/* XenithOS avatars: a 2D character built from parts and drawn on the fly.
 *
 * An avatar is stored as a short code: a format digit, then one base-36 digit per part in
 * the order of the enum below.
 *   "2" + 13 digits   the current format (all the parts below)
 *   "1" + 9 digits    the first format (the first nine parts); still read, and drawn the same
 * The website draws the same codes with an identical renderer (server/web/static/avatar.js),
 * and the server checks them (valid_avatar in server/xenith_cloud.py): keep all three in step.
 */
#include <stdint.h>

enum {
    AV_SKIN, AV_HAIR, AV_HAIR_COLOR, AV_EYES, AV_MOUTH, AV_TOP, AV_TOP_COLOR, AV_ACCESSORY, AV_BACKGROUND,
    AV_EYE_COLOR, AV_BROWS, AV_FACIAL_HAIR, AV_CHEEKS,
    AV_PARTS
};
#define AV_PARTS_V1 9

#define AVATAR_CODE_LEN 14                 /* the longest code (format 2) */
#define AVATAR_DEFAULT "22020010000000"

extern const int avatar_count[AV_PARTS];
extern const char *const avatar_part_name[AV_PARTS];
const char *avatar_option_name(int part, int value);
uint32_t avatar_option_color(int part, int value);   /* the swatch for colour parts, else 0 */
int  avatar_part_is_color(int part);
int  avatar_valid(const char *code);
void avatar_parse(const char *code, int v[AV_PARTS]);         /* invalid parts fall back to defaults */
void avatar_encode(const int v[AV_PARTS], char out[AVATAR_CODE_LEN + 1]);
void avatar_random(int v[AV_PARTS], uint32_t seed);
uint32_t avatar_bg_color(const char *code);

void avatar_draw(const char *code, int x, int y, int size);
void avatar_draw_v(const int v[AV_PARTS], int x, int y, int size);

#endif
