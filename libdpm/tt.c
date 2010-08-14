#include <stdio.h>

void count (void (*func) (int i));

#define dyn_paste(a,b) dyn_paste2(a,b)
#define dyn_paste2(a,b) a##b

#define dyn_foreach2(BODY,DECL,ITER,ARGS...) \
    auto void BODY (DECL);  \
    ITER (BODY, ## ARGS);      \
    void BODY (DECL)

#define dyn_foreach(DECL,ITER,ARGS...) \
    dyn_foreach2 (dyn_paste(__body__, __LINE__), DECL, ITER, ## ARGS)

int
main ()
{
  dyn_foreach (int i, count)
    {
      printf ("%d\n", i);
    }

  dyn_foreach (int i, count)
    {
      printf ("%d\n", i+1);
    }
}

void
count (void (*func) (int i))
{
  for (int i = 0; i < 5; i++)
    func (i);
}
