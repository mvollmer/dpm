#include <stdio.h>

void foo ();
void cleanup ();

void dyn_begin ();
void dyn_end ();

int
main ()
{
  for (int i __attribute__ ((cleanup (dyn_end))) = (dyn_begin(), 1); i; i=0)
    {
      // foo ();
    }

  return 0;
}
