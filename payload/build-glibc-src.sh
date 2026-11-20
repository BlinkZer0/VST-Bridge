#!/bin/bash
# glibc (and the libstdc++/libgcc_s/libatomic "gcc-libs") for aarch64 Android, built from source
# the way Termux-pacman builds them: the termux-packages build system, the CGCT cross toolchain and
# the recipes in termux-pacman/glibc-packages (gpkg/glibc, gpkg/gcc-libs-glibc), with both trees
# pinned to commits. This replaces the prebuilt packages payload/build.sh downloads in step_glibc:
# point TERMUX_GLIBC_LOCAL / TERMUX_GCCLIBS_LOCAL at the results (see versions.env).
#
#   payload/build-glibc-src.sh setup     once, as root (WSL: `wsl -u root`): apt packages
#   payload/build-glibc-src.sh perms     as root: the qemu-user + ownership part of 'setup' only (for
#                                        a setup that ran before these steps existed)
#   payload/build-glibc-src.sh tools     once, as the normal user: Android NDK + CGCT toolchain
#   payload/build-glibc-src.sh build     as the normal user: builds the packages (long: glibc ~20 min,
#                                        gcc-libs builds GCC, 1–2 h on 32 cores)
#
# Output: $OUT/glibc-<ver>-<rel>-aarch64.pkg.tar.xz and gcc-libs-glibc-…pkg.tar.xz, plus sha256 files.
set -euo pipefail

GLIBC_PACKAGES_REPO=https://github.com/termux-pacman/glibc-packages.git
GLIBC_PACKAGES_COMMIT=3d99b6682f8c74ad82bda39315325189d8d34966   # main, 2026-10-22
TERMUX_PACKAGES_REPO=https://github.com/termux/termux-packages.git
TERMUX_PACKAGES_COMMIT=2509fe96789c8f87254f50d1e2f24043cc5cf5f1  # master, 2026-11-19
PACKAGES="${PACKAGES:-glibc gcc-libs-glibc}"

W="${VG_WORK:-$HOME/vstghost-payload-work}/glibc-src"
OUT="${OUT:-$W/out}"
log() { echo "== $*"; }

fetch_tree() {
    mkdir -p "$W"
    cd "$W"
    for pair in "glibc-packages $GLIBC_PACKAGES_REPO $GLIBC_PACKAGES_COMMIT" "termux-packages $TERMUX_PACKAGES_REPO $TERMUX_PACKAGES_COMMIT"; do
        set -- $pair
        if [ ! -d "$1/.git" ]; then git clone -q "$2" "$1"; fi
        git -C "$1" fetch -q origin "$3"
        git -C "$1" checkout -q "$3"
    done
    # What glibc-packages/get-build-package.sh does, but from the pinned termux-packages tree.
    cd glibc-packages
    for i in build-package.sh clean.sh packages x11-packages root-packages scripts ndk-patches; do
        rm -rf "./$i"
        cp -r "../termux-packages/$i" ./
    done
    log "trees at $W (glibc-packages @${GLIBC_PACKAGES_COMMIT:0:10}, termux-packages @${TERMUX_PACKAGES_COMMIT:0:10})"
}

# The build system's prefix and the CGCT cross toolchain live under /data/data/com.termux
# (hard-coded in scripts/properties.sh). 'tools' and 'build' run as the normal user, so the tree
# must be theirs: WSL's default user, or VG_USER=<name>.
hand_over_prefix() {
    local user="${VG_USER:-${SUDO_USER:-$(id -nu 1000 2>/dev/null || true)}}"
    [ -n "$user" ] || { echo "cannot tell the build user: rerun with VG_USER=<name>"; exit 1; }
    mkdir -p /data/data/com.termux
    chown -R "$user" /data/data/com.termux
    # setup-cgct.sh untars "data/…" at /: tar also sets the timestamps of /data and /data/data,
    # which fails (and aborts the script) unless they are the user's too. They exist only for this.
    chown "$user" /data /data/data
    log "/data, /data/data, /data/data/com.termux owned by $user"
}

# glibc's `make install` runs the freshly built aarch64 localedef (through the new ld.so) to make
# the C.UTF-8 locale, and ldconfig; termux-pacman's CI does the same under qemu-user with binfmt.
# Without it the build ends in "Exec format error".
install_qemu() {
    if [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
        apt-get install -y qemu-user-static binfmt-support
        update-binfmts --enable qemu-aarch64 || true
    fi
    [ -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ] || { echo "qemu-aarch64 binfmt is not registered (is systemd/binfmt_misc enabled in WSL?)"; exit 1; }
    log "qemu-aarch64 binfmt registered"
}

case "${1:-}" in
    setup)
        [ "$(id -u)" = 0 ] || { echo "run 'setup' as root (wsl -u root)"; exit 1; }
        fetch_tree
        cd "$W/glibc-packages"
        # termux-packages' setup script targets Ubuntu "resolute" (its builder image); on 24.04
        # (noble) the same packages exist under the release's own names.
        if [ "$(. /etc/os-release && echo "$VERSION_CODENAME")" = noble ]; then
            sed -i -e 's/resolute/noble/g' -e 's/python3\.14-venv/python3-venv/g' -e 's/python3\.14/python3/g' scripts/setup-ubuntu.sh
        fi
        ./scripts/setup-ubuntu.sh
        install_qemu
        hand_over_prefix
        ;;
    perms)
        [ "$(id -u)" = 0 ] || { echo "run 'perms' as root (wsl -u root)"; exit 1; }
        install_qemu
        hand_over_prefix
        ;;
    tools)
        fetch_tree
        cd "$W/glibc-packages"
        [ -w /data/data/com.termux ] || { echo "/data/data/com.termux is not writable: run 'setup' as root first"; exit 1; }
        ./scripts/setup-android-sdk.sh     # NDK into $HOME/lib/android-ndk-*
        ./scripts/setup-cgct.sh            # CGCT (cross glibc compiler toolchain) into /data/data/com.termux/cgct
        ;;
    build)
        fetch_tree
        cd "$W/glibc-packages"
        NDK_DIR="$(ls -d "$HOME"/lib/android-ndk-* 2>/dev/null | sort | tail -1 || true)"
        [ -n "$NDK_DIR" ] || { echo "no NDK under $HOME/lib: run 'tools' first"; exit 1; }
        export NDK="$NDK_DIR" ANDROID_HOME="$HOME/lib/android-sdk"
        log "building: $PACKAGES"
        # -I: install dependencies from the repo instead of building the world; --library glibc:
        # the glibc-based package set; --format pacman: the .pkg.tar.xz step_glibc understands.
        ./build-package.sh -I -a aarch64 --format pacman --library glibc $PACKAGES
        mkdir -p "$OUT"
        cp -v output/*.pkg.tar.xz "$OUT"/
        (cd "$OUT" && sha256sum ./*.pkg.tar.xz | tee SHA256SUMS)
        log "done: $OUT"
        ;;
    *)
        sed -n 2,16p "$0"
        exit 1
        ;;
esac
