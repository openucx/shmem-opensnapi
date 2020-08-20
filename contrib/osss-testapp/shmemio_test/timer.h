// Copyright (c) 2018 - 2020 Arm, Ltd

#ifndef TIMER_H
#define TIMER_H

#include <time.h>
#include <stdio.h>

typedef struct
{
  double avg_seconds;
  double tot_seconds;
  unsigned int nsamples;
  struct timespec start;
  struct timespec stop;
} my_timer_t;

static void
timer_reset(my_timer_t * const timer)
{
  timer->avg_seconds = 0;
  timer->tot_seconds = 0;
  timer->nsamples = 0;
  timer->start.tv_sec = 0;
  timer->start.tv_nsec = 0;
  timer->stop.tv_sec = 0;
  timer->stop.tv_nsec = 0;
}

static void
report_times(my_timer_t* timer)
{
  printf ("nsamples,\tavg_seconds,\ttot_seconds\n");
  printf ("%d,\t%.4f,\t%.4f\n", timer->nsamples,
          timer->avg_seconds, timer->tot_seconds);
}

static void
timer_start(my_timer_t * const timer)
{
  clock_gettime(CLOCK_MONOTONIC, &(timer->start));
}

static void
timer_stop(my_timer_t * const timer)
{
  clock_gettime(CLOCK_MONOTONIC, &(timer->stop));
  double sec = (double) (timer->stop.tv_sec - timer->start.tv_sec);
  sec += (double) (timer->stop.tv_nsec - timer->start.tv_nsec)*1e-9;

  double rn = (double)timer->nsamples / (double)(timer->nsamples + 1);
  double rsec = sec / (double)(timer->nsamples + 1);
  timer->avg_seconds = (timer->avg_seconds * rn) + rsec;

  timer->tot_seconds += sec;
  timer->nsamples++;
}


void print_serial(my_timer_t * const timer, const char *msg, int me, int npes);

#endif
