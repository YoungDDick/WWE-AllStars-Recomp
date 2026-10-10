using System.Globalization;
using System.Text;

namespace WweAllStars.Launcher.Core;

public enum GameExitKind
{
    /// <summary>Exit code 0 and nothing worth a warning in the log.</summary>
    Closed,
    /// <summary>A non-zero exit code: a crash, or the process was ended from outside.</summary>
    Crashed,
    /// <summary>A clean exit from a session that ran on Emulated instead of Native.</summary>
    RendererFallback,
}

/// <summary>
/// What the launcher tells the player when the game exits (spec, "Running the game"): read from
/// the exit code and the last part of the session log.
/// </summary>
public sealed record GameExitReport(GameExitKind Kind, int ExitCode, string Message, bool OfferEmulated)
{
    /// <summary>The session fell back because of the shader pack: preparing shaders again fixes it.</summary>
    public bool OfferShaderRebuild { get; init; }

    private const string OpenLogs = "Open logs to see what happened.";

    // The game logs a renderer fallback as "native renderer: <reason>; running on Emulated..."
    // (src/native/native_renderer.cpp). The launcher reads that line; keep the wording. Game
    // builds before ADR-0020 wrote "running on Classic".
    private const string FallbackPrefix = "native renderer: ";
    private static readonly string[] FallbackMarkers = ["; running on Emulated", "; running on Classic"];

    /// <summary>Plain words for the Windows exception codes a crashing game exits with.</summary>
    private static readonly Dictionary<uint, string> CrashCodes = new()
    {
        [0xC0000005] = "it read or wrote memory it does not own",
        [0xC000001D] = "it ran an instruction this CPU does not have",
        [0xC0000094] = "it divided by zero",
        [0xC00000FD] = "it ran out of stack",
        [0xC0000374] = "its memory heap was corrupted",
        [0xC0000409] = "it stopped itself after detecting a fatal error",
        [0x80000003] = "it hit a debug breakpoint",
        [0xE06D7363] = "an error was not handled",
    };

    public static GameExitReport FromExit(int exitCode, string? sessionLogPath)
    {
        if (exitCode != 0)
            return new GameExitReport(GameExitKind.Crashed, exitCode, CrashMessage(exitCode), OfferEmulated: false);
        var reason = sessionLogPath is null ? null : FallbackReason(sessionLogPath);
        if (reason is not null && IsShaderPackReason(reason))
            return new GameExitReport(GameExitKind.RendererFallback, exitCode,
                "The game ran on Emulated because its Native shaders are missing, damaged or out of date. " +
                "Prepare shaders to play on Native.", OfferEmulated: false) { OfferShaderRebuild = true };
        if (reason is not null)
            return new GameExitReport(GameExitKind.RendererFallback, exitCode,
                $"The game ran on Emulated because Native could not start: {reason}. " +
                "Switch the renderer to Emulated to skip this next time.", OfferEmulated: true);
        return new GameExitReport(GameExitKind.Closed, exitCode, "The game closed.", OfferEmulated: false);
    }

    // The shader pack loader's fallback names (src/native/renderer_fallback.h); only
    // vulkan_setup is not about the pack.
    private static readonly string[] ShaderPackFallbacks =
    [
        "missing_shader_pack", "stale_shader_pack", "truncated_shader_pack", "bad_shader_pack_magic",
        "wrong_shader_pack_version", "corrupt_shader_pack",
    ];

    private static bool IsShaderPackReason(string reason) =>
        ShaderPackFallbacks.Any(name => reason.StartsWith("renderer fallback " + name, StringComparison.Ordinal));

    private static string CrashMessage(int exitCode)
    {
        var code = unchecked((uint)exitCode);
        // NTSTATUS errors (0xC...) and the MSVC C++ exception code are crashes; small codes are
        // the process ending on purpose, or being ended (Task Manager leaves 1).
        if (CrashCodes.TryGetValue(code, out var words))
            return $"The game crashed: {words} (0x{code:X8}). {OpenLogs}";
        if (code >= 0xC0000000)
            return $"The game crashed (0x{code:X8}). {OpenLogs}";
        return $"The game stopped unexpectedly (exit code {exitCode.ToString(CultureInfo.InvariantCulture)}). {OpenLogs}";
    }

    /// <summary>The fallback reason in the session log's last renderer fallback line, or null.</summary>
    private static string? FallbackReason(string logPath)
    {
        string? reason = null;
        try
        {
            // Shared read: something may still hold the log open for writing.
            using var stream = new FileStream(logPath, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream, Encoding.UTF8);
            while (reader.ReadLine() is { } line)
            {
                var marker = FallbackMarkers.Select(m => line.IndexOf(m, StringComparison.Ordinal))
                    .Where(at => at >= 0).DefaultIfEmpty(-1).Min();
                if (marker < 0) continue;
                var prefix = line.LastIndexOf(FallbackPrefix, marker, StringComparison.Ordinal);
                if (prefix >= 0) reason = line[(prefix + FallbackPrefix.Length)..marker].Trim();
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return null;
        }
        return reason;
    }
}
