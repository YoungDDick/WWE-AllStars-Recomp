using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class GameLauncherTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-launcher-").FullName;
    private readonly FakeProcessStarter _starter = new();
    private static readonly DateTimeOffset Now = new(2026, 10, 4, 12, 30, 5, TimeSpan.Zero);

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private PortableLayout LayoutWithGame()
    {
        var layout = new PortableLayout(_root);
        Directory.CreateDirectory(layout.GameDirectory);
        File.WriteAllText(layout.GameExecutable, "");
        return layout;
    }

    [Fact]
    public void Play_starts_the_game_exe_from_its_folder_with_the_plumbing_arguments()
    {
        var layout = LayoutWithGame();

        var session = new GameLauncher(layout, _starter, () => Now).Play();

        var start = Assert.Single(_starter.Starts);
        Assert.Equal(Path.Combine(_root, "game", "wweallstars.exe"), start.Executable);
        Assert.Equal(Path.Combine(_root, "game"), start.WorkingDirectory);
        Assert.Equal(LaunchArguments.Build(layout, session.LogPath!), start.Arguments);
    }

    [Fact]
    public void Each_session_logs_to_its_own_folder_under_logs()
    {
        var launcher = new GameLauncher(LayoutWithGame(), _starter, () => Now);

        var first = launcher.Play();
        _starter.Running[0].Exit(0);
        var second = launcher.Play();

        Assert.Equal(Path.Combine(_root, "logs", "20261004-123005", "runtime.log"), first.LogPath);
        Assert.Equal(Path.Combine(_root, "logs", "20261004-123005-2", "runtime.log"), second.LogPath);
        Assert.True(Directory.Exists(Path.GetDirectoryName(first.LogPath!)));
        Assert.True(Directory.Exists(Path.GetDirectoryName(second.LogPath!)));
    }

    [Fact]
    public void A_missing_game_exe_is_a_plain_error_and_starts_nothing()
    {
        var launcher = new GameLauncher(new PortableLayout(_root), _starter, () => Now);

        var error = Assert.Throws<LauncherException>(() => launcher.Play());

        Assert.Contains(Path.Combine(_root, "game", "wweallstars.exe"), error.Message);
        Assert.Empty(_starter.Starts);
    }

    [Fact]
    public void A_second_Play_is_refused_while_the_game_runs()
    {
        var launcher = new GameLauncher(LayoutWithGame(), _starter, () => Now);
        launcher.Play();

        var error = Assert.Throws<LauncherException>(() => launcher.Play());

        Assert.Equal("The game is already running.", error.Message);
        Assert.Single(_starter.Starts);
        Assert.True(launcher.IsRunning);
        Assert.Equal("Game running", launcher.Status);
    }

    [Fact]
    public async Task After_the_game_exits_the_launcher_is_ready_and_Play_starts_it_again()
    {
        var launcher = new GameLauncher(LayoutWithGame(), _starter, () => Now);
        var session = launcher.Play();

        _starter.Running[0].Exit(0);
        await session.Exited;

        Assert.False(launcher.IsRunning);
        Assert.Equal("Ready", launcher.Status);
        launcher.Play();
        Assert.Equal(2, _starter.Starts.Count);
    }

    private async Task<GameExitReport> RunAndExit(int exitCode, params string[] logLines)
    {
        var session = new GameLauncher(LayoutWithGame(), _starter, () => Now).Play();
        if (logLines.Length > 0) File.WriteAllLines(session.LogPath!, logLines);
        _starter.Running[^1].Exit(exitCode);
        return await session.Exited;
    }

    [Fact]
    public async Task A_clean_exit_reports_that_the_game_closed()
    {
        var report = await RunAndExit(0, "i> 00001234 native renderer: enabled, output 1280x720");

        Assert.Equal(GameExitKind.Closed, report.Kind);
        Assert.Equal("The game closed.", report.Message);
        Assert.False(report.OfferEmulated);
    }

    [Fact]
    public async Task Killing_the_game_from_Task_Manager_is_reported_as_an_unexpected_stop()
    {
        // TerminateProcess, which Task Manager's End task uses, leaves exit code 1.
        var report = await RunAndExit(1);

        Assert.Equal(GameExitKind.Crashed, report.Kind);
        Assert.Equal("The game stopped unexpectedly (exit code 1). Open logs to see what happened.",
            report.Message);
    }

    [Fact]
    public async Task A_crash_names_the_Windows_error_in_plain_words()
    {
        var report = await RunAndExit(unchecked((int)0xC0000005));

        Assert.Equal(GameExitKind.Crashed, report.Kind);
        Assert.Equal("The game crashed: it read or wrote memory it does not own (0xC0000005). " +
                     "Open logs to see what happened.", report.Message);
    }

    [Fact]
    public async Task An_unknown_crash_code_is_shown_in_hex()
    {
        var report = await RunAndExit(unchecked((int)0xC0001234));

        Assert.Equal("The game crashed (0xC0001234). Open logs to see what happened.", report.Message);
    }

    [Fact]
    public async Task A_renderer_fallback_in_the_session_log_is_reported_with_an_offer_of_Emulated()
    {
        var report = await RunAndExit(0,
            "i> 00001234 loading",
            // The game's Vulkan setup failure (src/native/native_renderer.cpp).
            "!> 00001234 native renderer: could not create Vulkan resources; running on Emulated",
            "i> 00001234 shutting down");

        Assert.Equal(GameExitKind.RendererFallback, report.Kind);
        Assert.Equal("The game ran on Emulated because Native could not start: could not create Vulkan " +
                     "resources. Switch the renderer to Emulated to skip this next time.",
            report.Message);
        Assert.True(report.OfferEmulated);
    }

    [Theory]
    [InlineData("missing_shader_pack", "no shader pack at C:\\wwe\\cache\\wweallstars.shaderpack")]
    [InlineData("stale_shader_pack", "converted with XenosRecomp patch 0000000000000001, this build expects 0000000000000002")]
    [InlineData("corrupt_shader_pack", "the SPIR-V does not decompress")]
    public async Task A_shader_pack_fallback_offers_to_prepare_the_shaders_again(string fallback, string detail)
    {
        var report = await RunAndExit(0,
            $"!> 00001234 native renderer: renderer fallback {fallback} ({detail}); running on Emulated. " +
            "Rebuild the shader pack with the launcher or pwsh scripts/convert-shaders.ps1.");

        Assert.Equal(GameExitKind.RendererFallback, report.Kind);
        Assert.True(report.OfferShaderRebuild);
        Assert.False(report.OfferEmulated);
        Assert.Equal("The game ran on Emulated because its Native shaders are missing, damaged or out of date. " +
                     "Prepare shaders to play on Native.", report.Message);
    }

    [Fact]
    public async Task A_fallback_line_from_a_game_before_the_Emulated_name_is_still_reported()
    {
        // Game builds before ADR-0020 called the Emulated renderer "Classic" in this line.
        var report = await RunAndExit(0, "!> native renderer: could not create Vulkan resources; running on Classic");

        Assert.Equal(GameExitKind.RendererFallback, report.Kind);
        Assert.True(report.OfferEmulated);
    }

    [Fact]
    public async Task A_vulkan_fallback_does_not_offer_to_prepare_the_shaders()
    {
        var report = await RunAndExit(0, "!> native renderer: could not create Vulkan resources; running on Emulated");

        Assert.False(report.OfferShaderRebuild);
        Assert.True(report.OfferEmulated);
    }

    [Fact]
    public async Task A_crash_after_a_renderer_fallback_is_reported_as_the_crash()
    {
        var report = await RunAndExit(unchecked((int)0xC0000005),
            "!> native renderer: could not create Vulkan resources; running on Emulated");

        Assert.Equal(GameExitKind.Crashed, report.Kind);
        Assert.False(report.OfferEmulated);
    }

    [Fact]
    public async Task A_session_without_a_log_still_reports_its_exit()
    {
        var report = await RunAndExit(0);

        Assert.Equal(GameExitKind.Closed, report.Kind);
    }

    [Fact]
    public async Task A_game_left_running_by_an_earlier_launcher_is_watched_and_blocks_Play()
    {
        var layout = LayoutWithGame();
        var log = Path.Combine(_root, "logs", "20261004-110000", "runtime.log");
        Directory.CreateDirectory(Path.GetDirectoryName(log)!);
        File.WriteAllText(log, "!> native renderer: could not create Vulkan resources; running on Emulated\n");
        _starter.AlreadyRunning = new FakeGame();
        var launcher = new GameLauncher(layout, _starter, () => Now);

        var session = launcher.AttachToRunning();

        Assert.NotNull(session);
        Assert.Equal(log, session.LogPath);
        Assert.Equal("Game running", launcher.Status);
        Assert.Throws<LauncherException>(() => launcher.Play());
        _starter.AlreadyRunning.Exit(0);
        var report = await session.Exited;
        Assert.Equal(GameExitKind.RendererFallback, report.Kind);
        Assert.Equal("Ready", launcher.Status);
    }

    [Fact]
    public void With_no_game_running_there_is_nothing_to_attach_to()
    {
        var launcher = new GameLauncher(LayoutWithGame(), _starter, () => Now);

        Assert.Null(launcher.AttachToRunning());
        Assert.Equal("Ready", launcher.Status);
    }

    private sealed class FakeProcessStarter : IProcessStarter
    {
        public List<ProcessStart> Starts { get; } = [];
        public List<FakeGame> Running { get; } = [];
        public FakeGame? AlreadyRunning { get; set; }

        public IRunningGame Start(ProcessStart start)
        {
            Starts.Add(start);
            var game = new FakeGame();
            Running.Add(game);
            return game;
        }

        public IRunningGame? FindRunning(string executable) => AlreadyRunning;
    }

    private sealed class FakeGame : IRunningGame
    {
        private readonly TaskCompletionSource<int> _exit = new();

        public Task<int> ExitCode => _exit.Task;

        public void Exit(int code) => _exit.SetResult(code);
    }
}
