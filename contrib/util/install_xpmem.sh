#!/bin/bash
# Copyright (c) 2018 - 2020 Arm, Ltd

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/../env.sh

if [ "${WITH_XPMEM}" == "0" ]; then
    echo "XPMEM disabled in environment. Set WITH_XPMEM to nonzero."
fi

get_xpmem_src()
{
    tar_file=${XPMEM_TAR}

    repo_url=${XPMEM_REPO}
    repo_co=${XPMEM_GITCO}

    src_path=${XPMEM_SRC}
    
    if [ -d ${src_path} ]; then
        echo "XPMEM source exists at ${src_path}"
        return 0
    fi

    local iamu="$( whoami )"
    local src_top="${src_path%/*}"
    local src_dir="${src_path##*/}"

    ${SUDO} mkdir -p ${src_top} && ${SUDO} chown ${iamu} ${src_top}
    clone_src
}

build_xpmem()
{
    if [ ! -d ${XPMEM_SRC} ]; then
        get_xpmem_src
    fi
    
    cd ${XPMEM_SRC} || return 1
    
    ./autogen.sh || return 1
    ./configure --prefix=${XPMEM_PREFIX} \
	&& make || return 1
    
    ## So root won't own this directory tree after install
    local iamu="$( whoami )"
    ${SUDO} mkdir -p ${XPMEM_PREFIX} && ${SUDO} chown ${iamu} ${XPMEM_PREFIX}
    
    ${SUDO} make install || return 1

    return 0
}

build_xpmem

${DIR}/xpmem_ctrl.sh load

