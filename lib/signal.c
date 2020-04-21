#include <signal.h>
#include <string.h>

void sig_restore(void);

sighandler_t
signal(int sig, sighandler_t func)
{
  struct sigaction act, oact;
  memset(&act, 0, sizeof(act));
  act.sa_handler = func;
  act.sa_restorer = sig_restore;
  if (sigaction(sig, &act, &oact) < 0)
    return SIG_ERR;
  else
    return oact.sa_handler;
}
