package com.irontom10.calibrationtransfer;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.text.InputFilter;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

public final class MainActivity extends Activity {
    private static final int PERMISSION_REQUEST = 1001;
    private static final int CREATE_PULL_FILE = 2001;
    private static final int OPEN_UPLOAD_FILE = 2002;
    private static final String LOG_TAG = "CalibrationTransfer";

    private static final Pattern MAC_PATTERN = Pattern.compile(
            "(?i)([0-9A-F]{2}:){5}[0-9A-F]{2}");

    private static final class DriverProfile {
        final String label;
        final String api;
        final String library;

        DriverProfile(String label, String api, String library) {
            this.label = label;
            this.api = api;
            this.library = library;
        }

        @Override public String toString() {
            return label;
        }
    }

    private static final DriverProfile[] DRIVERS = new DriverProfile[] {
            new DriverProfile("NEXIQ USB-Link 2",       "NULN2R32",  "nuln2r32"),
            new DriverProfile("NEXIQ USB-Link 3",       "NULN3R32",  "nuln3r32"),
            new DriverProfile("NEXIQ Blue-Link Mini",   "NBLR32",    "nblr32"),
            new DriverProfile("NEXIQ Blue-Link 2",      "NBL2R32",   "nbl2r32"),
            new DriverProfile("Cummins INLINE 7",       "CIL7R32",   "cil7r32"),
            new DriverProfile("Cummins INLINE Mini",    "CIMR32",    "cimr32"),
            new DriverProfile("Cummins INLINE Mini 16", "CIM16R32",  "cim16r32"),
            new DriverProfile("Cummins USB-Link 3",     "CULN3R32",  "culn3r32"),
            new DriverProfile("Kubota USB-Link 3",      "KULN3R32",  "kuln3r32")
    };

    private static final class DeviceEntry {
        final BluetoothDevice device;
        final String address;
        String name;
        int bondState;

        DeviceEntry(BluetoothDevice device,
                    String name,
                    String address,
                    int bondState) {
            this.device = device;
            this.name = name;
            this.address = address;
            this.bondState = bondState;
        }

        @Override public String toString() {
            String bond;

            if (bondState == BluetoothDevice.BOND_BONDED)
                bond = "paired";
            else if (bondState == BluetoothDevice.BOND_BONDING)
                bond = "pairing";
            else
                bond = "not paired";

            return name + "  [" + address + "]  - " + bond;
        }
    }

    private final NativeBridge nativeBridge = new NativeBridge();
    private final Map<String, DeviceEntry> devicesByMac =
            new LinkedHashMap<String, DeviceEntry>();

    /*
     * Discovery can report ACTION_FOUND before Android has finished resolving
     * the remote name.  Keep those candidates hidden and re-check them when
     * ACTION_NAME_CHANGED arrives / discovery finishes.  Only recognized
     * diagnostic adapters ever reach deviceRows.
     */
    private final Map<String, BluetoothDevice> scanCandidatesByMac =
            new LinkedHashMap<String, BluetoothDevice>();

    private final List<DeviceEntry> deviceRows =
            new ArrayList<DeviceEntry>();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private BluetoothAdapter bluetoothAdapter;
    private boolean receiverRegistered;
    private boolean acceptScanResults;
    private boolean restartDiscoveryAfterFinish;
    private boolean discoveryRetryPending;
    private volatile boolean busy;
    private boolean configLoaded;

    private Spinner driverSpinner;
    private Spinner deviceSpinner;
    private Spinner protocolSpinner;
    private Spinner baudSpinner;
    private EditText macEdit;
    private EditText toolSaEdit;
    private EditText ecmSaEdit;
    private ArrayAdapter<DeviceEntry> deviceAdapter;
    private ProgressBar progressBar;
    private TextView statusText;

    private Button refreshButton;
    private Button scanButton;
    private Button pairButton;
    private Button pullButton;
    private Button uploadButton;

    private final BroadcastReceiver bluetoothReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();

            if (BluetoothDevice.ACTION_FOUND.equals(action)) {
                BluetoothDevice device =
                        intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);

