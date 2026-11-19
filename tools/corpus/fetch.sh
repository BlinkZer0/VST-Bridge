#!/bin/bash
# Downloads the plugin corpus (corpus.txt) and extracts the .vst3 bundles.
#   tools/corpus/fetch.sh [name...]      (default: every entry)
# Output: $VG_CORPUS/dl/<name>.<zip|7z>, $VG_CORPUS/x/<name>/..., and a list of the .vst3 found.
# Archives are untrusted: extracted with path-traversal checks, never executed.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
C="${VG_CORPUS:-$HOME/vstghost-corpus}"
mkdir -p "$C/dl" "$C/x"
want=("$@")
while read -r name url note; do
    case "$name" in ''|'#'*) continue;; esac
    if [ ${#want[@]} -gt 0 ]; then
        hit=0; for w in "${want[@]}"; do [ "$w" = "$name" ] && hit=1; done
        [ $hit = 1 ] || continue
    fi
    ext="${url##*.}"; case "$ext" in 7z) ;; *) ext=zip;; esac
    f="$C/dl/$name.$ext"
    if [ ! -s "$f" ]; then
        echo "downloading $name"
        curl -sSL --fail -o "$f.part" "$url" && mv "$f.part" "$f" || { echo "DOWNLOAD FAILED $name"; rm -f "$f.part"; continue; }
    fi
    rm -rf "$C/x/$name"; mkdir -p "$C/x/$name"
    if [ "$ext" = 7z ]; then
        command -v 7z >/dev/null || { echo "$name: 7z not installed (apt install p7zip-full)"; continue; }
        7z x -y -o"$C/x/$name" "$f" >/dev/null || echo "$name: 7z extract failed"
    else
        python3 -I "$HERE/unzip_safe.py" "$f" "$C/x/$name" || echo "$name: unzip failed"
    fi
    # Old-style single-file VST3 modules shipped as vst3/<name>.dll: give them the .vst3 name.
    (cd "$C/x/$name" && find . -ipath '*/vst3/*.dll' -type f | while read -r d; do cp --update=none "$d" "${d%.dll}.vst3"; done)
    found=$(cd "$C/x/$name" && find . -iname '*.vst3' -prune | sed 's|^\./||' | tr '\n' ' ')
    inst=$(cd "$C/x/$name" && find . -iname '*.exe' -o -iname '*.msi' | head -3 | tr '\n' ' ')
    printf '%-12s %6s  vst3: %s%s\n' "$name" "$(du -h "$f" | cut -f1)" "${found:-NONE}" "${inst:+  (installers: $inst)}"
done < "$HERE/corpus.txt"
