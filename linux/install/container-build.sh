#!/bin/sh
# The packaged Linux edition built in a container of an old system (linux/install/Containerfile), so that the
# AppImage runs on that system and on everything newer:
#
#   linux/install/container-build.sh            ->  build/linux-release/RS_Re-ARMed-<version>-x86_64.AppImage
#
# It needs podman or docker (CONTAINER_ENGINE names another), and the network for the first run: the base image,
# its packages, SDL 3's source and, by the build itself, FFmpeg's source and appimagetool, each a fixed release
# checked against its SHA-256. The source tree is the one this script is in, used as it is; the build's files go to
# build/linux-release beside the others. At the end it says which C library the result asks for.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
engine=${CONTAINER_ENGINE:-$(command -v podman >/dev/null 2>&1 && echo podman || echo docker)}
image=rs-rearmed-linux-build
out=build/linux-release
"$engine" build -t "$image" -f "$root/linux/install/Containerfile" "$root/linux/install"
# (label=disable: a system with SELinux lets the container read the tree without relabelling it)
"$engine" run --rm --security-opt label=disable -v "$root":/src -w /src/linux "$image" \
    make OUT=../$out all gl bink check appimage
"$engine" run --rm --security-opt label=disable -v "$root":/src -w /src "$image" sh -c '
    need=$(for f in '"$out"'/AppDir/usr/lib/rs-rearmed/*; do objdump -T "$f" 2>/dev/null; done | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)
    echo "the packaged build asks for $need at most; this system has $(ldd --version | head -1 | grep -o "[0-9.]*$")"'
ls -l "$root/$out"/*.AppImage
