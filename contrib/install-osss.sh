#!/bin/bash
# Copyright (c) 2018 - 2020 Arm, Ltd 

OSH_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $OSH_DIR/env.sh

preinstall()
{
    local currentver="$(gcc -dumpversion)"
    requiredver="5.0.0"
    if [ "$(printf '%s\n' "$requiredver" "$currentver" | sort -V | head -n1)" = "$requiredver" ]; then 
        echo "Using gcc version $currentver"
    else
        echo "Require gcc version > 5.0.0"
        exit 1
    fi

    mkdir -p ${LOG_DIR}

    if [ "$WITH_XPMEM" != "0" ]; then
        $OSH_DIR/util/xpmem_ctrl.sh load

        stat="$( $OSH_DIR/util/xpmem_ctrl.sh status )"
        if [ "$stat" != "xpmem is loaded" ]; then
	    echo "xpmem requested but failed to load xpmem."
            echo "Please install and load xpmem"
	    exit 1
        fi

        export_xpmem
    fi
}

usage_rdmacore()
{
    echo "rdma-core requires cmake and other build packages"
    echo "For Centos 6/7:"
    echo "   yum install cmake gcc libnl3-devel libudev-devel make pkgconfig valgrind-devel"
    echo "Recommend:"
    echo "   yum install epel-release"
    echo "   yum install cmake3 ninja-build pandoc"
    echo
    echo "For other distros and more info see the rdma-core README"
    echo
}

build_rdmacore()
{
    if [ ! -d ${RDMACORE_HOME} ]; then
        tar_file=${RDMACORE_TAR}
        src_path=${RDMACORE_HOME}
        repo_url=${RDMACORE_REPO}
        repo_co=${RDMACORE_GITCO}
        clone_src
    fi
        
    cd ${RDMACORE_HOME} || return 1

    bash build.sh || { usage_rdmacore; return 1; }
    return 0
}

build_ucx_doc()
{
    if [ ! -d ${UCX_SRC} ]; then
        tar_file=${UCX_TAR}
        src_path=${UCX_SRC}
        repo_url=${UCX_REPO}
        repo_co=${UCX_GITCO}
        clone_src
    fi
    
    cd ${UCX_SRC} || return 1
    ./autogen.sh || return 1

    rm -rf BUILD-DOC
    mkdir BUILD-DOC
    cd BUILD-DOC || return 1

    ../configure MPICC= \
		 --with-docs-only \
		 --enable-doxygen-dot \
		 --enable-doxygen-pdf

    make -j ${MAKEJ} || return 1
    
    return 0
}


UCX_BUILD_RELEASE=BUILD-RELEASE
UCX_CONF_RELEASE="\
  --disable-logging \
  --disable-debug \
  --disable-assertions \
  --disable-params-check \
  --prefix=${UCX_PREFIX}"
        
UCX_BUILD_DEBUG=BUILD-DEBUG
UCX_CONF_DEBUG="\
   --enable-debug --enable-debug-data \
   --prefix=${UCX_PREFIX_DEBUG}"

UCX_BUILD_DIR=${UCX_BUILD_RELEASE}
UCX_CONF_FLAGS=${UCX_CONF_RELEASE}

reconf_ucx()
{
    if [ ! -d ${UCX_SRC} ]; then
        tar_file=${UCX_TAR}
        src_path=${UCX_SRC}
        repo_url=${UCX_REPO}
        repo_co=${UCX_GITCO}
        clone_src
    fi

    cd ${UCX_SRC} || return 1
    ./autogen.sh || return 1

    rm -rf ${UCX_BUILD_DIR}
    mkdir ${UCX_BUILD_DIR}
    cd ${UCX_BUILD_DIR} || return 1

    if [ ${WITH_RDMACORE} = "1" ]; then
        
        if [ ! -d ${RDMACORE_PREFIX}/lib ]; then
	    build_rdmacore
	fi
        
	export LD_LIBRARY_PATH="${RDMACORE_PREFIX}/lib:${LD_LIBRARY_PATH}"
	UCX_CONF_FLAGS+=" CFLAGS=-I${RDMACORE_PREFIX}/include \
	--with-verbs=${RDMACORE_PREFIX} \
	--with-rdmacm=${RDMACORE_PREFIX}"
    fi

    if [ ${WITH_XPMEM} -eq 1 ]; then
	../configure MPICC=${UCX_MPICC} \
	    CRAY_XPMEM_CFLAGS="${CRAY_XPMEM_INCLUDE_OPTS}" \
	    CRAY_XPMEM_LIBS="${CRAY_XPMEM_POST_LINK_OPTS}" \
	    --with-xpmem=${XPMEM_PREFIX} \
	    ${UCX_CONF_FLAGS}
    else
	../configure MPICC=${UCX_MPICC} \
		     ${UCX_CONF_FLAGS} \
		     --without-xpmem
    fi
    
    return 0
}

install_ucx()
{
    if [ ! -d ${UCX_SRC}/${UCX_BUILD_DIR} ]; then
        reconf_ucx || return 1
    fi

    cd ${UCX_SRC}/${UCX_BUILD_DIR} || return 1
    make -j ${MAKEJ} && ${SUDO} make -j ${MAKEJ} install || return 1

    return 0
}

