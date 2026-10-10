using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace WweAllStars.Launcher.Tests;

/// <summary>
/// Builds a tiny XDVDFS disc image (the layout scripts/inspect_disc.py reads) and the matching
/// disc manifest, so setup tests never need the real disc.
/// </summary>
internal sealed class TestDisc
{
    private const int Sector = 2048;
    private static readonly byte[] Magic = Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA");

    public const string ManifestVersion = "test-disc-v1";

    /// <summary>Path → content, with '/' separators; folders are implied by the paths.</summary>
    public SortedDictionary<string, byte[]> Files { get; } = new(StringComparer.OrdinalIgnoreCase)
    {
        ["default.xex"] = Encoding.ASCII.GetBytes("XEX2 fake executable"),
        ["Coalesced_INT.bin"] = Bytes(5000, 1),
        ["CookedXenon/Startup.xxx"] = Bytes(3 * Sector + 17, 2),
        ["CookedXenon/Maps/Ring.xxx"] = Bytes(Sector, 3),
        ["nxeart"] = Bytes(100, 4),
    };

    /// <summary>Content the manifest expects instead of the image's, to fake a damaged file.</summary>
    public Dictionary<string, byte[]> ManifestOverrides { get; } = new(StringComparer.OrdinalIgnoreCase);

    public long TotalBytes => Files.Values.Sum(data => (long)data.Length);

    public static byte[] Bytes(int length, int seed)
    {
        var data = new byte[length];
        for (var i = 0; i < length; i++) data[i] = (byte)(i * 31 + seed * 7);
        return data;
    }

    public string ManifestText()
    {
        var text = new StringBuilder("# test manifest\nmanifest_version\t" + ManifestVersion + "\n");
        foreach (var (path, data) in Files)
        {
            var expected = ManifestOverrides.GetValueOrDefault(path, data);
            text.Append(Convert.ToHexStringLower(SHA256.HashData(expected))).Append('\t')
                .Append(data.Length).Append('\t').Append(path).Append('\n');
        }
        return text.ToString();
    }

    public void WriteImage(string path) => File.WriteAllBytes(path, Image());

    public byte[] Image()
    {
        var root = new Folder();
        foreach (var (path, data) in Files)
        {
            var parts = path.Split('/');
            var folder = root;
            foreach (var part in parts[..^1])
            {
                if (!folder.Folders.TryGetValue(part, out var child)) folder.Folders[part] = child = new Folder();
                folder = child;
            }
            folder.Files[parts[^1]] = data;
        }

        // Sector 32 (0x10000) holds the volume descriptor; tables and files follow it.
        var image = new List<byte>(new byte[34 * Sector]);
        var (rootSector, rootSize) = WriteFolder(root, image);
        var bytes = image.ToArray();
        Magic.CopyTo(bytes, 0x10000);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(0x10000 + 20), rootSector);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(0x10000 + 24), rootSize);
        Magic.CopyTo(bytes, 0x10000 + Sector - Magic.Length);
        return bytes;
    }

    private sealed class Folder
    {
        public SortedDictionary<string, Folder> Folders { get; } = new(StringComparer.OrdinalIgnoreCase);
        public SortedDictionary<string, byte[]> Files { get; } = new(StringComparer.OrdinalIgnoreCase);
    }

    private static (uint Sector, uint Size) WriteFolder(Folder folder, List<byte> image)
    {
        // Children first, so the table knows their sectors.
        var entries = new List<(string Name, uint Sector, uint Size, byte Attributes)>();
        foreach (var (name, child) in folder.Folders)
        {
            var (sector, size) = WriteFolder(child, image);
            entries.Add((name, sector, size, 0x10));
        }
        foreach (var (name, data) in folder.Files)
            entries.Add((name, Append(image, data), (uint)data.Length, 0));

        // A right-leaning tree: each entry's right pointer is the next entry.
        var table = new List<byte>();
        for (var i = 0; i < entries.Count; i++)
        {
            var (name, sector, size, attributes) = entries[i];
            var length = 14 + name.Length;
            var next = i + 1 < entries.Count ? (table.Count + ((length + 3) & ~3)) / 4 : 0;
            var entry = new byte[(length + 3) & ~3];
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(2), (ushort)next);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(4), sector);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(8), size);
            entry[12] = attributes;
            entry[13] = (byte)name.Length;
            Encoding.ASCII.GetBytes(name).CopyTo(entry, 14);
            table.AddRange(entry);
        }
        return (Append(image, table.ToArray()), (uint)table.Count);
    }

    private static uint Append(List<byte> image, byte[] data)
    {
        var sector = (uint)(image.Count / Sector);
        image.AddRange(data);
        image.AddRange(new byte[(Sector - image.Count % Sector) % Sector]);
        return sector;
    }
}
