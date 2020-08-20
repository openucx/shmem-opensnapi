#!/bin/bash
# Copyright (c) 2020 Arm, Ltd 

ENV_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ENV_GIT_TOP="$( cd ${ENV_DIR} && git rev-parse --show-toplevel )"

set_ctrl_vars()
{
    ## Use sudo for install steps and kernel module loads?
    #: "${SUDO:=sudo}"
    : "${SUDO:=}"

    ## Parallelism of make build
    : "${MAKEJ:=$(grep -c ^processor /proc/cpuinfo)}"

    ## Legal build components
    BUILD_COMPONENTS_ALL="rdmacore ucx pmix ompi osss"
    ## List of components to rebuild/reinstall, or "all"
    : "${BUILD_COMPONENTS:=all}"
    if [ "${BUILD_COMPONENTS}" == "all" ]; then
        BUILD_COMPONENTS="${BUILD_COMPONENTS_ALL}"
    fi

    #### Top level directories
    #
    ## logs
    : "${LOG_DIR:=${ENV_DIR}/logs}"
    ## Source trees each with build subdirectory
    : "${TOP_SRC:=${HOME}/src}"
    ## Archive (tar) files are placed here with get_fresh_src
    : "${TAR_DIST:=${HOME}/src/tar}"
    ## Install components here
    : "${TOP_PREFIX:=${HOME}/opt}"

    #### RDMA CORE
    #
    ## Use rdma-core libraries when building other tools?
    : "${WITH_RDMACORE:=0}"
    ## tarfile to use if available
    : "${RDMACORE_TAR:=rdma-core.tar.gz}"
    ## Where to clone and build rdma-core
    : "${RDMACORE_HOME:=${TOP_PREFIX}/rdma-core}"
    : "${RDMACORE_PREFIX:=${RDMACORE_HOME}/build}"
    ## rmda-core repo and branch
    : "${RDMACORE_REPO:=https://github.com/linux-rdma/rdma-core.git}"
    : "${RDMACORE_GITCO:=master}"

    #### UCX
    #
    ## Use default system install of ucx? Set to 0 or ucx prefix path (e.g. /usr)
    : "${WITH_SYSTEM_UCX:=0}"
    #: "${WITH_SYSTEM_UCX:=/usr}"
    ## library path for system UCX
    if [ ${WITH_SYSTEM_UCX} != 0 ]; then
        WITH_SYSTEM_UCX_LIB="${WITH_SYSTEM_UCX}/lib64"
    fi
    #
    ## MPICC if required
    : "${UCX_MPICC:=}"
    #
    ## Tar file containing ucx to use if available
    : "${UCX_TAR:=ucx.tar.gz}"
    ## Where to clone the ucx repo
    : "${UCX_SRC:=${TOP_SRC}/ucx}"
    ## Where to get repo
    : "${UCX_REPO:=https://github.com/openucx/ucx.git}"
    ## Checkout this branch or tag once on initial download
    : "${UCX_GITCO:=v1.8.x}"

    #
    ## Prefix for release install
    : "${UCX_PREFIX:=${TOP_PREFIX}/ucx}"
    ## Prefix for the debug instal
    : "${UCX_PREFIX_DEBUG:=${UCX_PREFIX}-dbg}"

    #### OSSS
    ## Where osss source repo lives
    : "${OSSS_SRC:=${ENV_GIT_TOP}}"
    : "${OSSS_PREFIX:=${TOP_PREFIX}/osss-fspace}"

    #### XPMEM
    #
    ## Use xpmem?
    : "${WITH_XPMEM:=0}"
    ## tarfile to use if available
    : "${XPMEM_TAR:=xpmem.tar.gz}"
    ## repo to pull from
    : "${XPMEM_REPO:=https://github.com/hjelmn/xpmem.git}"
    ## default system wide install location of xpmem requries SUDO=sudo
    : "${XPMEM_SRC:=/usr/src/my_modules/xpmem}"
    : "${XPMEM_PREFIX:=/opt/xpmem}"

    #### PMIX
    PMIX_VER=2.2.2
    : "${PMIX_TAR:=pmix-${PMIX_VER}.tar.gz}"
    : "${PMIX_SRC:=${TOP_SRC}/pmix-${PMIX_VER}}"
    : "${PMIX_URL:=https://github.com/pmix/pmix/releases/download/v${PMIX_VER}/${PMIX_TAR}}"
    : "${PMIX_PREFIX:=${TOP_PREFIX}/pmix}"

    ### OMPI
    OMPI_VER=3.1.4
    : "${OMPI_TAR:=openmpi-${OMPI_VER}.tar.gz}"
    : "${OMPI_SRC:=${TOP_SRC}/openmpi-${OMPI_VER}}"
    : "${OMPI_URL:=https://download.open-mpi.org/release/open-mpi/v3.1/${OMPI_TAR}}"
    : "${OMPI_PREFIX:=${TOP_PREFIX}/ompi}"

}

set_ctrl_vars
#readarray -t CTRL_VARS < <( declare -f set_ctrl_vars | sed -rn 's/.*\{(\w+):=.*/\1/p' )
readarray -t CTRL_DFLT < <( declare -f set_ctrl_vars | sed -rn 's/.*\{(\w+):=(.*)\}.*/\1 [\2]/p' )

wget_archive()
{
    mkdir -p ${TAR_DIST}
    cd ${TAR_DIST} || return -1;

    if [ ! -f ${tar_file} ]; then
        echo "Fetching ${tar_url}..."
        wget ${tar_url}
    fi

    if [ ! -f ${tar_file} ]; then
        echo "Fetch of ${tar_file} from ${tar_url} failed."
        return -1
    fi
}

