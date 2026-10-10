using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class StfsPackageTests : IDisposable
{
    private readonly string _folder = Path.Combine(Path.GetTempPath(), "wwe-stfs-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(_folder)) Directory.Delete(_folder, recursive: true);
    }

    private string Write(TestStfs package, string name = "package") => package.WriteTo(Path.Combine(_folder, name));

    private static TestStfs Pack(params (string Path, byte[] Data)[] files)
    {
        var package = new TestStfs();
        package.Files.AddRange(files);
        return package;
    }

    [Fact]
    public void Reads_the_header_fields()
    {
        var path = Write(new TestStfs
        {
            Magic = "LIVE", ContentType = 0x000B0000, TitleId = 0x54510866, MediaId = 0x60CEF505,
            Version = 0x205, BaseVersion = 5, DisplayName = "WWE All Stars Title Update #2",
        });
        using var package = StfsPackage.Open(path);
        Assert.Equal("LIVE", package.Magic);
        Assert.Equal(StfsContentType.TitleUpdate, package.ContentType);
        Assert.Equal(0x54510866u, package.TitleId);
        Assert.Equal(0x60CEF505u, package.MediaId);
        Assert.Equal(0x205u, package.Version);
        Assert.Equal(5u, package.BaseVersion);
        Assert.Equal("WWE All Stars Title Update #2", package.DisplayName);
        Assert.Equal(0xFFFFFFFFu, package.Licenses[0].Bits);
    }

    [Fact]
    public void Lists_and_reads_files_in_folders_with_one_hash_level()
    {
        var ini = TestStfs.Content(200, 1);
        var big = TestStfs.Content(3 * 0x1000 + 17, 2);
        var path = Write(Pack(("Phase1/Xe-Game.ini", ini), ("Phase1/Big.xxx", big), ("DLCTOC.txt", TestStfs.Content(5, 3))));
        using var package = StfsPackage.Open(path);
        Assert.Equal(["DLCTOC.txt", "Phase1/Big.xxx", "Phase1/Xe-Game.ini"],
            package.Files.Select(f => f.Path).Order(StringComparer.Ordinal));
        Assert.Equal(ini, package.ReadAll(package.Files.Single(f => f.Path == "Phase1/Xe-Game.ini")));
        Assert.Equal(big, package.ReadAll(package.Files.Single(f => f.Path == "Phase1/Big.xxx")));
        package.Verify(null, CancellationToken.None);
    }

    [Fact]
    public void Reads_and_verifies_a_package_with_two_hash_levels()
    {
        // 200 data blocks: two level-0 tables under one level-1 table.
        var large = TestStfs.Content(200 * 0x1000 - 5, 4);
        var path = Write(Pack(("Large.xxx", large)));
        using var package = StfsPackage.Open(path);
        Assert.Equal(large, package.ReadAll(package.Files.Single()));
        package.Verify(null, CancellationToken.None);
    }

    [Theory]
    [InlineData(169)]
    [InlineData(170)]
    [InlineData(171)]
    public void The_level_1_table_appears_only_above_170_blocks(int blocks)
    {
        // The file table takes one block, so the file gets the rest.
        var content = TestStfs.Content((blocks - 1) * 0x1000, 13);
        var path = Write(Pack(("Edge.xxx", content)));
        using var package = StfsPackage.Open(path);
        Assert.Equal(content, package.ReadAll(package.Files.Single()));
        package.Verify(null, CancellationToken.None);
    }

    [Fact]
    public void Follows_the_block_chain_of_a_fragmented_file()
    {
        var content = TestStfs.Content(5 * 0x1000 + 100, 5);
        var path = Write(new TestStfs { Fragmented = true, Files = { ("Chained.xxx", content) } });
        using var package = StfsPackage.Open(path);
        Assert.Equal(content, package.ReadAll(package.Files.Single()));
        package.Verify(null, CancellationToken.None);
    }

    [Fact]
    public void Reads_a_package_with_two_tables_per_level_using_the_active_one()
    {
        var content = TestStfs.Content(4 * 0x1000, 6);
        var path = Write(new TestStfs
        {
            Magic = "CON ", TwoTablesPerLevel = true, SecondaryActive = true, Fragmented = true,
            Files = { ("Saved.bin", content) },
        });
        using var package = StfsPackage.Open(path);
        Assert.Equal(content, package.ReadAll(package.Files.Single()));
        package.Verify(null, CancellationToken.None);
    }

    [Fact]
    public void A_damaged_data_block_fails_verification()
    {
        var path = Write(Pack(("a.bin", TestStfs.Content(3 * 0x1000, 7))));
        var bytes = File.ReadAllBytes(path);
        bytes[TestStfs.DataBlockOffset(2) + 10] ^= 0xFF;
        File.WriteAllBytes(path, bytes);
        using var package = StfsPackage.Open(path);
        var error = Assert.Throws<InvalidDataException>(() => package.Verify(null, CancellationToken.None));
        Assert.Contains("block 2", error.Message);
    }

    [Fact]
    public void A_damaged_level_0_table_fails_verification()
    {
        var path = Write(Pack(("a.bin", TestStfs.Content(200 * 0x1000, 8))));
        var bytes = File.ReadAllBytes(path);
        bytes[TestStfs.DataBlockOffset(170) - 2 * 0x1000 + 0x18 * 3 + 0x15] ^= 0x01; // the first table's entry 3
        File.WriteAllBytes(path, bytes);
        using var package = StfsPackage.Open(path);
        Assert.Throws<InvalidDataException>(() => package.Verify(null, CancellationToken.None));
    }

    [Fact]
    public void A_truncated_package_fails_to_open_or_verify()
    {
        var path = Write(Pack(("a.bin", TestStfs.Content(10 * 0x1000, 9))));
        var bytes = File.ReadAllBytes(path);
        File.WriteAllBytes(path, bytes[..(bytes.Length - 0x1800)]);
        var error = Record.Exception(() =>
        {
            using var package = StfsPackage.Open(path);
            package.Verify(null, CancellationToken.None);
        });
        Assert.IsType<InvalidDataException>(error);
    }

    [Theory]
    [InlineData("ZIP!")]
    [InlineData("XEX2")]
    public void A_file_without_an_stfs_magic_is_not_a_package(string magic)
    {
        var path = Write(new TestStfs { Magic = magic });
        Assert.Throws<InvalidDataException>(() => StfsPackage.Open(path));
        Assert.Null(StfsPackage.TryReadHeader(path));
    }

    [Fact]
    public void Verification_reports_progress_and_can_be_cancelled()
    {
        var path = Write(Pack(("a.bin", TestStfs.Content(20 * 0x1000, 10))));
        using var package = StfsPackage.Open(path);
        var reports = new List<StfsProgress>();
        package.Verify(new SyncProgress<StfsProgress>(reports.Add), CancellationToken.None);
        Assert.NotEmpty(reports);
        Assert.Equal(reports[^1].Total, reports[^1].Done);
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        Assert.Throws<OperationCanceledException>(() => package.Verify(null, cancelled.Token));
    }

    [Fact]
    public void Extracts_every_file_with_folders_and_can_be_cancelled()
    {
        var a = TestStfs.Content(0x1000 + 1, 11);
        var b = TestStfs.Content(77, 12);
        var path = Write(new TestStfs { Fragmented = true, Files = { ("Phase1/a.xxx", a), ("b.txt", b) } });
        using var package = StfsPackage.Open(path);
        var target = Path.Combine(_folder, "out");
        long last = 0;
        package.Extract(target, new SyncProgress<StfsProgress>(p => last = p.Done), CancellationToken.None);
        Assert.Equal(a, File.ReadAllBytes(Path.Combine(target, "Phase1", "a.xxx")));
        Assert.Equal(b, File.ReadAllBytes(Path.Combine(target, "b.txt")));
        Assert.Equal(a.Length + b.Length, last);

        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        Assert.Throws<OperationCanceledException>(() =>
            package.Extract(Path.Combine(_folder, "out2"), null, cancelled.Token));
    }

    [Fact]
    public void Reads_the_title_thumbnail()
    {
        var path = Write(new TestStfs { TitleThumbnail = [1, 2, 3, 4] });
        using var package = StfsPackage.Open(path);
        Assert.Equal(new byte[] { 1, 2, 3, 4 }, package.TitleThumbnail);
    }
}

