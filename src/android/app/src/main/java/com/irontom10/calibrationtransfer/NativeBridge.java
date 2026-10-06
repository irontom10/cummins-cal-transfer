package com.irontom10.calibrationtransfer;

public final class NativeBridge {
    static {
        System.loadLibrary("caltransfer");
    }

    public static void loadVendorLibrary(String library) {
        System.loadLibrary(library);
    }

    public native int configureRp1210(String dataPath, String macAddress);

    public native int configLoad(String path);
    public native void configClose();
    public native String configGetString(
            String section,
            String key,
            String defaultValue);
    public native int configGetInt(
            String section,
            String key,
            int defaultValue);
    public native int configSetString(
            String section,
            String key,
            String value);
    public native int configSetInt(
            String section,
            String key,
            int value);
    public native int configSetRaw(
            String section,
            String key,
            String value);
    public native int configSave();
    public native String configGetPath();
    public native String configGetLastError();

    public native int pullCcal(
            String apiName,
            int baud,
            int toolSa,
            int ecmSa,
            String outputPath,
            Object progressTarget);

    public native int uploadCcal(
            String apiName,
            int baud,
            int toolSa,
            int ecmSa,
            String inputPath,
            Object progressTarget);

    public native String getLastError();
}
