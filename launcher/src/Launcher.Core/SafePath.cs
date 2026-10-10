namespace WweAllStars.Launcher.Core;

/// <summary>Paths from packages and archives, kept inside the folder they unpack to.</summary>
public static class SafePath
{
    /// <summary>
    /// The full path of <paramref name="relative"/> under <paramref name="root"/> ('/' or '\\'
    /// separators), or null when it climbs out of the folder ("../") or is rooted.
    /// </summary>
    public static string? Inside(string root, string relative)
    {
        var folder = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) + Path.DirectorySeparatorChar;
        var target = Path.GetFullPath(Path.Combine(folder, relative.Replace('\\', '/')));
        return target.StartsWith(folder, StringComparison.OrdinalIgnoreCase) && target.Length > folder.Length ? target : null;
    }
}
