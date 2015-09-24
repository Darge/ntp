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

time env ASSUME_ALWAYS_YES=YES /usr/sbin/pkg bootstrap
time env ASSUME_ALWAYS_YES=YES pkg install autotools 
time env ASSUME_ALWAYS_YES=YES pkg install lynx
time ./bootstrap
#./build --enable-local-libevent
mkdir A.builddir
cd A.builddir
time ../configure
time make
time make distcheck
exit
