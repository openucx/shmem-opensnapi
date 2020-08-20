# Copyright (c) 2018 - 2020 Arm, Ltd

CFLAGS= -O2 -fopenmp

EXE=connect.x fopen.x fflush.x sharing.x

all: $(EXE)

timer.o: timer.c
	oshcc $(CFLAGS) -o $@ -c $<

%.x : %.c timer.o
	oshcc $(CFLAGS) -o $@ $< timer.o

tidy: .FORCE
	rm -f *~

clean: tidy
	rm -f *.o

scrub: clean
	rm -f $(EXE)

.FORCE:
