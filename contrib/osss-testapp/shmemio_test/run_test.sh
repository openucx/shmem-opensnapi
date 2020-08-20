#!/bin/bash
# Copyright (c) 2018 - 2020 Arm, Ltd

SERVER_IPOIB=10.0.0.50
SERVER_PORT=13333

NPES=2

run_server() {
    fspace_server -n 4 -p ${SERVER_PORT} -v
}

run_client() {
    oshrun -n ${NPES} $@ ${SERVER_IPOIB} ${SERVER_PORT}
}

if [ "$1" == "server" ]; then
    run_server
fi

if [ "$1" == "sharing" ]; then
    run_client ./sharing.x
fi

if [ "$1" == "fopen" ]; then
    run_client ./fopen.x "/tmp/shmemio_testfile" 0
fi

if [ "$1" == "connect" ]; then
    run_client ./connect.x
fi

if [ "$1" == "fflush" ]; then
    export SHMEM_SYMMETRIC_SIZE=512000000
    run_client ./fflush.x "/tmp/fflush_testfile"
fi
