using System.Runtime.InteropServices;

namespace WweAllStars.Launcher.Platform;

/// <summary>
/// Gives headless modes a console. The launcher is a GUI exe, so on Windows it has none of its
/// own: it attaches to the terminal that started it, unless its output is already redirected.
/// </summary>
internal interface IHostConsole
{
    void Attach();
}

internal static class HostConsole
{
    public static IHostConsole Current { get; } =
        OperatingSystem.IsWindows() ? new WindowsHostConsole() : new NullHostConsole();
}

internal sealed class NullHostConsole : IHostConsole
{
    public void Attach() { }
}

internal sealed partial class WindowsHostConsole : IHostConsole
{
    private const int AttachParentProcess = -1;
    private const int StdOutputHandle = -11;

    public void Attach()
    {
        // A pipe or file the caller gave us already works; attaching would not replace it.
        if (GetStdHandle(StdOutputHandle) != 0) return;
        AttachConsole(AttachParentProcess);
    }

    [LibraryImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool AttachConsole(int processId);

    [LibraryImport("kernel32.dll")]
    private static partial nint GetStdHandle(int handle);
}
