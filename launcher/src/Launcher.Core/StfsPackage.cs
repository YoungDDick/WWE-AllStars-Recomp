using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace WweAllStars.Launcher.Core;

/// <summary>The STFS content types the launcher tells apart (header +0x344).</summary>
public enum StfsContentType : uint
{
    SavedGame = 0x00000001,
    MarketplaceContent = 0x00000002,
    Publisher = 0x00000003,
    ThematicSkin = 0x00002000,
    GamerPicture = 0x00020000,
    ArcadeTitle = 0x000D0000,
    /// <summary>A title update (Free60 calls the type "Installer").</summary>
    TitleUpdate = 0x000B0000,
}

/// <summary>One licence entry of the header (+0x22C, 16 of them).</summary>
public readonly record struct StfsLicense(ulong LicenseeId, uint Bits, uint Flags);

/// <summary>A file in a package: its path inside the package, with '/' between folders.</summary>
public sealed record StfsFile(string Path, long Size, int StartBlock, int BlockCount, bool Contiguous);

/// <summary>Work done and total, in blocks (verification) or bytes (extraction).</summary>
public readonly record struct StfsProgress(long Done, long Total);

/// <summary>
/// A read-only Xbox 360 STFS package (<c>LIVE</c>, <c>PIRS</c> or <c>CON </c>): title updates, DLC,
/// dashboard art. Reads the header, the file table and file data through the hash tables, in both
/// block layouts (one or two tables per hash level), and checks every block against its hash.
/// Layout after Free60's STFS page and Xenia Canary's <c>stfs_container_device.cc</c>.
/// </summary>
public sealed class StfsPackage : IDisposable
{
    public const int BlockSize = 0x1000;

    // Data blocks one hash table covers at each level: 170, 170², 170³.
    private static readonly int[] BlocksPerHashLevel = [170, 28900, 4913000];
    private const int HashEntrySize = 0x18;
    private const int DirectoryEntrySize = 0x40;
    private const int MaxNameLength = 0x28;
    private const int ThumbnailSizeOffset = 0x1716;
    private const int ThumbnailOffset = 0x571A;
    private const int ThumbnailMaxSize = 0x4000;
    private const int MinimumHeader = ThumbnailOffset;

    private readonly SafeFileHandle _file;
    private readonly long _length;
    private readonly long _dataStart;
    private readonly int _tablesPerLevel;
    private readonly bool _rootSecondary;
    private readonly int _totalBlocks;
    private readonly byte[] _topHash;
    private readonly int[] _hashBlockStep;
    private readonly Dictionary<long, byte[]> _tables = [];

    private StfsPackage(string path, SafeFileHandle file, StfsHeader header, byte[] raw)
    {
        PackagePath = path;
        _file = file;
        _length = RandomAccess.GetLength(file);
        Header = header;
        _dataStart = (header.HeaderSize + BlockSize - 1L) & ~(BlockSize - 1L);
        var descriptor = raw.AsSpan(0x379, 0x24);
        _tablesPerLevel = (descriptor[2] & 1) != 0 ? 1 : 2;
        _rootSecondary = _tablesPerLevel == 2 && (descriptor[2] & 2) != 0;
        var tableCount = BinaryPrimitives.ReadUInt16LittleEndian(descriptor[3..]);
        var tableStart = descriptor[5] | descriptor[6] << 8 | descriptor[7] << 16;
        _topHash = descriptor.Slice(8, 20).ToArray();
        _totalBlocks = BinaryPrimitives.ReadInt32BigEndian(descriptor[28..]);
        _hashBlockStep = [BlocksPerHashLevel[0] + _tablesPerLevel, BlocksPerHashLevel[1] + (BlocksPerHashLevel[0] + 1) * _tablesPerLevel];
        if (_totalBlocks <= 0 || _totalBlocks >= BlocksPerHashLevel[2] || tableCount == 0 || tableStart >= _totalBlocks)
            throw new InvalidDataException("The package's volume descriptor is damaged.");
        TitleThumbnail = Thumbnail(raw, raw.Length) ?? [];
        Files = ReadFileTable(tableStart, tableCount);
    }

