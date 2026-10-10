using System.Diagnostics;

namespace WweAllStars.Launcher.Core;

/// <summary>
/// The game exe's headless modes (<c>--list-gpus</c>, <c>--print-settings-schema</c>): each prints
/// its data and exits before any window opens or the game boots.
/// </summary>
public static class HeadlessGame
{
    /// <summary>
    /// Runs a headless game mode and returns its standard output; throws <see cref="TimeoutException"/>
    /// (after killing it) when it does not exit within <paramref name="timeout"/>.
    /// </summary>
    public static async Task<string> RunAsync(ProcessStart start, TimeSpan timeout, CancellationToken cancel)
    {
        if (!File.Exists(start.Executable))
            throw new LauncherException($"The game exe is missing: {start.Executable}");
        var info = new ProcessStartInfo(start.Executable)
        {
            WorkingDirectory = start.WorkingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        foreach (var argument in start.Arguments) info.ArgumentList.Add(argument);
        using var process = Process.Start(info)
            ?? throw new LauncherException($"The game did not start: {start.Executable}");
        var errors = process.StandardError.ReadToEndAsync(cancel);
        var output = process.StandardOutput.ReadToEndAsync(cancel);
        using var limit = CancellationTokenSource.CreateLinkedTokenSource(cancel);
        limit.CancelAfter(timeout);
        try
        {
            await process.WaitForExitAsync(limit.Token);
        }
        catch (OperationCanceledException) when (!cancel.IsCancellationRequested)
        {
            process.Kill(entireProcessTree: true);
            throw new TimeoutException($"{start.Executable} {string.Join(' ', start.Arguments)} did not exit.");
        }
        await errors;
        return await output;
    }
}
