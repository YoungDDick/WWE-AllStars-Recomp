using System.Buffers.Binary;
using System.IO.Compression;
using System.Text;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class TitleTileTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-launcher-").FullName;

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private PortableLayout Layout => new(_root);
    private string IconPath => Path.Combine(_root, "userdata", "icon.ico");

    [Fact]
    public void The_title_tile_is_read_from_nxeart_and_written_to_userdata_as_an_ico()
    {
        var tile = TestTile.Png(64, 64);
        WriteNxeart(TestTile.Package(tile));

        var written = TitleTile.WriteIcon(Layout);

        Assert.Equal(IconPath, written);
        var ico = File.ReadAllBytes(IconPath);
        // ICONDIR: reserved 0, type 1 (icon), one image.
        Assert.Equal(0, BinaryPrimitives.ReadUInt16LittleEndian(ico.AsSpan(0)));
        Assert.Equal(1, BinaryPrimitives.ReadUInt16LittleEndian(ico.AsSpan(2)));
        Assert.Equal(1, BinaryPrimitives.ReadUInt16LittleEndian(ico.AsSpan(4)));
        // ICONDIRENTRY: 64 x 64, 32 bits per pixel, the PNG right after the directory.
        Assert.Equal(64, ico[6]);
        Assert.Equal(64, ico[7]);
        Assert.Equal(1, BinaryPrimitives.ReadUInt16LittleEndian(ico.AsSpan(10)));
        Assert.Equal(32, BinaryPrimitives.ReadUInt16LittleEndian(ico.AsSpan(12)));
        Assert.Equal(tile.Length, BinaryPrimitives.ReadInt32LittleEndian(ico.AsSpan(14)));
        Assert.Equal(22, BinaryPrimitives.ReadInt32LittleEndian(ico.AsSpan(18)));
        Assert.Equal(tile, ico[22..]);
    }

    [Fact]
    public void A_file_that_is_not_dashboard_art_gives_no_icon()
    {
        WriteNxeart(TestTile.Package(TestTile.Png(64, 64), magic: "XEX2"));

        Assert.Throws<InvalidDataException>(() => TitleTile.WriteIcon(Layout));
        Assert.False(File.Exists(IconPath));
    }

    [Fact]
    public void A_package_without_a_png_tile_gives_no_icon()
    {
        WriteNxeart(TestTile.Package([]));
        Assert.Throws<InvalidDataException>(() => TitleTile.WriteIcon(Layout));

        WriteNxeart(TestTile.Package("not a png image, just text"u8.ToArray()));
        Assert.Throws<InvalidDataException>(() => TitleTile.WriteIcon(Layout));

        Assert.False(File.Exists(IconPath));
    }

    [Fact]
    public void A_truncated_package_gives_no_icon()
    {
        WriteNxeart(TestTile.Package(TestTile.Png(64, 64))[..(TestTile.TitleImageOffset + 40)]);

        Assert.Throws<InvalidDataException>(() => TitleTile.WriteIcon(Layout));
        Assert.False(File.Exists(IconPath));
    }

    private void WriteNxeart(byte[] package)
    {
        Directory.CreateDirectory(Layout.GameDataDirectory);
        File.WriteAllBytes(Path.Combine(Layout.GameDataDirectory, "nxeart"), package);
    }
}

/// <summary>Synthetic dashboard art: a small PNG and the STFS header that carries it.</summary>
internal static class TestTile
{
    /// <summary>The title thumbnail's size (big-endian) and data in an STFS header.</summary>
    public const int TitleImageOffset = 0x571A;

    public static byte[] Package(byte[] titleImage, string magic = "PIRS") =>
        new TestStfs { Magic = magic, ContentType = 0x00030000, TitleThumbnail = titleImage }.Build();

    /// <summary>A valid RGBA PNG of the given size, filled with one colour.</summary>
    public static byte[] Png(int width, int height)
    {
        var raw = new MemoryStream();
        for (var y = 0; y < height; y++)
        {
            raw.WriteByte(0); // filter: none
            for (var x = 0; x < width; x++) raw.Write([0xF5, 0xB8, 0x00, 0xFF]);
        }
        var compressed = new MemoryStream();
        using (var zlib = new ZLibStream(compressed, CompressionLevel.Optimal, leaveOpen: true)) raw.WriteTo(zlib);

        var png = new MemoryStream();
        png.Write([0x89, (byte)'P', (byte)'N', (byte)'G', 0x0D, 0x0A, 0x1A, 0x0A]);
        var header = new byte[13];
        BinaryPrimitives.WriteInt32BigEndian(header, width);
        BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(4), height);
        header[8] = 8; // bit depth
        header[9] = 6; // RGBA
        Chunk(png, "IHDR", header);
        Chunk(png, "IDAT", compressed.ToArray());
        Chunk(png, "IEND", []);
        return png.ToArray();
    }

    private static void Chunk(Stream png, string type, byte[] data)
    {
        var length = new byte[4];
        BinaryPrimitives.WriteInt32BigEndian(length, data.Length);
        png.Write(length);
        var typed = Encoding.ASCII.GetBytes(type).Concat(data).ToArray();
        png.Write(typed);
        var crc = new byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(crc, Crc32(typed));
        png.Write(crc);
    }

    private static uint Crc32(byte[] data)
    {
        var crc = 0xFFFFFFFFu;
        foreach (var b in data)
        {
            crc ^= b;
            for (var bit = 0; bit < 8; bit++) crc = (crc & 1) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
        return ~crc;
    }
}
