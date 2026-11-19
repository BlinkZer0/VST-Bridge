#!/bin/bash
# Phone triage of the corpus through the app (Play edition installed, screen may be locked):
# import each plugin, load it into a slot with the self-test (a chord → output peak), open its
# editor and keep a picture. Writes $OUT/phone.tsv and $OUT/<plugin>.png.
#
#   tools/corpus/phone-triage.sh [plugin.vst3 ...]   (default: every .vst3 under $VG_CORPUS/x)
# Environment: VG_CORPUS (~/vstghost-corpus), ADB (adb), OUT ($VG_CORPUS/results-phone),
#              PKG (com.vstghost), PY (python3), LOAD_WAIT (45 s), EDITOR_WAIT (45 s).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
C="${VG_CORPUS:-$HOME/vstghost-corpus}"
ADB="${ADB:-adb}"
PY="${PY:-python3}"
PKG="${PKG:-com.vstghost}"
OUT="${OUT:-$C/results-phone}"
mkdir -p "$OUT"
if [ $# -gt 0 ]; then PLUGS=("$@"); else mapfile -t PLUGS < <(find "$C/x" -iname '*.vst3' -prune | sort); fi
printf "plugin\timport\tload\tinstrument\tparams\tpeakDb\trenderAvgUs\tunderruns\teditor\topenMs\tsize\n" > "$OUT/phone.tsv"
for p in "${PLUGS[@]}"; do
    n=$(basename "$p" .vst3); s=$(echo "$n" | tr ' ' '_')
    echo "=== $n"
    # import: a zip of the bundle (or the single file) through the app's importer
    z="$OUT/$s.zip"; rm -f "$z"
    (cd "$(dirname "$p")" && "$PY" -m zipfile -c "$z" "$(basename "$p")")
    "$ADB" push "$z" "/sdcard/Android/data/$PKG/files/$s.zip" >/dev/null
    "$ADB" shell am force-stop "$PKG"; sleep 2; "$ADB" logcat -c
    "$ADB" shell am start -n "$PKG/.PlayerActivity" --es importZip "$s.zip" --el t "$(date +%s)" >/dev/null 2>&1
    sleep 25
    imp=$("$ADB" logcat -d -s VSTGHOST:* | grep -o "import $s.zip: .*" | head -1 | cut -c1-80)
    "$ADB" shell am force-stop "$PKG"; sleep 2; "$ADB" logcat -c
    "$ADB" shell am start -n "$PKG/.PlayerActivity" --es load "'$n'" --ez selftest true --el t "$(date +%s)" >/dev/null 2>&1
    sleep "${LOAD_WAIT:-45}"
    st=$("$ADB" logcat -d -s VSTGHOST:* | grep -o "selftest .*" | head -1)
    crash=$("$ADB" logcat -d -s VSTGHOST:* | grep -c "exited unexpectedly")
    read -r inst params < <("$ADB" logcat -d -s VSTGHOST:* | grep -o "req slot.*" >/dev/null; echo "? ?")
    peak=$(echo "$st" | grep -oE "\(-?[0-9.]+ dBFS\)" | tr -d '()' | cut -d' ' -f1)
    avg=$(echo "$st" | grep -oE "renderAvg [0-9]+" | cut -d' ' -f2)
    ur=$(echo "$st" | grep -oE "underruns [0-9]+" | cut -d' ' -f2)
    load=$([ -n "$st" ] && echo ok || { [ "$crash" -gt 0 ] && echo CRASH || echo "FAIL"; })
    # instrument/params from the "programs"/"info" request hook
    "$ADB" logcat -c; "$ADB" shell am start -n "$PKG/.PlayerActivity" --es req info --el t "$(date +%s)" >/dev/null 2>&1; sleep 4
    info=$("$ADB" logcat -d -s VSTGHOST:* | grep -o "req slot 1 info: .*" | sed 's/.*info: //')
    inst=$(echo "$info" | "$PY" -c 'import sys,json
try: d=json.loads(sys.stdin.read()); print(str(d.get("instrument","?")).lower(), d.get("paramCount","?"))
except Exception: print("? ?")')
    "$ADB" logcat -c
    "$ADB" shell "am start -n $PKG/.EditorActivity --ei slot 1 --ei dump 3" >/dev/null 2>&1
    sleep "${EDITOR_WAIT:-45}"
    eo=$("$ADB" logcat -d -s VSTGHOST:* | grep -o 'editor_open slot 1: .*' | head -1)
    ed=$(echo "$eo" | "$PY" -c 'import sys,re
s=sys.stdin.read(); m=re.search(r"\{.*?\}", s)
import json
try: d=json.loads(m.group(0)); print(("ok" if d.get("ok") else "FAIL:"+d.get("error","")[:40].replace(" ","_")), round(d.get("openMs",0)), "%sx%s"%(d.get("width","?"),d.get("height","?")))
except Exception: print("FAIL(none) 0 ?")')
    "$ADB" exec-out run-as "$PKG" cat files/editor-dump-2.bmp > "$OUT/$s.bmp" 2>/dev/null && "$PY" "$HERE/../phone/bmp2png.py" "$OUT/$s.bmp" "$OUT/$s.png" 2>/dev/null && rm -f "$OUT/$s.bmp"
    "$ADB" shell input keyevent 4
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$n" "${imp:-?}" "$load" "$inst" "${peak:-?}" "${avg:-?}" "${ur:-?}" "$ed" | tr ' ' '\t' | tee -a "$OUT/phone.tsv"
done
echo; column -t -s $'\t' "$OUT/phone.tsv"
