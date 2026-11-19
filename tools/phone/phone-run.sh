# Runs a Wine command inside the Play edition's sandbox with the app's environment (Payload.environment()).
# usage (on device, as the app via run-as): sh phone-run.sh <WINEDEBUG> <args...>
P=/data/user/0/com.vstghost/files
R=$P/rootfs
LIBDIR=${LIBDIR:-/data/app/~~u9w7TU7w1DXuA6rB5ZoIwg==/com.vstghost-XTvn7o0cElGu-FfQHrFE3w==/lib/arm64}
export HOME=$R/home TMPDIR=$R/tmp USER=vstghost TZ=UTC LANG=${XLANG:-C} PATH=$R/wine/bin:/system/bin
export LD_LIBRARY_PATH=$P/plibs BOX64_LD_LIBRARY_PATH=$R/box64/x64lib:$R/x64libs BOX64_PATH=$R/wine/bin
export BOX64_RCFILE=$R/etc/box64.box64rc BOX64_DYNACACHE=0 WINEPREFIX=$R/prefix
export WINEDLLOVERRIDES="winemenubuilder.exe=d;mscoree=d;mshtml=d"
export BOX64_EMULATED_LIBS="libwayland-client.so.0:libffi.so.8:libxkbcommon.so.0:libxkbregistry.so.0:libfreetype.so.6:libpng16.so.16:libz.so.1:libbz2.so.1.0:libbz2.so.1:libbrotlidec.so.1:libbrotlicommon.so.1:libEGL.so.1:libGLESv2.so.2:libGL.so.1:libglapi.so.0:libdrm.so.2:libexpat.so.1:libwayland-server.so.0:libwayland-egl.so.1:libzstd.so.1:libtinfo.so.6"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=2 LIBGL_DRIVERS_PATH=$R/x64libs/dri EGL_PLATFORM=wayland
export VSTGHOST_IMAGE_COPY=1 VSTGHOST_DENIED_IS_MISSING=1 BOX64_AVX=0 BOX64_AVX2=0 VSTGHOST_VBLANK_MS=50 VSTGHOST_PLUGIN_DIR=$P/plugins
export XDG_RUNTIME_DIR=$R/run WAYLAND_DISPLAY=$R/run/wl-0 VSTGHOST_WL_ONE_TOPLEVEL=1
export WINEDEBUG="$1"; shift
cd $R
exec $LIBDIR/libbox64.so $R/wine/bin/wine "$@"
