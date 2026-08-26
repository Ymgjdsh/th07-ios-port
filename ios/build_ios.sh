#!/bin/bash
set -Eeuo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build-ios"}
CONFIG=${CONFIG:-Release}
IOS_VERSION=${IOS_VERSION:-0.4.0}
IOS_BUILD=${IOS_BUILD:-38}
OUTPUT_IPA=${OUTPUT_IPA:-"$BUILD_DIR/th07-ios-${IOS_VERSION}-${IOS_BUILD}.ipa"}
LOG_DIR="$BUILD_DIR/logs"
mkdir -p "$LOG_DIR"

# Non-interactive SSH sessions do not load the user's shell profile. Make the
# common CMake.app and Homebrew locations available before tool checks.
for tool_dir in \
    /Applications/CMake.app/Contents/bin \
    /opt/homebrew/bin \
    /usr/local/bin; do
    if [ -d "$tool_dir" ]; then
        PATH="$tool_dir:$PATH"
    fi
done
export PATH

# Select the Xcode installation explicitly when more than one is installed.
# Examples:
#   XCODE_APP=/Applications/Xcode-14.0.app ./ios/build_ios.sh
#   DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer ./ios/build_ios.sh
if [ -n "${XCODE_APP:-}" ]; then
    case "$XCODE_APP" in
        *.app) export DEVELOPER_DIR="$XCODE_APP/Contents/Developer" ;;
        *) export DEVELOPER_DIR="$XCODE_APP" ;;
    esac
fi

exec > >(tee "$LOG_DIR/build-ios.log") 2>&1

echo "TH07 iOS build ${IOS_VERSION} (${IOS_BUILD})"
echo "Project: $ROOT_DIR"
echo "Output:  $OUTPUT_IPA"

fail() {
    code=$1
    echo ""
    echo "BUILD FAILED (exit $code)"
    echo "Diagnostic log: $LOG_DIR/build-ios.log"
    tail -n 120 "$LOG_DIR/build-ios.log" > "$LOG_DIR/last-120-lines.txt" 2>/dev/null || true
    exit "$code"
}
trap 'fail $?' EXIT

for tool in cmake xcrun xcodebuild python3 zip codesign; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: missing tool: $tool"; exit 2; }
done

echo "CMake: $(cmake --version | head -n 1)"
echo "Xcode: $(xcodebuild -version | tr '\n' ' ')"
SDK_PATH=$(xcrun --sdk iphoneos --show-sdk-path)
if [ ! -d "$SDK_PATH" ]; then
    echo "error: iPhoneOS SDK directory does not exist: $SDK_PATH"
    exit 2
fi
echo "SDK:   $SDK_PATH"

python3 "$ROOT_DIR/ios/verify_source.py" --root "$ROOT_DIR"

echo "Running host-side online protocol tests"
HOST_CXX=$(xcrun --sdk macosx --find clang++)
HOST_SDK=$(xcrun --sdk macosx --show-sdk-path)
"$HOST_CXX" -std=c++17 -O2 -I"$ROOT_DIR/src" \
    -isysroot "$HOST_SDK" \
    "$ROOT_DIR/ios/test_online_protocol.cpp" \
    -o "$BUILD_DIR/test-online-protocol"
"$BUILD_DIR/test-online-protocol"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$SDK_PATH" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DTH07_IOS_VERSION="$IOS_VERSION" \
    -DTH07_IOS_BUILD="$IOS_BUILD" \
    -DTH_EXTERNAL_ASSETS=OFF

for setting in SDL_GPU SDL_RENDER SDL_METAL SDL_VULKAN SDL_OPENGLES; do
    value=$(sed -n "s/^${setting}:BOOL=//p" "$BUILD_DIR/CMakeCache.txt" | tail -n 1)
    echo "SDL config: ${setting}=${value:-MISSING}"
done

for disabled in SDL_GPU SDL_METAL SDL_VULKAN; do
    value=$(sed -n "s/^${disabled}:BOOL=//p" "$BUILD_DIR/CMakeCache.txt" | tail -n 1)
    if [ "$value" != "OFF" ]; then
        echo "error: $disabled must be OFF for the iOS OpenGL ES build (got ${value:-MISSING})"
        exit 3
    fi
done

for enabled in SDL_RENDER SDL_OPENGLES; do
    value=$(sed -n "s/^${enabled}:BOOL=//p" "$BUILD_DIR/CMakeCache.txt" | tail -n 1)
    if [ "$value" != "ON" ]; then
        echo "error: $enabled must be ON for the iOS OpenGL ES build (got ${value:-MISSING})"
        exit 3
    fi
done

cmake --build "$BUILD_DIR" --config "$CONFIG" --target th07 -- -quiet
APP_PATH="$BUILD_DIR/$CONFIG-iphoneos/th07.app"

if [ ! -d "$APP_PATH" ] || [ ! -x "$APP_PATH/th07" ]; then
    echo "error: app bundle not produced: $APP_PATH"
    exit 3
fi

if [ ! -f "$APP_PATH/Assets.car" ]; then
    echo "Compiling iOS icon catalog"
    xcrun actool \
        --output-format human-readable-text --notices --warnings \
        --platform iphoneos --target-device iphone --target-device ipad \
        --minimum-deployment-target 14.0 --app-icon AppIcon \
        --output-partial-info-plist "$BUILD_DIR/assetcatalog-info.plist" \
        --compile "$APP_PATH" "$ROOT_DIR/ios/Assets.xcassets"
fi

codesign --force --deep --sign - "$APP_PATH"
"$ROOT_DIR/ios/package_ipa.sh" "$APP_PATH" "$OUTPUT_IPA"
python3 "$ROOT_DIR/ios/verify_ipa.py" "$OUTPUT_IPA"
echo ""
echo "SUCCESS: $OUTPUT_IPA"
trap - EXIT
