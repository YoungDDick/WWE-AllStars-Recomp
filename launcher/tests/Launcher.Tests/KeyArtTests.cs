using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class KeyArtTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-launcher-").FullName;

    public void Dispose() => Directory.Delete(_root, recursive: true);

    // Spans three blocks.
    private static readonly byte[] Background = TestDisc.Bytes(2 * 0x1000 + 123, 7);
    private static readonly byte[] Slot = TestDisc.Bytes(900, 8);

    private static TestStfs Package() => new()
    {
        Magic = "PIRS",
        ContentType = 0x00030000,
        Files = { ("nxeslot.jpg", Slot), ("nxebg.jpg", Background) },
    };

    private string Nxeart => Path.Combine(_root, KeyArt.PackageFile);

    [Fact]
    public void The_background_is_read_whole_from_the_package()
    {
        var path = Package().WriteTo(Nxeart);

        Assert.Equal(Background, KeyArt.Read(path, KeyArt.BackgroundFile));
        Assert.Equal(Slot, KeyArt.Read(path, "nxeslot.jpg"));
    }

    [Fact]
    public void The_home_screen_finds_the_background_in_the_game_data_folder()
    {
        Package().WriteTo(Nxeart);

        Assert.Equal(Background, KeyArt.BackgroundIn(_root));
    }

    [Fact]
    public void A_missing_package_means_no_key_art()
    {
        Assert.Null(KeyArt.BackgroundIn(_root));
        Assert.Null(KeyArt.BackgroundIn(Path.Combine(_root, "no such folder")));
    }

    [Fact]
    public void A_package_without_the_background_means_no_key_art()
    {
        new TestStfs { Magic = "PIRS", Files = { ("nxeslot.jpg", Slot) } }.WriteTo(Nxeart);

        Assert.Throws<FileNotFoundException>(() => KeyArt.Read(Path.Combine(_root, KeyArt.PackageFile), KeyArt.BackgroundFile));
        Assert.Null(KeyArt.BackgroundIn(_root));
    }

    [Theory]
    [InlineData("CON ")]
    [InlineData("LIVE")]
    public void A_file_that_is_not_a_dashboard_art_package_means_no_key_art(string magic)
    {
        var path = new TestStfs { Magic = magic, Files = { ("nxebg.jpg", Background) } }.WriteTo(Nxeart);

        Assert.Throws<InvalidDataException>(() => KeyArt.Read(path, KeyArt.BackgroundFile));
        Assert.Null(KeyArt.BackgroundIn(_root));
    }

    [Fact]
    public void A_truncated_package_means_no_key_art()
    {
        var bytes = Package().Build();
        File.WriteAllBytes(Path.Combine(_root, KeyArt.PackageFile), bytes[..^0x1800]);

        Assert.Throws<InvalidDataException>(() => KeyArt.Read(Path.Combine(_root, KeyArt.PackageFile), KeyArt.BackgroundFile));
        Assert.Null(KeyArt.BackgroundIn(_root));
    }
}
