#!/bin/bash
# Copyright (c) 2018 - 2020 Arm Ltd

ISXDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

## Connection info for fpsace server
FSPACE_SERVER=10.0.0.50
FSPACE_PORT=13333

## Parameters for benchmark runs
EDGE_FILE="/mnt/nfs/gbuild.dat"

#V_PE=4096
#E_PE=83886080
V_PE=512
E_PE=2090752

#PE_COUNTS=( "28" "16" "8" "4" "2" )
#XDIMS=( "4" "4" "2" "2" "1" )
PE_COUNTS=( "4" )
XDIMS=( "2" )

NUM_UP=20000
E_UPDATES="-${NUM_UP} 0 -${NUM_UP} 0 -${NUM_UP} 0"
#E_UPDATES="-${NUM_UP} 0"

RUNID=0

## Binary executables for shmemio and file io
SHMEMIO_BIN=${ISXDIR}/bin-shmemio/gbuild-weak.x
#SHMEMIO_BIN=${ISXDIR}/bin-shmemio-debug/gbuild-weak.x

FILEIO_BIN=${ISXDIR}/bin-optimized/gbuild-weak.x
#FILEIO_BIN=${ISXDIR}/bin-debug/gbuild-weak.x

server() {
    fspace_server -n 4 -p ${FSPACE_PORT} -v
}

osss_run()
{
    oshrun -n ${NPES} -bind-to core -map-by core $@ || exit
}

graph_generate()
{
    for ((i=0;i<${#PE_COUNTS[@]};++i)); do
	
	NPES=${PE_COUNTS[i]}
	XDIM=${XDIMS[i]}
    
	LOGPREFIX=gbuild-${RUN_TYPE}-N.${NPES}-gentuples.${GEN_TUPLES}-${RUNID}
    
	# generate and quit
	#GG=2
	#LOGFILE=${LOGPREFIX}-generate.log
	
	# generate, sort, writeback
	GG=1
	LOGFILE=${LOGPREFIX}-isx.log
    
	osss_run ${RUN_BIN} ${XDIM} ${E_PE} ${V_PE} ${LOGFILE} ${GG} ${EDGE_FILE} ${SERVER} ${PORT}

    done
}

run_app()
{
    LOGPREFIX=gbuild-${RUN_TYPE}-N.${NPES}-gentuples.${GEN_TUPLES}-${RUNID}
    LOGFILE=${LOGPREFIX}-weak.log

    # client usage
    # RUN_BIN <xdim> <num edges> <num vertices> <log_file> <generate tuples> <tuple file>

    # server usage
    # RUN_BIN <xdim> <num edges> <num vertices> <log_file> <generate tuples> <tuple file> <shmemio hostname> <shmemio port>

    osss_run ${RUN_BIN} ${XDIM} ${E_PE} ${V_PE} ${LOGFILE} ${GEN_TUPLES} ${EDGE_FILE} ${SERVER} ${PORT}

    RUNID=$((RUNID+1))
}

run_workflow()
{
    for ((i=0;i<${#PE_COUNTS[@]};++i)); do
	
	NPES=${PE_COUNTS[i]}
	XDIM=${XDIMS[i]}
	    
	echo "npes = $NPES xdim = $XDIM, run_type = $RUN_TYPE"
    
	GEN_TUPLES=1
	run_app
    
	for GEN_TUPLES in ${E_UPDATES}; do
	    run_app
	done

    done
}

client_shmemio()
{
    RUN_BIN=${SHMEMIO_BIN}
    SERVER=${FSPACE_SERVER}
    PORT=${FSPACE_PORT}
    
    $@
}

client_fileio()
{
    RUN_BIN=${FILEIO_BIN}
    SERVER=
    PORT=

    $@
}

cmd=$1
shift

$cmd $@

