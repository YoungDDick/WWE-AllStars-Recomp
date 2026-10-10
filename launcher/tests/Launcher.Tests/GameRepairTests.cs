using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class GameRepairTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-repair-").FullName;
    private readonly TestDisc _disc = new();
    private readonly FakeDiskSpace _space = new();
    private readonly GameSetup _setup;

    public GameRepairTests()
    {
        _disc.WriteImage(Iso);
        _setup = new GameSetup(DiscManifest.Parse(_disc.ManifestText()), _space, new FakeTime());
        _setup.Run(Iso, GameData, SetupStart.Resume, null, TestContext.Current.CancellationToken);
    }

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private string Iso => Path.Combine(_root, "WWE All Stars.iso");
    private string GameData => Path.Combine(_root, "gamedata");
    private string InGameData(string path) => Path.Combine(GameData, path.Replace('/', Path.DirectorySeparatorChar));

    private RepairCheck Check() => _setup.CheckInstalled(GameData, null, TestContext.Current.CancellationToken);

    [Fact]
    public void A_clean_folder_reports_nothing_to_repair()
    {
        var check = Check();

        Assert.True(check.NothingToRepair);
        Assert.Empty(check.Missing);
        Assert.Empty(check.Damaged);
    }

    [Fact]
    public void The_check_reports_each_file_it_verifies_and_ends_complete()
    {
        var reports = new List<SetupProgress>();

        _setup.CheckInstalled(GameData, new Reports(reports.Add), TestContext.Current.CancellationToken);

        Assert.All(reports, report => Assert.Equal(SetupStep.VerifyingGameFiles, report.Step));
        Assert.Contains(reports, report => report.CurrentFile == "CookedXenon/Startup.xxx");
        Assert.Equal(1.0, reports[^1].Fraction);
    }

    private void BreakTwoFiles()
    {
        // Same size, different bytes: only the hash can tell.
        var ring = InGameData("CookedXenon/Maps/Ring.xxx");
        var bytes = File.ReadAllBytes(ring);
        bytes[100] ^= 0xFF;
        File.WriteAllBytes(ring, bytes);
        File.Delete(InGameData("CookedXenon/Startup.xxx"));
    }

    [Fact]
    public void The_check_lists_a_corrupted_and_a_deleted_file_without_needing_the_disc()
    {
        BreakTwoFiles();
        File.Delete(Iso);

        var check = Check();

        Assert.False(check.NothingToRepair);
        Assert.Equal(["CookedXenon/Startup.xxx"], check.Missing);
        Assert.Equal(["CookedXenon/Maps/Ring.xxx"], check.Damaged);
    }

    [Fact]
    public void Repair_re_extracts_only_the_bad_files_and_leaves_good_ones_untouched()
    {
        BreakTwoFiles();
        var old = new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        foreach (var path in new[] { "default.xex", "Coalesced_INT.bin", "nxeart" })
            File.SetLastWriteTimeUtc(InGameData(path), old);

        var repaired = _setup.Repair(Iso, GameData, Check(), null, TestContext.Current.CancellationToken);

        Assert.Equal(["CookedXenon/Maps/Ring.xxx", "CookedXenon/Startup.xxx"], repaired.Order());
        foreach (var (path, data) in _disc.Files)
            Assert.Equal(data, File.ReadAllBytes(InGameData(path)));
        foreach (var path in new[] { "default.xex", "Coalesced_INT.bin", "nxeart" })
            Assert.Equal(old, File.GetLastWriteTimeUtc(InGameData(path)));
        Assert.True(Check().NothingToRepair);
        Assert.Equal(SetupStatus.Done, _setup.StatusOf(GameData));
        Assert.DoesNotContain(Directory.GetFiles(GameData, "*", SearchOption.AllDirectories),
            path => path.EndsWith(".tmp", StringComparison.Ordinal));
    }

    [Fact]
    public void Repair_from_another_disc_is_refused_and_writes_nothing()
    {
        BreakTwoFiles();
        var check = Check();
        var other = Path.Combine(_root, "other.iso");
        _disc.Files["default.xex"] = "XEX2 another region"u8.ToArray();
        _disc.WriteImage(other);

        var error = Assert.Throws<SetupException>(() =>
            _setup.Repair(other, GameData, check, null, TestContext.Current.CancellationToken));

        Assert.Equal(SetupError.WrongDisc, error.Error);
        Assert.False(File.Exists(InGameData("CookedXenon/Startup.xxx")));
        var after = Check();
        Assert.Equal(check.Missing, after.Missing);
        Assert.Equal(check.Damaged, after.Damaged);
    }

    /// <summary>A marker deleted (null) or left by another manifest version: the setup screen shows.</summary>
    private void LoseMarker(string? otherVersion)
    {
        var marker = Path.Combine(GameData, SetupMarker.FileName);
        if (otherVersion is null) File.Delete(marker);
        else SetupMarker.Write(GameData, otherVersion);
        Assert.Equal(SetupStatus.NeedsSetup, _setup.StatusOf(GameData));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("older-manifest")]
    public void Setup_takes_over_a_folder_that_lost_its_marker_and_re_extracts_only_bad_files(string? otherVersion)
    {
        BreakTwoFiles();
        LoseMarker(otherVersion);
        File.WriteAllText(InGameData("notes.txt"), "mine");
        var old = new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        foreach (var path in new[] { "default.xex", "Coalesced_INT.bin", "nxeart" })
            File.SetLastWriteTimeUtc(InGameData(path), old);

        _setup.Run(Iso, GameData, SetupStart.Resume, null, TestContext.Current.CancellationToken);

        Assert.Equal(SetupStatus.Done, _setup.StatusOf(GameData));
        foreach (var (path, data) in _disc.Files)
            Assert.Equal(data, File.ReadAllBytes(InGameData(path)));
        foreach (var path in new[] { "default.xex", "Coalesced_INT.bin", "nxeart" })
            Assert.Equal(old, File.GetLastWriteTimeUtc(InGameData(path)));
        Assert.Equal("mine", File.ReadAllText(InGameData("notes.txt")));
    }

    [Fact]
    public void Taking_over_a_folder_with_another_disc_is_refused_and_writes_nothing()
    {
        BreakTwoFiles();
        LoseMarker(null);
        var check = Check();
        var other = Path.Combine(_root, "other.iso");
        _disc.Files["default.xex"] = "XEX2 another region"u8.ToArray();
        _disc.WriteImage(other);

        var error = Assert.Throws<SetupException>(() =>
            _setup.Run(other, GameData, SetupStart.Resume, null, TestContext.Current.CancellationToken));

        Assert.Equal(SetupError.WrongDisc, error.Error);
        Assert.Equal(SetupStatus.NeedsSetup, _setup.StatusOf(GameData));
        Assert.Equal(check.Missing, Check().Missing);
        Assert.Equal(check.Damaged, Check().Damaged);
    }

    [Fact]
    public void The_summary_says_when_there_is_nothing_to_repair()
    {
        Assert.Equal("Every game file verifies. Nothing to repair.", new RepairCheck([], []).Summary());
    }

    [Fact]
    public void The_summary_names_one_bad_file()
    {
        Assert.Equal("1 file missing or damaged: nxeart.", new RepairCheck(["nxeart"], []).Summary());
    }

    [Fact]
    public void The_summary_names_up_to_three_files_missing_ones_first()
    {
        Assert.Equal("3 files missing or damaged: a.xxx, b.xxx, c.xxx.",
            new RepairCheck(["a.xxx", "b.xxx"], ["c.xxx"]).Summary());
    }

    [Fact]
    public void The_summary_counts_the_rest_when_many_files_are_bad()
    {
        Assert.Equal("5 files missing or damaged: a.xxx, b.xxx, c.xxx and 2 more.",
            new RepairCheck(["a.xxx", "b.xxx"], ["c.xxx", "d.xxx", "e.xxx"]).Summary());
    }

    [Fact]
    public void Repair_needs_only_the_space_of_the_bad_files()
    {
        BreakTwoFiles();
        var check = Check();
        var badBytes = _disc.Files["CookedXenon/Maps/Ring.xxx"].Length + _disc.Files["CookedXenon/Startup.xxx"].Length;
        _space.Free = badBytes - 1;

        var error = Assert.Throws<SetupException>(() =>
            _setup.Repair(Iso, GameData, check, null, TestContext.Current.CancellationToken));

        Assert.Equal(SetupError.DiskFull, error.Error);
        Assert.False(File.Exists(InGameData("CookedXenon/Startup.xxx")));

        _space.Free = badBytes;
        Assert.Equal(2, _setup.Repair(Iso, GameData, check, null, TestContext.Current.CancellationToken).Count);
    }
}
