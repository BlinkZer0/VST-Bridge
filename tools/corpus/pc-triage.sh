#!/bin/bash
# PC triage of the plugin corpus with the phone's configuration: our Wine (x86_64) with the Wayland
# driver into our compositor test driver, llvmpipe OpenGL, one-toplevel patch. Per plugin: `info`,
# a 20 s render, an editor screenshot. Writes $OUT/table.tsv and per-plugin json/png/err files.
#
#   tools/corpus/pc-triage.sh [plugin.vst3 ...]      (default: every .vst3 under $VG_CORPUS/x)
# Environment (defaults = the payload build layout):
#   VG_CORPUS   ~/vstghost-corpus         VG_WORK  ~/vstghost-payload-work
#   WINE_DIR    $VG_WORK/wine-x11-install/opt/vstghost-wine-x11 (a Wine with the Wayland driver)
#   HOST        $VG_WORK/stage/host/vstghost-host.exe       MIDI  $VG_WORK/stage/tests/phase1.mid
#   VGWL_TEST   ~/vgwl_test (tools/wl/vgwl_test.cpp)         OUT   $VG_CORPUS/results
#   PREFIX      $VG_CORPUS/prefix (created from $VG_WORK/stage/prefix on first run)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
C="${VG_CORPUS:-$HOME/vstghost-corpus}"
W="${VG_WORK:-$HOME/vstghost-payload-work}"
WX="${WINE_DIR:-$W/wine-x11-install/opt/vstghost-wine-x11}"
H="${HOST:-$W/stage/host/vstghost-host.exe}"
MID="${MIDI:-$W/stage/tests/phase1.mid}"
VGWL="${VGWL_TEST:-$HOME/vgwl_test}"
OUT="${OUT:-$C/results}"
mkdir -p "$OUT"
export WINEPREFIX="${PREFIX:-$C/prefix}"
[ -d "$WINEPREFIX" ] || cp -a "$W/stage/prefix" "$WINEPREFIX"
ln -sfn "$C/x" "$WINEPREFIX/dosdevices/d:"  # like the app: plugins live on D: (the host maps paths under VSTGHOST_PLUGIN_DIR)
RUN="$C/run"; mkdir -p "$RUN"; chmod 700 "$RUN"
export USER=vstghost WINEDEBUG=-all XDG_RUNTIME_DIR="$RUN" WAYLAND_DISPLAY="$RUN/vgwl-0" VSTGHOST_WL_ONE_TOPLEVEL=1
export WINEDLLOVERRIDES="mscoree=d;mshtml=d;winemenubuilder.exe=d"
export LD_LIBRARY_PATH="$W/mesa-install/opt/vstghost-mesa/lib" LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe EGL_PLATFORM=wayland
# The phone's plugin-path and probing accommodations (Wine patch 0004 / the host's D: mapping).
export VSTGHOST_DENIED_IS_MISSING=1 VSTGHOST_PLUGIN_DIR="$C/x"
unset DISPLAY

if [ $# -gt 0 ]; then PLUGS=("$@"); else mapfile -t PLUGS < <(find "$C/x" -iname '*.vst3' -prune | sort); fi
printf "plugin\tload\tinstrument\tparams\trender\tpeakDb\trtFactor\teditor\topenMs\tlit\tsize\n" > "$OUT/table.tsv"
json() { python3 -c "import sys,json
try:
    d=json.loads(sys.stdin.read().strip().splitlines()[-1]); print(eval(sys.argv[1]))
except Exception as e: print(sys.argv[2] if len(sys.argv)>2 else '?')" "$@"; }

for p in "${PLUGS[@]}"; do
    n=$(basename "$p" .vst3); s=$(echo "$n" | tr ' ' '_')
    echo "=== $n"
    "$WX/bin/wineserver" -k 2>/dev/null; sleep 1; "$WX/bin/wineserver" -p
    info=$(timeout 180 "$WX/bin/wine" "$H" info "$p" 2>"$OUT/$s.info.err"); rc=$?
    echo "$info" > "$OUT/$s.info.json"
    load=$([ $rc = 0 ] && echo ok || echo "FAIL($rc)")
    inst=$(echo "$info" | json 'str(d.get("instrument","?")).lower()')
    nparams=$(echo "$info" | json 'd.get("paramCount","?")')
    if [ "$inst" = true ]; then args=(--midi "$MID"); else args=(--midi "$MID" --input builtin); fi
    r=$(timeout 300 "$WX/bin/wine" "$H" render "$p" --out "Z:$OUT/$s.wav" "${args[@]}" --tail 2 2>"$OUT/$s.render.err" | tail -1); rrc=$?
    echo "$r" > "$OUT/$s.render.json"
    pk=$(echo "$r" | json 'round(max(d.get("peakDb",[-999])),1)')
    rt=$(echo "$r" | json 'round(d.get("realtimeFactor", d.get("rtFactor",0)),1)')
    render=$([ $rrc = 0 ] && echo ok || echo "FAIL($rrc)")
    rm -f "$OUT/$s.wav"
    # editor through our compositor: a screenshot at 20 s. The compositor must be listening before
    # the Wine session starts (the session picks its display driver once).
    "$WX/bin/wineserver" -k 2>/dev/null; sleep 1
    "$VGWL" "$RUN/vgwl-0" 30 "$OUT" "shot:22:$s.ed" > "$OUT/$s.vgwl.json" 2>/dev/null &
    VP=$!; sleep 0.5
    "$WX/bin/wineserver" -p
    e=$(timeout 90 "$WX/bin/wine" "$H" editor "$p" --seconds 24 2>"$OUT/$s.editor.err" | tail -1)
    wait $VP
    ed=$(echo "$e" | json '"ok" if d.get("ok") else "FAIL:"+d.get("error","")[:40].replace(" ","_")' "FAIL(no-output)")
    ms=$(echo "$e" | json 'round(d.get("openMs",0))' 0)
    lit=$(json 'd["lit"]' '?' < "$OUT/$s.vgwl.json")
    size=$(json '"%dx%d"%(d["width"],d["height"])' '?' < "$OUT/$s.vgwl.json")
    [ -f "$OUT/$s.ed.bmp" ] && python3 "$HERE/../phone/bmp2png.py" "$OUT/$s.ed.bmp" "$OUT/$s.ed.png" && rm -f "$OUT/$s.ed.bmp"
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$n" "$load" "$inst" "$nparams" "$render" "$pk" "$rt" "$ed" "$ms" "$lit" "$size" | tee -a "$OUT/table.tsv"
done
"$WX/bin/wineserver" -k 2>/dev/null
echo; column -t -s $'\t' "$OUT/table.tsv"
