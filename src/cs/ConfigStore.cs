using System;
using System.Runtime.InteropServices;
using System.Text;

internal static class NativeConfig
{
    private const string DllName = "ctconfig.dll";

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ct_config_load_default();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ct_config_save();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ct_config_close();

    [DllImport(
        DllName,
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    internal static extern int ct_config_get_string(
        string section,
        string key,
        string defaultValue,
        StringBuilder output,
        int outputSize);

    [DllImport(
        DllName,
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    internal static extern int ct_config_get_int(
        string section,
        string key,
        int defaultValue,
        out int value);

    [DllImport(
        DllName,
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    internal static extern int ct_config_set_string(
        string section,
        string key,
        string value);

    [DllImport(
        DllName,
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    internal static extern int ct_config_set_int(
        string section,
        string key,
        int value);

    [DllImport(
        DllName,
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    internal static extern int ct_config_set_raw(
        string section,
        string key,
        string tomlValue);

    [DllImport(
        DllName,
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    internal static extern int ct_config_get_last_error(
        StringBuilder output,
        int outputSize);
}

/*
 * UI-facing settings object.  Program.cs never parses TOML and the ECM/native
 * transport core never sees config data.  All persistence goes through the
 * standalone ctconfig.dll wrapper.
 */
internal sealed class UiConfig : IDisposable
{
    public string Api { get; set; } = String.Empty;
    public int DeviceId { get; set; } = -1;
    public int Baud { get; set; } = 250000;
    public byte ToolSa { get; set; } = 0xFA;
    public byte EcmSa { get; set; } = 0x00;

    private bool loaded;

    public bool Load(out string error)
    {
        StringBuilder text;
        int value;

        error = String.Empty;

        if (loaded)
            NativeConfig.ct_config_close();

        if (NativeConfig.ct_config_load_default() != 0)
        {
            error = GetNativeError();
            loaded = false;
            return false;
        }

        loaded = true;

        text = new StringBuilder(256);
        NativeConfig.ct_config_get_string(
            "adapter",
            "api",
            String.Empty,
            text,
            text.Capacity);
        Api = text.ToString();

        NativeConfig.ct_config_get_int(
            "adapter",
            "device",
            -1,
            out value);
        DeviceId = value;

        NativeConfig.ct_config_get_int(
            "adapter",
            "baud",
            250000,
            out value);
        Baud = value;

        NativeConfig.ct_config_get_int(
            "j1939",
            "tool_sa",
            0xFA,
            out value);
        if (value >= 0 && value <= 0xFF)
            ToolSa = (byte)value;

        NativeConfig.ct_config_get_int(
            "j1939",
            "ecm_sa",
            0x00,
            out value);
        if (value >= 0 && value <= 0xFF)
            EcmSa = (byte)value;

        return true;
    }

    public bool Save(out string error)
    {
        error = String.Empty;

        if (!loaded)
        {
            error = "Configuration is not loaded.";
            return false;
        }

        if (NativeConfig.ct_config_set_string(
                "adapter",
                "api",
                Api ?? String.Empty) != 0 ||
            NativeConfig.ct_config_set_int(
                "adapter",
                "device",
                DeviceId) != 0 ||
            NativeConfig.ct_config_set_int(
                "adapter",
                "baud",
                Baud) != 0 ||
            NativeConfig.ct_config_set_raw(
                "j1939",
                "tool_sa",
                "0x" + ToolSa.ToString("X2")) != 0 ||
            NativeConfig.ct_config_set_raw(
                "j1939",
                "ecm_sa",
                "0x" + EcmSa.ToString("X2")) != 0 ||
            NativeConfig.ct_config_save() != 0)
        {
            error = GetNativeError();
            return false;
        }

        return true;
    }

    private static string GetNativeError()
    {
        StringBuilder text = new StringBuilder(512);
        NativeConfig.ct_config_get_last_error(text, text.Capacity);
        return text.Length == 0
            ? "Unknown configuration error."
            : text.ToString();
    }

    public void Dispose()
    {
        if (!loaded)
            return;

        NativeConfig.ct_config_close();
        loaded = false;
    }
}
