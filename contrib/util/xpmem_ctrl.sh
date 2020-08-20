#!/bin/bash
# Copyright (c) 2018 - 2020 Arm, Ltd 

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/../env.sh

if [ "${WITH_XPMEM}" == "0" ]; then
    echo "XPMEM is disabled in environment. Set WITH_XPMEM to nonzero"
    exit 1
fi

export_xpmem

fix_perms()
{
    sleep 2

    echo "Fix permissions on /dev/xpmem. Must be world write-able"
    ${SUDO} chmod 777 /dev/xpmem
}

check_perms()
{
    PERMS="$( stat -c "%a" /dev/xpmem )"
    
    if [ "$PERMS" != "777" ]; then
        echo "Permission on /dev/xpmem was set to $PERMS"
        fix_perms
    fi

    PERMS="$( stat -c "%a" /dev/xpmem )"

    if [ "$PERMS" != "777" ]; then
        "Failed to fix permissions on /dev/xpmem, is $PERMS"
        exit 1
    fi
}

status()
{
    lsmod | grep "^xpmem "
    
    local modgrep="$?"

    if [ "${modgrep}" == "0" ]; then
        echo "xpmem is loaded"
    else
        echo "xpmem is not loaded"
    fi
}

load()
{
    if [ "$( status )" != "xpmem is loaded" ]; then
        echo "Loading xpmem module..."
        ${SUDO} insmod ${XPMEM_MODPATH}

        local ret=$?
        if [ $ret -ne 0 ]; then
            echo "Insert module failed with code $ret"
        fi
    fi

    if [ "$( status )" != "xpmem is loaded" ]; then
        echo "Failed to load xpmem"
    fi
}
    
unload()
{
    if [ "$( status )" = "xpmem is loaded" ]; then
        echo "Unloading xpmem module..."
        ${SUDO} rmmod xpmem

        local ret=$?
        if [ $ret -ne 0 ]; then
            echo "Remove module failed with code $ret"
        fi
    fi

    if [ "$( status )" = "xpmem is loaded" ]; then
        echo "Failed to unload xpmem"
    fi 
}


reload()
{
    unload
    load
}

"$@"
