using System.Buffers.Binary;

namespace WweAllStars.Launcher.Core;

/// <summary>
/// The game's 64×64 title tile, read from the player's own <c>gamedata/nxeart</c> and written as
/// <c>userdata/icon.ico</c> for the shortcuts the launcher creates (spec, "Icon"). The tile is the
/// title thumbnail in the package's STFS header (<see cref="StfsPackage.ReadTitleThumbnail"/>), so no file inside the package is decoded. The
/// icon is built only on the player's machine: it never enters the repo or the release zip.
/// </summary>
public static class TitleTile
{
    public const string IconFileName = "icon.ico";

    private const int IconHeaderSize = 6 + 16;

    private static ReadOnlySpan<byte> PngSignature => [0x89, (byte)'P', (byte)'N', (byte)'G', 0x0D, 0x0A, 0x1A, 0x0A];

    /// <summary>Builds <c>userdata/icon.ico</c> from the tile in <c>gamedata/nxeart</c> and returns its path.</summary>
    /// <exception cref="InvalidDataException">The package is not dashboard art or holds no usable tile.</exception>
    public static string WriteIcon(PortableLayout layout)
    {
        var icon = ToIcon(ReadPng(Path.Combine(layout.GameDataDirectory, "nxeart")));
        Directory.CreateDirectory(layout.UserDataDirectory);
        var path = Path.Combine(layout.UserDataDirectory, IconFileName);
        var temp = path + ".tmp";
        File.WriteAllBytes(temp, icon);
        File.Move(temp, path, overwrite: true);
        return path;
    }

    /// <summary>The title tile's bytes (a PNG on the disc) from an STFS package (<c>PIRS</c>, <c>LIVE</c> or <c>CON </c>).</summary>
    public static byte[] ReadPng(string packagePath)
    {
        try
        {
            return StfsPackage.ReadTitleThumbnail(packagePath);
        }
        catch (InvalidDataException error)
        {
            throw new InvalidDataException("The dashboard art package holds no usable title tile: " + error.Message, error);
        }
    }

    /// <summary>An <c>.ico</c> holding one PNG-compressed image (Windows Vista and later read these).</summary>
    /// <exception cref="InvalidDataException">The image is not a PNG of at most 256×256.</exception>
    public static byte[] ToIcon(byte[] png)
    {
        if (png.Length < 24 || !png.AsSpan().StartsWith(PngSignature))
            throw new InvalidDataException("The title tile is not a PNG image.");
        // IHDR is always the first chunk: width and height at 16 and 20.
        var width = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(16));
        var height = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(20));
        if (width is <= 0 or > 256 || height is <= 0 or > 256)
            throw new InvalidDataException($"The title tile is {width}×{height}; an icon holds at most 256×256.");

        var icon = new byte[IconHeaderSize + png.Length];
        var span = icon.AsSpan();
        BinaryPrimitives.WriteUInt16LittleEndian(span[2..], 1); // type: icon
        BinaryPrimitives.WriteUInt16LittleEndian(span[4..], 1); // one image
        span[6] = (byte)(width == 256 ? 0 : width); // 0 means 256
        span[7] = (byte)(height == 256 ? 0 : height);
        BinaryPrimitives.WriteUInt16LittleEndian(span[10..], 1); // colour planes
        BinaryPrimitives.WriteUInt16LittleEndian(span[12..], 32); // bits per pixel
        BinaryPrimitives.WriteInt32LittleEndian(span[14..], png.Length);
        BinaryPrimitives.WriteInt32LittleEndian(span[18..], IconHeaderSize);
        png.CopyTo(span[IconHeaderSize..]);
        return icon;
    }
}
