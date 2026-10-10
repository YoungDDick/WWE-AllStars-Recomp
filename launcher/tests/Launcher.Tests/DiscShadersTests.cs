using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class DiscShadersTests : IDisposable
{
    private readonly string _gameData = Directory.CreateTempSubdirectory("wwe shaders ").FullName;

    // Names from FNV-1a-64 of the bytes, worked out independently (the --wwe_shader_dump_dir naming).
    private const string Pixel = "ps_0ebfdcb75639341a";
    private const string Vertex = "vs_cf73b915d1bc6715";
    private const string OtherPixel = "ps_462f8e854c09056a";

    private static readonly byte[] PixelContainer = TestPackage.Container(vertex: false, 1, 2, 3, 4);
    private static readonly byte[] VertexContainer = TestPackage.Container(vertex: true, 9, 8, 7, 6, 5);
    private static readonly byte[] OtherPixelContainer = TestPackage.Container(vertex: false, 7, 7, 7, 7);

    public void Dispose() => Directory.Delete(_gameData, recursive: true);

    private string Cooked(string name) => Path.Combine(_gameData, "Game", "CookedXenon", name);

    private IReadOnlyList<ShaderContainer> Read() => DiscShaders.Read(_gameData, null, CancellationToken.None);

    [Fact]
    public void A_package_shader_cache_yields_its_containers_named_by_kind_and_hash()
    {
        new TestPackage().Add(PixelContainer).Add(VertexContainer, "FSD_UberPixelShader").Write(Cooked("Engine.xxx"));

        var shaders = Read();

        Assert.Equal([Pixel, Vertex], shaders.Select(shader => shader.Name).Order());
        Assert.Equal(PixelContainer, shaders.Single(shader => shader.Name == Pixel).Bytes);
        Assert.Equal(VertexContainer, shaders.Single(shader => shader.Name == Vertex).Bytes);
    }

    [Fact]
    public void A_container_in_many_packages_is_read_once()
    {
        new TestPackage().Add(PixelContainer).Write(Cooked("Engine.xxx"));
        new TestPackage().Add(PixelContainer).Add(OtherPixelContainer).Write(Cooked("Maps/Arena_002.xxx"));

        Assert.Equal([Pixel, OtherPixel], Read().Select(shader => shader.Name).Order());
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Compressed_packages_are_read(bool wholeFile)
    {
        var compression = wholeFile ? TestPackage.Compression.WholeFile : TestPackage.Compression.Chunks;
        new TestPackage { Compressed = compression }.Add(PixelContainer).Add(VertexContainer).Write(Cooked("Game.xxx"));

        Assert.Equal([Pixel, Vertex], Read().Select(shader => shader.Name).Order());
    }

    [Fact]
    public void A_record_with_type_bytes_before_its_target_is_read()
    {
        new TestPackage().Add(PixelContainer, "FBinkYCrCbToRGBNoPixelAlphaPixelShader", beforeTarget: [1, 2, 3, 4, 5, 6])
            .Write(Cooked("Startup.xxx"));

        Assert.Equal([Pixel], Read().Select(shader => shader.Name));
    }

    [Fact]
    public void Other_files_and_packages_without_a_shader_cache_are_skipped()
    {
        new TestPackage { HasShaderCache = false }.Add(PixelContainer).Write(Cooked("Textures.xxx"));
        File.WriteAllBytes(Cooked("AIFSM_Strategies.xxx"), [0x29, 0x23, 0xBE, 0x84, 1, 2, 3, 4]);
        File.WriteAllBytes(Path.Combine(_gameData, "default.xex"), TestPackage.LiteralLzo(PixelContainer));

        Assert.Empty(Read());
    }

    [Fact]
    public void Containers_for_another_platform_are_skipped()
    {
        new TestPackage().Add(PixelContainer, platform: 0).Add(VertexContainer).Write(Cooked("Engine.xxx"));

        Assert.Equal([Vertex], Read().Select(shader => shader.Name));
    }

    [Fact]
    public void A_damaged_package_is_an_error_that_names_it()
    {
        var bytes = new TestPackage { Compressed = TestPackage.Compression.Chunks }.Add(PixelContainer).Bytes();
        Directory.CreateDirectory(Path.GetDirectoryName(Cooked("x"))!);
        File.WriteAllBytes(Cooked("Engine.xxx"), bytes[..^40]);

        var error = Assert.Throws<InvalidDataException>(Read);

        Assert.Contains("Engine.xxx", error.Message);
    }

    [Fact]
    public void Progress_counts_the_packages()
    {
        new TestPackage().Add(PixelContainer).Write(Cooked("A.xxx"));
        new TestPackage().Add(VertexContainer).Write(Cooked("B.xxx"));
        var reports = new List<(int Done, int Total)>();

        DiscShaders.Read(_gameData, new SyncProgress<(int, int)>(reports.Add), CancellationToken.None);

        Assert.Equal((2, 2), reports[^1]);
    }

    [Fact]
    public void Reading_can_be_cancelled()
    {
        new TestPackage().Add(PixelContainer).Write(Cooked("A.xxx"));
        using var cancel = new CancellationTokenSource();
        cancel.Cancel();

        Assert.ThrowsAny<OperationCanceledException>(() => DiscShaders.Read(_gameData, null, cancel.Token));
    }

    /// <summary>
    /// Opt-in: WWE_TEST_GAME_DATA names an extracted disc. Ticket 02's research counted 2,019
    /// unique containers on it (1,706 pixel, 313 vertex).
    /// </summary>
    [Fact]
    public void The_real_disc_yields_every_container_the_research_counted()
    {
        var gameData = Environment.GetEnvironmentVariable("WWE_TEST_GAME_DATA");
        Assert.SkipWhen(string.IsNullOrEmpty(gameData), "Set WWE_TEST_GAME_DATA to an extracted disc to run this.");

        var shaders = DiscShaders.Read(gameData!, null, CancellationToken.None);

        Assert.Equal(1706, shaders.Count(shader => shader.Name.StartsWith("ps_", StringComparison.Ordinal)));
        Assert.Equal(313, shaders.Count(shader => shader.Name.StartsWith("vs_", StringComparison.Ordinal)));
    }
}

/// <summary>An <see cref="IProgress{T}"/> that reports on the calling thread.</summary>
internal sealed class SyncProgress<T>(Action<T> report) : IProgress<T>
{
    public void Report(T value) => report(value);
}
