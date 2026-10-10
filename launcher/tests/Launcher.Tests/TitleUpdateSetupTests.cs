using System.Security.Cryptography;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class TitleUpdateSetupTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-tu-").FullName;
    private readonly FakeDiskSpace _space = new();
    private readonly FakeTime _time = new();

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private string GameData => Path.Combine(_root, "gamedata");
    private string UpdateData => TitleUpdateSetup.UpdateDataDirectory(GameData);

    // A stand-in TU2: four files, the first being default.xexp, plus a manifest of them.
    private static readonly (string Path, byte[] Data)[] Tu2Files =
    [
        ("default.xexp", TestStfs.Content(5000, 21)),
        ("Coalesced.ini", TestStfs.Content(3 * 0x1000 + 5, 22)),
        ("Engine.xxx", TestStfs.Content(9 * 0x1000, 23)),
        ("PatchTOC.txt", TestStfs.Content(300, 24)),
    ];

    private static string ManifestText() =>
        "manifest_version\ttest-tu2\n" + string.Join('\n', Tu2Files.Select(f =>
            $"{Convert.ToHexStringLower(SHA256.HashData(f.Data))}\t{f.Data.Length}\t{f.Path}")) + "\n";

    private TitleUpdateSetup Setup() => new(TitleUpdateManifest.Parse(ManifestText()), _space, _time);

    private static TestStfs Tu(uint media = TitleUpdateSetup.MediaId, params (string Path, byte[] Data)[] files)
    {
        var package = new TestStfs
        {
            ContentType = 0x000B0000, MediaId = media, Version = 5, BaseVersion = 5,
            DisplayName = "WWE All Stars Title Update #2",
        };
        package.Files.AddRange(files.Length > 0 ? files : Tu2Files);
        return package;
    }

    private string Write(TestStfs package, string name = "tu00000005_00000000") =>
        package.WriteTo(Path.Combine(_root, "input", name));

    private void Install(string package, SetupStart start = SetupStart.Resume, IProgress<SetupProgress>? progress = null,
        CancellationToken? cancel = null) =>
        Setup().Install(package, UpdateData, start, progress, cancel ?? TestContext.Current.CancellationToken);

    [Fact]
    public void The_update_folder_sits_next_to_the_game_data_folder()
    {
        Assert.Equal(Path.Combine(_root, "updatedata"), UpdateData);
    }

    [Fact]
    public void Installing_unpacks_and_verifies_every_file_and_writes_the_marker()
    {
        var setup = Setup();
        Assert.Equal(SetupStatus.NeedsSetup, setup.StatusOf(UpdateData));
        Install(Write(Tu()));
        foreach (var (path, data) in Tu2Files) Assert.Equal(data, File.ReadAllBytes(Path.Combine(UpdateData, path)));
        Assert.Equal(SetupStatus.Done, setup.StatusOf(UpdateData));
        Assert.False(Directory.Exists(TitleUpdateSetup.PartialDirectory(UpdateData)));
    }

    [Fact]
    public void Missing_title_update_is_explained_without_links()
    {
        var empty = Directory.CreateDirectory(Path.Combine(_root, "empty")).FullName;
        using var intake = PackageIntake.Scan([empty], Path.Combine(_root, "temp"), CancellationToken.None);
        var error = Assert.Throws<SetupException>(() => TitleUpdateSetup.Choose(intake));
        Assert.Equal(SetupError.NoTitleUpdate, error.Error);
        Assert.Contains("your own console", error.Message);
        Assert.DoesNotContain("http", error.Message);
    }

    [Fact]
    public void Dlc_another_game_and_another_title_update_are_refused_with_what_they_are()
    {
        var dlc = Write(new TestStfs { DisplayName = "WWE ALL STARS PACK 1" }, "dlc");
        var other = Write(new TestStfs { ContentType = 0x000B0000, TitleId = 0x5451081F }, "other");
        var tu1 = Write(Tu(files: [("default.xexp", TestStfs.Content(5000, 99)), ("Coalesced.ini", [1])]), "tu1");
        var region = Write(Tu(media: 0x12345678), "region");

        foreach (var (path, expected) in new[] { (dlc, "DLC"), (other, "another game"), (tu1, "not Title Update 2"), (region, "another version") })
        {
            using var intake = PackageIntake.Scan([path], Path.Combine(_root, "temp"), CancellationToken.None);
            var error = Assert.Throws<SetupException>(() =>
            {
                var chosen = TitleUpdateSetup.Choose(intake);
                Setup().Check(chosen);
            });
            Assert.Equal(SetupError.WrongPackage, error.Error);
            Assert.Contains(expected, error.Message);
        }
        Assert.False(Directory.Exists(UpdateData));
    }

    [Fact]
    public void A_title_update_in_a_zip_is_found()
    {
        var zip = Path.Combine(_root, "input", "tu2.zip");
        TestArchives.Zip(zip, ("54510866/000B0000/tu00000005_00000000", Tu().Build()));
        using var intake = PackageIntake.Scan([zip], Path.Combine(_root, "temp"), CancellationToken.None);
        Install(TitleUpdateSetup.Choose(intake));
        Assert.Equal(SetupStatus.Done, Setup().StatusOf(UpdateData));
    }

    [Fact]
    public void A_damaged_title_update_is_refused_and_leaves_no_update_folder()
    {
        var path = Write(Tu());
        var bytes = File.ReadAllBytes(path);
        bytes[TestStfs.DataBlockOffset(9) + 100] ^= 0x55; // inside Engine.xxx (blocks 7-15)
        File.WriteAllBytes(path, bytes);

        var error = Assert.Throws<SetupException>(() => Install(path));
        Assert.Equal(SetupError.DamagedFile, error.Error);
        Assert.Contains("damaged", error.Message);
        Assert.False(Directory.Exists(UpdateData));
    }

    [Fact]
    public void Damage_inside_default_xexp_is_reported_as_damage_not_as_another_update()
    {
        var path = Write(Tu());
        var bytes = File.ReadAllBytes(path);
        bytes[TestStfs.DataBlockOffset(1) + 50] ^= 0x55; // default.xexp: blocks 1-2
        File.WriteAllBytes(path, bytes);

        Assert.Equal(SetupError.DamagedFile, Assert.Throws<SetupException>(() => Install(path)).Error);
        Assert.False(Directory.Exists(UpdateData));
    }

    [Fact]
    public void A_truncated_title_update_is_damaged_even_in_the_quick_check()
    {
        var path = Write(Tu());
        var bytes = File.ReadAllBytes(path);
        File.WriteAllBytes(path, bytes[..(int)(TestStfs.DataBlockOffset(2) + 10)]);

        Assert.Equal(SetupError.DamagedFile, Assert.Throws<SetupException>(() => Setup().Check(path)).Error);
    }

    [Fact]
    public void A_folder_that_lost_its_marker_but_verifies_is_marked_again()
    {
        Install(Write(Tu()));
        File.Delete(Path.Combine(UpdateData, TitleUpdateSetup.MarkerFileName));
        var setup = Setup();
        Assert.NotEqual(SetupStatus.Done, setup.StatusOf(UpdateData));
        var check = setup.CheckInstalled(UpdateData, null, CancellationToken.None);
        setup.MarkVerified(UpdateData, check);
        Assert.Equal(SetupStatus.Done, setup.StatusOf(UpdateData));
    }

    [Fact]
    public void Not_enough_space_is_refused_before_unpacking()
    {
        _space.Free = 1000;
        var error = Assert.Throws<SetupException>(() => Install(Write(Tu())));
        Assert.Equal(SetupError.DiskFull, error.Error);
        Assert.False(Directory.Exists(TitleUpdateSetup.PartialDirectory(UpdateData)));
    }

    [Fact]
    public void Cancel_during_the_unpack_leaves_a_partial_folder_that_resume_finishes()
    {
        var path = Write(Tu());
        using var cancel = new CancellationTokenSource();
        var reports = new Reports(report =>
        {
            if (report.Step == SetupStep.UnpackingTitleUpdate && report.CurrentFile == "Engine.xxx") cancel.Cancel();
        });
        Assert.ThrowsAny<OperationCanceledException>(() => Install(path, progress: reports, cancel: cancel.Token));
        var partial = TitleUpdateSetup.PartialDirectory(UpdateData);
        Assert.True(Directory.Exists(partial));
        Assert.False(Directory.Exists(UpdateData));
        Assert.Equal(SetupStatus.Partial, Setup().StatusOf(UpdateData));
        var kept = File.GetLastWriteTimeUtc(Path.Combine(partial, "default.xexp"));

        Install(path);
        Assert.Equal(SetupStatus.Done, Setup().StatusOf(UpdateData));
        Assert.Equal(kept, File.GetLastWriteTimeUtc(Path.Combine(UpdateData, "default.xexp")));
    }

    [Fact]
    public void Repair_finds_one_bad_file_and_replaces_only_that_one()
    {
        var path = Write(Tu());
        Install(path);
        var engine = Path.Combine(UpdateData, "Engine.xxx");
        File.WriteAllBytes(engine, [1, 2, 3]);
        File.Delete(Path.Combine(UpdateData, "PatchTOC.txt"));
        var untouched = File.GetLastWriteTimeUtc(Path.Combine(UpdateData, "Coalesced.ini"));

        var setup = Setup();
        var check = setup.CheckInstalled(UpdateData, null, CancellationToken.None);
        Assert.Equal(["PatchTOC.txt"], check.Missing);
        Assert.Equal(["Engine.xxx"], check.Damaged);

        var repaired = setup.Repair(path, UpdateData, check, null, CancellationToken.None);
        Assert.Equal(["Engine.xxx", "PatchTOC.txt"], repaired.Order(StringComparer.Ordinal));
        Assert.True(setup.CheckInstalled(UpdateData, null, CancellationToken.None).NothingToRepair);
        Assert.Equal(untouched, File.GetLastWriteTimeUtc(Path.Combine(UpdateData, "Coalesced.ini")));
    }

    [Fact]
    public void Progress_names_the_verify_and_unpack_steps()
    {
        var reports = new Reports(_ => { });
        Install(Write(Tu()), progress: reports);
        Assert.Contains(reports.All, r => r.Step == SetupStep.VerifyingTitleUpdate);
        Assert.Contains(reports.All, r => r.Step == SetupStep.UnpackingTitleUpdate && r.CurrentFile == "Coalesced.ini");
        Assert.Equal(SetupStep.Done, reports.All[^1].Step);
    }
}
