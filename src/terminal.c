#include <unistd.h>
#include <termios.h>

#include "terminal.h"

static struct termios term;
static tcflag_t lflag;

void secure_terminal(void)
{
  /* Disable terminal echoing and signals. */
  (void) tcgetattr(STDIN_FILENO, &term);
  lflag = term.c_lflag;
  term.c_lflag &= ~(ECHO | ISIG);
  (void) tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void restore_terminal(void)
{
  /* Restore the terminal. */
  term.c_lflag = lflag;
  (void) tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

