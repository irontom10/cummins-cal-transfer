#!/usr/bin/env sh
set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
VENDOR="$ROOT/.vendor-rp1210"
VENDOR_REPO="irontom10/rp1210-android-test"
RC=1

cleanup() {
    rm -rf "$VENDOR"
}
trap cleanup EXIT INT TERM

rm -rf "$VENDOR"

echo "[android] Staging private RP1210 Android vendor payload..."

if command -v gh >/dev/null 2>&1; then
    gh repo clone "$VENDOR_REPO" "$VENDOR" -- --depth 1 || exit 1
else
    git clone --depth 1 "https://github.com/$VENDOR_REPO.git" "$VENDOR" || exit 1
fi

test -f "$VENDOR/app/src/main/jniLibs/arm64-v8a/libnuln3r32.so" || {
    echo "ERROR: Vendor RP1210 libraries were not found."
    exit 1
}

test -f "$VENDOR/app/src/main/assets/Files/nuln3r32.ini" || {
    echo "ERROR: Vendor RP1210 assets were not found."
    exit 1
}

echo "[android] Building Calibration Transfer APK..."
"$VENDOR/gradlew" -p "$ROOT" clean assembleDebug
RC=$?

if [ "$RC" -eq 0 ]; then
    echo
    echo "Android build complete:"
    echo "  src/android/app/build/outputs/apk/debug/app-debug.apk"
fi

exit "$RC"
