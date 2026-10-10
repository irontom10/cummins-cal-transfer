using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

internal static class EmbeddedNative
{
    private static bool resolverInstalled;

    public static void Initialize()
    {
        if (resolverInstalled)
            return;

        NativeLibrary.SetDllImportResolver(
            typeof(EmbeddedNative).Assembly,
            ResolveNativeLibrary);

        resolverInstalled = true;
    }

    private static bool IsEmbeddedNative(string libraryName)
    {
        return String.Equals(
                   libraryName,
                   "rp1210scan.dll",
                   StringComparison.OrdinalIgnoreCase) ||
               String.Equals(
                   libraryName,
                   "ctconfig.dll",
                   StringComparison.OrdinalIgnoreCase);
    }

    private static IntPtr ResolveNativeLibrary(
        string libraryName,
        Assembly assembly,
        DllImportSearchPath? searchPath)
    {
        Stream resource;
        MemoryStream memory;
        byte[] image;
        byte[] digest;
        string hash;
        string nativeDir;
        string dllPath;
        string tempPath;
        string stem;

        if (!IsEmbeddedNative(libraryName))
            return IntPtr.Zero;

        resource = assembly.GetManifestResourceStream(libraryName);
        if (resource == null)
            throw new DllNotFoundException(
                "Embedded native resource " + libraryName + " was not found.");

        using (resource)
        using (memory = new MemoryStream())
        {
            resource.CopyTo(memory);
            image = memory.ToArray();
        }

        digest = SHA256.HashData(image);
        hash = Convert.ToHexString(digest);

        nativeDir = Path.Combine(
            Environment.GetFolderPath(
                Environment.SpecialFolder.LocalApplicationData),
            "CalibrationTransfer",
            "native",
            "x86");

        Directory.CreateDirectory(nativeDir);

        stem = Path.GetFileNameWithoutExtension(libraryName);
        dllPath = Path.Combine(
            nativeDir,
            stem + "-" + hash.Substring(0, 16) + ".dll");

        if (!File.Exists(dllPath))
        {
            tempPath = dllPath + "." + Environment.ProcessId.ToString() + ".tmp";
            File.WriteAllBytes(tempPath, image);

            try
            {
                File.Move(tempPath, dllPath);
            }
            catch (IOException)
            {
                /*
                 * Another process may have extracted this exact embedded DLL
                 * between the File.Exists check and File.Move.
                 */
                if (!File.Exists(dllPath))
                    throw;

                try
                {
                    File.Delete(tempPath);
                }
                catch
                {
                }
            }
        }

        return NativeLibrary.Load(dllPath);
    }
}
