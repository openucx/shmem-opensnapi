# Tests and documentation for OpenSNAPI fspace extension

This directory contains installation scripts and tests of the fspace server and
client API implemented in the OpenSHMEM-SNAPI project.

* See License in top level directory
  * Copyright (c) 2018 - 2020 Arm, Ltd

## Quick Start

- Fetch UCX, PMIX, and OpenMPI from the internet and build source in `$HOME/src/[ucx,pmix,ompi]`
- Places tar archives of sources in `$HOME/src/tar`
- Install ucx, pmix, openmpi and osss to `$HOME/opt/[ucx,pmix,ompi,osss]`

```
$ ./install-osss.sh install
```

It is advisible to run this command in a `screen` (or other disconnected terminal)
session since the build will take a while.

Once the build completes, [build and run OpenSHMEM tests](./osss-testapp/README.md)

## Configure the Install

The build is controlled with environment variable parameters. The default values
in the script will be overridden by anything set in the calling environment.

List all the parameters.
```
$ ./install-osss.sh help_env params
```

Change the build process by setting variables. Edit the script to set your own
defaults or add them at the top of the script. Or just set them in the
environment before running the script.

```
$ TOP_PREFIX=$HOME/local ./install-osss.sh install
```

Build individual components with install_COMPONENT.

```
$ ./install-osss.sh install_ucx
```

Purge old build directory and reconfigure component with reconf_COMPONENT.

```
$ ./install-osss.sh reconf_osss
```


