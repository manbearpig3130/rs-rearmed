#!/bin/sh
# The AppImage of a packaged build: appimage.sh APPDIR OUT.AppImage TOOLS
#
# appimagetool and the runtime it puts in front of the image are downloaded once into TOOLS, each a fixed release
# checked against its SHA-256 (as the FFmpeg source is, shims/bink). appimagetool is an AppImage itself, and is run
# unpacked: a build container has no FUSE.
set -eu
appdir=$1 out=$2 tools=$3
tool_url=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
tool_sha=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
runtime_url=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
runtime_sha=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

fetch() {  # URL SHA256 FILE
    if [ ! -f "$3" ] || [ "$(sha256sum "$3" | cut -d' ' -f1)" != "$2" ]; then
        echo "downloading $1"
        curl -fL --retry 3 -o "$3.part" "$1"
        got=$(sha256sum "$3.part" | cut -d' ' -f1)
        [ "$got" = "$2" ] || { echo "appimage: $1 is not the file expected (SHA-256 $got)" >&2; rm -f "$3.part"; exit 1; }
        mv "$3.part" "$3"
    fi
}
mkdir -p "$tools"
fetch "$tool_url" "$tool_sha" "$tools/appimagetool-x86_64.AppImage"
fetch "$runtime_url" "$runtime_sha" "$tools/runtime-x86_64"
chmod +x "$tools/appimagetool-x86_64.AppImage"
rm -f "$out"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tools/appimagetool-x86_64.AppImage" --no-appstream \
    --runtime-file "$tools/runtime-x86_64" "$appdir" "$out"
echo "made $out ($(du -h "$out" | cut -f1))"
