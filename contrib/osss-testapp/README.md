# Tests and experiments related to OpenSHMEM

This directory contains fspace API tests.

* See License in top level directory
  * Copyright (c) 2018 - 2020 Arm, Ltd

## Quick Start Server-Client Tests

Create the pmem data directory. The server expects a writeable directory at
`/mnt/pmemd/tmp/fspace-data`

If you have PMEM devices installed in the system that shows up as `/dev/pmemN`,
see the setup scripts in [../ucx-tests](../ucx-tests) for an example of how to
create a pmem dax namespace and mount it at the expected path.

If you do not have PMEM devices installed, you can test with a regular directory.
```
$ mkdir -p /mnt/pmemd/tmp/fspace-data
```

Or create a link to tmpfs mount.
```
$ mkdir -p /dev/shm/fspace-data
$ ln -s /dev/shm/fpsace-data /mnt/pmemd/tmp/fspace-data
```

Setup programming environment by exporting paths used for install. If you set
paths with environment variables during install, make sure they are in the
environment here too.

```
$ export TOP_PREFIX=$HOME/local
$ source shmem_env.sh
$ which oshcc
```

Compile OpenSHMEM test program clients.
```
$ cd shmemio_test
$ make
```

The OpenSHMEM I/O interface tests require you to run a server component. The
server is built and installed along with the OpenSHMEM library.

In one terminal, run the server interactively to see output and print raw memory
data.
```
$ fspace_server
```

Edit the run script to set the IP and port for your server.
```
$ vi run_test.sh
```

In another terminal, launch a client with the `run_test.sh` script
```
$ ./run_test.sh fopen
```

## Build and Run edge sort workflow

Setup programming environment
```
$ source shmem_env.sh
$ which oshcc
```

Build clients
```
$ cd ISx-sort-gbuild
$ make
```

Setup graph edge sort parameters at the top of script. Server IP and port, graph
size, graph file path in backing store, etc. See comments in script.
```
$ vi ./run-gbuild.sh
```

Run server in one terminal
```
$ ./run-gbuild.sh server
```

Run client in another terminal
```
$ ./run-gbuild.sh client_shmemio run_workflow
```