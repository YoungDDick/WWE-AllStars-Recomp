using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Platform;

/// <summary>Where this platform keeps desktop and Start shortcuts, and how it writes one.</summary>
internal interface IShortcutPlatform : IShortcutWriter
{
    IReadOnlyList<string> Folders { get; }
}

internal static class ShortcutPlatform
{
    /// <summary>The platform's shortcuts, or null where the launcher cannot create them yet (Linux: M6).</summary>
    public static IShortcutPlatform? Current { get; } =
        OperatingSystem.IsWindows() ? new WindowsShortcutPlatform() : null;
}

/// <summary>Shell links (<c>.lnk</c>) on the desktop and in the Start menu's Programs folder.</summary>
internal sealed partial class WindowsShortcutPlatform : IShortcutPlatform
{
    private static readonly Guid ShellLinkClass = new("00021401-0000-0000-C000-000000000046");
    private static readonly StrategyBasedComWrappers ComWrappers = new();
    private const uint InprocServer = 1;

    public IReadOnlyList<string> Folders =>
    [
        Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
        Environment.GetFolderPath(Environment.SpecialFolder.Programs),
    ];

    public void Write(Shortcut shortcut)
    {
        var linkInterface = typeof(IShellLinkW).GUID;
        Marshal.ThrowExceptionForHR(CoCreateInstance(ShellLinkClass, 0, InprocServer, linkInterface, out var pointer));
        try
        {
            var link = (IShellLinkW)ComWrappers.GetOrCreateObjectForComInstance(pointer, CreateObjectFlags.None);
            link.SetPath(shortcut.Target);
            link.SetWorkingDirectory(shortcut.WorkingDirectory);
            link.SetDescription(shortcut.Description);
            link.SetIconLocation(shortcut.IconPath, 0);
            Directory.CreateDirectory(Path.GetDirectoryName(shortcut.Path)!);
            ((IPersistFile)link).Save(shortcut.Path, remember: true);
        }
        finally
        {
            Marshal.Release(pointer);
        }
    }

    [LibraryImport("ole32.dll")]
    private static partial int CoCreateInstance(in Guid classId, nint outer, uint context, in Guid interfaceId, out nint pointer);
}

// Only the setters are called; the other slots keep the vtable order.
[GeneratedComInterface(StringMarshalling = StringMarshalling.Utf16)]
[Guid("000214F9-0000-0000-C000-000000000046")]
internal partial interface IShellLinkW
{
    void GetPath(nint file, int fileLength, nint findData, uint flags);
    void GetIDList(out nint idList);
    void SetIDList(nint idList);
    void GetDescription(nint name, int nameLength);
    void SetDescription(string name);
    void GetWorkingDirectory(nint directory, int directoryLength);
    void SetWorkingDirectory(string directory);
    void GetArguments(nint arguments, int argumentsLength);
    void SetArguments(string arguments);
    void GetHotkey(out ushort hotkey);
    void SetHotkey(ushort hotkey);
    void GetShowCmd(out int showCommand);
    void SetShowCmd(int showCommand);
    void GetIconLocation(nint iconPath, int iconPathLength, out int iconIndex);
    void SetIconLocation(string iconPath, int iconIndex);
    void SetRelativePath(string relativePath, uint reserved);
    void Resolve(nint window, uint flags);
    void SetPath(string file);
}

[GeneratedComInterface(StringMarshalling = StringMarshalling.Utf16)]
[Guid("0000010B-0000-0000-C000-000000000046")]
internal partial interface IPersistFile
{
    void GetClassID(out Guid classId);
    [PreserveSig] int IsDirty();
    void Load(string fileName, uint mode);
    void Save(string fileName, [MarshalAs(UnmanagedType.Bool)] bool remember);
    void SaveCompleted(string fileName);
    void GetCurFile(out nint fileName);
}
