using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class LauncherMotionTests
{
    [Fact]
    public void Motion_runs_only_when_focused_and_no_game_runs()
    {
        Assert.True(LauncherMotion.Allowed(focused: true, minimized: false, gameRunning: false));
    }

    [Theory]
    [InlineData(true, false, true)]   // the game runs
    [InlineData(false, false, false)] // another window has focus
    [InlineData(true, true, false)]   // minimized
    [InlineData(false, true, true)]   // all at once
    public void Motion_stops_while_the_game_runs_or_the_window_is_out_of_sight(
        bool focused, bool minimized, bool gameRunning)
    {
        Assert.False(LauncherMotion.Allowed(focused, minimized, gameRunning));
    }
}
