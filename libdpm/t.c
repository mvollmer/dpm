#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define dyn_foreach_iter(NAME, ITER, ARGS...)				\
  for (ITER NAME __attribute__ ((cleanup (ITER##_fini)))		\
	 = (ITER##_init (&NAME, ## ARGS), NAME);			\
       !ITER##_done (&NAME);						\
       ITER##_step (&NAME))

#define DYN_DECLARE_STRUCT_ITER(ITER, INIT_ARGS...)		\
  typedef struct ITER ITER;					\
  void ITER##_init (ITER *, ##INIT_ARGS);			\
  void ITER##_fini (ITER *);					\
  void ITER##_step (ITER *);					\
  bool ITER##_done (ITER *);					\
  struct ITER

#define dyn_foreach_iter2(NAME, ITER, ARGS...)				\
  for (ITER NAME __attribute__ ((cleanup (ITER##_fini)))		\
	 = (ITER##_init (&NAME, ## ARGS), NAME);			\
       ITER##_step (&NAME);)

#define DYN_DECLARE_STRUCT_ITER2(ITER, INIT_ARGS...)		\
  typedef struct ITER ITER;					\
  void ITER##_init (ITER *, ##INIT_ARGS);			\
  void ITER##_fini (ITER *);					\
  bool ITER##_step (ITER *);					\
  struct ITER

DYN_DECLARE_STRUCT_ITER2 (count, int n)
{
  int n;
  int i;
};

void
count_init (count *iter, int n)
{
  iter->n = n;
  iter->i = -1;
}

void
count_fini (count *iter)
{
}

bool
count_step (count *iter)
{
  return ++(iter->i) < iter->n;
}

DYN_DECLARE_STRUCT_ITER2 (subcount, int n, int m)
{
  count outer;
  int m;

  int i, j;
};

void
subcount_init (subcount *iter, int n, int m)
{
  count_init (&iter->outer, n);
  iter->m = m;
  iter->j = -1;
}

void
subcount_fini (subcount *iter)
{
  count_fini (&iter->outer);
}

bool
subcount_step (subcount *iter)
{
  while (++iter->j >= iter->m)
    {
      if (!count_step (&iter->outer))
	return false;
      iter->i = iter->outer.i;
      iter->j = -1;
    }
  return true;
}

int
main ()
{
  dyn_foreach_iter2 (c, subcount, 10, 10)
    printf ("%d %d\n", c.i, c.j);
}

