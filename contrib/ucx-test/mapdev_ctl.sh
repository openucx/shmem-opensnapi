# Copyright (c) 2018 - 2020 Arm, Ltd

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

SERVER_NET_DEV=mlx5_0:1
CLIENT_NET_DEV=mlx5_0:1

rm -f /mnt/pmemd/tmp/ucxfile
rm -f /tmp/ucp_mapdev_testfile

#TEST_TYPE="tempfile"
TEST_TYPE="pmemfile"

MSG_SIZES="8 16 32 64 128 256 512 1024 2048"

setup_ucx()
{
    #export UCX_TLS=rc_mlx5,ud_mlx5,rdmacm,sm
    export UCX_TLS=rc_verbs,ud_verbs,rdmacm,sm
    
    export UCX_NET_DEVICES=${net_dev}
    export UCX_SHM_DEVICES=memory
    #export UCX_IB_REG_METHODS=odp
    #export UCX_LOG_LEVEL=info
}

setup_pmem() {
    if [[ $EUID -ne 0 ]]; then
	echo "PMEM setup must be run as root"
	exit 1
    fi

    echo "Creating mount point."
    mkdir -p /mnt/pmemd
    echo "Unmounting existing device." 
    umount /mnt/pmemd
    echo "Creating pmem device namespace in fsdax mode..."
    ndctl create-namespace -f -e namespace0.0 --mode=fsdax || exit 1
    echo "Formating pmem as ext4..."
    mkfs.ext4 /dev/pmem0 || exit 1
    echo "Mounting pmem with DAX option..."
    mount -o dax /dev/pmem0 /mnt/pmemd || exit 1
    echo "Creating world writeable temp directory for tests..."
    mkdir -p /mnt/pmemd/tmp
    chmod 777 /mnt/pmemd/tmp
}

run_server() {
    net_dev=${SERVER_NET_DEV}
    setup_ucx
    
    ./ucp_mapdev.x -t ${TEST_TYPE} -N 32 -S 64
}

run_client() {
    net_dev=${CLIENT_NET_DEV}
    setup_ucx
    
    if [ "x" == "x${SERVER}" ]; then
        echo "SERVER not set in environment"
        return 1
    fi
    
    for msg in ${MSG_SIZES}; do
        ./ucp_mapdev.x -t ${TEST_TYPE} -a ${SERVER} -N 32 -S $msg
    done
}

$@

