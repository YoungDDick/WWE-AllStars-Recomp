using System.Text.Json;
using System.Text.Json.Serialization;

namespace WweAllStars.Launcher.Core;

/// <summary>
/// The launcher's own state file, <c>userdata/launcher-state.json</c> (spec, "Folder layout"):
/// the game data folder the player chose at setup and the window's size and position.
/// </summary>
public sealed record LauncherState
{
    public const string FileName = "launcher-state.json";

    public string? GameDataDirectory { get; init; }

    /// <summary>Where the window was when it last closed; null until it has been shown once.</summary>
    public WindowPlacement? Window { get; init; }

    public static string PathIn(string userDataDirectory) => Path.Combine(userDataDirectory, FileName);

    /// <summary>The saved state, or an empty one when there is none or it cannot be read.</summary>
    public static LauncherState Load(string userDataDirectory)
    {
        try
        {
            return JsonSerializer.Deserialize(File.ReadAllText(PathIn(userDataDirectory)), LauncherStateJson.Default.LauncherState)
                ?? new LauncherState();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
        {
            return new LauncherState();
        }
    }

    /// <summary>Records the chosen game data folder and keeps the rest of the state.</summary>
    public static void RememberGameData(string userDataDirectory, string gameDataDirectory) =>
        (Load(userDataDirectory) with { GameDataDirectory = gameDataDirectory }).Save(userDataDirectory);

    /// <summary>Records the window's placement and keeps the rest of the state.</summary>
    public static void RememberWindow(string userDataDirectory, WindowPlacement window) =>
        (Load(userDataDirectory) with { Window = window }).Save(userDataDirectory);

    /// <summary>Writes the state through a temp file and a rename, so a crash never leaves half a file.</summary>
    public void Save(string userDataDirectory)
    {
        Directory.CreateDirectory(userDataDirectory);
        var path = PathIn(userDataDirectory);
        var temp = path + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(this, LauncherStateJson.Default.LauncherState));
        File.Move(temp, path, overwrite: true);
    }
}

/// <summary>A screen's work area (without the taskbar), in physical pixels.</summary>
public readonly record struct ScreenArea(int X, int Y, int Width, int Height);

/// <summary>
/// The launcher window's placement: <see cref="X"/> and <see cref="Y"/> in physical pixels (the
/// window's position), <see cref="Width"/> and <see cref="Height"/> in device-independent units
/// (the window's size), and whether it was maximised.
/// </summary>
public sealed record WindowPlacement(int X, int Y, double Width, double Height, bool Maximized)
{
    // The part of the custom title bar the player can grab: below its top edge, right of the icon
    // and left of the middle, so it never covers the minimise and close buttons.
    private const int GrabTop = 18;
    private const int GrabLeft = 40;

    /// <summary>
    /// Whether the player can still grab the title bar on one of <paramref name="screens"/>; when not
    /// (a screen was unplugged, the resolution changed), the window opens at its default place.
    /// </summary>
    public bool IsReachableOn(IEnumerable<ScreenArea> screens)
    {
        var y = Y + GrabTop;
        var left = X + GrabLeft;
        // Half the width in pixels is inside the left half of the window at any scaling of 100 % or more.
        var right = X + (int)(Width / 2);
        return screens.Any(screen =>
            y >= screen.Y && y < screen.Y + screen.Height &&
            right >= screen.X && left < screen.X + screen.Width);
    }
}

[JsonSourceGenerationOptions(WriteIndented = true, PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase)]
[JsonSerializable(typeof(LauncherState))]
internal sealed partial class LauncherStateJson : JsonSerializerContext;
