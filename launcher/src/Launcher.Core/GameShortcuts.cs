namespace WweAllStars.Launcher.Core;

/// <summary>One shortcut file: where it goes, what it starts, and the icon it shows.</summary>
public sealed record Shortcut(string Path, string Target, string WorkingDirectory, string IconPath, string Description);

/// <summary>Writes shortcut files; the platform layer implements it (a <c>.lnk</c> on Windows).</summary>
public interface IShortcutWriter
{
    void Write(Shortcut shortcut);
}

/// <summary>What <see cref="GameShortcuts.Create"/> wrote.</summary>
public sealed record CreatedShortcuts(IReadOnlyList<Shortcut> Shortcuts, bool UsesTitleTile);

/// <summary>
/// The desktop and Start shortcuts the launcher creates after setup (spec, story 57): they start
/// the launcher and show the game's title tile from the player's disc, built into
/// <c>userdata/icon.ico</c>. The exes keep the project's original icon.
/// </summary>
public static class GameShortcuts
{
    public const string FileName = "WWE All Stars.lnk";
    public const string Description = "Play WWE All Stars (static recompilation)";

    /// <param name="folders">The folders to put a shortcut in, such as the desktop and the Start menu's Programs.</param>
    public static CreatedShortcuts Create(PortableLayout layout, string launcherExecutable,
        IEnumerable<string> folders, IShortcutWriter writer)
    {
        // A missing or damaged nxeart is no reason to refuse the shortcut: it shows the launcher's icon then.
        string icon;
        bool usesTile;
        try
        {
            icon = TitleTile.WriteIcon(layout);
            usesTile = true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            icon = launcherExecutable;
            usesTile = false;
        }

        var shortcuts = new List<Shortcut>();
        foreach (var folder in folders)
        {
            var shortcut = new Shortcut(Path.Combine(folder, FileName), launcherExecutable, layout.Root, icon, Description);
            writer.Write(shortcut);
            shortcuts.Add(shortcut);
        }
        return new CreatedShortcuts(shortcuts, usesTile);
    }
}
