using System.Diagnostics;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

/// <summary>
/// The launcher's committed settings list must be what the game exports. Runs only when a
/// game exe is built (like the scene tests); <c>WWE_GAME_EXE</c> points at another one.
/// </summary>
public sealed class SettingsSchemaDriftTests
{
    [Fact]
    public async Task The_committed_schema_matches_the_built_games_export()
    {
        var cancel = TestContext.Current.CancellationToken;
        var game = GameExecutable();
        if (game is null) Assert.Skip("No built game exe (build the game, or set WWE_GAME_EXE).");

        var info = new ProcessStartInfo(game)
        {
            WorkingDirectory = Path.GetDirectoryName(game)!,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        info.ArgumentList.Add("--print-settings-schema");
        using var process = Process.Start(info)!;
        var errors = process.StandardError.ReadToEndAsync(cancel);
        var exported = await process.StandardOutput.ReadToEndAsync(cancel);
        using (var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancel))
        {
            timeout.CancelAfter(TimeSpan.FromSeconds(30));
            await process.WaitForExitAsync(timeout.Token);
        }
        Assert.True(process.ExitCode == 0, $"--print-settings-schema exited with {process.ExitCode}: {await errors}");

        Assert.True(Normalize(SettingsSchema.CommittedJson()) == Normalize(exported),
            "launcher/src/Launcher.Core/SettingsSchema.json differs from the game's export: " +
            "run scripts/export-settings-schema.ps1 and commit the result.");
    }

    private static string Normalize(string text) => text.Replace("\r\n", "\n");

    private static string? GameExecutable()
    {
        var configured = Environment.GetEnvironmentVariable("WWE_GAME_EXE");
        if (!string.IsNullOrEmpty(configured)) return File.Exists(configured) ? configured : null;
        for (var folder = new DirectoryInfo(AppContext.BaseDirectory); folder is not null; folder = folder.Parent)
        {
            if (!File.Exists(Path.Combine(folder.FullName, "launcher", "global.json"))) continue;
            var built = Path.Combine(folder.FullName, "out", "build", "win-amd64-relwithdebinfo-vulkan",
                PortableLayout.GameExecutableName);
            return File.Exists(built) ? built : null;
        }
        return null;
    }
}
