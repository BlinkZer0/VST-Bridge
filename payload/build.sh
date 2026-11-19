#!/usr/bin/env bash
# Builds the VST gHost Wine/Box64 payload. Runs on Ubuntu 24.04 x86_64 (WSL2 or Docker);
# install dependencies with payload/setup-ubuntu.sh first.
#
# Output (payload/out/, git-ignored):
#   vstghost-payload.tar.zst          the payload (both editions)
#   vstghost-payload.tar.zst.sha256
#   jnilibs/arm64-v8a/*.so            Play edition: arm64 loader, Box64 and glibc as lib*.so
#   play-assets/                      Play edition: payload + soname map, bundled as APK assets
#
# Usage: payload/build.sh [step...]   steps: glibc box64 wine mesa x64libs prefix hello host etc pack play (default: all)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$HERE")"
# shellcheck source=versions.env
source "$HERE/versions.env"

# Build trees live on the Linux filesystem: building Wine on /mnt/c (WSL) is very slow.
CACHE="${VG_CACHE:-$HOME/.cache/vstghost-payload}"
WORK="${VG_WORK:-$HOME/vstghost-payload-work}"
OUT="${VG_OUT:-$HERE/out}"
STAGE="$WORK/stage"
JOBS="${JOBS:-$(nproc)}"

# Build-time install prefix, rewritten on the device to the real path (see app Payload.kt).
# "/vstghost_root" + 50 underscores = 64 chars; must match Payload.PLACEHOLDER.
PLACEHOLDER="/vstghost_root$(printf '_%.0s' $(seq 1 50))"
[ "${#PLACEHOLDER}" -eq 64 ] || { echo "placeholder length wrong" >&2; exit 1; }

log() { printf '\n=== %s\n' "$*"; }

fetch() {  # url sha256 filename
    local url="$1" sha="$2" name="$3"
    mkdir -p "$CACHE"
    if [ ! -f "$CACHE/$name" ]; then
        curl -fL --retry 3 -o "$CACHE/$name.part" "$url"
        mv "$CACHE/$name.part" "$CACHE/$name"
    fi
    echo "$sha  $CACHE/$name" | sha256sum -c - >/dev/null || {
        echo "SHA-256 mismatch for $name (expected $sha)" >&2; exit 1; }
    echo "$CACHE/$name"
}

# arm64 glibc libraries Box64 needs at runtime (sonames).
GLIBC_LIBS=(ld-linux-aarch64.so.1 libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1
            libresolv.so.2 libutil.so.1 libanl.so.1 libmvec.so.1 libnss_files.so.2)
GCC_LIBS=(libstdc++.so.6 libgcc_s.so.1 libatomic.so.1)

step_glibc() {
    local g c d="$WORK/glibc-pkg"
    if [ -n "${TERMUX_GLIBC_LOCAL:-}" ]; then
        # Built from source by payload/build-glibc-src.sh (same recipe as the prebuilt packages).
        log "glibc (from source: $TERMUX_GLIBC_LOCAL)"
        g="$TERMUX_GLIBC_LOCAL"; c="$TERMUX_GCCLIBS_LOCAL"
        [ -f "$g" ] && [ -f "$c" ] || { echo "TERMUX_GLIBC_LOCAL / TERMUX_GCCLIBS_LOCAL not found" >&2; exit 1; }
    else
        log "glibc (Termux-pacman ${TERMUX_GLIBC_FILE})"
        g="$(fetch "$TERMUX_GPKG_REPO/$TERMUX_GLIBC_FILE" "$TERMUX_GLIBC_SHA256" "$TERMUX_GLIBC_FILE")"
        c="$(fetch "$TERMUX_GPKG_REPO/$TERMUX_GCCLIBS_FILE" "$TERMUX_GCCLIBS_SHA256" "$TERMUX_GCCLIBS_FILE")"
    fi
    rm -rf "$d" && mkdir -p "$d"
    tar -xJf "$g" -C "$d" data
    tar -xJf "$c" -C "$d" data
    local src="$d/data/data/com.termux/files/usr/glibc/lib"
    rm -rf "$STAGE/glibc" && mkdir -p "$STAGE/glibc/lib"
    for l in "${GLIBC_LIBS[@]}" "${GCC_LIBS[@]}"; do
        cp -L "$src/$l" "$STAGE/glibc/lib/$l"
        aarch64-linux-gnu-strip --strip-debug "$STAGE/glibc/lib/$l"
    done
    file "$STAGE/glibc/lib/libc.so.6"
}