extract_archive()
{
    local tar_path="${TAR_DIST}/${tar_file}"
    
    if [ ! -f ${tar_path} ]; then
        echo "Failed to find archive ${tar_path}"
        return -1
    fi
    
    ## remove longest match front of string to /
    local src_dir="${src_path##*/}"
    local src_top="${src_path%/*}"

    cd ${src_top} || return 1
    tar xfvz ${tar_path}
    
    if [ ! -d ${src_dir} ]; then
        echo "Extract ${src_dir} from ${tar_path} failed."
        return -1
    fi
}

wget_src()
{
    if [ -d ${src_path} ]; then
        echo "source exists at ${src_path}"
        return 0
    fi

    if [ ! -f "${TAR_DIST}/${tar_file}" ]; then
        wget_archive
    fi
    extract_archive
}

clone_archive_repo()
{
    local src_top="${src_path%/*}"
    local src_dir="${src_path##*/}"

    cd ${src_top} || return -1
    
    if [ -d ${src_dir} ]; then
        echo "Updating ${src_dir} repo..."
        cd ${src_dir} && git pull
    else
        echo "Cloning ${repo_url} in ${src_top}..."
        git clone ${repo_url} ${src_dir}
        cd ${src_dir} || return -1
        git checkout ${repo_co}
    fi

    if [ "x${TAR_DIST}" != "x" ]; then
        echo "Saving archive of ${src_top}/${src_dir} to ${TAR_DIST}/${tar_file}"
        cd ${src_top} || return -1
        tar cfz ${TAR_DIST}/${tar_file} ${src_dir}
    fi
}

clone_src()
{
    if [ -f ${TAR_DIST}/${tar_file} ]; then
        extract_archive
    else
        clone_archive_repo
    fi
}

help_env()
{
    if [ "x$1" == "x" ]; then
        echo "Valid help_env commands are:"
        echo "   help_env VAR [VAR..]  : print env variable value(s)"
        echo "   help_env params       : print all env variables and values"
    elif [ "$1" == "params" ]; then
        echo "Control parameters (${#CTRL_DFLT[@]} total) format:"
        echo "    NAME [DEFAULT]"
        echo "        VALUE"
        for vd in "${CTRL_DFLT[@]}"; do 
            echo "    $vd"
            vn=${vd%%\ *}
            echo "        ${!vn}"
        done
    else
        while [ "x$1" != "x" ]; do
            echo "$1 = [${!1}]"
            shift
        done
    fi
}

export_xpmem()
{
    if [ "${WITH_XPMEM}" != "0" ]; then
        export XPMEM_MODPATH=${XPMEM_PREFIX}/lib/modules/$(uname -r)/xpmem.ko

        export LD_LIBRARY_PATH=${XPMEM_PREFIX}/lib:$LD_LIBRARY_PATH
        export PKG_CONFIG_PATH=${XPMEM_PREFIX}/lib/pkgconfig

        export CRAY_XPMEM_INCLUDE_OPTS="-I{XPMEM_PREFIX}/include"
        export CRAY_XPMEM_POST_LINK_OPTS="-L${XPMEM_PREFIX}/lib -lxpmem -Wl,-rpath=${XPMEM_PREFIX}/lib"
    fi
}

export_rdmacore()
{
    if [ "${WITH_RDMACORE}" != "0" ]; then
	export LD_LIBRARY_PATH="${RDMACORE_PREFIX}/lib:${LD_LIBRARY_PATH}"
    fi
}

export_ucx()
{
    export_xpmem
    export_rdmacore
    
    if [ "${WITH_SYSTEM_UCX}" == "0" ]; then
        export PATH="${UCX_PREFIX}/bin:${PATH}"
        export LD_LIBRARY_PATH="${UCX_PREFIX}/lib:${LD_LIBRARY_PATH}"
        export ENV_UCX_LFLAGS="-L${UCX_PREFIX}/lib"
        export ENV_UCX_IFLAGS="-I${UCX_PREFIX}/include"
    fi
}

export_ompi()
{
    export_ucx

    export PATH="${OMPI_PREFIX}/bin/:${PATH}"
    export LD_LIBRARY_PATH="${OMPI_PREFIX}/lib/:${PMIX_PREFIX}/lib:${LD_LIBRARY_PATH}"
}

export_osss()
{
    export_ompi

    export PATH="${OSSS_PREFIX}/bin/:${PATH}"
    export LD_LIBRARY_PATH="${OSSS_PREFIX}/lib/:${LD_LIBRARY_PATH}"
}

################## Use BUILD_COMPONENTS and checks to set testable values

set_install_flags() {
    INSTALL_RDMACORE=0
    INSTALL_UCX=0
    INSTALL_PMIX=0
    INSTALL_OMPI=0
    INSTALL_OSSS=0
    
    for comp in ${BUILD_COMPONENTS}; do
	case $comp in
	    all)
		INSTALL_UCX=1
		INSTALL_PMIX=1
		INSTALL_OMPI=1
		INSTALL_OSSS=1
		INSTALL_RDMACORE=1
		;;
	    rdmacore)
		INSTALL_RDMACORE=1
		;;
	    ucx)
		INSTALL_UCX=1
		;;
	    pmix)
		INSTALL_PMIX=1
		;;
	    ompi)
		INSTALL_OMPI=1
		;;
	    osss)
		INSTALL_OSSS=1
		;;
	    osss-ucx)
		INSTALL_OSSS_UCX=1
		;;
	esac
    done

    if [ "${WITH_RDMACORE}" == "0" ]; then
        INSTALL_RDMACORE=0
    fi
    
    if [ "${WITH_SYSTEM_UCX}" != "0" ]; then
	INSTALL_UCX=0
    fi
}

set_install_flags
