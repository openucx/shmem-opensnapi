#!/bin/bash
# Copyright (c) 2018 - 2020 Arm, Ltd

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/../env.sh

export_ucx

if [ "$1" == "UCX_LFLAGS" ]; then
    echo "$ENV_UCX_LFLAGS"
fi

if [ "$1" == "UCX_IFLAGS" ]; then
    echo "$ENV_UCX_IFLAGS"
fi
