using System;
using System.Runtime.InteropServices;
using System.Text;

internal static class NativeRP1210
{
    private const string DllName = "rp1210scan.dll";

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

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rp1210_refresh();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rp1210_count();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rp1210_get(int index, out Device device);

    [DllImport(
        DllName,
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
        DllName,
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
        DllName,
        CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    public static extern int rp1210_get_last_error(
        StringBuilder buffer,
        int bufferSize);
}
