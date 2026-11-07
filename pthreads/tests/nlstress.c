/* newlib's malloc(), realloc(), free() and stdio from several pthreads at
   once, at different priorities so that preemption lands inside them.
   Each thread keeps blocks with a pattern of its own and checks it before
   a block is resized or freed, writes numbered lines with a %f to a shared
   FILE, and now and then opens and closes a FILE of its own.  main then
   checks that every line is whole and that each thread's are all there,
   in order.  Only main prints.  Usage: nlstress [iterations] [file]
   Distributed under the pthread library's zlib license.  */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/execbase.h>
#include <proto/exec.h>

#define THREADS 4
#define LIVE 32

extern struct ExecBase *SysBase;

static FILE *shared;
static int iterations = 2000;
static const char *path = "nlstress.txt";
static const char *tmpname[THREADS] = {
  "nlstress0.tmp", "nlstress1.tmp", "nlstress2.tmp", "nlstress3.tmp"
};

struct worker
{
  int id;
  int pri;
  int bad;			/* pattern or stdio errors seen */
};

static unsigned long next_random(unsigned long *s)
{
  *s = *s * 1103515245UL + 12345UL;
  return *s >> 8;
}

static void fill(unsigned char *p, size_t n, int id, unsigned long tag)
{
  for (size_t i = 0; i < n; i++)
    p[i] = (unsigned char) (id * 37 + tag + i);
}

static int intact(const unsigned char *p, size_t n, int id, unsigned long tag)
{
  for (size_t i = 0; i < n; i++)
    if (p[i] != (unsigned char) (id * 37 + tag + i))
      return 0;
  return 1;
}

static void *work(void *arg)
{
  struct worker *w = arg;
  unsigned char *block[LIVE] = { 0 };
  size_t size[LIVE];
  unsigned long tag[LIVE], seed = 12345 + w->id;

  SetTaskPri(SysBase->ThisTask, w->pri);
  for (int i = 0; i < iterations; i++)
    {
      int k = next_random(&seed) % LIVE;

      if (block[k] && !intact(block[k], size[k], w->id, tag[k]))
        w->bad++;
      switch (next_random(&seed) % 3)
        {
        case 0:
          free(block[k]);
          block[k] = 0;
          break;
        case 1:
          if (block[k])
            {
              /* realloc keeps the common part.  */
              size_t n = 1 + next_random(&seed) % 4096;
              unsigned char *p = realloc(block[k], n);
              if (!p)
                break;
              if (!intact(p, n < size[k] ? n : size[k], w->id, tag[k]))
                w->bad++;
              block[k] = p;
              size[k] = n;
              fill(p, n, w->id, tag[k]);
              break;
            }
          /* fall through */
        default:
          free(block[k]);
          size[k] = 1 + next_random(&seed) % 4096;
          tag[k] = next_random(&seed);
          if ((block[k] = malloc(size[k])))
            fill(block[k], size[k], w->id, tag[k]);
          break;
        }
      if (fprintf(shared, "t%d %d %.3f\n", w->id, i, i / 8.0) < 0)
        w->bad++;
      if (i % 100 == 99)
        {
          FILE *f = fopen(tmpname[w->id], "w");
          if (!f || fprintf(f, "%d\n", i) < 0 || fclose(f))
            w->bad++;
        }
    }
  for (int k = 0; k < LIVE; k++)
    {
      if (block[k] && !intact(block[k], size[k], w->id, tag[k]))
        w->bad++;
      free(block[k]);
    }
  return 0;
}

int main(int argc, char **argv)
{
  static const int pri[THREADS] = { -1, 0, 0, 1 };
  struct worker w[THREADS];
  pthread_t t[THREADS];

  if (argc > 1)
    iterations = atoi(argv[1]);
  if (argc > 2)
    path = argv[2];
  if (!(shared = fopen(path, "w")))
    return 20;

  /* A thread that doesn't start counts as an error; the others are joined
     all the same, so that none runs on once main returns.  */
  int started = 0, bad = 0;
  for (; started < THREADS; started++)
    {
      w[started].id = started;
      w[started].pri = pri[started];
      w[started].bad = 0;
      if (pthread_create(&t[started], 0, work, &w[started]))
        {
          bad++;
          break;
        }
    }
  for (int i = 0; i < started; i++)
    {
      pthread_join(t[i], 0);
      bad += w[i].bad;
      remove(tmpname[i]);
    }
  fclose(shared);

  FILE *f = fopen(path, "r");
  if (!f)
    return 20;
  int next[THREADS] = { 0 }, lines = 0, malformed = 0, order = 0;
  char line[64];
  while (fgets(line, sizeof line, f))
    {
      int id, n;
      double x;

      lines++;
      if (sscanf(line, "t%d %d %lf", &id, &n, &x) != 3 || id < 0
          || id >= THREADS || x != n / 8.0)
        {
          if (malformed++ < 3)
            printf("malformed: %s", line);
          continue;
        }
      if (n != next[id]++)
        order++;
    }
  fclose(f);
  remove(path);
  for (int i = 0; i < THREADS; i++)
    if (next[i] != iterations)
      order++;
  printf("%d threads x %d: %d lines, %d malformed, %d out of order or missing, "
         "%d pattern or stdio errors\n",
         THREADS, iterations, lines, malformed, order, bad);
  return malformed || order || bad;
}
