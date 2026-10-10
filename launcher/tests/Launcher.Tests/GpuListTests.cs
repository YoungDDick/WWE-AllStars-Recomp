using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class GpuListTests
{
    [Fact]
    public void The_gpus_are_read_from_the_games_list_gpus_output_by_index_and_name()
    {
        // As wweallstars.exe --list-gpus prints them; anything else on the output is ignored.
        const string output =
            "[info] Loaded Vulkan runtime from vulkan-1.dll\r\n" +
            "gpu 0: NVIDIA GeForce RTX 4070 Laptop GPU\r\n" +
            "gpu 1: AMD Radeon(TM) 780M: integrated\r\n" +
            "gpu x: not a gpu line\r\n";

        Assert.Equal(
            [new Gpu(0, "NVIDIA GeForce RTX 4070 Laptop GPU"), new Gpu(1, "AMD Radeon(TM) 780M: integrated")],
            GpuList.Parse(output));
    }

    [Fact]
    public void Each_gpu_line_may_carry_the_games_suggested_preset()
    {
        // The game prints the suggestion after a tab (src/settings/gpu_list.h GpuListText).
        const string output =
            "gpu 0: AMD Radeon RX 7900 XT\tsuggested=high\r\n" +
            "gpu 1: AMD Custom GPU 0405: integrated\tsuggested=low\n" +
            "gpu 2: An older game without the field\n";

        Assert.Equal(
            [new Gpu(0, "AMD Radeon RX 7900 XT", "high"), new Gpu(1, "AMD Custom GPU 0405: integrated", "low"),
             new Gpu(2, "An older game without the field")],
            GpuList.Parse(output));
        Assert.Equal("", GpuList.Parse(output)[2].SuggestedPreset);
    }

    [Theory]
    [InlineData("1", "low")]      // the GPU the setting names
    [InlineData("-1", "high")]    // Automatic, from a game that marks no pick: the first GPU listed
    [InlineData("7", "")]         // a GPU that is gone: the game picks automatically, but say nothing
    [InlineData("x", "")]
    public void The_suggestion_follows_the_chosen_gpu(string vulkanDevice, string suggestion)
    {
        IReadOnlyList<Gpu> gpus = [new Gpu(0, "Discrete", "high"), new Gpu(1, "Integrated", "low")];

        Assert.Equal(suggestion, GpuList.SuggestionFor(gpus, vulkanDevice));
        Assert.Equal("", GpuList.SuggestionFor([], "-1"));
    }

    [Fact]
    public void Automatic_follows_the_gpu_the_game_marks_as_its_automatic_pick()
    {
        // The SDK may prefer a later GPU (feature scores); the game marks the one it would open.
        const string output =
            "gpu 0: Integrated\tsuggested=low\n" +
            "gpu 1: Discrete\tsuggested=high\tautomatic\n";

        var gpus = GpuList.Parse(output);

        Assert.Equal([new Gpu(0, "Integrated", "low"), new Gpu(1, "Discrete", "high", Automatic: true)], gpus);
        Assert.Equal("high", GpuList.SuggestionFor(gpus, GpuList.Automatic));
    }

    [Fact]
    public async Task The_launcher_asks_the_game_exe_for_its_gpus_headlessly()
    {
        var layout = new PortableLayout(Path.Combine(Path.GetTempPath(), "wwe-gpus"));
        ProcessStart? asked = null;

        var gpus = await GpuList.QueryAsync(layout, (start, _) =>
        {
            asked = start;
            return Task.FromResult("gpu 0: Test GPU\n");
        }, TestContext.Current.CancellationToken);

        Assert.Equal([new Gpu(0, "Test GPU")], gpus);
        Assert.NotNull(asked);
        Assert.Equal(layout.GameExecutable, asked.Executable);
        Assert.Equal(layout.GameDirectory, asked.WorkingDirectory);
        Assert.Equal(["--list-gpus"], asked.Arguments);
    }

    [Fact]
    public async Task A_game_exe_that_fails_or_hangs_gives_no_gpus_so_only_automatic_is_offered()
    {
        var layout = new PortableLayout(Path.Combine(Path.GetTempPath(), "wwe-gpus"));

        var failed = await GpuList.QueryAsync(layout,
            (_, _) => throw new LauncherException("The game did not start."), TestContext.Current.CancellationToken);
        var hung = await GpuList.QueryAsync(layout,
            (_, _) => throw new TimeoutException(), TestContext.Current.CancellationToken);

        Assert.Empty(failed);
        Assert.Empty(hung);
    }
}
