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
/* A transient line ("CodeJr is thinking"), erased by the next character. */
void scr_temp(const char *s, unsigned char attr);
int scr_col(void); /* transcript column on the bottom row */
/* Scrollback and paging: after a page of new lines since the user's last
 * line (or the last page), the view holds and new text waits in the
 * buffer. */
long scr_unread(void);       /* lines below the view */
int scr_following(void);     /* the view tracks the end */
void scr_page(void);         /* show the next page */
void scr_scroll(long delta); /* PgUp/PgDn: move the view */
void scr_mark_input(void);   /* the user sent a line: count from here */

void scr_status(const char *s);   /* replaces the status bar */
void scr_input(const char *s, int prompt); /* input line; "> " only if prompt */
void scr_spin(void); /* busy: advances the spinner where the prompt goes */
void scr_save(const char *path);  /* writes the screen's text to a file */

#endif
