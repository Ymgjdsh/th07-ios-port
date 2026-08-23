#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 /path/to/th07.app /path/to/output.ipa" >&2
    exit 2
fi

APP_PATH=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
IPA_PATH=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")

for required in th07 Info.plist th07.dat thbgm.dat msgothic.ttc Assets.car; do
    if [ ! -s "$APP_PATH/$required" ]; then
        echo "error: app bundle is missing $required" >&2
        exit 3
    fi
done

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/th07-ipa.XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT INT TERM
mkdir -p "$WORK_DIR/Payload"
cp -R "$APP_PATH" "$WORK_DIR/Payload/th07.app"
rm -f "$IPA_PATH"
(cd "$WORK_DIR" && /usr/bin/zip -qry "$IPA_PATH" Payload)
echo "created $IPA_PATH"
