# Setting up pmem for mapping files in UCX

Copyright (c) 2018 - 2020 Arm, Ltd

Suppose the system has booted with pmem device /dev/pmem0

```
$ ls -l /dev/pmem0 
brw-rw---- 1 root disk 259, 0 Oct  8 17:45 /dev/pmem0
```

The steps in this Readme are used in the 'ucp_mapdev.sh' script in this
directory. It must be run as root to setup pmem and will destory any existing
data on the pmem device

```
$ sudo ./mapdev_ctl.sh setup_pmem
```

## Explanation of steps

These steps create filesystem and mount it with DAX options
```
mkdir /mnt/pmemd
mkfs.ext4 /dev/pmem0
mount -o dax /dev/pmem0 /mnt/pmemd
```

Verify DAX success in dmesg and mount
```
$ dmesg | tail
[2085167.272372] EXT4-fs (pmem0): DAX enabled. Warning: EXPERIMENTAL, use at your own risk
[2085167.282165] EXT4-fs (pmem0): mounted filesystem with ordered data mode. Opts: dax

$ mount | grep pmem
/dev/pmem0 on /mnt/pmemd type ext4 (rw,relatime,dax)
```

## Troubleshoot DAX mounting

DAX mounting may fail with this message (depends Linux kernel version)
```
$ dmesg | tail
[  257.720456] EXT4-fs (pmem0): DAX enabled. Warning: EXPERIMENTAL, use at your own risk
[  257.728324] EXT4-fs (pmem0): DAX unsupported by block device.
```

From https://docs.pmem.io/ndctl-users-guide/troubleshooting, check namespaces
```
$ ndctl list
[
  {
    "dev":"namespace0.0",
    "mode":"raw",
    "size":17179869184,
    "blockdev":"pmem0"
  }
]
```

"raw" mode does not support DAX, change to fsdax mode
```
$ sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
{
  "dev":"namespace0.0",
  "mode":"fsdax",
  "map":"dev",
  "size":"15.75 GiB (16.91 GB)",
  "uuid":"219019e4-4bdf-44d9-8236-8582373a85f8",
  "sector_size":512,
  "blockdev":"pmem0",
  "numa_node":0
}
```

Check dmesg output from namespace creation:
```
[  347.938736] pfn0.0 initialised, 4128256 pages in 72ms
[  347.944601] pmem0: detected capacity change from 0 to 16909336576
```

Retry above steps to mount with DAX enabled

