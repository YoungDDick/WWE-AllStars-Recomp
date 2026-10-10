using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace WweAllStars.Launcher.Tests;

/// <summary>
/// Builds small Xbox 360 STFS packages with generated contents: the header fields the launcher
/// reads, a volume descriptor, a file table, data blocks and hash tables at one or two levels.
/// The layout is built in disk order (a level-0 table before every 170 data blocks, the level-1
/// table after the first group), independently of the reader's block arithmetic. No game data.
/// </summary>
internal sealed class TestStfs
{
    private const int Block = 0x1000;
    private const int HeaderSize = 0xAD0E; // as on the real packages: data starts at 0xB000
    private const int PerTable = 170;

    public string Magic { get; init; } = "LIVE";
    public uint ContentType { get; init; } = 0x00000002;
    public uint TitleId { get; init; } = 0x54510866;
    public uint MediaId { get; init; }
    public uint Version { get; init; }
    public uint BaseVersion { get; init; }
    public string DisplayName { get; init; } = "TEST PACK";

    /// <summary>Two hash tables per level (a writable CON package); the active one is chosen by flags.</summary>
    public bool TwoTablesPerLevel { get; init; }

    /// <summary>With two tables per level: the secondary copy is the active one (the primary holds junk).</summary>
    public bool SecondaryActive { get; init; }

    /// <summary>Lay files out with their blocks in reverse order, so every file is a chain.</summary>
    public bool Fragmented { get; init; }

    /// <summary>Path ("Phase1/a.ini") → content. Folders are made from the paths.</summary>
    public List<(string Path, byte[] Data)> Files { get; } = [];

    public byte[] TitleThumbnail { get; init; } = [];

    /// <summary>Filler data blocks after the files (to reach a second hash level).</summary>
    public int ExtraBlocks { get; init; }

    public byte[] Build()
    {
        // Directory entries: folders first, then files; parent index -1 is the root.
        var folders = new List<string>();
        foreach (var (path, _) in Files)
        {
            var parts = path.Split('/');
            for (var i = 1; i < parts.Length; i++)
            {
                var folder = string.Join('/', parts[..i]);
                if (!folders.Contains(folder)) folders.Add(folder);
            }
        }
        var entryCount = folders.Count + Files.Count;
        var tableBlocks = Math.Max(1, (entryCount * 0x40 + Block - 1) / Block);

        // Logical data blocks: file table first, then each file's blocks.
        var data = new List<byte[]>();
        for (var i = 0; i < tableBlocks; i++) data.Add(new byte[Block]);
        var starts = new List<(int Start, int Count)>();
        foreach (var (_, content) in Files)
        {
            var count = (content.Length + Block - 1) / Block;
            starts.Add((data.Count, count));
            for (var b = 0; b < count; b++)
            {
                var block = new byte[Block];
                content.AsSpan(b * Block, Math.Min(Block, content.Length - b * Block)).CopyTo(block);
                data.Add(block);
            }
        }
        for (var i = 0; i < ExtraBlocks; i++) data.Add(Filler(i));

        // Where each file's n-th block lives, and the chain links ("next block") for hash entries.
        var next = new int[data.Count];
        for (var i = 0; i < next.Length; i++) next[i] = i + 1 < next.Length ? i + 1 : 0xFFFFFF;
        var fileStart = new int[Files.Count];
        if (Fragmented)
        {
            var shuffled = data.ToArray();
            for (var f = 0; f < Files.Count; f++)
            {
                var (start, count) = starts[f];
                // Reverse the file's blocks on disk: block k of the file sits at start + count - 1 - k.
                for (var k = 0; k < count; k++) shuffled[start + count - 1 - k] = data[start + k];
                for (var k = 0; k < count; k++)
                    next[start + count - 1 - k] = k + 1 < count ? start + count - 2 - k : 0xFFFFFF;
                fileStart[f] = start + count - 1;
            }
            data = [.. shuffled];
        }
        else
        {
            for (var f = 0; f < Files.Count; f++) fileStart[f] = starts[f].Start;
        }
        // The file table chains through its own blocks.
        for (var i = 0; i < tableBlocks; i++) next[i] = i + 1 < tableBlocks ? i + 1 : 0xFFFFFF;

        var table = new byte[tableBlocks * Block];
        var index = 0;
        foreach (var folder in folders)
        {
            WriteEntry(table.AsSpan(index++ * 0x40, 0x40), folder.Split('/')[^1], directory: true, contiguous: false,
                blocks: 0, start: 0, parent: ParentIndex(folders, folder), size: 0);
        }
        for (var f = 0; f < Files.Count; f++)
        {
            var (path, content) = Files[f];
            var name = path.Split('/')[^1];
            WriteEntry(table.AsSpan(index++ * 0x40, 0x40), name, directory: false, contiguous: !Fragmented,
                blocks: starts[f].Count, start: fileStart[f], parent: ParentIndex(folders, path), size: content.Length);
        }
        for (var i = 0; i < tableBlocks; i++) table.AsSpan(i * Block, Block).CopyTo(data[i]);

        // Hash tables: level 0 per 170 blocks, level 1 over the level-0 tables when needed.
        var level0 = new List<byte[]>();
        for (var t = 0; t * PerTable < data.Count; t++)
        {
            var hashes = new byte[Block];
            for (var r = 0; r < PerTable && t * PerTable + r < data.Count; r++)
            {
                var b = t * PerTable + r;
                SHA1.HashData(data[b]).CopyTo(hashes, r * 0x18);
                BinaryPrimitives.WriteUInt32BigEndian(hashes.AsSpan(r * 0x18 + 0x14), 0x80000000u | (uint)next[b]);
            }
            level0.Add(hashes);
        }
        byte[]? level1 = null;
        if (data.Count > PerTable) // a level-1 table only above 170 blocks
        {
            level1 = new byte[Block];
            for (var t = 0; t < level0.Count; t++) SHA1.HashData(level0[t]).CopyTo(level1, t * 0x18);
        }
        var top = level1 ?? level0[0];

        using var package = new MemoryStream();
        package.Write(Header(SHA1.HashData(top), data.Count, tableBlocks));
        package.SetLength(0xB000);
        package.Position = 0xB000;
        for (var b = 0; b < data.Count; b++)
        {
            if (b % PerTable == 0)
            {
                if (b == PerTable && level1 != null) WriteTable(package, level1);
                WriteTable(package, level0[b / PerTable]);
            }
            package.Write(data[b]);
        }
        return package.ToArray();
    }

