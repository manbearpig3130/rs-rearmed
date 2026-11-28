#!/bin/bash
# The Bink library's FFmpeg for Linux: the same minimal static FFmpeg, with the same Bink 2 patch, that
# shims/bink/build-ffmpeg.sh builds for the Mac. The version and its checksum are read from that script, so the two
# cannot differ.
#
#   linux/bink/build-ffmpeg.sh <prefix>
#
# Leaves <prefix>/{include,lib/libavformat.a,libavcodec.a,libavutil.a}. Only what the Bink library needs is
# enabled: the bink demuxer, the bink and bink2 video decoders and both Bink audio decoders; no protocols, no
# programs, no assembly (Bink is cheap to decode, and nasm is one tool less to ask for).
# Needs curl, tar with xz, patch, make and a C compiler.
set -euo pipefail

mkdir -p "$1"
prefix=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
mac="$here/../../shims/bink/build-ffmpeg.sh"
version=$(sed -n 's/^version=//p' "$mac")
sha256=$(sed -n 's/^sha256=//p' "$mac")
patch="$here/../../shims/bink/third_party/ffmpeg-bink2.patch"
[ -n "$version" ] && [ -n "$sha256" ] || { echo "build-ffmpeg: no version or checksum in $mac" >&2; exit 1; }

if [ -f "$prefix/lib/libavcodec.a" ] && [ "$prefix/.stamp" -nt "$patch" ]; then
  echo "ffmpeg: up to date"
  exit 0
fi
for tool in curl tar patch make; do
  command -v "$tool" >/dev/null || { echo "build-ffmpeg: $tool is needed" >&2; exit 1; }
done

mkdir -p "$prefix/src"
tarball="$prefix/src/ffmpeg-$version.tar.xz"
if [ ! -f "$tarball" ]; then
  curl -fL --retry 3 --retry-all-errors -o "$tarball.part" "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"
  mv "$tarball.part" "$tarball"
fi
echo "$sha256  $tarball" | sha256sum -c - >/dev/null

src="$prefix/src/ffmpeg-$version"
rm -rf "$src" "$prefix/lib" "$prefix/include"
mkdir -p "$src"
tar -xf "$tarball" -C "$src" --strip-components=1
patch -d "$src" -p1 --quiet < "$patch"
(cd "$src" && ./configure \
  --prefix="$prefix" --cc="${CC:-cc}" --extra-cflags=-fPIC \
  --enable-static --disable-shared --enable-pic --disable-x86asm \
  --disable-programs --disable-doc --disable-network --disable-autodetect \
  --disable-everything --disable-avdevice --disable-avfilter \
  --disable-swscale --disable-swresample --disable-postproc \
  --enable-demuxer=bink \
  --enable-decoder=bink,bink2,binkaudio_dct,binkaudio_rdft >/dev/null)
make -C "$src" -j"$(nproc)" >"$prefix/src/build.log" 2>&1 || { tail -30 "$prefix/src/build.log" >&2; exit 1; }
make -C "$src" install >/dev/null
touch "$prefix/.stamp"
echo "ffmpeg $version: installed to $prefix"
