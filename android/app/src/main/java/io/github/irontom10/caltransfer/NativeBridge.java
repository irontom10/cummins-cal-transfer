package io.github.irontom10.caltransfer;

public final class NativeBridge {
    static {
        System.loadLibrary("caltransfer");
    }

    private NativeBridge() {
    }

    public interface ProgressSink {
        void onProgress(int percent, String message);
    }

    public static native int pull(
            SlcanUsbTransport transport,
            int baud,
            int toolSa,
            int ecmSa,
            String outputPath,
            ProgressSink progress);

    public static native int upload(
            SlcanUsbTransport transport,
            int baud,
            int toolSa,
            int ecmSa,
            String ccalPath,
            ProgressSink progress);

    public static native String lastError();
}
