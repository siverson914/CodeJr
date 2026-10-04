/* CodeJr's screen: 80x25 text drawn straight into video memory.
 *
 *   row 0      header
 *   rows 1-21  transcript (scrolls)
 *   row 22     divider
 *   row 23     input line
 *   row 24     status bar
 */
#ifndef SCREEN_H
#define SCREEN_H

void scr_init(const char *title);
void scr_done(void);

/* Transcript: appends one character in a color attribute. '\n' starts a
 * new line; long lines wrap. */
void scr_putc(unsigned char c, unsigned char attr);
void scr_puts(const char *s, unsigned char attr);
void scr_clear_transcript(void);

void scr_status(const char *s);   /* replaces the status bar */
void scr_input(const char *s);    /* redraws the input line; cursor at end */
void scr_save(const char *path);  /* writes the screen's text to a file */

#endif
