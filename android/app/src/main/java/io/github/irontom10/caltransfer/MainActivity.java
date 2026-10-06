package io.github.irontom10.caltransfer;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int REQUEST_PULL_FILE = 1001;
    private static final int REQUEST_UPLOAD_FILE = 1002;

    private String usbPermissionAction;
    private UsbManager usbManager;
    private SlcanUsbTransport transport;
    private ExecutorService worker;

    private TextView adapterStatus;
    private TextView status;
    private ProgressBar progress;
    private Spinner baudSpinner;
    private EditText toolSa;
    private EditText ecmSa;
    private Button connectButton;
    private Button pullButton;
    private Button uploadButton;

    private final BroadcastReceiver usbPermissionReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!usbPermissionAction.equals(intent.getAction()))
                return;

            UsbDevice device;
            if (Build.VERSION.SDK_INT >= 33)
                device = intent.getParcelableExtra(
                        UsbManager.EXTRA_DEVICE, UsbDevice.class);
            else
                device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);

            if (!intent.getBooleanExtra(
                    UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                setStatus("USB permission denied.");
                return;
            }

            connectDevice(device);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_main);

        usbManager = (UsbManager)getSystemService(Context.USB_SERVICE);
        worker = Executors.newSingleThreadExecutor();
        usbPermissionAction = getPackageName() + ".USB_PERMISSION";

        adapterStatus = findViewById(R.id.adapterStatus);
        status = findViewById(R.id.status);
        progress = findViewById(R.id.progress);
        baudSpinner = findViewById(R.id.baudSpinner);
        toolSa = findViewById(R.id.toolSa);
        ecmSa = findViewById(R.id.ecmSa);
        connectButton = findViewById(R.id.connectButton);
        pullButton = findViewById(R.id.pullButton);
        uploadButton = findViewById(R.id.uploadButton);

        ArrayAdapter<String> baudAdapter = new ArrayAdapter<>(
                this,
                android.R.layout.simple_spinner_item,
                new String[] {"250000", "500000", "125000", "1000000"});
        baudAdapter.setDropDownViewResource(
                android.R.layout.simple_spinner_dropdown_item);
        baudSpinner.setAdapter(baudAdapter);

        connectButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                chooseUsbDevice();
            }
        });

        pullButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if (!checkReady())
                    return;

                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/octet-stream");
                intent.putExtra(Intent.EXTRA_TITLE, "ecm-upload.ccal");
                startActivityForResult(intent, REQUEST_PULL_FILE);
            }
        });

        uploadButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if (!checkReady())
                    return;

                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                startActivityForResult(intent, REQUEST_UPLOAD_FILE);
            }
        });

        IntentFilter filter = new IntentFilter(usbPermissionAction);
        if (Build.VERSION.SDK_INT >= 33)
            registerReceiver(usbPermissionReceiver, filter, RECEIVER_NOT_EXPORTED);
        else
            registerReceiver(usbPermissionReceiver, filter);
    }

    @Override
    protected void onDestroy() {
        try {
            unregisterReceiver(usbPermissionReceiver);
        }
        catch (IllegalArgumentException ignored) {
        }

        if (transport != null) {
            transport.shutdown();
            transport = null;
        }

        worker.shutdownNow();
        super.onDestroy();
    }

    @Override
    protected void onActivityResult(
            int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (resultCode != RESULT_OK || data == null || data.getData() == null)
            return;

        Uri uri = data.getData();
        if (requestCode == REQUEST_PULL_FILE) {
            runPull(uri);
        }
        else if (requestCode == REQUEST_UPLOAD_FILE) {
            confirmUpload(uri);
        }
    }

    private void chooseUsbDevice() {
        final List<UsbDevice> devices = new ArrayList<>();
        final List<String> labels = new ArrayList<>();

        for (UsbDevice device : usbManager.getDeviceList().values()) {
            if (!SlcanUsbTransport.isCandidate(device))
                continue;
            devices.add(device);
            String name = device.getProductName();
            if (name == null)
                name = device.getDeviceName();
            labels.add(name);
        }

        if (devices.isEmpty()) {
            Toast.makeText(
                    this,
                    "No USB device with bulk IN/OUT endpoints was found.",
                    Toast.LENGTH_LONG).show();
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle("USB CAN adapter")
                .setItems(labels.toArray(new String[0]),
                        (dialog, which) -> requestUsbPermission(devices.get(which)))
                .show();
    }

    private void requestUsbPermission(UsbDevice device) {
        if (usbManager.hasPermission(device)) {
            connectDevice(device);
            return;
        }

        PendingIntent permissionIntent = PendingIntent.getBroadcast(
                this,
                0,
                new Intent(usbPermissionAction).setPackage(getPackageName()),
                PendingIntent.FLAG_IMMUTABLE);
        usbManager.requestPermission(device, permissionIntent);
    }

    private void connectDevice(UsbDevice device) {
        SlcanUsbTransport next;

        if (device == null) {
            setStatus("USB device disappeared.");
            return;
        }

        next = SlcanUsbTransport.create(usbManager, device);
        if (next == null) {
            setStatus("Could not open USB device as an SLCAN transport.");
            return;
        }

        if (transport != null)
            transport.shutdown();

        transport = next;
        adapterStatus.setText("USB adapter: " + transport.label());
        setStatus("USB adapter ready.");
    }

    private boolean checkReady() {
        if (transport == null) {
            Toast.makeText(
                    this, "Connect a USB CAN adapter first.", Toast.LENGTH_SHORT).show();
            return false;
        }

        try {
            selectedBaud();
            selectedSa(toolSa);
            selectedSa(ecmSa);
            return true;
        }
        catch (NumberFormatException ex) {
            Toast.makeText(
                    this,
                    "Bitrate and source addresses are invalid.",
                    Toast.LENGTH_LONG).show();
            return false;
        }
    }

    private int selectedBaud() {
        return Integer.parseInt((String)baudSpinner.getSelectedItem());
    }

    private static int selectedSa(EditText edit) {
        int value = Integer.parseInt(edit.getText().toString().trim(), 16);
        if (value < 0 || value > 255)
            throw new NumberFormatException("source address");
        return value;
    }

    private NativeBridge.ProgressSink progressSink() {
        return new NativeBridge.ProgressSink() {
            @Override
            public void onProgress(final int percent, final String message) {
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        progress.setProgress(Math.max(0, Math.min(100, percent)));
                        status.setText(message == null ? "" : message);
                    }
                });
            }
        };
    }

    private void runPull(final Uri destination) {
        final int baud = selectedBaud();
        final int tool = selectedSa(toolSa);
        final int ecm = selectedSa(ecmSa);

        setBusy(true);
        progress.setProgress(0);
        setStatus("Starting Android J1939 transfer...");

        worker.submit(new Runnable() {
            @Override
            public void run() {
                File temp = new File(getCacheDir(), "cal-pull.ccal");
                int rc = NativeBridge.pull(
                        transport, baud, tool, ecm,
                        temp.getAbsolutePath(), progressSink());

                if (rc == 0) {
                    try {
                        copyFileToUri(temp, destination);
                        finishOperation(true, "Calibration saved.");
                    }
                    catch (Exception ex) {
                        finishOperation(false,
                                "Transfer succeeded, but saving failed: " + ex.getMessage());
                    }
                }
                else {
                    finishOperation(false, NativeBridge.lastError());
                }

                temp.delete();
            }
        });
    }

    private void confirmUpload(final Uri source) {
        new AlertDialog.Builder(this)
                .setTitle("Program calibration?")
                .setMessage(
                        "The native core verifies the CCAL CRC before CAN traffic is sent. "
                        + "Use stable vehicle power and do not disconnect the USB adapter.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Program", (dialog, which) -> runUpload(source))
                .show();
    }

    private void runUpload(final Uri source) {
        final int baud = selectedBaud();
        final int tool = selectedSa(toolSa);
        final int ecm = selectedSa(ecmSa);

        setBusy(true);
        progress.setProgress(0);
        setStatus("Copying calibration into app cache...");

        worker.submit(new Runnable() {
            @Override
            public void run() {
                File temp = new File(getCacheDir(), "cal-upload.ccal");

                try {
                    copyUriToFile(source, temp);
                }
                catch (Exception ex) {
                    finishOperation(false, "Could not read calibration: " + ex.getMessage());
                    return;
                }

                int rc = NativeBridge.upload(
                        transport, baud, tool, ecm,
                        temp.getAbsolutePath(), progressSink());

                if (rc == 0)
                    finishOperation(true, "Calibration upload completed.");
                else
                    finishOperation(false, NativeBridge.lastError());

                temp.delete();
            }
        });
    }

    private void finishOperation(final boolean ok, final String message) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                setBusy(false);
                status.setText(message == null ? "" : message);
                if (ok)
                    progress.setProgress(100);
                new AlertDialog.Builder(MainActivity.this)
                        .setTitle(ok ? "Calibration Transfer" : "Transfer failed")
                        .setMessage(message == null ? "" : message)
                        .setPositiveButton("OK", null)
                        .show();
            }
        });
    }

    private void setBusy(boolean busy) {
        connectButton.setEnabled(!busy);
        pullButton.setEnabled(!busy);
        uploadButton.setEnabled(!busy);
        baudSpinner.setEnabled(!busy);
        toolSa.setEnabled(!busy);
        ecmSa.setEnabled(!busy);
    }

    private void setStatus(final String text) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                status.setText(text);
            }
        });
    }

    private void copyFileToUri(File source, Uri destination) throws Exception {
        try (InputStream in = new FileInputStream(source);
             OutputStream out = getContentResolver().openOutputStream(destination, "w")) {
            if (out == null)
                throw new IllegalStateException("No output stream.");
            copy(in, out);
        }
    }

    private void copyUriToFile(Uri source, File destination) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(source);
             OutputStream out = new FileOutputStream(destination)) {
            if (in == null)
                throw new IllegalStateException("No input stream.");
            copy(in, out);
        }
    }

    private static void copy(InputStream in, OutputStream out) throws Exception {
        byte[] buffer = new byte[64 * 1024];
        int n;

        while ((n = in.read(buffer)) >= 0) {
            if (n != 0)
                out.write(buffer, 0, n);
        }
        out.flush();
    }
}