step_box64() {
    log "Box64 ${BOX64_VERSION}"
    local t src="$WORK/box64-src" b="$WORK/box64-build"
    t="$(fetch "$BOX64_URL" "$BOX64_SHA256" "box64-${BOX64_VERSION}.tar.gz")"
    rm -rf "$src" "$b" && mkdir -p "$src"
    tar -xzf "$t" -C "$src" --strip-components=1
    # -DSD8EG5 passes -mtune=oryon-1, which GCC 14.2 does not know (despite Box64's comment; it
    # arrived in GCC 15). -mtune only affects scheduling, so drop it and keep the SD8EG5 -march set.
    grep -q -- '-mtune=oryon-1' "$src/CMakeLists.txt"
    sed -i 's/ -mtune=oryon-1//g' "$src/CMakeLists.txt"
    local cc=aarch64-linux-gnu-gcc-14
    command -v "$cc" >/dev/null || { echo "$cc not found: re-run payload/setup-ubuntu.sh" >&2; exit 1; }
    cmake -S "$src" -B "$b" -G Ninja \
        $BOX64_CPU_FLAG -DWINLATOR_GLIBC=1 -DNOGIT=1 \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER="$cc" \
        -DCMAKE_ASM_COMPILER="$cc" \
        -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--dynamic-linker=$PLACEHOLDER/glibc/lib/ld-linux-aarch64.so.1"
    cmake --build "$b" -j "$JOBS"
    rm -rf "$STAGE/box64" && mkdir -p "$STAGE/box64/bin" "$STAGE/box64/x64lib"
    cp "$b/box64" "$STAGE/box64/bin/box64"
    aarch64-linux-gnu-strip --strip-debug "$STAGE/box64/bin/box64"
    if [ -d "$src/x64lib" ]; then cp -a "$src/x64lib/." "$STAGE/box64/x64lib/"; else echo "note: no x64lib in Box64 source"; fi
    local interp
    interp="$(aarch64-linux-gnu-readelf -l "$STAGE/box64/bin/box64" | sed -n 's/.*interpreter: \(.*\)]/\1/p')"
    echo "box64 PT_INTERP: $interp"
    [ "$interp" = "$PLACEHOLDER/glibc/lib/ld-linux-aarch64.so.1" ] || { echo "unexpected PT_INTERP" >&2; exit 1; }
}

