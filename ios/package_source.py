#!/usr/bin/env python3
import argparse
import os
import shutil
import stat
import zipfile


parser = argparse.ArgumentParser()
parser.add_argument("--root", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--folder", default="th07-ios14-port")
parser.add_argument("--exclude-assets", action="store_true",
                    help="omit the top-level assets directory for incremental remote uploads")
args = parser.parse_args()

root = os.path.abspath(args.root)
output = os.path.abspath(args.output)
excluded_roots = {".git", ".agents", "__pycache__", "dist", "_msg_extract"}
excluded_prefixes = ("build", "package-build", "_verify")
excluded_suffixes = (".obj", ".exe", ".pdb", ".ilk", ".pyc", ".zip", ".dat")
private_suffixes = (".local.psd1", ".pem", ".key", ".p12", ".pfx", ".mobileprovision")

files = []
for current, directories, names in os.walk(root):
    relative_dir = os.path.relpath(current, root)
    top = "" if relative_dir == "." else relative_dir.split(os.sep, 1)[0]
    if relative_dir == ".":
        directories[:] = [
            name for name in directories
            if name not in excluded_roots and not name.startswith(excluded_prefixes)
            and not (args.exclude_assets and name == "assets")
        ]
    else:
        directories[:] = [name for name in directories if name not in excluded_roots]
    if top and top.startswith(excluded_prefixes):
        continue
    for name in names:
        if name.endswith(excluded_suffixes) or name.lower().endswith(private_suffixes):
            continue
        path = os.path.join(current, name)
        relative = os.path.relpath(path, root).replace(os.sep, "/")
        files.append((path, f"{args.folder}/{relative}"))

files.sort(key=lambda item: item[1])
os.makedirs(os.path.dirname(output), exist_ok=True)
with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                     compresslevel=6, allowZip64=True) as archive:
    for path, arcname in files:
        info = zipfile.ZipInfo.from_file(path, arcname=arcname)
        info.compress_type = zipfile.ZIP_DEFLATED
        if arcname.endswith((".sh", ".py")):
            mode = stat.S_IFREG | 0o755
            info.external_attr = mode << 16
            info.create_system = 3
        # Stream large game archives instead of loading a 400+ MB BGM file into
        # memory. This also ensures the ZIP central directory is written promptly.
        with open(path, "rb") as source, archive.open(info, "w", force_zip64=True) as target:
            shutil.copyfileobj(source, target, length=1024 * 1024)

print(f"created: {output}")
print(f"files: {len(files)}")
