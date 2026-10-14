#!/bin/bash
# Runs inside waterfox-stereo-build (Dockerfile.build) as the calling user.
# Steps: toolchains, tree preparation, mach build, mach package.
# Usage: build-in-container.sh <src> <version> <build-date> [step...]
set -euo pipefail

src=$1 version=$2 build_date=$3
shift 3
steps=${*:-toolchains prepare build package}
here=$(cd "$(dirname "$0")" && pwd)
locales="en-US pl pt-BR fr ja ko zh-CN zh-TW"

export RUSTUP_HOME=$HOME/.rustup CARGO_HOME=$HOME/.cargo
export PATH=$CARGO_HOME/bin:$PATH
export MOZCONFIG=$here/mozconfig MOZ_BUILD_DATE=$build_date
export MOZ_NOSPAM=1 MACH_MAIN_PID=0 USER=builder LOGNAME=builder
cd "$src"

stamp() { printf '%s %s\n' "$(date -u +%FT%TZ)" "$*"; }

for step in $steps; do
	stamp "begin $step"
	case $step in
	toolchains)
		mkdir -p "$HOME/.mozbuild"
		sh scripts/fetch-clang-22.sh
		rustup toolchain install 1.98.1 --profile minimal
		rustup default 1.98.1
		rustc --version
		cargo --version
		;;
	prepare)
		# The edition's changes to Gecko and Waterfox, in order; a patch
		# already in the tree is left as it is.
		for patch in "$here"/patches/*.patch; do
			[ -e "$patch" ] || continue
			# A tree that holds the commits (the stereo3d branch) has them.
			subject=$(sed -n 's/^Subject: \[PATCH[^]]*\] //p' "$patch" | head -n 1)
			if [ -n "$subject" ] && git log --format=%s | grep -qF -- "$subject"; then
				continue
			fi
			if git apply --check "$patch" 2>/dev/null; then
				git apply "$patch"
			elif ! git apply --reverse --check "$patch" 2>/dev/null; then
				echo "patch does not apply: $patch" >&2
				exit 1
			fi
		done
		python3 "$here/l10n/brand-strings.py" waterfox/browser/locales $locales
		python3 "$here/l10n/stereo-strings.py" waterfox/browser/locales $locales
		printf '%s\n' "$version" > browser/config/version_display.txt
		rm -rf browser/branding/sparky-stereo
		cp -a browser/branding/unofficial browser/branding/sparky-stereo
		cp -a "$here/branding/." browser/branding/sparky-stereo/
		# Waterfox's build template for its branding: it installs the 22,
		# 24 and 256 pixel icons its package manifest lists.
		cp waterfox/browser/branding/moz.build \
			waterfox/browser/branding/branding-common.mozbuild \
			browser/branding/sparky-stereo/
		rm -f browser/branding/sparky-stereo/README
		;;
	build)
		./mach build
		;;
	package)
		./mach package
		./mach package-multi-locale --locales $locales
		;;
	*)
		echo "unknown step $step" >&2
		exit 2
		;;
	esac
	stamp "end $step"
done