step_wine() {
    log "Wine ${WINE_VERSION} (x86_64 only; Wayland driver for plugin editors, FreeType for fonts)"
    local t src="$WORK/wine-src" b="$WORK/wine-build" inst="$WORK/wine-install"
    t="$(fetch "$WINE_URL" "$WINE_SHA256" "wine-${WINE_VERSION}.tar.xz")"
    rm -rf "$src" && mkdir -p "$src"
    tar -xJf "$t" -C "$src" --strip-components=1
    for p in "$HERE"/patches/wine/*.patch; do
        echo "applying $(basename "$p")"
        patch -d "$src" -p1 --forward < "$p"
    done
    rm -rf "$b" "$inst" && mkdir -p "$b"
    (cd "$b" && "$src/configure" \
        --prefix=/opt/vstghost-wine \
        --enable-archs=x86_64 --disable-tests \
        --without-x --with-wayland --with-opengl --without-vulkan --without-sdl \
        --with-freetype --without-fontconfig \
        --without-pulse --without-alsa --without-oss --without-gstreamer --without-ffmpeg \
        --without-cups --without-dbus --without-sane --without-gphoto --without-v4l2 \
        --without-usb --without-udev --without-pcap --without-pcsclite --without-capi \
        --without-gnutls --without-krb5 --without-netapi --without-gssapi \
        --without-unwind --without-xinput --without-xinput2)
    make -C "$b" -j "$JOBS"
    make -C "$b" install DESTDIR="$inst"
    rm -rf "$STAGE/wine" && cp -a "$inst/opt/vstghost-wine" "$STAGE/wine"
    rm -rf "$STAGE/wine/share/man" "$STAGE/wine/share/applications" "$STAGE/wine/include"
    # Strip unix-side ELF and PE modules.
    find "$STAGE/wine" -type f \( -name '*.so' -o -perm -u+x \) -exec sh -c \
        'for f; do case "$(file -b "$f")" in ELF*) strip --strip-unneeded "$f";; PE32+*) x86_64-w64-mingw32-strip --strip-unneeded "$f";; esac; done' sh {} +
    find "$STAGE/wine/lib/wine/x86_64-windows" -type f -exec x86_64-w64-mingw32-strip --strip-unneeded {} + 2>/dev/null || true
    "$STAGE/wine/bin/wine" --version
    echo "x86_64 unix-side libraries Wine links (Box64 must wrap or find these):"
    find "$STAGE/wine/lib/wine/x86_64-unix" -name '*.so' -exec objdump -p {} \; | awk '/NEEDED/{print $2}' | sort | uniq -c
}

# Mesa, x86_64, run by Box64 like Wine: OpenGL through EGL on Wayland (our compositor), rendered in
# software. llvmpipe (LLVM linked statically, only the parts Mesa uses) is needed for the Direct3D 10/11
# feature level that Direct2D (JUCE 8 editors) requires; softpipe stays as a fallback. Plugin editors
# that draw with OpenGL need it too (Surge XT, DPF plugins, ...).
step_mesa() {
    log "Mesa ${MESA_VERSION} (x86_64, software GL, EGL on Wayland)"
    local t src="$WORK/mesa-src" b="$WORK/mesa-build" inst="$WORK/mesa-install"
    t="$(fetch "$MESA_URL" "$MESA_SHA256" "mesa-${MESA_VERSION}.tar.xz")"
    rm -rf "$src" "$b" "$inst" && mkdir -p "$src"
    tar -xJf "$t" -C "$src" --strip-components=1
    printf "[binaries]\nllvm-config = '%s'\n" "${LLVM_CONFIG:-/usr/lib/llvm-18/bin/llvm-config}" > "$WORK/mesa-native.ini"
    meson setup "$b" "$src" --native-file "$WORK/mesa-native.ini" --prefix=/opt/vstghost-mesa --libdir=lib \
        --buildtype=release -Dstrip=true -Db_asneeded=true \
        -Dplatforms=wayland -Dgallium-drivers=llvmpipe,softpipe -Dvulkan-drivers= -Dglx=disabled -Degl=enabled \
        -Dgbm=disabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true -Dglvnd=disabled \
        -Dllvm=enabled -Dshared-llvm=disabled \
        -Dshared-glapi=enabled -Dzstd=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled \
        -Dbuild-tests=false -Dvideo-codecs= -Dgallium-va=disabled -Dgallium-vdpau=disabled -Dgallium-xa=disabled \
        -Dgallium-nine=false -Dgallium-opencl=disabled -Dosmesa=false -Dexpat=enabled -Dshader-cache=disabled
    ninja -C "$b"
    DESTDIR="$inst" ninja -C "$b" install
    find "$inst/opt/vstghost-mesa/lib" -maxdepth 2 \( -name '*.so*' -o -name '*.json' \) | sort
}

# x86_64 libraries the Wine unix side needs at runtime (run emulated by Box64; see BOX64_EMULATED_LIBS
# in app/.../Payload.kt). Copied from the build host's Ubuntu 24.04 packages, which Wine was compiled
# against; exact package versions go into x64libs/MANIFEST. libxkbregistry is a stub (payload/stubs).
X64LIBS=(libwayland-client.so.0 libffi.so.8 libxkbcommon.so.0
         libfreetype.so.6 libpng16.so.16 libz.so.1 libbz2.so.1.0 libbrotlidec.so.1 libbrotlicommon.so.1)
step_x64libs() {
    log "x86_64 runtime libraries for Wine (Wayland client, xkbcommon, FreeType)"
    local d="$STAGE/x64libs" l real pkg
    rm -rf "$d" && mkdir -p "$d"
    : > "$d/MANIFEST"
    for l in "${X64LIBS[@]}"; do
        real="$(readlink -f "/usr/lib/x86_64-linux-gnu/$l")"
        [ -f "$real" ] || { echo "missing $l (see payload/setup-ubuntu.sh)"; exit 1; }
        cp "$real" "$d/$l"
        strip --strip-unneeded "$d/$l"
        pkg="$(dpkg -S "$real" 2>/dev/null | head -1 | cut -d: -f1-2)"
        printf '%s\t%s\t%s\n' "$l" "$pkg" "$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null)" >> "$d/MANIFEST"
    done
    # Mesa (step_mesa): EGL + GLES + its gallium megadriver and DRI stubs, plus what they link.
    local m="$WORK/mesa-install/opt/vstghost-mesa/lib"
    if [ -d "$m" ]; then
        cp -a "$m"/libEGL.so* "$m"/libGLESv2.so* "$m"/libgallium-*.so "$d/"  # (Mesa 25: glapi is inside libgallium)
        [ -d "$m/dri" ] && cp -a "$m/dri" "$d/dri"
        for l in libdrm.so.2 libexpat.so.1 libwayland-server.so.0 libwayland-egl.so.1 libzstd.so.1 libtinfo.so.6; do
            real="$(readlink -f "/usr/lib/x86_64-linux-gnu/$l")"
            cp "$real" "$d/$l" && strip --strip-unneeded "$d/$l"
            pkg="$(dpkg -S "$real" 2>/dev/null | head -1 | cut -d: -f1-2)"
            printf '%s\t%s\t%s\n' "$l" "$pkg" "$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null)" >> "$d/MANIFEST"
        done
        printf 'mesa\t%s\t%s\n' "$MESA_VERSION" "$MESA_SHA256" >> "$d/MANIFEST"
    else
        echo "note: no Mesa build (step mesa); OpenGL editors will not work"
    fi
    gcc -shared -fPIC -O2 -s -Wl,-soname,libxkbregistry.so.0 -Wl,--version-script="$HERE/stubs/xkbregistry_stub.map" \
        "$HERE/stubs/xkbregistry_stub.c" -o "$d/libxkbregistry.so.0"
    printf 'libxkbregistry.so.0\tstub\tpayload/stubs/xkbregistry_stub.c\n' >> "$d/MANIFEST"
    cat "$d/MANIFEST"
    # Every library a Wine unix module needs must be shipped here, in Box64's x64lib, or be glibc (wrapped natively).
    local miss=0 n
    for n in $(find "$STAGE/wine/lib/wine/x86_64-unix" -name '*.so' -exec objdump -p {} \; | awk '/NEEDED/{print $2}' | sort -u) \
             $(objdump -p "$d"/*.so* | awk '/NEEDED/{print $2}' | sort -u); do
        case "$n" in ntdll.so|win32u.so|libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|ld-linux-x86-64.so.2) continue;; esac
        [ -f "$d/$n" ] || [ -f "$STAGE/box64/x64lib/$n" ] || { echo "NOT SHIPPED: $n"; miss=1; }
    done
    [ "$miss" = 0 ] || exit 1
    du -sh "$d"
}

step_prefix() {
    log "WINEPREFIX (created natively on the build host)"
    rm -rf "$STAGE/prefix"
    # Same user name as on the device, so drive_c/users/<name> matches.
    env -i PATH="$STAGE/wine/bin:/usr/bin:/bin" HOME="$WORK/home" USER=vstghost \
        WINEPREFIX="$STAGE/prefix" WINEDEBUG=-all \
        WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" \
        "$STAGE/wine/bin/wine" wineboot -i
    # Graphics: our in-app Wayland compositor (plugin editors, Phase 4) when it is reachable, else
    # Wine's "null" driver, which still lets apps create (invisible) windows. With no driver at all,
    # nodrv_CreateWindow refuses every non-message window, e.g. JUCE's hidden message window.
    # (The build host has no compositor at $WAYLAND_DISPLAY here, so wineboot itself uses null.)
    env -i PATH="$STAGE/wine/bin:/usr/bin:/bin" HOME="$WORK/home" USER=vstghost \
        WINEPREFIX="$STAGE/prefix" WINEDEBUG=-all \
        "$STAGE/wine/bin/wine" reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d wayland,null /f
    # Fonts: Wine ships Tahoma (+ Marlett, Symbol), not Windows' UI fonts. DirectWrite and GDI then
    # find no family for e.g. Verdana (JUCE 9's default) and draw no text at all; map them to Tahoma.
    local f
    for f in "Verdana" "Segoe UI" "Segoe UI Variable" "Arial" "Helvetica" "Calibri" "Microsoft Sans Serif" \
             "MS Sans Serif" "MS Shell Dlg" "MS Shell Dlg 2" "Consolas" "Courier New" "Lucida Console" \
             "Lucida Grande" "Trebuchet MS" "Inter" "Roboto"; do
        env -i PATH="$STAGE/wine/bin:/usr/bin:/bin" HOME="$WORK/home" USER=vstghost \
            WINEPREFIX="$STAGE/prefix" WINEDEBUG=-all \
            "$STAGE/wine/bin/wine" reg add 'HKCU\Software\Wine\Fonts\Replacements' /v "$f" /d Tahoma /f > /dev/null
    done
    env -i PATH="$STAGE/wine/bin:/usr/bin:/bin" WINEPREFIX="$STAGE/prefix" "$STAGE/wine/bin/wineserver" -w
    # Never re-run wineboot's update on the phone (slow): prefix and Wine ship together.
    echo disable > "$STAGE/prefix/.update-timestamp"
    # Keep only C: and Z:; drop anything detected from the build host's mounts.
    find "$STAGE/prefix/dosdevices" -mindepth 1 -maxdepth 1 ! -name 'c:' ! -name 'z:' -exec rm -f {} +
    # User shell folders may be symlinks into the build host's $HOME; make them plain dirs.
    find "$STAGE/prefix/drive_c" -type l -lname '/*' -print -exec sh -c 'rm -f "$1" && mkdir -p "$1"' sh {} \;
    rm -rf "$STAGE/prefix/.wineserver"
    ls -la "$STAGE/prefix/dosdevices"
    du -sh "$STAGE/prefix"
}

step_hello() {
    log "tests/hello.exe"
    mkdir -p "$STAGE/tests"
    x86_64-w64-mingw32-gcc -O2 -ffp-contract=off -D__USE_MINGW_ANSI_STDIO=1 -static \
        -I"$REPO/shared" "$HERE/tests/hello.c" -o "$STAGE/tests/hello.exe"
    x86_64-w64-mingw32-strip "$STAGE/tests/hello.exe"
    file "$STAGE/tests/hello.exe"
}

# Bridge host (MinGW x86_64 PE) + Phase 1 test MIDI.
step_host() {
    log "bridge host (vstghost-host.exe)"
    local t pi="$WORK/vst3pi" b="$WORK/host-build"
    t="$(fetch "$VST3PI_URL" "$VST3PI_SHA256" "vst3_pluginterfaces-${VST3PI_VERSION}.tar.gz")"
    rm -rf "$pi" "$b" && mkdir -p "$pi/pluginterfaces" "$b"
    tar -xzf "$t" -C "$pi/pluginterfaces" --strip-components=1
    local src="$REPO/host/src"
    local flags=(-std=c++20 -O2 -g0 -municode -DUNICODE -D_UNICODE -Wall -I"$pi" -I"$src" -I"$REPO/shared")
    for f in "$src"/*.cpp "$pi/pluginterfaces/base/funknown.cpp"; do
        x86_64-w64-mingw32-g++ "${flags[@]}" -c "$f" -o "$b/$(basename "${f%.cpp}").o"
    done
    mkdir -p "$STAGE/host" "$STAGE/tests"
    x86_64-w64-mingw32-g++ -municode -static -s -o "$STAGE/host/vstghost-host.exe" "$b"/*.o -lole32 -luuid -lgdi32 -luser32
    file "$STAGE/host/vstghost-host.exe"
    python3 "$REPO/tools/make_test_midi.py" "$STAGE/tests/phase1.mid"
}

step_etc() {
    log "config + version"
    mkdir -p "$STAGE/etc" "$STAGE/tmp" "$STAGE/home"
    cp "$HERE/box64rc" "$STAGE/etc/box64.box64rc"
    local ver="s${PAYLOAD_SERIES}-$(date -u +%Y%m%d)-box64-${BOX64_VERSION}-wine-${WINE_VERSION}-glibc-${TERMUX_GLIBC_FILE%%-aarch64*}"
    echo "$ver" > "$STAGE/VERSION"
    cat > "$STAGE/manifest.json" <<EOF
{
  "version": "$ver",
  "placeholder": "$PLACEHOLDER",
  "box64": {"version": "$BOX64_VERSION", "sha256": "$BOX64_SHA256", "cpu_flag": "$BOX64_CPU_FLAG"},
  "wine": {"version": "$WINE_VERSION", "sha256": "$WINE_SHA256"},
  "glibc": {"file": "$TERMUX_GLIBC_FILE", "sha256": "$TERMUX_GLIBC_SHA256"},
  "gcc_libs": {"file": "$TERMUX_GCCLIBS_FILE", "sha256": "$TERMUX_GCCLIBS_SHA256"}
}
EOF
    cat "$STAGE/VERSION"
}

step_pack() {
    log "pack"
    mkdir -p "$OUT"
    tar -C "$STAGE" --sort=name --owner=0 --group=0 --numeric-owner --format=gnu -cf - . \
        | zstd -19 -T0 -q -f -o "$OUT/vstghost-payload.tar.zst"
    (cd "$OUT" && sha256sum vstghost-payload.tar.zst > vstghost-payload.tar.zst.sha256)
    du -sh "$STAGE" "$OUT/vstghost-payload.tar.zst"
}

# Play edition: arm64 executables must come from nativeLibraryDir, so they are packaged as
# lib*.so. Sonames are restored on the device through a symlink dir (jnilibs.map).
step_play() {
    log "play edition jniLibs + assets"
    local jl="$OUT/jnilibs/arm64-v8a" pa="$OUT/play-assets"
    rm -rf "$OUT/jnilibs" "$pa" && mkdir -p "$jl" "$pa"
    cp "$STAGE/glibc/lib/ld-linux-aarch64.so.1" "$jl/libldlinux.so"
    cp "$STAGE/box64/bin/box64" "$jl/libbox64.so"
    # Box64 re-execs itself (argv[0]) for every x86 child process, so libbox64.so must be directly
    # executable. nativeLibraryDir's path is unpredictable, but filesDir's is not: point PT_INTERP
    # at a symlink the app creates in files/plibs (-> nativeLibraryDir/libldlinux.so). SELinux
    # checks the symlink target (apk_data_file), which apps may execute. Primary user only.
    local interp="/data/data/${PLAY_PACKAGE:-com.vstghost}/files/plibs/ld-linux-aarch64.so.1"
    python3 - "$jl/libbox64.so" "$interp" <<'PY'
import struct, sys
path, new = sys.argv[1], sys.argv[2].encode()
with open(path, "r+b") as f:
    data = bytearray(f.read())
    assert data[:4] == b"\x7fELF" and data[4] == 2
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    for i in range(phnum):
        off = phoff + i * phentsize
        ptype, = struct.unpack_from("<I", data, off)
        if ptype == 3:  # PT_INTERP
            p_offset, = struct.unpack_from("<Q", data, off + 8)
            p_filesz, = struct.unpack_from("<Q", data, off + 32)
            assert len(new) + 1 <= p_filesz, "interp too long"
            data[p_offset:p_offset + p_filesz] = new + b"\0" * (p_filesz - len(new))
            f.seek(0); f.write(data)
            print("libbox64.so PT_INTERP ->", new.decode())
            break
    else:
        sys.exit("no PT_INTERP")
PY
    : > "$pa/jnilibs.map"
    for l in "${GLIBC_LIBS[@]}" "${GCC_LIBS[@]}"; do
        [ "$l" = ld-linux-aarch64.so.1 ] && continue
        local n="lib$(echo "${l#lib}" | sed 's/+/x/g; s/\./_/g').so"   # libstdc++.so.6 -> libstdcxx_so_6.so
        cp "$STAGE/glibc/lib/$l" "$jl/$n"
        echo "$l $n" >> "$pa/jnilibs.map"
    done
    echo "ld-linux-aarch64.so.1 libldlinux.so" >> "$pa/jnilibs.map"
    cp "$OUT/vstghost-payload.tar.zst" "$pa/"
    ls -la "$jl"
    cat "$pa/jnilibs.map"
}

STEPS=("$@")
[ ${#STEPS[@]} -eq 0 ] && STEPS=(glibc box64 wine mesa x64libs prefix hello host etc pack play)
mkdir -p "$WORK" "$STAGE"
for s in "${STEPS[@]}"; do "step_$s"; done
log "done: $OUT"
