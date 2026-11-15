#ifndef XENITH_AVATAR_H
#define XENITH_AVATAR_H

/* XenithOS avatars: a 2D character built from parts and drawn on the fly.
 *
 * An avatar is stored as a 10-character code: "1" (format version) followed by one
 * base-36 digit per part, in the order of the enum below. The website draws the same
 * code with an identical renderer (server/web/avatar.js), so keep the two in step.
 */
#include <stdint.h>

enum { AV_SKIN, AV_HAIR, AV_HAIR_COLOR, AV_EYES, AV_MOUTH, AV_TOP, AV_TOP_COLOR, AV_ACCESSORY, AV_BACKGROUND, AV_PARTS };

#define AVATAR_CODE_LEN 10
#define AVATAR_DEFAULT "1202001000"

extern const int avatar_count[AV_PARTS];
extern const char *const avatar_part_name[AV_PARTS];
const char *avatar_option_name(int part, int value);
int  avatar_valid(const char *code);
void avatar_parse(const char *code, int v[AV_PARTS]);         /* invalid parts fall back to defaults */
void avatar_encode(const int v[AV_PARTS], char out[AVATAR_CODE_LEN + 1]);
void avatar_random(int v[AV_PARTS], uint32_t seed);
uint32_t avatar_bg_color(const char *code);

void avatar_draw(const char *code, int x, int y, int size);
void avatar_draw_v(const int v[AV_PARTS], int x, int y, int size);

#endif