    public string PackagePath { get; }
    public StfsHeader Header { get; }
    public string Magic => Header.Magic;
    public StfsContentType ContentType => Header.ContentType;
    public uint TitleId => Header.TitleId;
    public uint MediaId => Header.MediaId;
    public uint Version => Header.Version;
    public uint BaseVersion => Header.BaseVersion;
    public string DisplayName => Header.DisplayName;
    public IReadOnlyList<StfsLicense> Licenses => Header.Licenses;

    /// <summary>The title tile (a PNG on the disc's packages), empty when the header holds none.</summary>
    public byte[] TitleThumbnail { get; }

    /// <summary>Every file, folders left out.</summary>
    public IReadOnlyList<StfsFile> Files { get; }

    /// <summary>Total size of the files, in bytes.</summary>
    public long ContentBytes => Files.Sum(f => f.Size);

    /// <summary>Opens a package. Throws <see cref="InvalidDataException"/> for a file that is not one or is damaged.</summary>
    public static StfsPackage Open(string path)
    {
        var file = File.OpenHandle(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        try
        {
            var raw = ReadRawHeader(file);
            var header = StfsHeader.Parse(raw) ?? throw new InvalidDataException("Not an Xbox 360 content package.");
            return new StfsPackage(path, file, header, raw);
        }
        catch
        {
            file.Dispose();
            throw;
        }
    }

    /// <summary>The header of the package at <paramref name="path"/>, or null when it is not a package.</summary>
    public static StfsHeader? TryReadHeader(string path)
    {
        try
        {
            using var file = File.OpenHandle(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            return StfsHeader.Parse(ReadRawHeader(file));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return null;
        }
    }

    /// <summary>
    /// The title thumbnail from the header alone (no file table is read): a PNG on the disc's
    /// packages. Throws <see cref="InvalidDataException"/> for a file that is not a package, or a
    /// header without a whole thumbnail.
    /// </summary>
    public static byte[] ReadTitleThumbnail(string path)
    {
        using var file = File.OpenHandle(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        var raw = new byte[ThumbnailOffset + ThumbnailMaxSize];
        var length = RandomAccess.Read(file, raw, 0);
        if (length < MinimumHeader || StfsHeader.Parse(raw) == null)
            throw new InvalidDataException("Not an Xbox 360 content package.");
        return Thumbnail(raw, length) ?? throw new InvalidDataException("The package holds no title thumbnail.");
    }

    // The thumbnail in the first `length` bytes of a header, or null when there is no whole one.
    private static byte[]? Thumbnail(byte[] raw, int length)
    {
        var size = BinaryPrimitives.ReadInt32BigEndian(raw.AsSpan(ThumbnailSizeOffset));
        return size is > 0 and <= ThumbnailMaxSize && ThumbnailOffset + size <= length
            ? raw.AsSpan(ThumbnailOffset, size).ToArray()
            : null;
    }

    /// <summary>A file's bytes (for small files such as the key art).</summary>
    public byte[] ReadAll(StfsFile file)
    {
        var result = new byte[file.Size];
        var done = 0;
        foreach (var block in FileBlocks(file))
        {
            var length = (int)Math.Min(BlockSize, file.Size - done);
            ReadBlock(block).AsSpan(0, length).CopyTo(result.AsSpan(done));
            done += length;
        }
        return result;
    }

    /// <summary>The file at <paramref name="path"/> ('/' between folders, any case), or null.</summary>
    public StfsFile? Find(string path) =>
        Files.FirstOrDefault(f => string.Equals(f.Path, path, StringComparison.OrdinalIgnoreCase));

    /// <summary>
    /// Checks every data block against its level-0 hash, every hash table against the level above,
    /// and the top table against the volume descriptor. Throws <see cref="InvalidDataException"/>
    /// naming the first bad block. Progress counts blocks.
    /// </summary>
    public void Verify(IProgress<StfsProgress>? progress, CancellationToken cancel)
    {
        var top = TopLevel();
        var topTable = Table(0, top, _rootSecondary);
        if (!SHA1.HashData(topTable).AsSpan().SequenceEqual(_topHash))
            throw new InvalidDataException("The package's top hash table is damaged.");
        // Each lower-level table against its entry in the table above.
        for (var level = top; level > 0; level--)
        {
            var span = BlocksPerHashLevel[level - 1];
            for (var first = 0; first < _totalBlocks; first += span)
            {
                var entry = HashEntry(first, level);
                var lower = Table(first, level - 1, EntryActiveSecondary(entry));
                if (!SHA1.HashData(lower).AsSpan().SequenceEqual(entry.AsSpan(0, 20)))
                    throw new InvalidDataException($"The package's hash table for block {first} is damaged.");
            }
        }
        var lastReport = 0L;
        for (var block = 0; block < _totalBlocks; block++)
        {
            cancel.ThrowIfCancellationRequested();
            var expected = HashEntry(block, 0).AsSpan(0, 20);
            if (!SHA1.HashData(ReadBlock(block)).AsSpan().SequenceEqual(expected))
                throw new InvalidDataException($"The package is damaged: block {block} does not match its hash.");
            if (block + 1 == _totalBlocks || block - lastReport >= 256)
            {
                lastReport = block;
                progress?.Report(new StfsProgress(block + 1, _totalBlocks));
            }
        }
    }

    /// <summary>Passes a file's bytes to <paramref name="output"/> block by block.</summary>
    public void CopyTo(StfsFile file, Action<ReadOnlySpan<byte>> output, CancellationToken cancel)
    {
        long written = 0;
        foreach (var block in FileBlocks(file))
        {
            cancel.ThrowIfCancellationRequested();
            var length = (int)Math.Min(BlockSize, file.Size - written);
            output(ReadBlock(block).AsSpan(0, length));
            written += length;
        }
    }

    /// <summary>
    /// Writes every file under <paramref name="targetDirectory"/>, folders included. Progress
    /// counts bytes. A cancelled extraction leaves what it wrote; the caller removes it.
    /// </summary>
    public void Extract(string targetDirectory, IProgress<StfsProgress>? progress, CancellationToken cancel)
    {
        var total = ContentBytes;
        long done = 0;
        foreach (var file in Files)
        {
            cancel.ThrowIfCancellationRequested();
            var target = SafePath.Inside(targetDirectory, file.Path)
                ?? throw new InvalidDataException("The package names a file outside its folder.");
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            using (var output = new FileStream(target, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16))
            {
                long written = 0;
                CopyTo(file, block =>
                {
                    output.Write(block);
                    written += block.Length;
                    done += block.Length;
                    if (written % (64 * BlockSize) == 0 || written == file.Size)
                        progress?.Report(new StfsProgress(done, total));
                }, cancel);
            }
            if (file.Size == 0) progress?.Report(new StfsProgress(done, total));
        }
    }

    public void Dispose() => _file.Dispose();

    private static byte[] ReadRawHeader(SafeFileHandle file)
    {
        var raw = new byte[ThumbnailOffset + ThumbnailMaxSize];
        var length = RandomAccess.Read(file, raw, 0);
        if (length < MinimumHeader) throw new InvalidDataException("Not an Xbox 360 content package.");
        return raw;
    }

    private List<StfsFile> ReadFileTable(int start, int count)
    {
        // Parents are absolute slot numbers in the table (64 per block), empty slots included.
        var entries = new Dictionary<int, (string Name, bool Directory, StfsFile File, int Parent)>();
        var block = start;
        for (var i = 0; i < count; i++)
        {
            var data = ReadBlock(block);
            for (var slot = 0; slot < BlockSize / DirectoryEntrySize; slot++)
            {
                var entry = data.AsSpan(slot * DirectoryEntrySize, DirectoryEntrySize);
                var nameLength = entry[0x28] & 0x3F;
                if (nameLength == 0) continue;
                if (nameLength > MaxNameLength) throw new InvalidDataException("The package's file table is damaged.");
                var name = Encoding.Latin1.GetString(entry[..nameLength]);
                var blocks = entry[0x29] | entry[0x2A] << 8 | entry[0x2B] << 16;
                var first = entry[0x2F] | entry[0x30] << 8 | entry[0x31] << 16;
                var parent = BinaryPrimitives.ReadInt16BigEndian(entry[0x32..]);
                var size = BinaryPrimitives.ReadUInt32BigEndian(entry[0x34..]);
                var isDirectory = (entry[0x28] & 0x80) != 0;
                var contiguous = (entry[0x28] & 0x40) != 0;
                if (!isDirectory && (size > (long)blocks * BlockSize || (blocks > 0 && contiguous && first + (long)blocks > _totalBlocks)))
                    throw new InvalidDataException("The package's file table is damaged.");
                entries[i * (BlockSize / DirectoryEntrySize) + slot] =
                    (name, isDirectory, new StfsFile(name, size, first, blocks, contiguous), parent);
            }
            if (i + 1 < count) block = NextBlock(block);
        }
        var files = new List<StfsFile>();
        foreach (var (_, item) in entries.OrderBy(pair => pair.Key))
        {
            if (item.Directory) continue;
            var parts = new List<string> { item.Name };
            var parent = item.Parent;
            for (var depth = 0; parent >= 0; depth++)
            {
                if (depth >= 64 || !entries.TryGetValue(parent, out var folder))
                    throw new InvalidDataException("The package's file table is damaged.");
                parts.Add(folder.Name);
                parent = folder.Parent;
            }
            parts.Reverse();
            files.Add(item.File with { Path = string.Join('/', parts) });
        }
        return files;
    }

    private IEnumerable<int> FileBlocks(StfsFile file)
    {
        var block = file.StartBlock;
        for (var i = 0; i < file.BlockCount; i++)
        {
            if (block < 0 || block >= _totalBlocks) throw new InvalidDataException("A file's block chain is damaged.");
            yield return block;
            if (i + 1 < file.BlockCount) block = file.Contiguous ? block + 1 : NextBlock(block);
        }
    }

    private int NextBlock(int block)
    {
        var entry = HashEntry(block, 0);
        return entry[0x15] << 16 | entry[0x16] << 8 | entry[0x17];
    }

    private byte[] ReadBlock(int block) => Read(_dataStart + ((long)DataBlockNumber(block) << 12), BlockSize);

    private byte[] Read(long offset, int count)
    {
        if (offset < 0 || offset + count > _length)
            throw new InvalidDataException("The package is truncated.");
        var buffer = new byte[count];
        if (RandomAccess.Read(_file, buffer, offset) != count) throw new InvalidDataException("The package is truncated.");
        return buffer;
    }

    // The physical block number of data block `block`: the hash tables before it are skipped.
    private long DataBlockNumber(int block)
    {
        long number = block;
        for (var level = 0; level < BlocksPerHashLevel.Length; level++)
        {
            number += (long)((block + BlocksPerHashLevel[level]) / BlocksPerHashLevel[level]) * _tablesPerLevel;
            if (block < BlocksPerHashLevel[level]) break;
        }
        return number;
    }

    // The physical block number of the level-`level` hash table covering data block `block`.
    private long HashBlockNumber(int block, int level)
    {
        if (level == 2) return _hashBlockStep[1];
        if (block < BlocksPerHashLevel[level]) return level == 0 ? 0 : _hashBlockStep[level - 1];
        long number = (long)(block / BlocksPerHashLevel[level]) * _hashBlockStep[level];
        if (level == 0)
        {
            number += (long)(block / BlocksPerHashLevel[1] + 1) * _tablesPerLevel;
            if (block < BlocksPerHashLevel[1]) return number;
        }
        return number + _tablesPerLevel;
    }

    // A level-n table exists only above 170^n blocks (ReXGlue's stfs_container_device.cpp,
    // Velocity, Free60): 170 blocks still have a single level-0 table.
    private int TopLevel()
    {
        for (var level = 0; level < BlocksPerHashLevel.Length; level++)
            if (_totalBlocks <= BlocksPerHashLevel[level]) return level;
        return BlocksPerHashLevel.Length - 1;
    }

    private byte[] Table(int block, int level, bool secondary)
    {
        var offset = _dataStart + (HashBlockNumber(block, level) << 12) + (secondary ? BlockSize : 0);
        if (!_tables.TryGetValue(offset, out var table))
        {
            table = Read(offset, BlockSize);
            _tables[offset] = table;
        }
        return table;
    }

    // The 24-byte hash entry for data block `block` (level 0) or for the table covering it.
    private byte[] HashEntry(int block, int level)
    {
        // Walk down from the top so that two-table packages read the active copies.
        var top = TopLevel();
        var secondary = _rootSecondary;
        byte[] entry = [];
        for (var current = top; current >= level; current--)
        {
            var table = Table(block, current, secondary);
            var record = current == 0 ? block % BlocksPerHashLevel[0] : block / BlocksPerHashLevel[current - 1] % BlocksPerHashLevel[0];
            entry = table.AsSpan(record * HashEntrySize, HashEntrySize).ToArray();
            secondary = EntryActiveSecondary(entry);
        }
        return entry;
    }

    private bool EntryActiveSecondary(byte[] entry) =>
        _tablesPerLevel == 2 && (BinaryPrimitives.ReadUInt32BigEndian(entry.AsSpan(0x14)) & 0x40000000) != 0;
}

/// <summary>The package header fields the launcher uses.</summary>
public sealed record StfsHeader(
    string Magic,
    int HeaderSize,
    StfsContentType ContentType,
    uint TitleId,
    uint MediaId,
    uint Version,
    uint BaseVersion,
    string DisplayName,
    IReadOnlyList<StfsLicense> Licenses)
{
    /// <summary>The header fields, or null when the bytes do not start an STFS package.</summary>
    public static StfsHeader? Parse(ReadOnlySpan<byte> raw)
    {
        if (raw.Length < 0x411 + 0x80) return null;
        var magic = Encoding.ASCII.GetString(raw[..4]);
        if (magic is not ("LIVE" or "PIRS" or "CON ")) return null;
        var headerSize = BinaryPrimitives.ReadInt32BigEndian(raw[0x340..]);
        // The descriptor type at +0x3A9 is 0 for STFS (1 is SVOD, which games on disc use).
        if (headerSize < 0x971A || headerSize > 0x10000 || raw[0x379] != 0x24 ||
            BinaryPrimitives.ReadUInt32BigEndian(raw[0x3A9..]) != 0)
            return null;
        var licenses = new StfsLicense[16];
        for (var i = 0; i < licenses.Length; i++)
        {
            var entry = raw.Slice(0x22C + i * 16, 16);
            licenses[i] = new StfsLicense(BinaryPrimitives.ReadUInt64BigEndian(entry),
                BinaryPrimitives.ReadUInt32BigEndian(entry[8..]), BinaryPrimitives.ReadUInt32BigEndian(entry[12..]));
        }
        var name = Encoding.BigEndianUnicode.GetString(raw.Slice(0x411, 0x80));
        var end = name.IndexOf('\0');
        return new StfsHeader(magic, headerSize, (StfsContentType)BinaryPrimitives.ReadUInt32BigEndian(raw[0x344..]),
            BinaryPrimitives.ReadUInt32BigEndian(raw[0x360..]), BinaryPrimitives.ReadUInt32BigEndian(raw[0x354..]),
            BinaryPrimitives.ReadUInt32BigEndian(raw[0x358..]), BinaryPrimitives.ReadUInt32BigEndian(raw[0x35C..]),
            (end < 0 ? name : name[..end]).Trim(), licenses);
    }
}
