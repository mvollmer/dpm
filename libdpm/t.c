#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define dyn_eat_parens(ARGS...) ARGS

#define DYN_DECLARE_ITER(TYPE, ITER, ELTS, INIT_ARGS...)	\
  typedef TYPE ITER##_type; \
  typedef struct ITER dyn_eat_parens ELTS ITER;	\
  void ITER##_init (ITER *, ##INIT_ARGS);	\
  void ITER##_fini (ITER *);	\
  void ITER##_step (ITER *);	\
  bool ITER##_not_done (ITER *);	\
  TYPE ITER##_elt (ITER *);

#define dyn_paste(a,b) dyn_paste__aux(a,b)
#define dyn_paste__aux(a,b) a##b

#define dyn_foreach_iter(NAME, ITER, ARGS...) \
  for (ITER NAME __attribute__ ((cleanup (ITER##_fini))) = ITER##_init (&NAME, ARGS), NAME;  \
       ITER##_not_done (&NAME); \
       ITER##_step (&NAME))

#define dyn_foreach(VAR, ITER, ARGS...) \
  for (bool __c = true; __c;) \
    for (ITER##_type VAR; __c; __c = false) \
      for (ITER __i __attribute__ ((cleanup (ITER##_fini))) \
	     = (ITER##_init (&__i, ARGS), VAR = ITER##_elt (&__i), __i); \
           ITER##_not_done (&__i); \
	   ITER##_step (&__i), VAR = ITER##_elt (&__i))

DYN_DECLARE_ITER (int, count, ({ int i, n; }), int n);

void
count_init (count *iter, int n)
{
  iter->n = n;
  iter->i = 0;
}

void
count_fini (count *iter)
{
  printf ("fini: %d %d\n", iter->i, iter->n);
}

void
count_step (count *iter)
{
  iter->i++;
}

bool
count_not_done (count *iter)
{
  return iter->i < iter->n;
}

int
count_elt (count *iter)
{
  return iter->i;
}

int
main (int argc, char **argv)
{
  dyn_foreach (i, count, atoi (argv[1]))
    {
      if (i % 3 == 0)
	continue;

      if (i > 5)
	break;
      printf ("%d\n", i);
    }

  return 0;
}
