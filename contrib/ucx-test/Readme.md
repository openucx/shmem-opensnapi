# UCX Tests

Copyright (c) 2018 - 2020 Arm, Ltd

This directory contains tests to be run on the ucx installation done by the
osss-distro scripts.

ucp_mapdev.c
- A client server program where client connects and server provides memory
region info so client can put/get values
- Memory regions can be mapped from files or allocated by the program

mapdev_ctrl.sh
- Sets up pmem for tests
- See info on process to [Setup PMEM DAX filesystem](./Readme-pmem.md)
- Runs basic tests from this directory


## Quickstart: Run pmem file mapping test

Setup environment
```
$ source ./ucx_env.sh
```

Compile test
```
$ make
```

Setup pmem
```
$ mapdev_ctl.sh setup_pmem
```

Run server
```
$ mapdev_ctl.sh run_server
```

Run client (must get IP address for server IPoIB interface)
```
$ SERVER=<server IP> mapdev_ctl.sh run_client
```

## Troubleshoot: Run perftest on osss ucx installation

Setup environment to find ucx (and rdmacore if specified in env.sh)
```
$ source ./ucx_env.sh
$ which ucx_info
$ ucx_info -v
```

Build test
```
$ make
```

Get IP for IPoIB configured for client/server tests
```
$ ifconfig | grep ib
```

Run a server perftest
```
$ ucx_perftest -t put_bw -d mlx5_0:1 -x rc -c 0
```

Client perftest
```
$ ucx_perftest -t put_bw -d mlx5_0:1 -x rc -c 0 <server IP>
```

## Run PMEM file mapping test



The `ucp_mapdev` program will make a temporary test file on a PMEM DAX
filesystem. See the program or the `run_mapdev.sh` script for temp file paths.

Assuming the temporary file paths are valid, the file mapping test is built
and run in this directory.

In one terminal:
```
$ source ./ucx_env.sh
$ make ucp_mapdev.x
$ ./run_mapdev.sh server
```

In second terminal:
```
$ source ./ucx_env.sh
$ ./run_mapdev.sh client
```