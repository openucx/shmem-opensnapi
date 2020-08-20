#!/bin/sh
# For license: see LICENSE file at top-level

touch README
touch INSTALL
mkdir -p m4
autoreconf -ivf
rm -rf autom4te.cache
