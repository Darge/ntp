#!/bin/sh
set -x
touch COPYRIGHT sntp/scm-rev
pgk install autotools
/usr/sbin/pkg
./bootstrap
./build --enable-local-libevent
cat $(find . -name "make.log")
cd $(find . -name "A.*")
make
make check
make distcheck
logout