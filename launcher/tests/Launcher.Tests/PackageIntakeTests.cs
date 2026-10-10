using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class PackageIntakeTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-intake-").FullName;
    private string Temp => Path.Combine(_root, "temp");

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private static byte[] Tu2(uint version = 0x205) => new TestStfs
    {
        ContentType = 0x000B0000, MediaId = 0x60CEF505, Version = version, BaseVersion = 5,
        DisplayName = "WWE All Stars Title Update #2", Files = { ("default.xexp", TestStfs.Content(3000, 1)) },
    }.Build();

    private static byte[] Dlc(string name = "WWE ALL STARS PACK 1") => new TestStfs
    {
        DisplayName = name, Files = { ("Phase1/DLCTOC.txt", TestStfs.Content(40, 2)) },
    }.Build();

    private static byte[] OtherGame() => new TestStfs { TitleId = 0x5451081F, DisplayName = "Another game's pack" }.Build();

    private string File(string relative, byte[] data)
    {
        var path = Path.Combine(_root, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        System.IO.File.WriteAllBytes(path, data);
        return path;
    }

    private PackageIntakeResult Scan(params string[] inputs) => PackageIntake.Scan(inputs, Temp, CancellationToken.None);

    [Fact]
    public void A_single_package_file_is_identified()
    {
        using var result = Scan(File("tu00000005_00000000", Tu2()));
        var item = Assert.Single(result.Items);
        Assert.Equal(PackageKind.TitleUpdate, item.Kind);
        Assert.Equal("WWE All Stars Title Update #2", item.Header!.DisplayName);
        Assert.True(System.IO.File.Exists(item.PackagePath));
    }

    [Fact]
    public void A_nested_folder_and_a_console_content_tree_are_searched()
    {
        File("usb/Content/0000000000000000/54510866/000B0000/tu00000005_00000000", Tu2());
        File("usb/Content/0000000000000000/54510866/00000002/F5B3E0579C2BD2939FD88B0E0A936E8095E284D754", Dlc());
        File("usb/Content/0000000000000000/54510866/00000002/readme.txt", "not a package"u8.ToArray());
        File("usb/Cache/TU_16L61V6_0000008000000.00000000000O2", Tu2(version: 0x105)); // a cache file, another TU

        using var result = Scan(Path.Combine(_root, "usb"));
        Assert.Equal(3, result.Items.Count);
        Assert.Equal(2, result.Items.Count(i => i.Kind == PackageKind.TitleUpdate));
        Assert.Single(result.Items, i => i.Kind == PackageKind.Dlc);
    }

    [Fact]
    public void A_zip_is_unpacked_to_temp_and_cleaned_up_afterwards()
    {
        var zip = Path.Combine(_root, "tu2.zip");
        TestArchives.Zip(zip, ("TU2/tu00000005_00000000", Tu2()), ("TU2/notes.txt", "hi"u8.ToArray()));
        string package;
        using (var result = Scan(zip))
        {
            var item = Assert.Single(result.Items);
            Assert.Equal(PackageKind.TitleUpdate, item.Kind);
            Assert.Equal(zip, item.Source);
            package = item.PackagePath!;
            Assert.StartsWith(Temp, package);
            Assert.True(System.IO.File.Exists(package));
        }
        Assert.False(System.IO.File.Exists(package));
    }

    [Fact]
    public void A_7z_is_unpacked()
    {
        var archive = Path.Combine(_root, "dlc.7z");
        TestArchives.SevenZip(archive, ("F5B3E0579C2BD2939FD88B0E0A936E8095E284D754", Dlc()));
        using var result = Scan(archive);
        Assert.Equal(PackageKind.Dlc, Assert.Single(result.Items).Kind);
    }

    [Fact]
    public void A_rar_is_unpacked()
    {
        var archive = Path.Combine(_root, "dlc.rar");
        TestArchives.Rar(archive, ("Pack1/F5B3E0579C2BD2939FD88B0E0A936E8095E284D754", Dlc()));
        using var result = Scan(archive);
        Assert.Equal(PackageKind.Dlc, Assert.Single(result.Items).Kind);
    }

    [Fact]
    public void A_mix_of_good_and_bad_inputs_reports_each()
    {
        var good = File("good/tu", Tu2());
        var other = File("other/pack", OtherGame());
        var text = File("text.txt", "hello"u8.ToArray());
        var missing = Path.Combine(_root, "nothing here");
        var empty = Directory.CreateDirectory(Path.Combine(_root, "empty")).FullName;
        var badZip = File("broken.zip", "PK not really"u8.ToArray());

        using var result = Scan(good, other, text, missing, empty, badZip);
        Assert.Equal(PackageKind.TitleUpdate, result.Items.Single(i => i.Source == good).Kind);
        Assert.Equal(PackageKind.OtherGame, result.Items.Single(i => i.Source == other).Kind);
        Assert.Equal(PackageKind.NotAPackage, result.Items.Single(i => i.Source == text).Kind);
        Assert.Equal(PackageKind.NotAPackage, result.Items.Single(i => i.Source == missing).Kind);
        Assert.Equal(PackageKind.NotAPackage, result.Items.Single(i => i.Source == empty).Kind);
        Assert.Equal(PackageKind.NotAPackage, result.Items.Single(i => i.Source == badZip).Kind);
        Assert.All(result.Items.Where(i => i.Kind == PackageKind.NotAPackage), i => Assert.NotNull(i.Problem));
    }

    [Fact]
    public void The_same_package_found_twice_is_listed_once()
    {
        var bytes = Dlc();
        var a = File("a/pack", bytes);
        File("a/copy/pack", bytes);
        using var result = Scan(a, Path.Combine(_root, "a"));
        Assert.Single(result.Items);
    }

    [Fact]
    public void An_archive_entry_that_climbs_out_of_the_temp_folder_is_skipped()
    {
        var zip = Path.Combine(_root, "evil.zip");
        TestArchives.Zip(zip, ("../../escaped", Dlc()), ("ok/pack", Dlc("OK PACK")));
        using var result = Scan(zip);
        Assert.Equal("OK PACK", Assert.Single(result.Items).Header!.DisplayName);
        Assert.False(System.IO.File.Exists(Path.Combine(_root, "escaped")));
    }

    [Fact]
    public void Scanning_can_be_cancelled()
    {
        var path = File("tu", Tu2());
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        Assert.Throws<OperationCanceledException>(() => PackageIntake.Scan([path], Temp, cancelled.Token));
    }
}
