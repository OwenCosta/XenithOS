#ifndef XENITH_KEYBOARD_H
#define XENITH_KEYBOARD_H

/* The on-screen keyboard. On PCs, typing on a real keyboard works in it too.
 * Returns 1 when the player picks Done (buf holds the text), 0 if they back out. */
#define KB_TEXT 0
#define KB_PIN  1
#define KB_LINE 2     /* any printable text, with a row of symbols: commands, messages */
int keyboard(const char *title, const char *hint, char *buf, int maxlen, int mode);

#endif
