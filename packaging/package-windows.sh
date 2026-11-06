#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================
# package-windows.sh — carto2D and render2d as one folder that runs on a
# Windows PC without MSYS2 (notions §79, step 7e). In the MSYS2 UCRT64
# terminal, from carto2D's folder:
#
#     packaging/package-windows.sh
#
# 1. builds carto2D (make release) and render2d alone, both in Release
# 2. copies carto2d.exe, render2d.exe and every DLL they need from /ucrt64/bin
# 3. copies GDAL's and PROJ's reference files into share/
# 4. copies render2d's styles, basemaps and symbol library, carto2D's notes,
#    the demonstration project, README, LICENSE and every licence
# 5. checks that both start WITHOUT MSYS2 in the PATH, and that render2d draws
# 6. zips it: dist/carto2D-<version>-windows-x64.zip (the portable version)
# 7. when Inno Setup 6 is installed, builds the installer from the folder
#    (packaging/carto2d.iss): dist/carto2D-<version>-setup.exe
#
# The folder dist/carto2D-<version>-windows-x64/ is what the installer
# (packaging/carto2d.iss, Inno Setup) puts on the user's computer. The same
# script runs on GitHub's Windows machine at every tag (build.yml).
# Modelled on render2d's tools/package-windows.sh (its notes §68).
# =============================================================================

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

prefix="${MINGW_PREFIX:-/ucrt64}"
if [[ ! -d "$prefix/bin" ]]; then
    echo "MSYS2 UCRT64 not found ($prefix): run this in the 'MSYS2 UCRT64' terminal" >&2
    exit 2
fi

# render2d: the submodule, else the working copy beside (as CMakeLists.txt).
if [[ -f render2d/render2d/CMakeLists.txt ]]; then r2d="$root/render2d/render2d"
else r2d="$(cd "$root/../render2d/render2d" && pwd)"; fi

