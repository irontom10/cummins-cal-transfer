package com.irontom10.calibrationtransfer;

public final class NativeBridge {
    static {
        System.loadLibrary("caltransfer");
    }

    public static void loadVendorLibrary(String library) {
        System.loadLibrary(library);
    }

    public native int configureRp1210(String dataPath, String macAddress);

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
