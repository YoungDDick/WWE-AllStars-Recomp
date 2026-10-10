using System.Buffers.Binary;
using System.Globalization;
using System.Text.RegularExpressions;

namespace WweAllStars.Launcher.Core;

/// <summary>Why a shader pack cannot be used; anything but <see cref="None"/> runs the game on Emulated.</summary>
public enum ShaderPackProblem
{
    None,
    /// <summary>No pack file.</summary>
    Missing,
    /// <summary>A pack of another format version, or converted with another XenosRecomp patch.</summary>
    Stale,
    /// <summary>Not a pack, cut short, or its parts do not add up.</summary>
    Damaged,
}

public sealed record ShaderPackCheck(ShaderPackProblem Problem, string Detail)
{
    public bool Ready => Problem == ShaderPackProblem.None;
}

/// <summary>
/// The shader pack (spec, "Shader pack"): XenosRecomp's converted shaders in one file under the
/// cache folder, which Native loads at startup. src/native/shader_pack.h owns the layout;
/// scripts/shader_pack.py writes the same bytes for developers.
/// </summary>
public static partial class ShaderPackFile
{
    /// <summary>The pack's name in the cache folder (kShaderPackFileName in the game).</summary>
    public const string FileName = "wweallstars.shaderpack";

    public const uint FormatVersion = 1;

    private const int HeaderSize = 32;
    private const int EntrySize = 24;
    private const int FetchSlotSize = 16;
    private const int Slots = 32;

    /// <summary>
    /// The pack built from XenosRecomp's output folder: <c>shader_cache.cpp</c> and the
    /// <c>hlsl</c> folder of per-shader sources, which give each shader's fetch slots.
    /// </summary>
    /// <exception cref="InvalidDataException">The folder does not hold XenosRecomp output.</exception>
    public static byte[] Build(string converterOutputDirectory, ulong converterId)
    {
        var text = File.ReadAllText(Path.Combine(converterOutputDirectory, "shader_cache.cpp"));
        var table = EntryTable().Match(text);
        var spirv = CompressedSpirv().Match(text);
        var sizes = new Dictionary<string, long>(StringComparer.Ordinal);
        foreach (Match size in Size().Matches(text))
            sizes[size.Groups[1].Value] = long.Parse(size.Groups[2].Value, CultureInfo.InvariantCulture);
        if (!table.Success || !spirv.Success || sizes.Count != 2)
            throw new InvalidDataException("XenosRecomp did not write a shader cache.");

        var entries = Entry().Matches(table.Groups[1].Value)
            .Select(match => (Hash: ParseNumber(match.Groups[1].Value), Offset: uint.Parse(match.Groups[4].Value, CultureInfo.InvariantCulture),
                Size: uint.Parse(match.Groups[5].Value, CultureInfo.InvariantCulture), Mask: uint.Parse(match.Groups[6].Value, CultureInfo.InvariantCulture)))
            .OrderBy(entry => entry.Hash).ToList();
        var compressed = spirv.Groups[1].Value.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
            .Select(value => byte.Parse(value, CultureInfo.InvariantCulture)).ToArray();
        if (compressed.Length != sizes["g_spirvCacheCompressedSize"])
            throw new InvalidDataException("XenosRecomp's compressed SPIR-V does not match its size.");
        var slots = FetchSlots(Path.Combine(converterOutputDirectory, "hlsl"));

        var pack = new byte[HeaderSize + entries.Count * EntrySize + slots.Count * FetchSlotSize + compressed.Length];
        var span = pack.AsSpan();
        "WWSP"u8.CopyTo(span);
        BinaryPrimitives.WriteUInt32LittleEndian(span[4..], FormatVersion);
        BinaryPrimitives.WriteUInt64LittleEndian(span[8..], converterId);
        BinaryPrimitives.WriteInt32LittleEndian(span[16..], entries.Count);
        BinaryPrimitives.WriteInt32LittleEndian(span[20..], slots.Count);
        BinaryPrimitives.WriteInt32LittleEndian(span[24..], compressed.Length);
        BinaryPrimitives.WriteUInt32LittleEndian(span[28..], checked((uint)sizes["g_spirvCacheDecompressedSize"]));
        var p = HeaderSize;
        foreach (var (hash, offset, size, mask) in entries)
        {
            BinaryPrimitives.WriteUInt64LittleEndian(span[p..], hash);
            BinaryPrimitives.WriteUInt32LittleEndian(span[(p + 8)..], offset);
            BinaryPrimitives.WriteUInt32LittleEndian(span[(p + 12)..], size);
            BinaryPrimitives.WriteUInt32LittleEndian(span[(p + 16)..], mask);
            p += EntrySize;
        }
        foreach (var (fnv, mask) in slots)
        {
            BinaryPrimitives.WriteUInt64LittleEndian(span[p..], fnv);
            BinaryPrimitives.WriteUInt32LittleEndian(span[(p + 8)..], mask);
            p += FetchSlotSize;
        }
        compressed.CopyTo(span[p..]);
        return pack;
    }

