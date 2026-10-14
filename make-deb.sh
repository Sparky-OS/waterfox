#!/bin/bash
# Runs inside waterfox-stereo-build: turns the packaged browser tree from
# ./mach package into the .deb, with debhelper.
# Usage: make-deb.sh <src> <outdir> <waterfox-version>
set -euo pipefail

src=$1 out=$2 version=$3
here=$(cd "$(dirname "$0")" && pwd)
pkg=sparky-stereo-browser
revision=${REVISION:-0+stereo3d5}
debversion=$version-$revision
gecko=$(tail -n 1 "$src/config/milestone.txt")
dist=$src/obj-x86_64-pc-linux-gnu/dist

tarball=$(ls "$dist"/$pkg-*.linux-x86_64.tar.* | head -n 1)
work=$(dirname "$out")/deb
rm -rf "$work"
mkdir -p "$work/$pkg-$version" "$out"
tar -xf "$tarball" -C "$work/$pkg-$version"
cp -a "$here/debian" "$work/$pkg-$version/debian"
sed -i -e "s/@DEBVERSION@/$debversion/" -e "s/@VERSION@/$version/" \
	-e "s/@GECKO@/$gecko/" -e "s/@DATE@/$(date -R -u -d @"${SOURCE_DATE_EPOCH:-$(date +%s)}")/" \
	"$work/$pkg-$version/debian/changelog"
cd "$work/$pkg-$version"
dpkg-buildpackage -b -us -uc
cd "$work"
cp -a ./*.deb ./*.buildinfo ./*.changes "$out/"
ls -l "$out"
