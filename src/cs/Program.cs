using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

internal static class NativeRP1210
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct Device
    {
        public int DeviceId;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Api;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string Vendor;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Description;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void ProgressCallback(int percent, IntPtr message);

    [DllImport("rp1210scan.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int rp1210_refresh();

    [DllImport("rp1210scan.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int rp1210_count();

    [DllImport("rp1210scan.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int rp1210_get(int index, out Device device);

    [DllImport(
        "rp1210scan.dll",
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    public static extern int rp1210_pull_ccal(
        string api,
        int deviceId,
        int baud,
        byte toolSa,
        byte ecmSa,
        string outPath,
        ProgressCallback progress);

    [DllImport(
        "rp1210scan.dll",
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    public static extern int rp1210_upload_ccal(
        string api,
        int deviceId,
        int baud,
        byte toolSa,
        byte ecmSa,
        string ccalPath,
        ProgressCallback progress);

    [DllImport(
        "rp1210scan.dll",
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    public static extern int rp1210_get_last_error(
        StringBuilder buffer,
        int bufferSize);
}

public sealed class Rp1210Form : Form
{
    private ComboBox apiCombo;
    private ComboBox deviceCombo;
    private ComboBox protocolCombo;
    private ComboBox baudCombo;
    private TextBox toolSaText;
    private TextBox ecmSaText;
    private Button refreshButton;
    private Button pullButton;
    private Button uploadButton;
    private ProgressBar progressBar;
    private Label statusLabel;

    private readonly List<NativeRP1210.Device> devices =
        new List<NativeRP1210.Device>();

    private NativeRP1210.ProgressCallback nativeProgress;
    private readonly UiConfig uiConfig = new UiConfig();
    private bool configLoaded;

    public Rp1210Form()
    {
        Text = "ECM Calibration Transfer";
        Width = 700;
        Height = 500;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;

        CreateControls();

        Load += delegate
        {
            string configError;

            configLoaded = uiConfig.Load(out configError);
            RefreshDevices();

            if (configLoaded)
            {
                ApplyConfiguration();
            }
            else if (!String.IsNullOrEmpty(configError))
            {
                statusLabel.Text =
                    statusLabel.Text + " Config warning: " + configError;
            }
        };

        FormClosing += delegate
        {
            SaveConfiguration(false);
            uiConfig.Dispose();
        };
    }

    private Label MakeLabel(string text, int top)
    {
        Label label = new Label();
        label.Text = text;
        label.Left = 28;
        label.Top = top + 4;
        label.Width = 128;
        Controls.Add(label);
        return label;
    }

    private ComboBox MakeCombo(int top)
    {
        ComboBox combo = new ComboBox();
        combo.Left = 175;
        combo.Top = top;
        combo.Width = 360;
        combo.DropDownStyle = ComboBoxStyle.DropDownList;
        Controls.Add(combo);
        return combo;
    }

    private TextBox MakeTextBox(int top, string text)
    {
        TextBox box = new TextBox();
        box.Left = 175;
        box.Top = top;
        box.Width = 90;
        box.Text = text;
        box.CharacterCasing = CharacterCasing.Upper;
        box.MaxLength = 2;
        Controls.Add(box);
        return box;
    }

    private void CreateControls()
    {
        MakeLabel("Vendor:", 15);
        apiCombo = MakeCombo(15);

        MakeLabel("Device:", 55);
        deviceCombo = MakeCombo(55);

        MakeLabel("Protocol:", 105);
        protocolCombo = MakeCombo(105);
        protocolCombo.Items.Add("J1939");
        protocolCombo.SelectedIndex = 0;
        protocolCombo.Enabled = false;

        MakeLabel("Baud:", 145);
        baudCombo = MakeCombo(145);
        baudCombo.Items.Add("125000");
        baudCombo.Items.Add("250000");
        baudCombo.Items.Add("500000");
        baudCombo.Items.Add("1000000");
        baudCombo.SelectedItem = "250000";

        MakeLabel("Tool SA (hex):", 195);
        toolSaText = MakeTextBox(195, "FA");

        MakeLabel("ECM SA (hex):", 235);
        ecmSaText = MakeTextBox(235, "00");

        refreshButton = new Button();
        refreshButton.Text = "Refresh";
        refreshButton.Left = 175;
        refreshButton.Top = 295;
        refreshButton.Width = 125;
        refreshButton.Height = 32;
        refreshButton.Click += delegate { RefreshDevices(); };
        Controls.Add(refreshButton);

        pullButton = new Button();
        pullButton.Text = "Pull ECM CAL";
        pullButton.Left = 310;
        pullButton.Top = 295;
        pullButton.Width = 130;
        pullButton.Height = 32;
        pullButton.Click += PullClicked;
        Controls.Add(pullButton);

        uploadButton = new Button();
        uploadButton.Text = "Upload CCAL";
        uploadButton.Left = 455;
        uploadButton.Top = 295;
        uploadButton.Width = 130;
        uploadButton.Height = 32;
        uploadButton.Click += UploadClicked;
        Controls.Add(uploadButton);

        progressBar = new ProgressBar();
        progressBar.Left = 28;
        progressBar.Top = 360;
        progressBar.Width = 615;
        progressBar.Height = 22;
        progressBar.Minimum = 0;
        progressBar.Maximum = 100;
        Controls.Add(progressBar);

        statusLabel = new Label();
        statusLabel.Left = 28;
        statusLabel.Top = 395;
        statusLabel.Width = 615;
        statusLabel.Height = 45;
        statusLabel.Text = "Ready.";
        Controls.Add(statusLabel);

        apiCombo.SelectedIndexChanged += delegate
        {
            PopulateDevices();
        };
    }

    private void RefreshDevices()
    {
        int count;
        int i;
        int j;
        bool found;

        devices.Clear();
        apiCombo.Items.Clear();
        deviceCombo.Items.Clear();

        try
        {
            NativeRP1210.rp1210_refresh();
            count = NativeRP1210.rp1210_count();

            for (i = 0; i < count; ++i)
            {
                NativeRP1210.Device d;

                if (NativeRP1210.rp1210_get(i, out d) == 0)
                    continue;

                devices.Add(d);
                found = false;

                for (j = 0; j < apiCombo.Items.Count; ++j)
                {
                    ApiItem item = (ApiItem)apiCombo.Items[j];
                    if (String.Equals(
                            item.Api,
                            d.Api,
                            StringComparison.OrdinalIgnoreCase))
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                    apiCombo.Items.Add(new ApiItem(d.Api, d.Vendor));
            }

            if (apiCombo.Items.Count > 0)
            {
                apiCombo.SelectedIndex = 0;
                statusLabel.Text = count.ToString() + " RP1210 device(s) found.";
            }
            else
            {
                statusLabel.Text = "No RP1210 devices found.";
            }
        }
        catch (DllNotFoundException)
        {
            statusLabel.Text = "rp1210scan.dll not found.";
        }
        catch (BadImageFormatException)
        {
            statusLabel.Text = "32/64-bit DLL architecture mismatch. Build both pieces x86.";
        }
        catch (EntryPointNotFoundException)
        {
            statusLabel.Text = "rp1210scan.dll is missing the uploader exports.";
        }
        catch (Exception ex)
        {
            statusLabel.Text = "Error: " + ex.Message;
        }
    }

    private void PopulateDevices()
    {
        ApiItem selectedApi;
        int i;

        deviceCombo.Items.Clear();
        deviceCombo.Items.Add(new DeviceItem(-1, "Auto - first device for this API"));

        if (apiCombo.SelectedItem == null)
        {
            deviceCombo.SelectedIndex = 0;
            return;
        }

        selectedApi = (ApiItem)apiCombo.SelectedItem;

        for (i = 0; i < devices.Count; ++i)
        {
            NativeRP1210.Device d = devices[i];

            if (!String.Equals(
                    d.Api,
                    selectedApi.Api,
                    StringComparison.OrdinalIgnoreCase))
                continue;

            deviceCombo.Items.Add(
                new DeviceItem(
                    d.DeviceId,
                    d.DeviceId.ToString() + " - " + d.Description));
        }

        deviceCombo.SelectedIndex = 0;
    }

    private void ApplyConfiguration()
    {
        int i;
        int wantedApi;
        int wantedDevice;
        string baudText;

        if (!configLoaded)
            return;

        wantedApi = -1;
        for (i = 0; i < apiCombo.Items.Count; ++i)
        {
            ApiItem item = (ApiItem)apiCombo.Items[i];
            if (String.Equals(
                    item.Api,
                    uiConfig.Api,
                    StringComparison.OrdinalIgnoreCase))
            {
                wantedApi = i;
                break;
            }
        }

        if (wantedApi >= 0)
            apiCombo.SelectedIndex = wantedApi;

        wantedDevice = -1;
        for (i = 0; i < deviceCombo.Items.Count; ++i)
        {
            DeviceItem item = (DeviceItem)deviceCombo.Items[i];
            if (item.DeviceId == uiConfig.DeviceId)
            {
                wantedDevice = i;
                break;
            }
        }

        if (wantedDevice >= 0)
            deviceCombo.SelectedIndex = wantedDevice;

        baudText = uiConfig.Baud.ToString();
        if (baudCombo.Items.Contains(baudText))
            baudCombo.SelectedItem = baudText;

        toolSaText.Text = uiConfig.ToolSa.ToString("X2");
        ecmSaText.Text = uiConfig.EcmSa.ToString("X2");
    }

    private bool SaveConfiguration(bool showError)
    {
        ApiItem api;
        DeviceItem device;
        int baud;
        byte toolSa;
        byte ecmSa;
        string error;

        if (!configLoaded ||
            apiCombo.SelectedItem == null ||
            deviceCombo.SelectedItem == null)
            return false;

        api = (ApiItem)apiCombo.SelectedItem;
        device = (DeviceItem)deviceCombo.SelectedItem;

        if (!Int32.TryParse((string)baudCombo.SelectedItem, out baud))
            baud = 250000;

        if (!TryParseHexByte(toolSaText.Text, out toolSa) ||
            !TryParseHexByte(ecmSaText.Text, out ecmSa))
            return false;

        uiConfig.Api = api.Api;
        uiConfig.DeviceId = device.DeviceId;
        uiConfig.Baud = baud;
        uiConfig.ToolSa = toolSa;
        uiConfig.EcmSa = ecmSa;

        if (uiConfig.Save(out error))
            return true;

        if (showError)
        {
            MessageBox.Show(
                error,
                "Configuration Save Failed",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
        }

        return false;
    }

    private int ResolveDeviceId(string api, int requestedId)
    {
        int i;

        if (requestedId >= 0)
            return requestedId;

        for (i = 0; i < devices.Count; ++i)
        {
            if (String.Equals(
                    devices[i].Api,
                    api,
                    StringComparison.OrdinalIgnoreCase))
                return devices[i].DeviceId;
        }

        return -1;
    }

    private static bool TryParseHexByte(string text, out byte value)
    {
        try
        {
            int parsed = Convert.ToInt32(text.Trim(), 16);
            if (parsed < 0 || parsed > 255)
            {
                value = 0;
                return false;
            }

            value = (byte)parsed;
            return true;
        }
        catch
        {
            value = 0;
            return false;
        }
    }

    private void SetBusy(bool busy)
    {
        apiCombo.Enabled = !busy;
        deviceCombo.Enabled = !busy;
        baudCombo.Enabled = !busy;
        toolSaText.Enabled = !busy;
        ecmSaText.Enabled = !busy;
        refreshButton.Enabled = !busy;
        pullButton.Enabled = !busy;
        uploadButton.Enabled = !busy;
    }

    private string GetNativeError()
    {
        StringBuilder sb = new StringBuilder(512);
        NativeRP1210.rp1210_get_last_error(sb, sb.Capacity);
        if (sb.Length == 0)
            return "Unknown native uploader error.";
        return sb.ToString();
    }

    private async void PullClicked(object sender, EventArgs e)
    {
        ApiItem api;
        DeviceItem device;
        int deviceId;
        int baud;
        byte toolSa;
        byte ecmSa;
        string path;
        int rc;

        if (apiCombo.SelectedItem == null || deviceCombo.SelectedItem == null)
        {
            MessageBox.Show(
                "Select an RP1210 API/device.",
                "Calibration Download",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            return;
        }

        api = (ApiItem)apiCombo.SelectedItem;
        device = (DeviceItem)deviceCombo.SelectedItem;
        deviceId = ResolveDeviceId(api.Api, device.DeviceId);

        if (deviceId < 0)
        {
            MessageBox.Show(
                "No physical device is available for the selected RP1210 API.",
                "Calibration Download",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            return;
        }

        if (!Int32.TryParse((string)baudCombo.SelectedItem, out baud))
            baud = 250000;

        if (!TryParseHexByte(toolSaText.Text, out toolSa) ||
            !TryParseHexByte(ecmSaText.Text, out ecmSa))
        {
            MessageBox.Show(
                "Tool SA and ECM SA must be two-digit hex values, e.g. FA and 00.",
                "Calibration Download",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            return;
        }

        SaveConfiguration(false);

        using (SaveFileDialog dialog = new SaveFileDialog())
        {
            dialog.Filter = "Calibration (*.ccal)|*.ccal|All files (*.*)|*.*";
            dialog.DefaultExt = "ccal";
            dialog.AddExtension = true;
            dialog.FileName = "ecm-upload.ccal";

            if (dialog.ShowDialog(this) != DialogResult.OK)
                return;

            path = dialog.FileName;
        }

        progressBar.Value = 0;
        statusLabel.Text = "Starting...";
        SetBusy(true);

        nativeProgress = delegate(int percent, IntPtr message)
        {
            string text = Marshal.PtrToStringAnsi(message) ?? String.Empty;

            try
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    if (percent < 0)
                        percent = 0;
                    if (percent > 100)
                        percent = 100;
                    progressBar.Value = percent;
                    statusLabel.Text = text;
                });
            }
            catch (InvalidOperationException)
            {
            }
        };

        try
        {
            rc = await Task.Run(delegate
            {
                return NativeRP1210.rp1210_pull_ccal(
                    api.Api,
                    deviceId,
                    baud,
                    toolSa,
                    ecmSa,
                    path,
                    nativeProgress);
            });

            if (rc == 0)
            {
                progressBar.Value = 100;
                statusLabel.Text = "Saved and native CRC verified: " + path;
                MessageBox.Show(
                    "ECM calibration saved and calibration CRC verified.\r\n\r\n" + path,
                    "Calibration Download",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
            }
            else
            {
                string error = GetNativeError();
                statusLabel.Text = error;
                MessageBox.Show(
                    error + "\r\n\r\nNative return code: " + rc.ToString(),
                    "Calibration Download Failed",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
        }
        catch (DllNotFoundException)
        {
            statusLabel.Text = "rp1210scan.dll not found.";
        }
        catch (BadImageFormatException)
        {
            statusLabel.Text = "Architecture mismatch. Build/run the GUI and DLL as x86.";
        }
        catch (EntryPointNotFoundException)
        {
            statusLabel.Text = "Uploader entry point missing. Rebuild rp1210scan.dll.";
        }
        catch (Exception ex)
        {
            statusLabel.Text = "Error: " + ex.Message;
        }
        finally
        {
            nativeProgress = null;
            SetBusy(false);
        }
    }


    private async void UploadClicked(object sender, EventArgs e)
    {
        ApiItem api;
        DeviceItem device;
        int deviceId;
        int baud;
        byte toolSa;
        byte ecmSa;
        string path;
        int rc;

        if (apiCombo.SelectedItem == null || deviceCombo.SelectedItem == null)
        {
            MessageBox.Show(
                "Select an RP1210 API/device.",
                "Calibration Upload",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            return;
        }

        api = (ApiItem)apiCombo.SelectedItem;
        device = (DeviceItem)deviceCombo.SelectedItem;
        deviceId = ResolveDeviceId(api.Api, device.DeviceId);

        if (deviceId < 0)
        {
            MessageBox.Show(
                "No physical device is available for the selected RP1210 API.",
                "Calibration Upload",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            return;
        }

        if (!Int32.TryParse((string)baudCombo.SelectedItem, out baud))
            baud = 250000;

        if (!TryParseHexByte(toolSaText.Text, out toolSa) ||
            !TryParseHexByte(ecmSaText.Text, out ecmSa))
        {
            MessageBox.Show(
                "Tool SA and ECM SA must be two-digit hex values, e.g. FA and 00.",
                "Calibration Upload",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            return;
        }

        SaveConfiguration(false);

        using (OpenFileDialog dialog = new OpenFileDialog())
        {
            dialog.Filter = "Calibration (*.ccal)|*.ccal|All files (*.*)|*.*";
            dialog.CheckFileExists = true;
            dialog.Multiselect = false;

            if (dialog.ShowDialog(this) != DialogResult.OK)
                return;

            path = dialog.FileName;
        }

        if (MessageBox.Show(
                "Program this calibration into the ECM?\r\n\r\n" +
                path +
                "\r\n\r\nThe native uploader will verify the CCAL CRC " +
                "before opening the RP1210 adapter. If CRC verification fails, " +
                "no programming traffic is sent.",
                "Confirm Calibration Upload",
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning,
                MessageBoxDefaultButton.Button2) != DialogResult.Yes)
            return;

        progressBar.Value = 0;
        statusLabel.Text = "Verifying calibration CRC...";
        SetBusy(true);

        nativeProgress = delegate(int percent, IntPtr message)
        {
            string text = Marshal.PtrToStringAnsi(message) ?? String.Empty;

            try
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    if (percent < 0)
                        percent = 0;
                    if (percent > 100)
                        percent = 100;
                    progressBar.Value = percent;
                    statusLabel.Text = text;
                });
            }
            catch (InvalidOperationException)
            {
            }
        };

        try
        {
            rc = await Task.Run(delegate
            {
                return NativeRP1210.rp1210_upload_ccal(
                    api.Api,
                    deviceId,
                    baud,
                    toolSa,
                    ecmSa,
                    path,
                    nativeProgress);
            });

            if (rc == 0)
            {
                progressBar.Value = 100;
                statusLabel.Text = "Calibration upload completed.";
                MessageBox.Show(
                    "Calibration upload completed successfully.\r\n\r\n" + path,
                    "Calibration Upload",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
            }
            else
            {
                string error = GetNativeError();
                statusLabel.Text = error;
                MessageBox.Show(
                    error + "\r\n\r\nNative return code: " + rc.ToString(),
                    "Calibration Upload Failed",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
        }
        catch (DllNotFoundException)
        {
            statusLabel.Text = "rp1210scan.dll not found.";
        }
        catch (BadImageFormatException)
        {
            statusLabel.Text = "Architecture mismatch. Build/run the GUI and DLL as x86.";
        }
        catch (EntryPointNotFoundException)
        {
            statusLabel.Text = "Upload entry point missing. Rebuild rp1210scan.dll.";
        }
        catch (Exception ex)
        {
            statusLabel.Text = "Error: " + ex.Message;
        }
        finally
        {
            nativeProgress = null;
            SetBusy(false);
        }
    }

    private sealed class ApiItem
    {
        public readonly string Api;
        public readonly string Vendor;

        public ApiItem(string api, string vendor)
        {
            Api = api;
            Vendor = vendor;
        }

        public override string ToString()
        {
            if (String.IsNullOrEmpty(Vendor))
                return Api;

            if (String.Equals(Vendor, Api, StringComparison.OrdinalIgnoreCase))
                return Api;

            return Vendor + " (" + Api + ")";
        }
    }

    private sealed class DeviceItem
    {
        public readonly int DeviceId;
        private readonly string text;

        public DeviceItem(int deviceId, string text)
        {
            DeviceId = deviceId;
            this.text = text;
        }

        public override string ToString()
        {
            return text;
        }
    }
}

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        EmbeddedNative.Initialize();

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new Rp1210Form());
    }
}
