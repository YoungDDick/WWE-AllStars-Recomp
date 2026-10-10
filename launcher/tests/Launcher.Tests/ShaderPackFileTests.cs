using System.Buffers.Binary;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class ShaderPackFileTests : IDisposable
{
    private readonly string _folder = Directory.CreateTempSubdirectory("wwe pack ").FullName;

    public void Dispose() => Directory.Delete(_folder, recursive: true);

    /// <summary>XenosRecomp's shader_cache.cpp, in the shape its main.cpp prints it.</summary>
    internal const string XenosCache =
        "#include \"shader_cache.h\"\n" +
        "ShaderCacheEntry g_shaderCacheEntries[] = {\n" +
        "\t{ 0xB0, 0, 0, 3, 2, 0 },\n" +
        "\t{ 0xA0, 0, 0, 0, 3, 1 },\n" +
        "};\n" +
        "const uint8_t g_compressedSpirvCache[] = {40,181,47,253,};\n" +
        "const size_t g_spirvCacheCompressedSize = 4;\n" +
        "const size_t g_spirvCacheDecompressedSize = 5;\n" +
        "const size_t g_shaderCacheEntryCount = 2;\n";

    internal const string SampledHlsl =
        "#define sampDiffuse_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 20)\n" +
        "#define sampMask_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 140)\n" +
        "float4 main() { return g_Texture2DDescriptorHeap[sampDiffuse_Texture2DDescriptorIndex]" +
        ".Sample(g_SamplerDescriptorHeap[sampMask_SamplerDescriptorIndex], 0); }\n";

    private string WriteOutput(string cache, params (string Name, string Text)[] hlsl)
    {
        var output = Path.Combine(_folder, "converted");
        Directory.CreateDirectory(Path.Combine(output, "hlsl"));
        File.WriteAllText(Path.Combine(output, "shader_cache.cpp"), cache);
        foreach (var (name, text) in hlsl) File.WriteAllText(Path.Combine(output, "hlsl", name), text);
        return output;
    }

    [Fact]
    public void A_pack_holds_the_sorted_shaders_the_fetch_slots_and_the_spirv()
    {
        var output = WriteOutput(XenosCache,
            ("ps_00000000000000ff.hlsl", SampledHlsl),
            ("vs_0000000000000010.hlsl", "float4 main() { return 0; }\n"),
            ("notes.txt", "ignored"));

        var pack = ShaderPackFile.Build(output, 0x0123456789ABCDEF);

        byte[] expected =
        [
            .. "WWSP"u8, 1, 0, 0, 0, 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
            2, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0,
            // { hash, SPIR-V offset, size, spec constants mask, reserved }, sorted by hash
            0xA0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
            0xB0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            // { FNV-1a, slot mask, reserved }: slot 20/4 = 5 and slot (140/4) % 32 = 3; then none
            0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0xFF, 0, 0, 0, 0, 0, 0, 0, 0x28, 0, 0, 0, 0, 0, 0, 0,
            40, 181, 47, 253,
        ];
        Assert.Equal(expected, pack);
    }

    [Fact]
    public void A_shader_that_uses_an_undeclared_slot_binds_every_slot()
    {
        var output = WriteOutput(XenosCache,
            ("ps_0000000000000001.hlsl", "float4 main() { return g_Texture2DDescriptorHeap[other_Texture2DDescriptorIndex]; }\n"));

        var pack = ShaderPackFile.Build(output, 1);

        Assert.Equal(uint.MaxValue, BinaryPrimitives.ReadUInt32LittleEndian(pack.AsSpan(32 + 2 * 24 + 8)));
    }

    [Fact]
    public void Output_that_is_not_from_XenosRecomp_is_refused()
    {
        var output = WriteOutput("int main() {}\n");

        Assert.Throws<InvalidDataException>(() => ShaderPackFile.Build(output, 1));
    }

    private string WritePack(byte[] bytes)
    {
        var path = Path.Combine(_folder, ShaderPackFile.FileName);
        File.WriteAllBytes(path, bytes);
        return path;
    }

    private byte[] ValidPack(ulong converterId) => ShaderPackFile.Build(WriteOutput(XenosCache), converterId);

    [Fact]
    public void A_pack_from_this_converter_is_ready()
    {
        var check = ShaderPackFile.Check(WritePack(ValidPack(7)), expectedConverterId: 7);

        Assert.Equal(ShaderPackProblem.None, check.Problem);
    }

    [Fact]
    public void No_pack_is_missing()
    {
        var check = ShaderPackFile.Check(Path.Combine(_folder, ShaderPackFile.FileName), 7);

        Assert.Equal(ShaderPackProblem.Missing, check.Problem);
    }

    [Fact]
    public void A_pack_from_another_converter_is_stale()
    {
        var check = ShaderPackFile.Check(WritePack(ValidPack(6)), expectedConverterId: 7);

        Assert.Equal(ShaderPackProblem.Stale, check.Problem);
    }

    [Fact]
    public void A_pack_of_another_format_version_is_stale()
    {
        var pack = ValidPack(7);
        pack[4] = 2;

        Assert.Equal(ShaderPackProblem.Stale, ShaderPackFile.Check(WritePack(pack), 7).Problem);
    }

    [Theory]
    [InlineData(10)]
    [InlineData(-1)]
    public void A_cut_or_padded_pack_is_damaged(int change)
    {
        var pack = ValidPack(7);
        byte[] changed = change < 0 ? pack[..^1] : [.. pack, .. new byte[change]];

        Assert.Equal(ShaderPackProblem.Damaged, ShaderPackFile.Check(WritePack(changed), 7).Problem);
    }

    [Fact]
    public void A_file_that_is_not_a_pack_is_damaged()
    {
        Assert.Equal(ShaderPackProblem.Damaged, ShaderPackFile.Check(WritePack("hello world, not a pack"u8.ToArray()), 7).Problem);
    }
}
