#ifndef XENITH_KEYBOARD_H
#define XENITH_KEYBOARD_H

/* The on-screen keyboard. On PCs, typing on a real keyboard works in it too.
 * Returns 1 when the player picks Done (buf holds the text), 0 if they back out.
 * In a desktop app (ui_desktop) it's a text box over the screen instead, typed into with the
 * real keyboard, with OK and Cancel (Enter and Esc); buf is as it was after Cancel. */
#define KB_TEXT 0
#define KB_PIN  1
#define KB_LINE 2     /* any printable text, with a row of symbols: commands, messages */
int keyboard(const char *title, const char *hint, char *buf, int maxlen, int mode);

/* A one-line text field for desktop apps, the one that box uses: the real keyboard types at the
 * caret, which the arrows, Home, End and a click move; Backspace and Delete; Ctrl+V pastes and
 * Ctrl+A selects all of it. With all of it selected (all = 1, as the box opens), what's typed
 * replaces it. While it's in use, typing is on (plat_text_input(1)) and so is ui_keys_on.
 *   kb_field_input()  this frame's typing, after ui_poll(): 1 when Enter was pressed
 *   kb_field_click()  a click in its box (x, y, w, h) puts the caret there: 1 when it did
 *   kb_field_draw()   in that box, in font, with the placeholder while it's empty */
struct kb_field { char *buf; int max, mode, caret, first, all; };
void kb_field_init(struct kb_field *f, char *buf, int max, int mode);
int  kb_field_input(struct kb_field *f);
int  kb_field_click(struct kb_field *f, int x, int y, int w, int h, int font);
void kb_field_draw(struct kb_field *f, int x, int y, int w, int h, int font, const char *placeholder, int focused);

#endif
