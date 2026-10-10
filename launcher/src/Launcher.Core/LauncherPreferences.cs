using System.Text.Json;
using System.Text.Json.Nodes;

namespace WweAllStars.Launcher.Core;

/// <summary>
/// The launcher's own preferences, kept in <c>userdata/launcher-preferences.json</c>; the
/// game's settings file is not involved. Only <c>check_for_updates</c> so far. A missing or
/// damaged file gives the defaults, and saving keeps keys this launcher does not know.
/// </summary>
public sealed class LauncherPreferences
{
    public const string FileName = "launcher-preferences.json";
    private const string CheckForUpdatesKey = "check_for_updates";

    private readonly JsonObject _json;

    private LauncherPreferences(JsonObject json) => _json = json;

    /// <summary>Whether the launcher asks GitHub for a newer release when it opens (spec, story 51).</summary>
    public bool CheckForUpdates =>
        _json[CheckForUpdatesKey] is JsonValue value && value.TryGetValue(out bool on) ? on : true;

    public static string PathIn(PortableLayout layout) => Path.Combine(layout.UserDataDirectory, FileName);

    public static LauncherPreferences Load(PortableLayout layout)
    {
        try
        {
            if (JsonNode.Parse(File.ReadAllText(PathIn(layout))) is JsonObject json)
                return new LauncherPreferences(json);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
        {
        }
        return new LauncherPreferences([]);
    }

    public LauncherPreferences With(bool checkForUpdates)
    {
        var json = (JsonObject)_json.DeepClone();
        json[CheckForUpdatesKey] = checkForUpdates;
        return new LauncherPreferences(json);
    }

    /// <summary>Writes a temp file beside the real one and renames it over, so a crash never leaves half a file.</summary>
    public void Save(PortableLayout layout)
    {
        var path = PathIn(layout);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temp = path + ".tmp";
        using (var stream = File.Create(temp))
        using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
            _json.WriteTo(writer);
        File.Move(temp, path, overwrite: true);
    }
}