version=$(grep -m1 -oE 'kVersion *= *"[0-9.]+"' src/Version.h | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
name="carto2D-$version-windows-x64"
dist="$root/dist"
out="$dist/$name"

step() { echo; echo "== $*"; }

# ---- 1. the two programs, Release ------------------------------------------------
step "1. build: carto2D (make release), render2d alone (from $r2d)"
make release > /dev/null
cmake -S "$r2d" -B build-release/render2d-alone -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build-release/render2d-alone --parallel > /dev/null
carto="$root/build-release/carto2d.exe"
render="$root/build-release/render2d-alone/render2d.exe"
for exe in "$carto" "$render"; do
    [[ -x "$exe" ]] || { echo "no $exe after the build" >&2; exit 1; }
done
"$carto" --version | head -1
"$render" --version | head -1

rm -rf "$out" "$dist/$name.zip"
mkdir -p "$out"
cp "$carto" "$render" "$out/"

# ---- 2. the DLLs ----------------------------------------------------------------
# ldd lists everything an executable loads, the DLLs of the DLLs included.
# Only those of MSYS2 are copied: Windows' own (kernel32, opengl32...) are on
# every PC. The two programs share most of them: one copy each.
step "2. DLLs from $prefix/bin"
mapfile -t dlls < <( { ldd "$carto"; ldd "$render"; } | grep -oE "$prefix/bin/[^ ]+\.dll" | sort -u)
for dll in "${dlls[@]}"; do
    cp "$dll" "$out/"
done
echo "${#dlls[@]} DLL(s)"

# ---- 3. GDAL and PROJ reference files ---------------------------------------------
step "3. share/gdal and share/proj"
mkdir -p "$out/share"
cp -r "$prefix/share/gdal" "$out/share/"
mkdir -p "$out/share/proj"
# proj.db and the small files; not the big transformation grids (*.tif),
# downloaded by PROJ on demand when one is ever needed.
find "$prefix/share/proj" -maxdepth 1 -type f ! -name '*.tif' -exec cp {} "$out/share/proj/" \;
[[ -f "$out/share/proj/proj.db" ]] || { echo "proj.db not found in $prefix/share/proj" >&2; exit 1; }

# ---- 4. data, documents, licences -------------------------------------------------
step "4. styles, basemaps, symbols, notes, demonstration, licences"
cp -r "$r2d/styles" "$r2d/basemaps" "$r2d/symbols" "$out/"   # render2d's, shared by both (§78)
mkdir -p "$out/docs"
cp docs/notions.en.html "$out/docs/"                          # Help ▸ carto2D notes (§71)
cp -r docs/img "$out/docs/"
cp packaging/carto2d.yaml "$out/"                             # opened at start (§71): the demonstration
cp LICENSE "$out/LICENSE.txt"

# The libraries' licences: the MSYS2 package of each DLL and its licence
# folder (PACKAGES.txt lists them with their versions), then the code
# compiled into the programs themselves.
mkdir -p "$out/licenses"
: > "$out/licenses/PACKAGES.txt"
declare -A seen=()
for dll in "${dlls[@]}"; do
    pkg=$(pacman -Qqo "$dll" 2>/dev/null || true)
    [[ -z "$pkg" || -n "${seen[$pkg]:-}" ]] && continue
    seen[$pkg]=1
    pacman -Q "$pkg" >> "$out/licenses/PACKAGES.txt"
    if [[ -d "$prefix/share/licenses/${pkg#mingw-w64-ucrt-x86_64-}" ]]; then
        cp -r "$prefix/share/licenses/${pkg#mingw-w64-ucrt-x86_64-}" "$out/licenses/"
    fi
done
compiled() {   # name, licence file, what
    mkdir -p "$out/licenses/$1"
    cp "$2" "$out/licenses/$1/"
    echo "$1 (compiled in: $3)" >> "$out/licenses/PACKAGES.txt"
}
deps="$root/build-release/_deps"
compiled imgui "$deps/imgui-src/LICENSE.txt" "Dear ImGui, MIT"
compiled glfw "$deps/glfw-src/LICENSE.md" "GLFW, zlib"
compiled nlohmann-json "$deps/json-src/LICENSE.MIT" "JSON for Modern C++, MIT"
mkdir -p "$out/licenses/nanosvg" "$out/licenses/glad"
awk '{ print } /\*\// { exit }' "$r2d/third_party/nanosvg/nanosvg.h" > "$out/licenses/nanosvg/LICENSE.txt"
echo "nanosvg (compiled in: render2d's SVG symbols, zlib)" >> "$out/licenses/PACKAGES.txt"
head -20 third_party/glad/include/glad/gl.h > "$out/licenses/glad/NOTICE.txt"
echo "glad (compiled in: the OpenGL loader, WTFPL or CC0, Khronos definitions Apache-2.0)" >> "$out/licenses/PACKAGES.txt"
echo "Maki and Temaki (the glyphs of symbols/, CC0: symbols/SOURCES.txt)" >> "$out/licenses/PACKAGES.txt"
echo "$(wc -l < "$out/licenses/PACKAGES.txt") component(s), licences in licenses/"

cat > "$out/README.txt" <<EOF
carto2D $version for Windows (64-bit)
=====================================

See, query, measure and print maps. https://github.com/nfr25/carto2d
With render2d, the map renderer it is built on: https://github.com/nfr25/render2d

carto2d.exe     the program; it opens carto2d.yaml, the demonstration
                (Brussels on OpenStreetMap: an Internet connection is needed)
render2d.exe    the same maps from a command prompt: render2d --help
symbols\\        the symbol library; your own symbols go in symbols\\user\\
styles\\         example styles; basemaps\\  ready web basemaps (drop one on the map)
docs\\           the design notes (Help > carto2D notes)

Everything both programs need is in this folder: the DLLs, and GDAL's and
PROJ's reference files in share\\. Nothing else to install.

Licence: GNU AGPL v3 or later (LICENSE.txt). The libraries included keep
their own licences (licenses\\, PACKAGES.txt lists them with their versions).
Map data in the demonstration: (c) OpenStreetMap contributors (ODbL).
EOF
sed -i 's/$/\r/' "$out/README.txt"   # Windows line endings, for Notepad

# ---- 5. the check: without MSYS2 in the PATH ---------------------------------------
# Only Windows' own folders in the PATH: every DLL must come from our folder.
# carto2d --version opens no window, but Windows loads all its DLLs first:
# a missing one and it does not start.
step "5. check, without MSYS2 in the PATH"
winpath="/c/Windows/System32:/c/Windows"
run() { env -i PATH="$winpath" SYSTEMROOT="${SYSTEMROOT:-C:\\Windows}" "$@"; }
check=$(mktemp -d)
cat > "$check/square.geojson" <<'EOF'
{"type":"FeatureCollection",
 "crs":{"type":"name","properties":{"name":"urn:ogc:def:crs:EPSG::31370"}},
 "features":[{"type":"Feature","properties":{},
   "geometry":{"type":"Polygon","coordinates":[[[148000,170000],[149000,170000],[149000,171000],[148000,171000],[148000,170000]]]}}]}
EOF
(
    cd "$check"
    run "$out/carto2d.exe" --version
    run "$out/render2d.exe" --version
    run "$out/render2d.exe" square.geojson --size 200 -o square.png > report.txt 2>&1
) || { echo "a program does not start without MSYS2: a DLL is missing?" >&2; exit 1; }
grep -q 'EPSG:31370' "$check/report.txt" || { cat "$check/report.txt"; echo "the CRS was not recognised: is share/proj/proj.db found?" >&2; exit 1; }
[[ -s "$check/square.png" ]] || { echo "render2d wrote no image" >&2; exit 1; }
rm -rf "$check"
echo "OK: both start, render2d draws, without MSYS2"

# ---- 6. the zip --------------------------------------------------------------------
step "6. zip"
cd "$dist"
if command -v zip > /dev/null; then
    zip -qr "$name.zip" "$name"
else
    powershell.exe -NoProfile -Command "Compress-Archive -Path '$name' -DestinationPath '$name.zip'"
fi

# ---- 7. the installer (Inno Setup) ------------------------------------------------
step "7. installer"
setup="carto2D-$version-setup.exe"
rm -f "$setup"
iscc=$(command -v iscc || command -v ISCC || true)
for c in "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe" \
         "$(cygpath -u "${LOCALAPPDATA:-C:\\}" 2>/dev/null)/Programs/Inno Setup 6/ISCC.exe"; do
    [[ -z "$iscc" && -x "$c" ]] && iscc="$c"
done
if [[ -n "$iscc" ]]; then
    # MSYS2 would turn "/DAppVersion=..." into a path: no conversion for this call.
    MSYS2_ARG_CONV_EXCL='*' "$iscc" /Qp "/DAppVersion=$version" "$(cygpath -w "$root/packaging/carto2d.iss")"
    [[ -s "$setup" ]] || { echo "Inno Setup wrote no $setup" >&2; exit 1; }
else
    echo "Inno Setup 6 not found (ISCC.exe): no installer, the folder and the zip only."
    echo "To get it: https://jrsoftware.org/isinfo.php, or: winget install JRSoftware.InnoSetup"
fi

echo
echo "dist/$name/      $(du -sm "$name" | cut -f1) MB, $(find "$name" -type f | wc -l) files"
echo "dist/$name.zip  $(du -m "$name.zip" | cut -f1) MB"
[[ -s "$setup" ]] && echo "dist/$setup  $(du -m "$setup" | cut -f1) MB"
true
