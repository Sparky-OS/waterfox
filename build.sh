#!/bin/bash
# Build Sparky Stereo OS's package of Waterfox in a Debian testing container.
# Usage: build.sh [step...]   (steps: image toolchains prepare build package deb)
# The tree, toolchains and object directory live under $WORK (root NVMe).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/var/tmp/waterfox-stereo}
SRC=$WORK/src
VERSION=${VERSION:-6.7.5}
BUILD_DATE=${BUILD_DATE:-20260929212553}
IMAGE=${IMAGE:-waterfox-stereo-build:m1}
steps=${*:-image toolchains prepare build package deb}

run() {
	docker run --rm --user "$(id -u):$(id -g)" --cpus 4 --memory 32g \
		-e HOME="$WORK/home" -e TZ=UTC \
		-v "$WORK:$WORK" -v "$here:$here:ro" -w "$SRC" \
		"$IMAGE" "$@"
}

mkdir -p "$WORK/home" "$WORK/logs"
for step in $steps; do
	case $step in
	image)
		docker build -t "$IMAGE" -f "$here/Dockerfile.build" "$here"
		;;
	deb)
		# Built in the bench's deb-build stage (tests/Dockerfile.stereo):
		# debhelper and the libraries dh_shlibdeps resolves the browser
		# against.
		IMAGE=${DEB_IMAGE:-waterfox-stereo-deb-build:m3} run bash "$here/make-deb.sh" "$SRC" "$WORK/out" "$VERSION"
		;;
	*)
		run bash "$here/build-in-container.sh" "$SRC" "$VERSION" "$BUILD_DATE" "$step"
		;;
	esac 2>&1 | tee "$WORK/logs/$step.log"
done
