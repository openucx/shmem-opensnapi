# Copyright (c) 2018 - 2020 Arm, Ltd

all: bin-optimized bin-shmemio

debug: bin-shmemio-debug bin-debug

bin-%: .FORCE
	mkdir -p $@
	cd SHMEM && make clean && make $* CC=oshcc && cp ./bin/* ../$@/.

print: .FORCE
	source ../shmem_env.sh && cd SHMEM && make print

clean: .FORCE
	cd SHMEM && make clean

scrub: .FORCE
	cd SHMEM && make scrub
	rm -rf bin-*

.FORCE:
