#!/bin/sh
set -x
touch COPYRIGHT sntp/scm-rev
pgk install autotools
env ASSUME_ALWAYS_YES=YES pkg bootstrap
./bootstrap
./build --enable-local-libevent
cat $(find . -name "make.log")
cd $(find . -name "A.*")
make
make check
make distcheck
logout