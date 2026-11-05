#!/usr/bin/env bash
# Cross-compiles opman for 64-bit Windows on Linux and builds the installer: dist/opman-<version>-windows-x64-setup.exe
# Runs itself inside `nix develop .#windows` (MinGW-w64 GCC, CMake, NSIS); everything is linked statically.
# usage: packaging/build-windows.sh [extra cmake args...]   env: BUILD_DIR (default build-win), JOBS
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
if [ -z "${OPMAN_WIN_SHELL:-}" ]; then
  exec nix develop "$root#windows" -c env OPMAN_WIN_SHELL=1 "$0" "$@"
fi

build=${BUILD_DIR:-build-win}
version=$(git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || true); version=${version#v}; version=${version:-0.1.0}
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$root/packaging/mingw-w64.cmake" "$@"
cmake --build "$build" --target opman ${JOBS:+-j "$JOBS"}

# only Windows system DLLs may be imported (no libstdc++/libgcc/winpthread DLLs)
bad=$(x86_64-w64-mingw32-objdump -p "$build/opman.exe" | sed -n 's/.*DLL Name: //p' \
  | grep -viE '^(kernel32|user32|gdi32|shell32|ole32|opengl32|advapi32|msvcrt|ucrtbase|api-ms-win-[a-z0-9-]*|imm32|dwmapi|version|ntdll|uxtheme|comctl32)\.dll$' || true)
[ -z "$bad" ] || { echo "opman.exe imports non-system DLLs: $bad" >&2; exit 1; }

stage="$build/opman"
rm -rf "$stage" && mkdir -p "$stage" dist
cp "$build/opman.exe" "$stage/" && cp -r "$build/assets" LICENSE "$stage/"
x86_64-w64-mingw32-strip "$stage/opman.exe"
out="dist/opman-$version-windows-x64-setup.exe"
makensis -V2 -DVERSION="$version" -DSTAGE="$root/$stage" -DOUTFILE="$root/$out" packaging/opman.nsi
echo "$out ($(du -h "$out" | cut -f1))"
