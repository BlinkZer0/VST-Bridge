# Extracts a zip into a directory, refusing entries that would escape it. Usage: unzip_safe.py zip dir
import os
import sys
import zipfile

zip_path, dest = sys.argv[1], os.path.abspath(sys.argv[2])
with zipfile.ZipFile(zip_path) as z:
    for m in z.infolist():
        # Some Windows-made zips store backslash separators; zipfile would keep them in the name.
        name = m.filename.replace("\\", "/")
        target = os.path.abspath(os.path.join(dest, name))
        if not (target == dest or target.startswith(dest + os.sep)):
            print("skipped", m.filename, "(outside the target directory)")
            continue
        if name.endswith("/"):
            os.makedirs(target, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with z.open(m) as src, open(target, "wb") as dst:
            dst.write(src.read())
