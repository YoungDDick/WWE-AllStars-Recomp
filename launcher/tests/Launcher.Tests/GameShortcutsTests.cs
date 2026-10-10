using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class GameShortcutsTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-launcher-").FullName;

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private PortableLayout Layout => new(_root);
    private string LauncherExe => Path.Combine(_root, "WWEAllStarsLauncher.exe");
    private string Desktop => Path.Combine(_root, "Desktop");
    private string StartMenu => Path.Combine(_root, "Start Menu", "Programs");

    [Fact]
    public void Desktop_and_Start_shortcuts_start_the_launcher_and_show_the_title_tile()
    {
        Directory.CreateDirectory(Layout.GameDataDirectory);
        File.WriteAllBytes(Path.Combine(Layout.GameDataDirectory, "nxeart"), TestTile.Package(TestTile.Png(64, 64)));
        var writer = new RecordingShortcutWriter();

        var created = GameShortcuts.Create(Layout, LauncherExe, [Desktop, StartMenu], writer);

        var icon = Path.Combine(_root, "userdata", "icon.ico");
        Assert.True(File.Exists(icon));
        Assert.Equal(
        [
            new Shortcut(Path.Combine(Desktop, "WWE All Stars.lnk"), LauncherExe, _root, icon, GameShortcuts.Description),
            new Shortcut(Path.Combine(StartMenu, "WWE All Stars.lnk"), LauncherExe, _root, icon, GameShortcuts.Description),
        ], writer.Written);
        Assert.Equal(writer.Written, created.Shortcuts);
        Assert.True(created.UsesTitleTile);
    }

    [Fact]
    public void Without_a_readable_tile_the_shortcuts_keep_the_launcher_icon()
    {
        var writer = new RecordingShortcutWriter();

        var created = GameShortcuts.Create(Layout, LauncherExe, [Desktop], writer);

        Assert.Equal(LauncherExe, Assert.Single(writer.Written).IconPath);
        Assert.False(created.UsesTitleTile);
        Assert.False(File.Exists(Path.Combine(_root, "userdata", "icon.ico")));
    }

    private sealed class RecordingShortcutWriter : IShortcutWriter
    {
        public List<Shortcut> Written { get; } = [];

        public void Write(Shortcut shortcut) => Written.Add(shortcut);
    }
}
