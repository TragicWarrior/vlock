#define _GNU_SOURCE
#include <stdio.h>

#include <signal.h>

#include <string.h>

#include "signals.h"
#include "util.h"

static const char *termination_blurb =
  "\n"
  "*******************************************************************************\n"
  "*** vlock caught a fatal signal and will now terminate.  The reason for     ***\n"
  "*** this is very likely an error in the program.  Please notify the author  ***\n"
  "*** about this problem by sending an email to the address below.  Include   ***\n"
  "*** all messages leading up to this one and as much information as possible ***\n"
  "*** about your system and configuration.                                    ***\n"
  "*** Please include the word \"vlock\" in the subject of your email.  Sorry    ***\n"
  "*** for any inconvenience.                                                  ***\n"
  "***                                                                         ***\n"
  "*** Frank Benkstein <frank-vlock@benkstein.net>                             ***\n"
  "*******************************************************************************\n"
  "\n"
;

static void terminate(int signum)
{
  vlock_invoke_atexit();

  fprintf(stderr, "vlock: Killed by signal %d (%s)!\n", signum,
          strsignal(signum));

  if (signum != SIGTERM)
    fputs(termination_blurb, stderr);

  raise(signum);
}

void install_signal_handlers(void)
{
  struct sigaction sa;

  /* Ignore some signals. */
  (void) sigemptyset(&(sa.sa_mask));
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = SIG_IGN;
  (void) sigaction(SIGTSTP, &sa, NULL);

  /* Handle termination signals.  None of these should be delivered in a normal
   * run of the program because terminal signals (INT, QUIT) are disabled
   * below. */
  sa.sa_flags = SA_RESETHAND;
  sa.sa_handler = terminate;
  (void) sigaction(SIGINT, &sa, NULL);
  (void) sigaction(SIGQUIT, &sa, NULL);
  (void) sigaction(SIGTERM, &sa, NULL);
  (void) sigaction(SIGHUP, &sa, NULL);
  (void) sigaction(SIGABRT, &sa, NULL);
  (void) sigaction(SIGSEGV, &sa, NULL);
}