                if (device != null &&
                    acceptScanResults &&
                    safeBondState(device) != BluetoothDevice.BOND_BONDED) {
                    String mac = safeAddress(device);

                    if (mac.length() != 0)
                        scanCandidatesByMac.put(mac, device);

                    if (isDiagnosticAdapter(device))
                        addOrUpdateDevice(device);
                }
            }
            else if (BluetoothDevice.ACTION_NAME_CHANGED.equals(action)) {
                BluetoothDevice device =
                        intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);

                if (device != null) {
                    String mac = safeAddress(device);

                    if (scanCandidatesByMac.containsKey(mac) &&
                        safeBondState(device) != BluetoothDevice.BOND_BONDED &&
                        isDiagnosticAdapter(device)) {
                        scanCandidatesByMac.put(mac, device);
                        addOrUpdateDevice(device);
                    }
                }
            }
            else if (BluetoothAdapter.ACTION_DISCOVERY_STARTED.equals(action)) {
                acceptScanResults = true;
                scanButton.setText("Scanning...");
                setStatus("Bluetooth scan started. Looking for diagnostic adapters...");
            }
            else if (BluetoothAdapter.ACTION_DISCOVERY_FINISHED.equals(action)) {
                /*
                 * Some devices acquire their Bluetooth name late in discovery.
                 * Give every hidden candidate one final classification pass.
                 */
                for (BluetoothDevice device : scanCandidatesByMac.values()) {
                    if (safeBondState(device) != BluetoothDevice.BOND_BONDED &&
                        isDiagnosticAdapter(device)) {
                        addOrUpdateDevice(device);
                    }
                }

                acceptScanResults = false;
                scanButton.setText("Scan Bluetooth");

                if (restartDiscoveryAfterFinish) {
                    restartDiscoveryAfterFinish = false;
                    startBluetoothDiscovery(false);
                }
                else {
                    setStatus("Bluetooth scan finished. " +
                            deviceRows.size() + " diagnostic adapter(s) listed.");
                }
            }
            else if (BluetoothDevice.ACTION_BOND_STATE_CHANGED.equals(action)) {
                BluetoothDevice device =
                        intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);

                if (device != null &&
                    (isDiagnosticAdapter(device) ||
                     devicesByMac.containsKey(safeAddress(device)))) {
                    addOrUpdateDevice(device);

                    int state = intent.getIntExtra(
                            BluetoothDevice.EXTRA_BOND_STATE,
                            BluetoothDevice.ERROR);

                    if (state == BluetoothDevice.BOND_BONDED)
                        setStatus("Paired: " + safeName(device));
                }
            }
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        BluetoothManager manager =
                (BluetoothManager)getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = manager != null ? manager.getAdapter() : null;

        buildUi();
        logBuildInfo();
        registerBluetoothReceiver();
        copyVendorFiles();
        loadSettings();
        requestBluetoothPermissionsIfNeeded();

        if (hasBluetoothPermissions())
            refreshPairedDevices();

        if (configLoaded)
            setStatus("Ready. Settings: " + nativeBridge.configGetPath());
        else
            setStatus("Configuration unavailable: " +
                    nativeBridge.configGetLastError());
    }

    private void buildUi() {
        int pad = dp(10);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("ECM Calibration Transfer");
        title.setTextSize(22.0f);
        title.setGravity(Gravity.CENTER_HORIZONTAL);
        title.setPadding(0, 0, 0, pad);
        root.addView(title, fullWidth());

        driverSpinner = new Spinner(this);
        ArrayAdapter<DriverProfile> driverAdapter =
                largeSpinnerAdapter(DRIVERS);
        driverSpinner.setAdapter(driverAdapter);
        driverSpinner.setMinimumHeight(dp(56));
        driverSpinner.setSelection(1);
        root.addView(row("Vendor:", driverSpinner), fullWidth());

        deviceSpinner = new Spinner(this);
        deviceAdapter = largeSpinnerAdapter(deviceRows);
        deviceSpinner.setAdapter(deviceAdapter);
        deviceSpinner.setMinimumHeight(dp(56));
        root.addView(row("Device:", deviceSpinner), fullWidth());

        LinearLayout btButtons = new LinearLayout(this);
        btButtons.setOrientation(LinearLayout.HORIZONTAL);

        refreshButton = button("Refresh Paired");
        scanButton = button("Scan Bluetooth");
        pairButton = button("Pair");

        btButtons.addView(refreshButton, weighted());
        btButtons.addView(scanButton, weighted());
        btButtons.addView(pairButton, weighted());
        root.addView(btButtons, fullWidth());

        macEdit = new EditText(this);
        macEdit.setSingleLine(true);
        macEdit.setHint("Bluetooth MAC");
        root.addView(row("MAC:", macEdit), fullWidth());

        protocolSpinner = new Spinner(this);
        ArrayAdapter<String> protocolAdapter =
                largeSpinnerAdapter(new String[] { "J1939" });
        protocolSpinner.setAdapter(protocolAdapter);
        protocolSpinner.setMinimumHeight(dp(56));
        protocolSpinner.setEnabled(false);
        root.addView(row("Protocol:", protocolSpinner), fullWidth());

        baudSpinner = new Spinner(this);
        ArrayAdapter<String> baudAdapter =
                largeSpinnerAdapter(new String[] {
                        "Auto", "125000", "250000", "500000", "1000000"
                });
        baudSpinner.setAdapter(baudAdapter);
        baudSpinner.setMinimumHeight(dp(56));
        baudSpinner.setSelection(2);
        root.addView(row("Baud:", baudSpinner), fullWidth());

        toolSaEdit = hexByteEdit("FA");
        root.addView(row("Tool SA (hex):", toolSaEdit), fullWidth());

        ecmSaEdit = hexByteEdit("00");
        root.addView(row("ECM SA (hex):", ecmSaEdit), fullWidth());

        LinearLayout transferButtons = new LinearLayout(this);
        transferButtons.setOrientation(LinearLayout.HORIZONTAL);

        pullButton = button("Pull ECM CAL");
        uploadButton = button("Upload CCAL");

        transferButtons.addView(pullButton, weighted());
        transferButtons.addView(uploadButton, weighted());
        root.addView(transferButtons, fullWidth());

        progressBar = new ProgressBar(
                this,
                null,
                android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        progressBar.setProgress(0);
        root.addView(progressBar, fullWidth());

        statusText = new TextView(this);
        statusText.setTextIsSelectable(true);
        statusText.setPadding(0, pad, 0, pad);
        statusText.setMinLines(3);
        root.addView(statusText, fullWidth());

        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);

        refreshButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                refreshPairedDevices();
            }
        });

        scanButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                scanBluetooth();
            }
        });

        pairButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                pairSelectedDevice();
            }
        });

        deviceSpinner.setOnItemSelectedListener(
                new AdapterView.OnItemSelectedListener() {
                    @Override
                    public void onItemSelected(
                            AdapterView<?> parent,
                            View view,
                            int position,
                            long id) {
                        if (position < 0 || position >= deviceRows.size())
                            return;

                        DeviceEntry entry = deviceRows.get(position);
                        macEdit.setText(entry.address);

                        int driver = detectDriver(entry.name);
                        if (driver >= 0)
                            driverSpinner.setSelection(driver);
                    }

                    @Override
                    public void onNothingSelected(AdapterView<?> parent) {
                    }
                });

        pullButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                choosePullDestination();
            }
        });

        uploadButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                chooseUploadSource();
            }
        });
    }

    private <T> ArrayAdapter<T> largeSpinnerAdapter(final T[] items) {
        return new ArrayAdapter<T>(
                this,
                android.R.layout.simple_spinner_item,
                items) {
            private View sizeView(View view) {
                if (view instanceof TextView) {
                    TextView text = (TextView)view;
                    text.setTextSize(18.0f);
                    text.setMinHeight(dp(56));
                    text.setGravity(Gravity.CENTER_VERTICAL);
                    text.setPadding(dp(10), 0, dp(10), 0);
                }
                return view;
            }

            @Override
            public View getView(
                    int position,
                    View convertView,
                    android.view.ViewGroup parent) {
                return sizeView(
                        super.getView(position, convertView, parent));
            }

            @Override
            public View getDropDownView(
                    int position,
                    View convertView,
                    android.view.ViewGroup parent) {
                return sizeView(
                        super.getDropDownView(position, convertView, parent));
            }
        };
    }

    private <T> ArrayAdapter<T> largeSpinnerAdapter(final List<T> items) {
        return new ArrayAdapter<T>(
                this,
                android.R.layout.simple_spinner_item,
                items) {
            private View sizeView(View view) {
                if (view instanceof TextView) {
                    TextView text = (TextView)view;
                    text.setTextSize(18.0f);
                    text.setMinHeight(dp(56));
                    text.setGravity(Gravity.CENTER_VERTICAL);
                    text.setPadding(dp(10), 0, dp(10), 0);
                }
                return view;
            }

            @Override
            public View getView(
                    int position,
                    View convertView,
                    android.view.ViewGroup parent) {
                return sizeView(
                        super.getView(position, convertView, parent));
            }

            @Override
            public View getDropDownView(
                    int position,
                    View convertView,
                    android.view.ViewGroup parent) {
                return sizeView(
                        super.getDropDownView(position, convertView, parent));
            }
        };
    }

    private LinearLayout row(String labelText, View control) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        TextView label = new TextView(this);
        label.setText(labelText);
        label.setWidth(dp(125));

        row.addView(label);
        row.addView(control, new LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1.0f));

        return row;
    }

    private Button button(String text) {
        Button button = new Button(this);
        button.setText(text);
        return button;
    }

    private EditText hexByteEdit(String value) {
        EditText edit = new EditText(this);
        edit.setSingleLine(true);
        edit.setText(value);
        edit.setInputType(
                InputType.TYPE_CLASS_TEXT |
                InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS);
        edit.setFilters(new InputFilter[] { new InputFilter.LengthFilter(2) });
        return edit;
    }

    private LinearLayout.LayoutParams fullWidth() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams weighted() {
        return new LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                1.0f);
    }

    private int dp(int value) {
        return (int)(value *
                getResources().getDisplayMetrics().density + 0.5f);
    }

    private DriverProfile selectedDriver() {
        int index = driverSpinner.getSelectedItemPosition();

        if (index < 0 || index >= DRIVERS.length)
            return DRIVERS[1];

        return DRIVERS[index];
    }

    private int selectedBaud() {
        String value = String.valueOf(baudSpinner.getSelectedItem());

        if ("Auto".equalsIgnoreCase(value))
            return 0;

        try {
            return Integer.parseInt(value);
        }
        catch (NumberFormatException e) {
            return 250000;
        }
    }

    private int parseHexByte(EditText edit, String name) {
        try {
            int value = Integer.parseInt(
                    edit.getText().toString().trim(),
                    16);

            if (value < 0 || value > 255)
                throw new NumberFormatException();

            return value;
        }
        catch (NumberFormatException e) {
            throw new IllegalArgumentException(
                    name + " must be a two-digit hex byte.");
        }
    }

    private String selectedMac() {
        String mac = macEdit.getText().toString().trim().toUpperCase(Locale.US);

        if (!MAC_PATTERN.matcher(mac).matches())
            throw new IllegalArgumentException(
                    "Select a paired diagnostic adapter or enter a valid Bluetooth MAC.");

        return mac;
    }

    private void choosePullDestination() {
        if (!validateTransferSettings())
            return;

        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/octet-stream");
        intent.putExtra(Intent.EXTRA_TITLE, "ecm-upload.ccal");
        startActivityForResult(intent, CREATE_PULL_FILE);
    }

    private void chooseUploadSource() {
        if (!validateTransferSettings())
            return;

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, OPEN_UPLOAD_FILE);
    }

    @Override
    protected void onActivityResult(
            int requestCode,
            int resultCode,
            Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (resultCode != RESULT_OK ||
            data == null ||
            data.getData() == null) {
            return;
        }

        final Uri uri = data.getData();

        if (requestCode == CREATE_PULL_FILE) {
            beginPull(uri);
        }
        else if (requestCode == OPEN_UPLOAD_FILE) {
            new AlertDialog.Builder(this)
                    .setTitle("Confirm Calibration Upload")
                    .setMessage(
                            "Program this calibration into the ECM?\n\n" +
                            "The native uploader verifies the CCAL CRC before " +
                            "opening the RP1210 adapter. If CRC verification " +
                            "fails, no programming traffic is sent.")
                    .setNegativeButton("Cancel", null)
                    .setPositiveButton(
                            "Upload",
                            new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(
                                        DialogInterface dialog,
                                        int which) {
                                    beginUpload(uri);
                                }
                            })
                    .show();
        }
    }

    private boolean validateTransferSettings() {
        if (busy)
            return false;

        try {
            selectedMac();
            parseHexByte(toolSaEdit, "Tool SA");
            parseHexByte(ecmSaEdit, "ECM SA");
            return true;
        }
        catch (IllegalArgumentException e) {
            showMessage("Calibration Transfer", e.getMessage());
            return false;
        }
    }

    private void beginPull(final Uri destination) {
        final DriverProfile driver = selectedDriver();
        final int baud = selectedBaud();
        final int toolSa = parseHexByte(toolSaEdit, "Tool SA");
        final int ecmSa = parseHexByte(ecmSaEdit, "ECM SA");
        final String mac = selectedMac();
        final File temp = new File(
                getCacheDir(),
                "calibration-pull-" + System.currentTimeMillis() + ".ccal");

        saveSettings();
        setBusy(true);
        progressBar.setProgress(0);
        setStatus("Starting calibration download...");

        new Thread(new Runnable() {
            @Override public void run() {
                int rc;

                try {
                    prepareDriver(driver, mac);

                    rc = nativeBridge.pullCcal(
                            driver.api,
                            baud,
                            toolSa,
                            ecmSa,
                            temp.getAbsolutePath(),
                            MainActivity.this);

                    if (rc == 0)
                        copyFileToUri(temp, destination);

                    final int result = rc;
                    runOnUiThread(new Runnable() {
                        @Override public void run() {
                            if (result == 0) {
                                progressBar.setProgress(100);
                                setStatus("Calibration saved and CRC verified.");
                                showMessage(
                                        "Calibration Download",
                                        "ECM calibration saved and native CRC verified.");
                            }
                            else {
                                String error = nativeBridge.getLastError();
                                setStatus(error);
                                showMessage(
                                        "Calibration Download Failed",
                                        error + "\n\nNative return code: " + result);
                            }
                            setBusy(false);
                        }
                    });
                }
                catch (final Throwable t) {
                    runOnUiThread(new Runnable() {
                        @Override public void run() {
                            setStatus("Download failed: " + t);
                            showMessage("Calibration Download Failed", String.valueOf(t));
                            setBusy(false);
                        }
                    });
                }
                finally {
                    if (temp.exists())
                        temp.delete();

                    stopService(new Intent(
                            MainActivity.this,
                            TransferGuardService.class));
                }
            }
        }, "calibration-pull").start();
    }

    private void beginUpload(final Uri source) {
        final DriverProfile driver = selectedDriver();
        final int baud = selectedBaud();
        final int toolSa = parseHexByte(toolSaEdit, "Tool SA");
        final int ecmSa = parseHexByte(ecmSaEdit, "ECM SA");
        final String mac = selectedMac();
        final File temp = new File(
                getCacheDir(),
                "calibration-upload-" + System.currentTimeMillis() + ".ccal");

        saveSettings();
        setBusy(true);
        progressBar.setProgress(0);
        setStatus("Reading selected calibration file...");

        new Thread(new Runnable() {
            @Override public void run() {
                int rc;

                try {
                    copyUriToFile(source, temp);
                    prepareDriver(driver, mac);

                    rc = nativeBridge.uploadCcal(
                            driver.api,
                            baud,
                            toolSa,
                            ecmSa,
                            temp.getAbsolutePath(),
                            MainActivity.this);

                    final int result = rc;
                    runOnUiThread(new Runnable() {
                        @Override public void run() {
                            if (result == 0) {
                                progressBar.setProgress(100);
                                setStatus("Calibration upload complete.");
                                showMessage(
                                        "Calibration Upload",
                                        "Calibration upload completed successfully.");
                            }
                            else {
                                String error = nativeBridge.getLastError();
                                setStatus(error);
                                showMessage(
                                        "Calibration Upload Failed",
                                        error + "\n\nNative return code: " + result);
                            }
                            setBusy(false);
                        }
                    });
                }
                catch (final Throwable t) {
                    runOnUiThread(new Runnable() {
                        @Override public void run() {
                            setStatus("Upload failed: " + t);
                            showMessage("Calibration Upload Failed", String.valueOf(t));
                            setBusy(false);
                        }
                    });
                }
                finally {
                    if (temp.exists())
                        temp.delete();

                    stopService(new Intent(
                            MainActivity.this,
                            TransferGuardService.class));
                }
            }
        }, "calibration-upload").start();
    }

    private void prepareDriver(
            DriverProfile driver,
            String mac) {
        if (bluetoothAdapter != null) {
            try {
                if (hasBluetoothPermissions() &&
                    bluetoothAdapter.isDiscovering()) {
                    bluetoothAdapter.cancelDiscovery();
                }
            }
            catch (SecurityException ignored) {
            }
        }

        NativeBridge.loadVendorLibrary(driver.library);

        if (nativeBridge.configureRp1210(
                getFilesDir().getPath(),
                mac) == 0) {
            throw new IllegalStateException(
                    "Could not configure Android RP1210 device.");
        }
    }

    public void onNativeProgress(
            final int percent,
            final String message) {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                int value = percent;

                if (value < 0)
                    value = 0;
                if (value > 100)
                    value = 100;

                progressBar.setProgress(value);
                setStatus(message);
            }
        });
    }

    private void copyUriToFile(Uri uri, File file)
            throws IOException {
        InputStream in = null;
        FileOutputStream out = null;

        try {
            in = getContentResolver().openInputStream(uri);
            if (in == null)
                throw new IOException("Unable to open selected calibration file.");

            out = new FileOutputStream(file);
            copyStream(in, out);
        }
        finally {
            if (in != null)
                try { in.close(); } catch (IOException ignored) { }
            if (out != null)
                try { out.close(); } catch (IOException ignored) { }
        }
    }

    private void copyFileToUri(File file, Uri uri)
            throws IOException {
        FileInputStream in = null;
        OutputStream out = null;

        try {
            in = new FileInputStream(file);
            out = getContentResolver().openOutputStream(uri, "w");
            if (out == null)
                throw new IOException("Unable to open destination file.");

            copyStream(in, out);
        }
        finally {
            if (in != null)
                try { in.close(); } catch (IOException ignored) { }
            if (out != null)
                try { out.close(); } catch (IOException ignored) { }
        }
    }

    private void copyStream(InputStream in, OutputStream out)
            throws IOException {
        byte[] buffer = new byte[32768];
        int n;

        while ((n = in.read(buffer)) > 0)
            out.write(buffer, 0, n);

        out.flush();
    }

    private void setBusy(boolean value) {
        busy = value;
        setTransferGuard(value);

        if (value)
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        driverSpinner.setEnabled(!value);
        deviceSpinner.setEnabled(!value);
        baudSpinner.setEnabled(!value);
        macEdit.setEnabled(!value);
        toolSaEdit.setEnabled(!value);
        ecmSaEdit.setEnabled(!value);
        refreshButton.setEnabled(!value);
        scanButton.setEnabled(!value);
        pairButton.setEnabled(!value);
        pullButton.setEnabled(!value);
        uploadButton.setEnabled(!value);
    }

    private void setTransferGuard(boolean active) {
        Intent intent = new Intent(this, TransferGuardService.class);

        if (active) {
            if (Build.VERSION.SDK_INT >= 26)
                startForegroundService(intent);
            else
                startService(intent);
        }
        else {
            stopService(intent);
        }
    }

    private void logBuildInfo() {
        String versionName;
        long versionCode;

        try {
            android.content.pm.PackageInfo info =
                    getPackageManager().getPackageInfo(getPackageName(), 0);
            versionName = info.versionName == null ? "unknown" : info.versionName;
            if (Build.VERSION.SDK_INT >= 28)
                versionCode = info.getLongVersionCode();
            else
                versionCode = info.versionCode;
        }
        catch (PackageManager.NameNotFoundException e) {
            versionName = "unknown";
            versionCode = -1;
        }

        Log.i(LOG_TAG,
                "Build info: version=" + versionName +
                " versionCode=" + versionCode +
                " buildType=" + (getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) +
                " sdk=" + Build.VERSION.SDK_INT +
                " device=" + Build.MANUFACTURER + " " + Build.MODEL +
                " abi=" + Build.SUPPORTED_ABIS[0]);
    }

    private void showMessage(String title, String message) {
        new AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(message)
                .setPositiveButton("OK", null)
                .show();
    }

    private void setStatus(String text) {
        statusText.setText(text == null ? "" : text);
    }

    private void copyVendorFiles() {
        try {
            String[] names = getAssets().list("Files");

            if (names == null)
                return;

            byte[] buffer = new byte[16384];

            for (String name : names) {
                InputStream in = null;
                FileOutputStream out = null;

                try {
                    in = getAssets().open("Files/" + name);
                    out = openFileOutput(name, Context.MODE_PRIVATE);

                    int n;
                    while ((n = in.read(buffer)) > 0)
                        out.write(buffer, 0, n);
                }
                finally {
                    if (in != null)
                        try { in.close(); } catch (IOException ignored) { }
                    if (out != null)
                        try { out.close(); } catch (IOException ignored) { }
                }
            }
        }
        catch (IOException e) {
            setStatus("Vendor file setup failed: " + e);
        }
    }

    private boolean isDiagnosticAdapter(BluetoothDevice device) {
        return detectDriver(safeName(device)) >= 0;
    }

    /*
     * Match the Bluetooth names the hardware actually advertises, not the
     * pretty marketing descriptions from the RP1210 INI files.
     *
     * Seen on real hardware:
     *   CILMini_9677
     *   USBL3-36604
     *
     * The compact form also accepts the SDK/Windows descriptions such as
     * "CIMini Bluetooth", "Cummins INLINE Mini", and "USB-Link 3".
     */
    private String compactDeviceName(String deviceName) {
        if (deviceName == null)
            return "";

        return deviceName
                .toUpperCase(Locale.US)
                .replaceAll("[^A-Z0-9]", "");
    }

    private int detectDriver(String deviceName) {
        String name = compactDeviceName(deviceName);

        if (name.startsWith("CILMINI16") ||
            name.startsWith("CIMINI16") ||
            name.startsWith("CIM16") ||
            name.contains("INLINEMINI16"))
            return 6;

        if (name.startsWith("CILMINI") ||
            name.startsWith("CIMINI") ||
            name.contains("INLINEMINI"))
            return 5;

        if (name.startsWith("CIL7") ||
            name.contains("INLINE7"))
            return 4;

        if (name.startsWith("NBL2") ||
            name.contains("BLUELINK2"))
            return 3;

        if (name.startsWith("BLMINI") ||
            name.startsWith("NBL") ||
            name.contains("BLUELINK"))
            return 2;

        if (name.startsWith("KUSBL3") ||
            (name.contains("KUBOTA") &&
             (name.contains("USBL3") || name.contains("USBLINK3"))))
            return 8;

        if (name.startsWith("CUSBL3") ||
            (name.contains("CUMMINS") &&
             (name.contains("USBL3") || name.contains("USBLINK3"))))
            return 7;

        if (name.startsWith("USBL2") ||
            name.contains("USBLINK2"))
            return 0;

        if (name.startsWith("USBL3") ||
            name.contains("USBLINK3") ||
            name.equals("BTUSBLINK") ||
            name.startsWith("NEXIQUSBL3") ||
            name.startsWith("NEXIQUSBLINK3"))
            return 1;

        return -1;
    }

    private void requestBluetoothPermissionsIfNeeded() {
        List<String> needed = new ArrayList<String>();

        if (Build.VERSION.SDK_INT >= 31) {
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) !=
                    PackageManager.PERMISSION_GRANTED) {
                needed.add(Manifest.permission.BLUETOOTH_SCAN);
            }

            if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) !=
                    PackageManager.PERMISSION_GRANTED) {
                needed.add(Manifest.permission.BLUETOOTH_CONNECT);
            }
        }
        else if (Build.VERSION.SDK_INT >= 23) {
            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) !=
                    PackageManager.PERMISSION_GRANTED) {
                needed.add(Manifest.permission.ACCESS_FINE_LOCATION);
            }
        }

        if (!needed.isEmpty())
            requestPermissions(
                    needed.toArray(new String[needed.size()]),
                    PERMISSION_REQUEST);
    }

    private boolean hasBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= 31) {
            return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) ==
                        PackageManager.PERMISSION_GRANTED &&
                   checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) ==
                        PackageManager.PERMISSION_GRANTED;
        }

        if (Build.VERSION.SDK_INT >= 23) {
            return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) ==
                    PackageManager.PERMISSION_GRANTED;
        }

        return true;
    }

    @Override
    public void onRequestPermissionsResult(
            int requestCode,
            String[] permissions,
            int[] grantResults) {
        super.onRequestPermissionsResult(
                requestCode,
                permissions,
                grantResults);

        if (requestCode != PERMISSION_REQUEST)
            return;

        for (int result : grantResults) {
            if (result != PackageManager.PERMISSION_GRANTED) {
                setStatus("Bluetooth permission denied.");
                return;
            }
        }

        refreshPairedDevices();
    }

    private boolean bluetoothReady() {
        if (!hasBluetoothPermissions()) {
            requestBluetoothPermissionsIfNeeded();
            setStatus("Bluetooth permission is required.");
            return false;
        }

        if (bluetoothAdapter == null) {
            setStatus("This Android device has no Bluetooth adapter.");
            return false;
        }

        try {
            if (!bluetoothAdapter.isEnabled()) {
                startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));
                setStatus("Bluetooth is disabled.");
                return false;
            }
        }
        catch (SecurityException e) {
            setStatus("Bluetooth access failed: " + e);
            return false;
        }

        return true;
    }

    private void refreshPairedDevices() {
        if (!bluetoothReady())
            return;

        try {
            acceptScanResults = false;
            restartDiscoveryAfterFinish = false;
            discoveryRetryPending = false;
            scanCandidatesByMac.clear();

            if (bluetoothAdapter.isDiscovering())
                bluetoothAdapter.cancelDiscovery();

            devicesByMac.clear();
            rebuildDeviceRows();

            Set<BluetoothDevice> bonded = bluetoothAdapter.getBondedDevices();
            int added = 0;

            for (BluetoothDevice device : bonded) {
                if (isDiagnosticAdapter(device)) {
                    addOrUpdateDevice(device);
                    ++added;
                }
            }

            restoreSelectedMac();
            setStatus("Refreshed " + added +
                    " paired diagnostic adapter(s).");
        }
        catch (SecurityException e) {
            setStatus("Could not read paired devices: " + e);
        }
    }

    private void scanBluetooth() {
        if (!bluetoothReady())
            return;

        try {
            if (bluetoothAdapter.isDiscovering()) {
                restartDiscoveryAfterFinish = true;
                acceptScanResults = false;

                if (!bluetoothAdapter.cancelDiscovery()) {
                    restartDiscoveryAfterFinish = false;
                    setStatus("Could not stop existing Bluetooth discovery.");
                }
                return;
            }

            startBluetoothDiscovery(false);
        }
        catch (SecurityException e) {
            setStatus("Bluetooth scan failed: " + e);
        }
    }

    private void startBluetoothDiscovery(final boolean retry) {
        try {
            discoveryRetryPending = false;

            if (!retry)
                scanCandidatesByMac.clear();

            if (bluetoothAdapter.startDiscovery()) {
                acceptScanResults = true;
                scanButton.setText("Scanning...");
                setStatus("Scanning for diagnostic adapters...");
                return;
            }

            acceptScanResults = false;

            if (!retry) {
                discoveryRetryPending = true;
                setStatus("Bluetooth discovery did not start; retrying once...");

                mainHandler.postDelayed(new Runnable() {
                    @Override public void run() {
                        if (!discoveryRetryPending)
                            return;

                        discoveryRetryPending = false;
                        startBluetoothDiscovery(true);
                    }
                }, 750);
            }
            else {
                setStatus("Android refused to start Bluetooth discovery.");
                scanButton.setText("Scan Bluetooth");
            }
        }
        catch (SecurityException e) {
            setStatus("Bluetooth scan failed: " + e);
            scanButton.setText("Scan Bluetooth");
        }
    }

    private void pairSelectedDevice() {
        if (!bluetoothReady())
            return;

        BluetoothDevice device = null;
        int position = deviceSpinner.getSelectedItemPosition();

        if (position >= 0 && position < deviceRows.size())
            device = deviceRows.get(position).device;

        if (device == null) {
            String mac = macEdit.getText().toString().trim();

            if (!MAC_PATTERN.matcher(mac).matches()) {
                setStatus("Select a device or enter a valid Bluetooth MAC.");
                return;
            }

            try {
                device = bluetoothAdapter.getRemoteDevice(mac);
            }
            catch (IllegalArgumentException e) {
                setStatus("Invalid Bluetooth MAC.");
                return;
            }
        }

        try {
            if (bluetoothAdapter.isDiscovering())
                bluetoothAdapter.cancelDiscovery();

            int state = device.getBondState();

            if (state == BluetoothDevice.BOND_BONDED) {
                setStatus("Already paired: " + safeName(device));
                return;
            }

            if (state == BluetoothDevice.BOND_BONDING) {
                setStatus("Pairing is already in progress.");
                return;
            }

            if (!device.createBond())
                setStatus("Android did not start pairing.");
            else
                setStatus("Pairing with " + safeName(device) + "...");
        }
        catch (SecurityException e) {
            setStatus("Pairing failed: " + e);
        }
    }

    private void addOrUpdateDevice(BluetoothDevice device) {
        String mac = safeAddress(device);

        if (mac.length() == 0)
            return;

        String name = safeName(device);
        int bond = safeBondState(device);
        DeviceEntry existing = devicesByMac.get(mac);

        if (existing == null) {
            devicesByMac.put(
                    mac,
                    new DeviceEntry(device, name, mac, bond));
        }
        else {
            existing.name = name;
            existing.bondState = bond;
        }

        rebuildDeviceRows();
    }

    private void rebuildDeviceRows() {
        String selected = macEdit == null ?
                "" : macEdit.getText().toString();

        deviceRows.clear();
        deviceRows.addAll(devicesByMac.values());
        deviceAdapter.notifyDataSetChanged();

        if (selected.length() != 0) {
            for (int i = 0; i < deviceRows.size(); ++i) {
                if (selected.equalsIgnoreCase(deviceRows.get(i).address)) {
                    deviceSpinner.setSelection(i);
                    return;
                }
            }
        }

        if (!deviceRows.isEmpty())
            deviceSpinner.setSelection(0);
    }

    private String safeName(BluetoothDevice device) {
        try {
            String name = device.getName();

            return name == null || name.length() == 0 ?
                    "Unnamed diagnostic adapter" : name;
        }
        catch (SecurityException e) {
            return "Diagnostic adapter";
        }
    }

    private String safeAddress(BluetoothDevice device) {
        try {
            String address = device.getAddress();
            return address == null ? "" : address;
        }
        catch (SecurityException e) {
            return "";
        }
    }

    private int safeBondState(BluetoothDevice device) {
        try {
            return device.getBondState();
        }
        catch (SecurityException e) {
            return BluetoothDevice.BOND_NONE;
        }
    }

    private void registerBluetoothReceiver() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(BluetoothDevice.ACTION_FOUND);
        filter.addAction(BluetoothDevice.ACTION_NAME_CHANGED);
        filter.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
        filter.addAction(BluetoothAdapter.ACTION_DISCOVERY_STARTED);
        filter.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);

        if (Build.VERSION.SDK_INT >= 33)
            registerReceiver(
                    bluetoothReceiver,
                    filter,
                    Context.RECEIVER_EXPORTED);
        else
            registerReceiver(bluetoothReceiver, filter);

        receiverRegistered = true;
    }

    private void applySettings(
            String api,
            int baud,
            int toolSa,
            int ecmSa,
            String mac) {
        String baudText = baud == 0 ? "Auto" : String.valueOf(baud);

        for (int i = 0; i < DRIVERS.length; ++i) {
            if (DRIVERS[i].api.equalsIgnoreCase(api)) {
                driverSpinner.setSelection(i);
                break;
            }
        }

        for (int i = 0; i < baudSpinner.getCount(); ++i) {
            if (String.valueOf(baudSpinner.getItemAtPosition(i))
                    .equalsIgnoreCase(baudText)) {
                baudSpinner.setSelection(i);
                break;
            }
        }

        toolSaEdit.setText(String.format(Locale.US, "%02X", toolSa & 0xFF));
        ecmSaEdit.setText(String.format(Locale.US, "%02X", ecmSa & 0xFF));
        macEdit.setText(mac == null ? "" : mac);
    }

    private int legacyBaud(String value) {
        if (value == null || "Auto".equalsIgnoreCase(value))
            return 0;

        try {
            return Integer.parseInt(value);
        }
        catch (NumberFormatException e) {
            return 250000;
        }
    }

    private int legacyHexByte(String value, int defaultValue) {
        try {
            return Integer.parseInt(value == null ? "" : value.trim(), 16);
        }
        catch (NumberFormatException e) {
            return defaultValue;
        }
    }

    private boolean migrateLegacyPreferences() {
        SharedPreferences p = getSharedPreferences(
                "calibration_transfer",
                MODE_PRIVATE);

        if (!p.contains("api") &&
            !p.contains("baud") &&
            !p.contains("tool_sa") &&
            !p.contains("ecm_sa") &&
            !p.contains("mac")) {
            return false;
        }

        applySettings(
                p.getString("api", "NULN3R32"),
                legacyBaud(p.getString("baud", "250000")),
                legacyHexByte(p.getString("tool_sa", "FA"), 0xFA),
                legacyHexByte(p.getString("ecm_sa", "00"), 0x00),
                p.getString("mac", ""));

        if (!saveSettings())
            return false;

        p.edit().clear().apply();
        return true;
    }

    private void loadSettings() {
        File configFile = new File(getFilesDir(), "config.toml");
        boolean migrateLegacy = !configFile.exists();

        configLoaded =
                nativeBridge.configLoad(configFile.getAbsolutePath()) == 0;

        if (!configLoaded)
            return;

        if (migrateLegacy && migrateLegacyPreferences())
            return;

        applySettings(
                nativeBridge.configGetString(
                        "adapter",
                        "api",
                        "NULN3R32"),
                nativeBridge.configGetInt(
                        "adapter",
                        "baud",
                        250000),
                nativeBridge.configGetInt(
                        "j1939",
                        "tool_sa",
                        0xFA),
                nativeBridge.configGetInt(
                        "j1939",
                        "ecm_sa",
                        0x00),
                nativeBridge.configGetString(
                        "adapter",
                        "mac",
                        ""));
    }

    private boolean saveSettings() {
        int toolSa;
        int ecmSa;
        int rc;

        if (!configLoaded)
            return false;

        try {
            toolSa = parseHexByte(toolSaEdit, "Tool SA");
            ecmSa = parseHexByte(ecmSaEdit, "ECM SA");
        }
        catch (IllegalArgumentException e) {
            return false;
        }

        rc = nativeBridge.configSetString(
                "adapter",
                "api",
                selectedDriver().api);
        if (rc == 0) {
            rc = nativeBridge.configSetInt(
                    "adapter",
                    "baud",
                    selectedBaud());
        }
        if (rc == 0) {
            rc = nativeBridge.configSetString(
                    "adapter",
                    "mac",
                    macEdit.getText().toString().trim());
        }
        if (rc == 0) {
            rc = nativeBridge.configSetRaw(
                    "j1939",
                    "tool_sa",
                    String.format(Locale.US, "0x%02X", toolSa));
        }
        if (rc == 0) {
            rc = nativeBridge.configSetRaw(
                    "j1939",
                    "ecm_sa",
                    String.format(Locale.US, "0x%02X", ecmSa));
        }
        if (rc == 0)
            rc = nativeBridge.configSave();

        if (rc != 0) {
            setStatus("Could not save config.toml: " +
                    nativeBridge.configGetLastError());
            return false;
        }

        return true;
    }

    private void restoreSelectedMac() {
        String wanted = macEdit.getText().toString().trim();

        for (int i = 0; i < deviceRows.size(); ++i) {
            if (wanted.equalsIgnoreCase(deviceRows.get(i).address)) {
                deviceSpinner.setSelection(i);
                return;
            }
        }
    }

    @Override
    public void onBackPressed() {
        if (busy) {
            showMessage(
                    "Calibration Transfer In Progress",
                    "Do not close the app or disconnect the adapter until the calibration transfer has finished.");
            return;
        }

        super.onBackPressed();
    }

    @Override
    protected void onPause() {
        saveSettings();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        discoveryRetryPending = false;
        restartDiscoveryAfterFinish = false;
        acceptScanResults = false;
        mainHandler.removeCallbacksAndMessages(null);

        if (bluetoothAdapter != null) {
            try {
                if (hasBluetoothPermissions() &&
                    bluetoothAdapter.isDiscovering()) {
                    bluetoothAdapter.cancelDiscovery();
                }
            }
            catch (Throwable ignored) {
            }
        }

        if (receiverRegistered) {
            try {
                unregisterReceiver(bluetoothReceiver);
            }
            catch (Throwable ignored) {
            }
        }

        /*
         * A foreground transfer deliberately survives task removal.  The
         * worker thread still owns this Activity instance until native work
         * returns, so do not tear down the shared native config store beneath
         * it.  Normal destruction closes it as before.
         */
        if (configLoaded && !busy) {
            nativeBridge.configClose();
            configLoaded = false;
        }

        super.onDestroy();
    }
}
