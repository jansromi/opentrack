#!/bin/sh
# Build opentrack on macOS and install opentrack.app into <repo>/install.
#
# usage: bin/build-macos.sh [Release|Debug|RelWithDebInfo]
#
# Requires Homebrew: brew install cmake ninja qt opencv@4 imagemagick create-dmg
# OpenCV 5 (brew's "opencv") is not supported yet, tracker-easy needs calib3d.

set -e

root="$(cd "$(dirname "$0")/.." && pwd)"
build_type="${1:-Release}"
build="$root/build"
install="$root/install"
app="$install/opentrack.app"

missing=""
for f in cmake ninja qt opencv@4 imagemagick create-dmg; do
    brew list --versions "$f" >/dev/null 2>&1 || missing="$missing $f"
done
if [ -n "$missing" ]; then
    echo "missing Homebrew packages:$missing"
    echo "run: brew install$missing"
    exit 1
fi

cmake -GNinja -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_INSTALL_PREFIX="$install" \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
    -DOpenCV_DIR="$(brew --prefix opencv@4)/lib/cmake/opencv4"

# macdeployqt never overwrites libraries already in the bundle, so a
# leftover install/ would run stale opentrack libraries; start clean.
rm -rf "$install"

cmake --build "$build" --target install

# the build regenerates translation files, revert them like build-msvc.cmd does
git -C "$root" checkout -f -- ':(glob)**/lang/*.ts'
git -C "$root" clean -fq -- ':(glob)**/lang/*.ts'

# make-app-bundle.sh also tries to build a .dmg, which fails without the
# X-Plane plugin; the .app is complete by then, so drop the leftovers.
rm -f "$build"/rw.*.dmg

# macdeployqt copies dependencies into Contents/Frameworks but leaves rpaths
# to Homebrew and the install dir, so Qt, OpenCV and libomp get loaded twice
# and the app aborts. Make the libraries and plugins resolve inside the bundle.
fix_rpaths() {
    lib="$1"
    bundled="$2"
    otool -l "$lib" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' | while read -r rpath; do
        case "$rpath" in
            @*) ;;
            *) install_name_tool -delete_rpath "$rpath" "$lib" 2>/dev/null ;;
        esac
    done
    otool -l "$lib" | grep -q "path $bundled " || install_name_tool -add_rpath "$bundled" "$lib" 2>/dev/null
}
for lib in "$app/Contents/Frameworks"/*.dylib; do
    fix_rpaths "$lib" @loader_path
done
for lib in "$app/Contents/MacOS/Plugins"/*.dylib; do
    fix_rpaths "$lib" @loader_path/../../Frameworks
done

# macdeployqt invalidates the signature and macOS kills the app on launch.
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

echo
echo "built: $app"
echo "run:   open \"$app\""
