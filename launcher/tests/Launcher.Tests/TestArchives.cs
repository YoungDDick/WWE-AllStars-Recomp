using System.Buffers.Binary;
using System.IO.Compression;
using System.Text;
using SharpCompress.Common;
using SharpCompress.Writers;

namespace WweAllStars.Launcher.Tests;

/// <summary>Archives of test files, made in code: .zip (.NET), .7z (SharpCompress) and .rar (RAR 4, stored).</summary>
internal static class TestArchives
{
    public static void Zip(string path, params (string Name, byte[] Data)[] files)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var archive = ZipFile.Open(path, ZipArchiveMode.Create);
        foreach (var (name, data) in files)
        {
            using var stream = archive.CreateEntry(name).Open();
            stream.Write(data);
        }
    }

    public static void SevenZip(string path, params (string Name, byte[] Data)[] files)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var output = File.Create(path);
        using var writer = WriterFactory.OpenWriter(output, ArchiveType.SevenZip, new WriterOptions(CompressionType.LZMA));
        foreach (var (name, data) in files)
        {
            using var input = new MemoryStream(data);
            writer.Write(name, input, DateTime.UnixEpoch);
        }
    }

    /// <summary>
    /// A RAR 4 archive with stored (uncompressed) entries: marker, archive header, one file header
    /// per file, end block. RAR can't be written by a library, and the stored format is small.
    /// </summary>
    public static void Rar(string path, params (string Name, byte[] Data)[] files)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var output = new MemoryStream();
        output.Write([0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00]);
        Block(output, 0x73, 0x0000, new byte[6]);
        foreach (var (name, data) in files)
        {
            var nameBytes = Encoding.ASCII.GetBytes(name.Replace('/', '\\'));
            var body = new byte[25 + nameBytes.Length];
            var span = body.AsSpan();
            BinaryPrimitives.WriteUInt32LittleEndian(span, (uint)data.Length); // packed size
            BinaryPrimitives.WriteUInt32LittleEndian(span[4..], (uint)data.Length); // unpacked size
            span[8] = 2; // host OS: Windows
            BinaryPrimitives.WriteUInt32LittleEndian(span[9..], Crc32(data));
            BinaryPrimitives.WriteUInt32LittleEndian(span[13..], 0x5A210000); // a DOS date and time
            span[17] = 20; // version needed to unpack
            span[18] = 0x30; // method: store
            BinaryPrimitives.WriteUInt16LittleEndian(span[19..], (ushort)nameBytes.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(span[21..], 0x20); // attributes: archive
            nameBytes.CopyTo(span[25..]);
            Block(output, 0x74, 0x8000, body); // 0x8000: data follows the header
            output.Write(data);
        }
        Block(output, 0x7B, 0x4000, []);
        File.WriteAllBytes(path, output.ToArray());
    }

    private static void Block(Stream output, byte type, ushort flags, byte[] body)
    {
        var header = new byte[7 + body.Length];
        header[2] = type;
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(3), flags);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(5), (ushort)header.Length);
        body.CopyTo(header, 7);
        BinaryPrimitives.WriteUInt16LittleEndian(header, (ushort)Crc32(header.AsSpan(2)));
        output.Write(header);
    }

    private static uint Crc32(ReadOnlySpan<byte> data)
    {
        var crc = 0xFFFFFFFFu;
        foreach (var b in data)
        {
            crc ^= b;
            for (var bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (uint)-(int)(crc & 1));
        }
        return ~crc;
    }
}
