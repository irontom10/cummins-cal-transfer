#!/usr/bin/env sh
set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
VENDOR="$ROOT/.vendor-rp1210"
SDK_URL="https://download.nexiq.com/Nexiq/SDK/RP1210_Mobile_Native_Android_SDK.zip"
SDK_ZIP="$VENDOR/RP1210_Mobile_Native_Android_SDK.zip"
EXTRACT="$VENDOR/_extract"
WRAPPER_JAR="$ROOT/gradle/wrapper/gradle-wrapper.jar"
WRAPPER_URL="https://raw.githubusercontent.com/gradle/gradle/v8.9.0/gradle/wrapper/gradle-wrapper.jar"
REMOVE_WRAPPER_JAR=0
RC=1

cleanup() {
    rm -rf "$VENDOR"
    if [ "$REMOVE_WRAPPER_JAR" -eq 1 ]; then
        rm -f "$WRAPPER_JAR"
    fi
}
trap cleanup EXIT INT TERM

download() {
    url="$1"
    output="$2"

    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --retry-delay 2 -o "$output" "$url"
        return $?
    fi

    if command -v wget >/dev/null 2>&1; then
        wget -O "$output" "$url"
        return $?
    fi

    echo "ERROR: curl or wget is required."
    return 1
}

rm -rf "$VENDOR"
mkdir -p "$VENDOR" || exit 1

echo "[android] Downloading RP1210 Mobile Native Android SDK from NEXIQ..."
download "$SDK_URL" "$SDK_ZIP" || exit 1

command -v unzip >/dev/null 2>&1 || {
    echo "ERROR: unzip is required to extract the NEXIQ SDK."
    exit 1
}

mkdir -p "$EXTRACT"
unzip -q "$SDK_ZIP" -d "$EXTRACT" || exit 1

for abi in arm64-v8a armeabi-v7a; do
    anchor="$(find "$EXTRACT" -type f -path "*/$abi/libnuln3r32.so" -print -quit)"
    if [ -z "$anchor" ]; then
        echo "ERROR: Could not find libnuln3r32.so for $abi in the NEXIQ SDK."
        exit 1
    fi

    source_dir="$(dirname "$anchor")"
    destination="$VENDOR/app/src/main/jniLibs/$abi"
    mkdir -p "$destination"
    cp -R "$source_dir/." "$destination/"
done

ini_anchor="$(find "$EXTRACT" -type f -iname "nuln3r32.ini" -print -quit)"
if [ -z "$ini_anchor" ]; then
    echo "ERROR: Could not find nuln3r32.ini in the NEXIQ SDK."
    exit 1
fi

asset_source="$(dirname "$ini_anchor")"
asset_destination="$VENDOR/app/src/main/assets/Files"
mkdir -p "$asset_destination"
cp -R "$asset_source/." "$asset_destination/"

rm -rf "$EXTRACT"
rm -f "$SDK_ZIP"

test -f "$VENDOR/app/src/main/jniLibs/arm64-v8a/libnuln3r32.so" || exit 1
test -f "$VENDOR/app/src/main/jniLibs/armeabi-v7a/libnuln3r32.so" || exit 1
test -f "$VENDOR/app/src/main/assets/Files/nuln3r32.ini" || exit 1

if [ ! -f "$WRAPPER_JAR" ]; then
    echo "[android] Fetching Gradle wrapper bootstrap..."
    mkdir -p "$ROOT/gradle/wrapper"
    download "$WRAPPER_URL" "$WRAPPER_JAR" || exit 1
    REMOVE_WRAPPER_JAR=1
fi

echo "[android] Building Calibration Transfer APK..."
"$ROOT/gradlew" clean assembleDebug
RC=$?

if [ "$RC" -eq 0 ]; then
    echo
    echo "Android build complete:"
    echo "  src/android/app/build/outputs/apk/debug/app-debug.apk"
fi

exit "$RC"