reconf_pmix()
{
    if [ ! -d ${PMIX_SRC} ]; then
        tar_file=${PMIX_TAR}
        tar_url=${PMIX_URL}
        src_path=${PMIX_SRC}
        wget_src
    fi

    cd ${PMIX_SRC} || return 1
    
    rm -rf BUILD
    mkdir BUILD
    cd BUILD || return 1

    ../configure --prefix=${PMIX_PREFIX} || return 1
    return 0
}

install_pmix()
{
    if [ ! -d ${PMIX_SRC}/BUILD ]; then
        reconf_pmix || return 1
    fi

    cd ${PMIX_SRC}/BUILD || return 1

    make -j ${MAKEJ} all && ${SUDO} make -j ${MAKEJ} install || return 1
    return 0
}


reconf_ompi()
{
    if [ ! -d ${OMPI_SRC} ]; then
        tar_file=${OMPI_TAR}
        tar_url=${OMPI_URL}
        src_path=${OMPI_SRC}
        wget_src
    fi

    cd ${OMPI_SRC} || return 1

    rm -rf BUILD
    mkdir BUILD
    cd BUILD || return 1
    
    OMPI_CONFIG_FLAGS="
           --prefix=${OMPI_PREFIX} \
	   --enable-oshmem \
	   --disable-java \
	   --disable-mpi-fortran \
	   --disable-oshmem-fortran \
	   --without-verbs \
	   --without-mxm "

    if [ "${WITH_SYSTEM_UCX}" = "0" ]; then
	OMPI_CONFIG_FLAGS+=" --with-ucx=${UCX_PREFIX} \
	   		     --with-ucx-libdir=${UCX_PREFIX}/lib "
    else
	OMPI_CONFIG_FLAGS+=" --with-ucx=${WITH_SYSTEM_UCX} \
	   		     --with-ucx-libdir=${WITH_SYSTEM_UCX_LIB} "
    fi

    if [ ${WITH_XPMEM} -eq 1 ]; then
	OMPI_CONFIG_FLAGS+=" --with-xpmem=${XPMEM_PREFIX}"
    fi
    
    ../configure ${OMPI_CONFIG_FLAGS} || return 1
    return 0
}

install_ompi()
{
    if [ ! -d ${OMPI_SRC}/BUILD ]; then
        reconf_ompi || return 1
    fi

    cd ${OMPI_SRC}/BUILD || return 1
	
    make -j ${MAKEJ} && ${SUDO} make -j ${MAKEJ} install || return 1
    return 0
}


reconf_osss()
{
    cd ${OSSS_SRC} || return 1

    touch README
    ./autogen.sh || return 1

    rm -rf BUILD
    mkdir BUILD
    cd BUILD || return 1

    OSSS_CONFIG_FLAGS="
	--with-pmix=${PMIX_PREFIX} \
	--prefix=${OSSS_PREFIX}"

    if [ "${WITH_SYSTEM_UCX}" = "0" ]; then
	OSSS_CONFIG_FLAGS+=" --with-ucx=${UCX_PREFIX}"
    else
	OSSS_CONFIG_FLAGS+=" --with-ucx=${WITH_SYSTEM_UCX}"
    fi
    
    ../configure ${OSSS_CONFIG_FLAGS} || return 1
    return 0
}

install_osss()
{
    if [ ! -d ${OSSS_SRC}/BUILD ]; then
        reconf_osss || return 1
    fi

    cd ${OSSS_SRC}/BUILD || return 1
    make -j ${MAKEJ} && ${SUDO} make -j ${MAKEJ} install || return 1
    return 0
}

logit()
{
    local logid=1
    local logf=${LOG_DIR}/$1.${logid}

    while [ -f $logf ]; do
        logid=$((logid+1))
        logf=${LOG_DIR}/$1.${logid}
    done
    
    $@ 2>&1 | tee $LOG_DIR/$1.log
    if [ "${PIPESTATUS[0]}" -ne 0 ]; then
	FAILED="$FAILED $1"
	return 1
    else
	SUCCEED="$SUCCEED $1"
	return 0
    fi
}

install()
{
    if [ ${INSTALL_RDMACORE} -eq 1 ]; then
        logit install_rdmacore || exit
    fi
    
    if [ ${INSTALL_UCX} -eq 1 ]; then
        #logit build_ucx_doc || exit
        logit install_ucx  || exit
    fi
    
    if [ ${INSTALL_PMIX} -eq 1 ]; then
        logit install_pmix || exit
    fi
    
    if [ ${INSTALL_OMPI} -eq 1 ]; then
        logit install_ompi || exit
    fi
    
    if [ ${INSTALL_OSSS} -eq 1 ]; then
        logit install_osss  || exit
    fi
}

usage()
{
    echo "Usage: $0 COMMAND"
    echo ""
    echo "Valid commands are:"
    echo "   help                  : print this message"
    echo "   install               : install BUILD_COMPONENTS in the right order"
    echo "   install_COMPONENT     : install an individual component by name"
    echo "   reconf_COMPONENT      : for a component, rerun autoreconf, then delete and reconfigure in build directory"
    echo ""
    help_env
}

help()
{
    usage
}

if [ "x$1" = "x" ]; then
    usage
    exit 1
fi

preinstall
$@

echo ""
echo "Command results:"
echo "   SUCCEED : $SUCCEED"
echo "   FAILED  : $FAILED"
