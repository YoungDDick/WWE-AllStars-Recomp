using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class WindowPlacementTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-launcher-").FullName;

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private string UserData => Path.Combine(_root, "userdata");

    private static readonly WindowPlacement Placement = new(X: 300, Y: 120, Width: 1280, Height: 720, Maximized: false);

    // Two 1920×1080 screens side by side; the taskbar takes the bottom 48 pixels of each.
    private static readonly ScreenArea[] TwoScreens = [new(0, 0, 1920, 1032), new(1920, 0, 1920, 1032)];

    [Fact]
    public void The_window_opens_at_the_default_place_until_it_has_been_moved()
    {
        Assert.Null(LauncherState.Load(UserData).Window);
    }

    [Fact]
    public void Size_and_position_are_remembered_across_restarts()
    {
        LauncherState.RememberWindow(UserData, Placement);

        Assert.Equal(Placement, LauncherState.Load(UserData).Window);

        LauncherState.RememberWindow(UserData, Placement with { Maximized = true });
        Assert.True(LauncherState.Load(UserData).Window!.Maximized);
    }

    [Fact]
    public void The_window_and_the_game_data_folder_never_overwrite_each_other()
    {
        var gameData = Path.Combine(_root, "elsewhere", "gamedata");
        LauncherState.RememberWindow(UserData, Placement);
        LauncherState.RememberGameData(UserData, gameData);

        Assert.Equal(Placement, LauncherState.Load(UserData).Window);
        Assert.Equal(gameData, LauncherState.Load(UserData).GameDataDirectory);

        LauncherState.RememberWindow(UserData, Placement with { X = 2400 });
        Assert.Equal(gameData, LauncherState.Load(UserData).GameDataDirectory);
        Assert.Equal(2400, LauncherState.Load(UserData).Window!.X);
    }

    [Theory]
    [InlineData(300, 120)] // on the first screen
    [InlineData(2400, 200)] // on the second screen
    [InlineData(-500, 40)] // partly off the left edge, title bar still reachable
    public void A_window_whose_title_bar_is_on_a_screen_opens_where_it_was(int x, int y)
    {
        Assert.True((Placement with { X = x, Y = y }).IsReachableOn(TwoScreens));
    }

    [Theory]
    [InlineData(4000, 120)] // the second screen was unplugged
    [InlineData(300, 1030)] // title bar under the taskbar
    [InlineData(300, -500)] // title bar above every screen
    [InlineData(-1280, 120)] // just past the left edge
    public void A_window_whose_title_bar_cannot_be_reached_opens_at_the_default_place(int x, int y)
    {
        Assert.False((Placement with { X = x, Y = y }).IsReachableOn(TwoScreens));
        Assert.False(Placement.IsReachableOn([]));
    }
}