    /// <summary>
    /// Whether the game would load the pack at <paramref name="path"/>, from its header and sizes
    /// (the checks of src/native/shader_pack.cpp short of decompressing the SPIR-V).
    /// </summary>
    public static ShaderPackCheck Check(string path, ulong expectedConverterId)
    {
        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(path);
        }
        catch (Exception error) when (error is FileNotFoundException or DirectoryNotFoundException)
        {
            return new(ShaderPackProblem.Missing, "there is no shader pack");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return new(ShaderPackProblem.Damaged, "the shader pack cannot be read: " + error.Message);
        }
        var span = bytes.AsSpan();
        if (bytes.Length < 8 || !span.StartsWith("WWSP"u8))
            return new(ShaderPackProblem.Damaged, "the file is not a shader pack");
        if (BinaryPrimitives.ReadUInt32LittleEndian(span[4..]) is var version && version != FormatVersion)
            return new(ShaderPackProblem.Stale, $"the shader pack has format version {version}, this game reads {FormatVersion}");
        if (bytes.Length < HeaderSize) return new(ShaderPackProblem.Damaged, "the shader pack is cut short");
        var converterId = BinaryPrimitives.ReadUInt64LittleEndian(span[8..]);
        var entryCount = BinaryPrimitives.ReadUInt32LittleEndian(span[16..]);
        var slotCount = BinaryPrimitives.ReadUInt32LittleEndian(span[20..]);
        var compressedSize = BinaryPrimitives.ReadUInt32LittleEndian(span[24..]);
        var expectedSize = HeaderSize + (long)entryCount * EntrySize + (long)slotCount * FetchSlotSize + compressedSize;
        if (bytes.Length != expectedSize)
            return new(ShaderPackProblem.Damaged, $"the shader pack is {bytes.Length} bytes, its header describes {expectedSize}");
        if (converterId != expectedConverterId)
            return new(ShaderPackProblem.Stale,
                $"the shader pack was converted with XenosRecomp patch {converterId:x16}, this game expects {expectedConverterId:x16}");
        if (entryCount == 0) return new(ShaderPackProblem.Missing, "the shader pack holds no shaders");
        return new(ShaderPackProblem.None, $"{entryCount} shaders");
    }

    /// <summary>
    /// The FNV-1a hashes (the container names) of the shaders in a pack that
    /// <see cref="Check"/> found ready: its fetch slot table lists every converted shader.
    /// </summary>
    public static HashSet<ulong> ConvertedShaders(string path)
    {
        var bytes = File.ReadAllBytes(path).AsSpan();
        var entryCount = BinaryPrimitives.ReadInt32LittleEndian(bytes[16..]);
        var slotCount = BinaryPrimitives.ReadInt32LittleEndian(bytes[20..]);
        var hashes = new HashSet<ulong>();
        for (var i = 0; i < slotCount; i++)
            hashes.Add(BinaryPrimitives.ReadUInt64LittleEndian(bytes[(HeaderSize + entryCount * EntrySize + i * FetchSlotSize)..]));
        return hashes;
    }

    /// <summary>(FNV-1a container hash, mask of the fetch slots its body reads) per converted shader, sorted.</summary>
    private static List<(ulong Fnv, uint Mask)> FetchSlots(string hlslDirectory)
    {
        var slots = new List<(ulong, uint)>();
        if (!Directory.Exists(hlslDirectory)) return slots;
        foreach (var path in Directory.EnumerateFiles(hlslDirectory, "*.hlsl"))
        {
            var name = HlslName().Match(Path.GetFileName(path));
            if (!name.Success) continue;
            slots.Add((ulong.Parse(name.Groups[1].Value, NumberStyles.HexNumber, CultureInfo.InvariantCulture),
                SlotMask(File.ReadAllText(path))));
        }
        slots.Sort();
        return slots;
    }

    /// <summary>
    /// XenosRecomp names each sampler slot with a define (offset / 4 = dimension * 32 + slot); a
    /// reference to a name without a define means "unsure", so the shader binds every slot.
    /// </summary>
    private static uint SlotMask(string hlsl)
    {
        var slots = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (Match define in SlotDefine().Matches(hlsl))
            slots[define.Groups[1].Value] = int.Parse(define.Groups[2].Value, CultureInfo.InvariantCulture) / 4 % Slots;
        var mask = 0u;
        foreach (var line in hlsl.Split('\n'))
        {
            var stripped = line.Trim();
            if (stripped.StartsWith("#define", StringComparison.Ordinal) || stripped.Contains("packoffset", StringComparison.Ordinal)) continue;
            foreach (Match reference in SlotReference().Matches(line))
            {
                if (!slots.TryGetValue(reference.Groups[1].Value, out var slot)) return uint.MaxValue;
                mask |= 1u << slot;
            }
        }
        return mask;
    }

    private static ulong ParseNumber(string text) =>
        text.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? ulong.Parse(text.AsSpan(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture)
            : ulong.Parse(text, CultureInfo.InvariantCulture);

    // The same patterns as scripts/shader_pack.py and scripts/shader_fetch_slots.py.
    [GeneratedRegex(@"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}")]
    private static partial Regex Entry();

    [GeneratedRegex(@"g_shaderCacheEntries\[\]\s*=\s*\{(.*?)\n\};", RegexOptions.Singleline)]
    private static partial Regex EntryTable();

    [GeneratedRegex(@"g_compressedSpirvCache\[\]\s*=\s*\{([^}]*)\}")]
    private static partial Regex CompressedSpirv();

    [GeneratedRegex(@"\b(g_spirvCacheCompressedSize|g_spirvCacheDecompressedSize)\s*=\s*(\d+)")]
    private static partial Regex Size();

    [GeneratedRegex(@"^#define (\w+)_(?:Texture\w+|Sampler)DescriptorIndex vk::RawBufferLoad<uint>\(g_PushConstants\.SharedConstants \+ (\d+)\)", RegexOptions.Multiline)]
    private static partial Regex SlotDefine();

    [GeneratedRegex(@"\b(\w+)_(?:Texture(?:2D|3D|Cube)|Sampler)DescriptorIndex\b")]
    private static partial Regex SlotReference();

    [GeneratedRegex(@"^(?:ps|vs)_([0-9a-f]{16})\.hlsl$")]
    private static partial Regex HlslName();
}
