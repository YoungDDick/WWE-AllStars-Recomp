using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

/// <summary>The real runner, with cmd.exe standing in for the game.</summary>
public sealed class StreamingPrepareRunnerTests
{
    private static readonly string Cmd = Path.Combine(Environment.SystemDirectory, "cmd.exe");

    [Fact]
    public async Task Hands_over_each_output_line_and_returns_the_exit_code()
    {
        Assert.SkipUnless(OperatingSystem.IsWindows(), "cmd.exe stands in for the game");
        var lines = new List<string>();

        var code = await new StreamingPrepareRunner(TimeSpan.FromMinutes(1)).RunAsync(
            new ProcessStart(Cmd, Environment.SystemDirectory, ["/c", "echo wwe-prepare-pipelines 1&echo result status=partial&exit 3"]),
            lines.Add, TestContext.Current.CancellationToken);

        Assert.Equal(3, code);
        Assert.Equal(["wwe-prepare-pipelines 1", "result status=partial"], lines);
    }

    [Fact]
    public async Task A_missing_exe_is_a_launcher_error()
    {
        await Assert.ThrowsAsync<LauncherException>(() => new StreamingPrepareRunner(TimeSpan.FromMinutes(1)).RunAsync(
            new ProcessStart(Path.Combine(Path.GetTempPath(), "no such game.exe"), Path.GetTempPath(), []), _ => { },
            TestContext.Current.CancellationToken));
    }

    [Fact]
    public async Task A_run_that_does_not_end_in_time_is_reported_and_never_killed()
    {
        Assert.SkipUnless(OperatingSystem.IsWindows(), "cmd.exe stands in for the game");
        var marker = Path.Combine(Path.GetTempPath(), $"wwe-prepare-{Guid.NewGuid():N}.txt");

        // The game is never killed (owner rule): it is asked to close and then left alone.
        var error = await Assert.ThrowsAsync<LauncherException>(() =>
            new StreamingPrepareRunner(TimeSpan.FromMilliseconds(300), closeGrace: TimeSpan.FromMilliseconds(100)).RunAsync(
                new ProcessStart(Cmd, Environment.SystemDirectory, ["/c", $"ping -n 3 127.0.0.1 >nul & echo done>{marker}"]), _ => { },
                TestContext.Current.CancellationToken));

        Assert.Contains("did not finish", error.Message);
        for (var i = 0; i < 100 && !File.Exists(marker); i++) await Task.Delay(100, TestContext.Current.CancellationToken);
        Assert.True(File.Exists(marker), "the process was ended instead of left to finish");
        File.Delete(marker);
    }
}
