#!/bin/sh
set -x
touch COPYRIGHT sntp/scm-rev
env ASSUME_ALWAYS_YES=YES /usr/sbin/pkg bootstrap
env ASSUME_ALWAYS_YES=YES pkg install autotools 
./bootstrap
./build --enable-local-libevent
cat $(find . -name "make.log")
cd $(find . -name "A.*")
make
make check
make distcheck
logout