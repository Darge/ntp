#!/bin/sh
set -x
touch COPYRIGHT sntp/scm-rev
mkdir -p /usr/local/etc/pkg/repos

# configuring pkg
echo 'FreeBSD: {
  url: "pkg+http://pkg.FreeBSD.org/${ABI}/latest",
  mirror_type: "srv",
  enabled: yes,
}' > /usr/local/etc/pkg/repos/FreeBSD.conf 

env ASSUME_ALWAYS_YES=YES /usr/sbin/pkg bootstrap
env ASSUME_ALWAYS_YES=YES pkg install autotools 
env ASSUME_ALWAYS_YES=YES pkg install lynx
./bootstrap
./build --enable-local-libevent
cat $(find . -name "make.log")
cd $(find . -name "A.*")
make
make check
make distcheck
logout