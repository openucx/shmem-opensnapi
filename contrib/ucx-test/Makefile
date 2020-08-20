# Copyright (c) 2018 - 2020 Arm, Ltd

UCX_ENV_SH=./ucx_env.sh

MY_UCX_LDFLAGS=$(shell ${UCX_ENV_SH} "UCX_LFLAGS")
MY_UCX_IFLAGS=$(shell ${UCX_ENV_SH} "UCX_IFLAGS")
MY_UCX_LIBS=-lucp -lucs -luct -lucm

export CFLAGS=-std=gnu11 -g

ALL_EXE= ucp_mapdev.x

all: $(ALL_EXE)

%.x : %.c
	gcc $(CFLAGS) -o $@ $< $(MY_UCX_LIBS) $(MY_UCX_IFLAGS) -I. $(MY_UCX_LDFLAGS)

path_test: .FORCE
	which ucx_info && ucx_info -v

flag_test: .FORCE
	@echo "MY_UCX_LDFLAGS = $(MY_UCX_LDFLAGS)"
	@echo "MY_UCX_IFLAGS = $(MY_UCX_IFLAGS)"

tidy: .FORCE
	rm -f *~

clean: tidy
	rm -f *.o

scrub: clean
	rm -f $(ALL_EXE)

.FORCE:
