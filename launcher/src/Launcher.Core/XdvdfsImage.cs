using System.Buffers.Binary;
using System.Text;

namespace WweAllStars.Launcher.Core;

/// <summary>One file inside a disc image: its path ('/' separators), byte offset and size.</summary>
public sealed record DiscFile(string Path, long Offset, long Size);

/// <summary>
/// Reads an Xbox 360 XDVDFS disc image without mounting it. Ported from SVR2011-Recomp's
/// launcher (<c>launcher/portable/DiscImage.cs</c>) and mirrors <c>scripts/inspect_disc.py</c>,
/// including its defences against malformed images. Opens the image read-only and shared, so
/// it can never change the player's ISO.
/// </summary>
public sealed class XdvdfsImage : IDisposable
{
    private static readonly byte[] Magic = Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA");
    // Where the game partition starts: plain XDVDFS, then the XGD3, XGD2 and XGD1 layouts.
    private static readonly long[] PartitionOffsets = [0, 0xFD90000, 0x2080000, 0x18300000];
    private const int Sector = 2048;
    private const int VolumeDescriptor = 0x10000;
    private const int BlockSize = 1 << 20;

    private readonly FileStream _stream;
    private readonly long _partition;
    private readonly uint _rootSector;
    private readonly uint _rootSize;

    private XdvdfsImage(FileStream stream, long partition, uint rootSector, uint rootSize)
    {
        _stream = stream;
        _partition = partition;
        _rootSector = rootSector;
        _rootSize = rootSize;
    }

    /// <summary>Opens <paramref name="path"/>; null when it holds no XDVDFS volume.</summary>
    public static XdvdfsImage? TryOpen(string path)
    {
        var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, BlockSize);
        try
        {
            var header = new byte[Sector];
            foreach (var partition in PartitionOffsets)
            {
                if (partition + VolumeDescriptor + Sector > stream.Length) continue;
                stream.Position = partition + VolumeDescriptor;
                stream.ReadExactly(header);
                if (header.AsSpan(0, Magic.Length).SequenceEqual(Magic)
                    && header.AsSpan(Sector - Magic.Length).SequenceEqual(Magic))
                {
                    return new XdvdfsImage(stream, partition,
                        BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(20)),
                        BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(24)));
                }
            }
        }
        catch
        {
            stream.Dispose();
            throw;
        }
        stream.Dispose();
        return null;
    }

    public void Dispose() => _stream.Dispose();

    /// <summary>Every file on the disc, sorted by path, case-insensitively.</summary>
    public IReadOnlyList<DiscFile> Files()
    {
        var result = new List<DiscFile>();
        ReadDirectory(_rootSector, _rootSize, "", 0, [], result);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var file in result)
            if (!names.Add(file.Path)) throw new InvalidDataException("Duplicate file names in the disc image.");
        result.Sort((a, b) => string.Compare(a.Path, b.Path, StringComparison.OrdinalIgnoreCase));
        return result;
    }

    /// <summary>Copies one file's bytes to <paramref name="output"/>, block by block.</summary>
    public void CopyTo(DiscFile file, Action<ReadOnlySpan<byte>> output, CancellationToken cancel)
    {
        var buffer = new byte[BlockSize];
        _stream.Position = file.Offset;
        for (var remaining = file.Size; remaining > 0;)
        {
            cancel.ThrowIfCancellationRequested();
            var read = _stream.Read(buffer, 0, (int)Math.Min(buffer.Length, remaining));
            if (read == 0) throw new InvalidDataException("The disc image is truncated.");
            output(buffer.AsSpan(0, read));
            remaining -= read;
        }
    }

    private byte[] Read(long offset, int count)
    {
        if (offset < 0 || count < 0 || offset + count > _stream.Length)
            throw new InvalidDataException("Read outside the disc image.");
        _stream.Position = offset;
        var data = new byte[count];
        try { _stream.ReadExactly(data); }
        catch (EndOfStreamException) { throw new InvalidDataException("The disc image is truncated."); }
        return data;
    }

    private void ReadDirectory(uint sector, uint length, string parent, int depth, HashSet<uint> seen, List<DiscFile> result)
    {
        if (depth > 64 || seen.Contains(sector)) throw new InvalidDataException("Cyclic or excessively deep directory structure.");
        if (length == 0) return;
        seen.Add(sector);
        if (length > 16 * 1024 * 1024) throw new InvalidDataException("Unreasonably large directory table.");
        var data = Read(_partition + (long)sector * Sector, (int)length);
        var pending = new Stack<int>();
        var visited = new HashSet<int>();
        pending.Push(0);
        while (pending.Count > 0)
        {
            var offset = pending.Pop();
            if (!visited.Add(offset) || offset + 14 > length) throw new InvalidDataException("Invalid directory entry pointer.");
            var entry = data.AsSpan(offset);
            var left = BinaryPrimitives.ReadUInt16LittleEndian(entry);
            var right = BinaryPrimitives.ReadUInt16LittleEndian(entry[2..]);
            var entrySector = BinaryPrimitives.ReadUInt32LittleEndian(entry[4..]);
            var entryLength = BinaryPrimitives.ReadUInt32LittleEndian(entry[8..]);
            var attributes = entry[12];
            var nameLength = entry[13];
            if (nameLength == 0 || offset + 14 + nameLength > length) throw new InvalidDataException("Invalid entry name length.");
            var name = Encoding.ASCII.GetString(data, offset + 14, nameLength);
            if (name is "." or ".." || name.IndexOfAny(['/', '\\', ':', '\0', '<', '>', '"', '|', '?', '*']) >= 0)
                throw new InvalidDataException("Unsafe entry name in the disc image.");
            var relative = parent.Length > 0 ? parent + "/" + name : name;
            var absolute = _partition + (long)entrySector * Sector;
            if (absolute + entryLength > _stream.Length) throw new InvalidDataException("Entry outside the disc image: " + relative);
            if ((attributes & 0x10) != 0) ReadDirectory(entrySector, entryLength, relative, depth + 1, seen, result);
            else result.Add(new DiscFile(relative, absolute, entryLength));
            if (right != 0) pending.Push(right * 4);
            if (left != 0) pending.Push(left * 4);
        }
    }
}
