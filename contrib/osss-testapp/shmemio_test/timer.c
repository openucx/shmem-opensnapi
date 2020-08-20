// Copyright (c) 2018 - 2020 Arm, Ltd

#include "timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <shmem.h>
#include <unistd.h>

void print_serial(my_timer_t * const timer, const char *label, int me, int npes)
{
  for (int idx = 0; idx < npes; idx++) {
    if (idx == me) {
      printf ("%s [%d]:\n", label, me);
      report_times(timer);
    }
    fflush(stdout);
    shmem_barrier_all();
    sleep(1);
  }
}



