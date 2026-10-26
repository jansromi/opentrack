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

# macdeployqt invalidates the signature and macOS kills the app on launch.
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

echo
echo "built: $app"
echo "run:   open \"$app\""
