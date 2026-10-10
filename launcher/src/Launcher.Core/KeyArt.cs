namespace WweAllStars.Launcher.Core;

/// <summary>
/// Reads the dashboard art from the player's own disc (the <c>nxeart</c> package), so the
/// launcher shows the game's key art without this project ever shipping it. Ported from
/// SVR2011-Recomp's <c>launcher/portable/KeyArt.cs</c>.
/// </summary>
public static class KeyArt
{
    /// <summary>The package in the game data folder.</summary>
    public const string PackageFile = "nxeart";

    /// <summary>The 1920×1080 JPEG key art the home screen shows.</summary>
    public const string BackgroundFile = "nxebg.jpg";

    /// <summary>
    /// The key art JPEG from the <c>nxeart</c> in <paramref name="gameDataDirectory"/>, or null when
    /// the package is missing, unreadable or damaged; the home screen then stays typographic.
    /// </summary>
    public static byte[]? BackgroundIn(string gameDataDirectory)
    {
        try
        {
            return Read(Path.Combine(gameDataDirectory, PackageFile), BackgroundFile);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return null;
        }
    }

    /// <summary>
    /// The bytes of <paramref name="fileName"/> in the STFS package at <paramref name="packagePath"/>.
    /// Throws <see cref="InvalidDataException"/> for a file that is not a dashboard art package or is
    /// damaged, and <see cref="FileNotFoundException"/> when the package has no such file.
    /// </summary>
    public static byte[] Read(string packagePath, string fileName)
    {
        using var package = StfsPackage.Open(packagePath);
        if (package.Magic != "PIRS") throw new InvalidDataException("Not a dashboard art package.");
        var file = package.Find(fileName) ?? throw new FileNotFoundException(fileName + " is not in the dashboard art package.");
        return package.ReadAll(file);
    }
}
