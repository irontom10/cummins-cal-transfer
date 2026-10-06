#!/usr/bin/env sh
set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
VENDOR="$ROOT/.vendor-rp1210"
CACHE="$ROOT/.nexiq-cache"
SDK_URL="https://download.nexiq.com/Nexiq/SDK/RP1210_Mobile_Native_Android_SDK.zip"
SDK_ZIP="$CACHE/RP1210_Mobile_Native_Android_SDK.zip"
SDK_PART="$SDK_ZIP.part"
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

    if [ -f "$SDK_ZIP" ]; then
        echo "[android] Keeping cached NEXIQ SDK:"
        echo "          $SDK_ZIP"
    fi
}
trap cleanup EXIT INT TERM

download_resumable() {
    url="$1"
    partial="$2"

    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --retry-delay 2 -C - -o "$partial" "$url"
        return $?
    fi

    if command -v wget >/dev/null 2>&1; then
        wget -c -O "$partial" "$url"
        return $?
    fi

    echo "ERROR: curl or wget is required."
    return 1
}

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
mkdir -p "$CACHE" || exit 1

if [ -f "$SDK_ZIP" ]; then
    echo "[android] Using cached NEXIQ RP1210 SDK:"
    echo "          $SDK_ZIP"
else
    echo "[android] Downloading RP1210 Mobile Native Android SDK from NEXIQ..."
    download_resumable "$SDK_URL" "$SDK_PART" || {
        echo "ERROR: Could not download the NEXIQ Android RP1210 SDK."
        echo "       Partial download, if any, was kept at:"
        echo "       $SDK_PART"
        exit 1
    }
    mv "$SDK_PART" "$SDK_ZIP" || exit 1
fi

mkdir -p "$VENDOR" "$EXTRACT" || exit 1

command -v unzip >/dev/null 2>&1 || {
    echo "ERROR: unzip is required to extract the NEXIQ SDK."
    exit 1
}

echo "[android] Extracting the vendor runtime payload..."
unzip -q "$SDK_ZIP" -d "$EXTRACT" || {
    echo "ERROR: Could not extract the NEXIQ SDK."
    echo "       Cached archive was kept at:"
    echo "       $SDK_ZIP"
    exit 1
}

for abi in arm64-v8a armeabi-v7a; do
    destination="$VENDOR/app/src/main/jniLibs/$abi"
    mkdir -p "$destination"

    for library in \
        libc++_shared.so \
        libnuln2r32.so \
        libnuln3r32.so \
        libnblr32.so \
        libnbl2r32.so \
        libcil7r32.so \
        libcimr32.so \
        libcim16r32.so \
        libculn3r32.so \
        libkuln3r32.so
    do
        match="$(find "$EXTRACT" -type f -path "*/$abi/$library" -print -quit)"
        if [ -z "$match" ]; then
            echo "ERROR: Could not find $library for $abi in the NEXIQ SDK."
            exit 1
        fi

        cp "$match" "$destination/$library"
    done
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
sh "$ROOT/gradlew" clean assembleDebug
RC=$?

if [ "$RC" -eq 0 ]; then
    echo
    echo "Android build complete:"
    echo "  src/android/app/build/outputs/apk/debug/app-debug.apk"
fi

exit "$RC"
