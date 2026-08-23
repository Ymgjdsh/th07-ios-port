#!/usr/bin/env python3
import os
import plistlib
import sys
import zipfile


if len(sys.argv) != 2:
    raise SystemExit("usage: verify_ipa.py output.ipa")

ipa = os.path.abspath(sys.argv[1])
with zipfile.ZipFile(ipa) as archive:
    names = set(archive.namelist())
    prefix = "Payload/th07.app/"
    required = ["th07", "Info.plist", "th07.dat", "thbgm.dat", "msgothic.ttc", "Assets.car"]
    for relative in required:
        name = prefix + relative
        if name not in names or archive.getinfo(name).file_size == 0:
            raise SystemExit("error: IPA missing " + name)
    info = plistlib.loads(archive.read(prefix + "Info.plist"))
    if info.get("MinimumOSVersion") != "14.0":
        raise SystemExit("error: MinimumOSVersion is not 14.0")
    print("IPA verified:", ipa)
    print("  bundle:", info.get("CFBundleIdentifier"))
    print("  version:", info.get("CFBundleShortVersionString"), info.get("CFBundleVersion"))
    print("  size:", os.path.getsize(ipa))