    public string WriteTo(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, Build());
        return path;
    }

    /// <summary>Generated, recognisable contents of a given size.</summary>
    public static byte[] Content(int size, int seed)
    {
        var bytes = new byte[size];
        for (var i = 0; i < size; i++) bytes[i] = (byte)(i * 31 + seed * 7 + (i >> 12));
        return bytes;
    }

    /// <summary>The physical offset of a block in a single-table package with fewer than 28,900 blocks.</summary>
    public static long DataBlockOffset(int block) =>
        0xB000 + (long)(block + block / PerTable + 1 + (block >= PerTable ? 1 : 0)) * Block;


    private void WriteTable(Stream package, byte[] table)
    {
        if (!TwoTablesPerLevel)
        {
            package.Write(table);
            return;
        }
        var junk = new byte[Block];
        Array.Fill(junk, (byte)0xEE);
        package.Write(SecondaryActive ? junk : table);
        package.Write(SecondaryActive ? table : junk);
    }

    private byte[] Header(byte[] topHash, int totalBlocks, int tableBlocks)
    {
        var header = new byte[HeaderSize];
        Encoding.ASCII.GetBytes(Magic).CopyTo(header, 0);
        var span = header.AsSpan();
        BinaryPrimitives.WriteUInt32BigEndian(span[0x340..], HeaderSize);
        BinaryPrimitives.WriteUInt32BigEndian(span[0x344..], ContentType);
        BinaryPrimitives.WriteUInt32BigEndian(span[0x348..], 2);
        BinaryPrimitives.WriteUInt32BigEndian(span[0x354..], MediaId);
        BinaryPrimitives.WriteUInt32BigEndian(span[0x358..], Version);
        BinaryPrimitives.WriteUInt32BigEndian(span[0x35C..], BaseVersion);
        BinaryPrimitives.WriteUInt32BigEndian(span[0x360..], TitleId);
        // Licence 0: any profile, full.
        BinaryPrimitives.WriteUInt64BigEndian(span[0x22C..], 0xFFFFFFFFFFFFFFFFul);
        BinaryPrimitives.WriteUInt32BigEndian(span[0x234..], 0xFFFFFFFF);
        // Volume descriptor.
        span[0x379] = 0x24;
        span[0x37B] = (byte)((TwoTablesPerLevel ? 0 : 1) | (TwoTablesPerLevel && SecondaryActive ? 2 : 0));
        BinaryPrimitives.WriteUInt16LittleEndian(span[0x37C..], (ushort)tableBlocks);
        topHash.CopyTo(span[0x381..]);
        BinaryPrimitives.WriteUInt32BigEndian(span[0x395..], (uint)totalBlocks);
        Encoding.BigEndianUnicode.GetBytes(DisplayName).CopyTo(span[0x411..]);
        BinaryPrimitives.WriteInt32BigEndian(span[0x1716..], TitleThumbnail.Length);
        TitleThumbnail.CopyTo(span[0x571A..]);
        return header;
    }

    private static int ParentIndex(List<string> folders, string path)
    {
        var cut = path.LastIndexOf('/');
        return cut < 0 ? -1 : folders.IndexOf(path[..cut]);
    }

    private static void WriteEntry(Span<byte> entry, string name, bool directory, bool contiguous, int blocks,
        int start, int parent, int size)
    {
        Encoding.ASCII.GetBytes(name).CopyTo(entry);
        entry[0x28] = (byte)(name.Length | (contiguous ? 0x40 : 0) | (directory ? 0x80 : 0));
        WriteUInt24LittleEndian(entry[0x29..], blocks);
        WriteUInt24LittleEndian(entry[0x2C..], blocks);
        WriteUInt24LittleEndian(entry[0x2F..], start);
        BinaryPrimitives.WriteInt16BigEndian(entry[0x32..], (short)parent);
        BinaryPrimitives.WriteInt32BigEndian(entry[0x34..], size);
    }

    private static byte[] Filler(int seed)
    {
        var block = new byte[Block];
        BinaryPrimitives.WriteInt32BigEndian(block, seed + 1);
        return block;
    }

    private static void WriteUInt24LittleEndian(Span<byte> target, int value)
    {
        target[0] = (byte)value;
        target[1] = (byte)(value >> 8);
        target[2] = (byte)(value >> 16);
    }
}
