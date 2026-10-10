using System.Buffers.Binary;
using System.Text;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class DlcLibraryTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-dlc-").FullName;

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private string UserData => Path.Combine(_root, "userdata");
    private DlcLibrary Library() => new(UserData);

    private static readonly (string Path, byte[] Data)[] PackFiles =
    [
        ("Phase1/DLCTOC.txt", TestStfs.Content(120, 41)),
        ("Phase1/Xe-Game.ini", TestStfs.Content(200, 42)),
        ("Phase1/Shared_Characters_DLC.xxx", TestStfs.Content(5 * 0x1000 + 3, 43)),
    ];

    private string Pack(string fileName = DlcLibrary.Pack1FileName, string displayName = "WWE ALL STARS PACK 1",
        uint contentType = 0x00000002, uint titleId = 0x54510866, (string, byte[])[]? files = null)
    {
        var package = new TestStfs { ContentType = contentType, TitleId = titleId, DisplayName = displayName };
        package.Files.AddRange(files ?? PackFiles);
        return package.WriteTo(Path.Combine(_root, "dumps", fileName));
    }

    private string ContentFolder(string fileName) =>
        Path.Combine(UserData, "0000000000000000", "54510866", "00000002", fileName);

    private string HeaderFile(string fileName) =>
        Path.Combine(UserData, "0000000000000000", "54510866", "Headers", "00000002", fileName + ".header");

    private DlcPack Install(string package) => Library().Install(package, null, CancellationToken.None);

    private string[] GameVisibleFolders() =>
        Directory.Exists(Path.Combine(UserData, "0000000000000000", "54510866", "00000002"))
            ? Directory.GetDirectories(Path.Combine(UserData, "0000000000000000", "54510866", "00000002")).Select(folder => Path.GetFileName(folder)!).ToArray()
            : [];

    [Fact]
    public void Installing_unpacks_into_the_marketplace_content_folder_with_a_header()
    {
        var installed = Install(Pack());

        foreach (var (path, data) in PackFiles)
            Assert.Equal(data, File.ReadAllBytes(Path.Combine(ContentFolder(DlcLibrary.Pack1FileName), path)));
        var header = File.ReadAllBytes(HeaderFile(DlcLibrary.Pack1FileName));
        // XCONTENT_AGGREGATE_DATA (0x148 bytes, big-endian fields) then the licence mask.
        Assert.Equal(0x148 + 4, header.Length);
        Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(header)); // device: HDD
        Assert.Equal(2u, BinaryPrimitives.ReadUInt32BigEndian(header.AsSpan(4))); // marketplace content
        Assert.Equal("WWE ALL STARS PACK 1", Encoding.BigEndianUnicode.GetString(header, 8, 40));
        Assert.Equal(DlcLibrary.Pack1FileName, Encoding.ASCII.GetString(header, 0x108, 42).TrimEnd('\0'));
        Assert.Equal(0ul, BinaryPrimitives.ReadUInt64BigEndian(header.AsSpan(0x138)));
        Assert.Equal(0x54510866u, BinaryPrimitives.ReadUInt32BigEndian(header.AsSpan(0x140)));
        Assert.Equal(0xFFFFFFFFu, BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(0x148)));
        Assert.Equal("WWE ALL STARS PACK 1", installed.DisplayName);
    }

    [Fact]
    public void Pack_1_shows_its_name_and_five_wrestlers_and_an_unknown_pack_its_own_name()
    {
        Install(Pack());
        Install(Pack("0123456789ABCDEF0123456789ABCDEF0123456789", "SOME OTHER PACK"));

        var packs = Library().Installed();
        var pack1 = packs.Single(p => p.FileName == DlcLibrary.Pack1FileName);
        Assert.Equal("WWE ALL STARS PACK 1", pack1.DisplayName);
        Assert.Equal(["Million Dollar Man", "Honky Tonk Man", "Dusty Rhodes", "Ted DiBiase Jr", "Cody Rhodes"], pack1.Wrestlers);
        var other = packs.Single(p => p.FileName != DlcLibrary.Pack1FileName);
        Assert.Equal("SOME OTHER PACK", other.DisplayName);
        Assert.Empty(other.Wrestlers);
        Assert.Equal(PackFiles.Sum(f => (long)f.Data.Length), other.Bytes);
    }

    [Fact]
    public void Re_adding_a_pack_replaces_it()
    {
        Install(Pack());
        var stray = Path.Combine(ContentFolder(DlcLibrary.Pack1FileName), "Phase1", "old-only.txt");
        File.WriteAllText(stray, "from the old copy");

        Install(Pack());

        Assert.False(File.Exists(stray));
        Assert.Single(Library().Installed());
    }

    [Fact]
    public void Remove_deletes_the_folder_and_its_header()
    {
        Install(Pack());
        Library().Remove(DlcLibrary.Pack1FileName);
        Assert.False(Directory.Exists(ContentFolder(DlcLibrary.Pack1FileName)));
        Assert.False(File.Exists(HeaderFile(DlcLibrary.Pack1FileName)));
        Assert.Empty(Library().Installed());
    }

    [Fact]
    public void The_source_package_is_left_where_it_was_and_not_copied()
    {
        var source = Pack();
        Install(source);
        Assert.True(File.Exists(source));
        Assert.Empty(Directory.EnumerateFiles(UserData, DlcLibrary.Pack1FileName, SearchOption.AllDirectories));
    }

    [Fact]
    public void A_title_update_on_the_dlc_page_points_to_title_update_2_and_another_games_pack_is_named()
    {
        var tu = Pack("tu00000005_00000000", "WWE All Stars Title Update #2", contentType: 0x000B0000);
        var other = Pack("other", "SMACKDOWN PACK", titleId: 0x5451081F);
        using (var intake = PackageIntake.Scan([tu], Path.Combine(_root, "temp"), CancellationToken.None))
        {
            var error = Assert.Throws<SetupException>(() => DlcLibrary.Choose(intake));
            Assert.Contains("Title Update 2", error.Message);
        }
        using (var intake = PackageIntake.Scan([other], Path.Combine(_root, "temp"), CancellationToken.None))
        {
            var error = Assert.Throws<SetupException>(() => DlcLibrary.Choose(intake));
            Assert.Contains("SMACKDOWN PACK", error.Message);
            Assert.Contains("another game", error.Message);
        }
        Assert.Empty(Library().Installed());
    }

    [Fact]
    public void Several_packs_in_one_folder_are_all_chosen()
    {
        Pack();
        Pack("0123456789ABCDEF0123456789ABCDEF0123456789", "SOME OTHER PACK");
        using var intake = PackageIntake.Scan([Path.Combine(_root, "dumps")], Path.Combine(_root, "temp"), CancellationToken.None);
        Assert.Equal(2, DlcLibrary.Choose(intake).Count);
    }

    [Fact]
    public void A_damaged_pack_is_refused_and_installs_nothing()
    {
        var path = Pack();
        var bytes = File.ReadAllBytes(path);
        bytes[TestStfs.DataBlockOffset(3) + 7] ^= 0xFF;
        File.WriteAllBytes(path, bytes);

        var error = Assert.Throws<SetupException>(() => Install(path));
        Assert.Equal(SetupError.DamagedFile, error.Error);
        Assert.False(Directory.Exists(ContentFolder(DlcLibrary.Pack1FileName)));
        Assert.False(File.Exists(HeaderFile(DlcLibrary.Pack1FileName)));
    }

    [Fact]
    public void A_renamed_pack_1_is_still_recognised_and_installed_under_its_console_name()
    {
        Install(Pack("pack1-copy.bin"));
        var pack = Assert.Single(Library().Installed());
        Assert.Equal(DlcLibrary.Pack1FileName, pack.FileName);
        Assert.Equal(5, pack.Wrestlers.Count);
    }

    [Fact]
    public void The_game_never_sees_a_half_installed_pack()
    {
        // The game lists every folder under 00000002: staging lives elsewhere.
        var path = Pack();
        using var cancelled = new CancellationTokenSource();
        var progress = new SyncProgress<DlcProgress>(p => { if (p.Unpacking) cancelled.Cancel(); });
        Assert.ThrowsAny<OperationCanceledException>(() => Library().Install(path, progress, cancelled.Token));
        Assert.Empty(GameVisibleFolders());
        Install(path);
        Assert.Equal([DlcLibrary.Pack1FileName], GameVisibleFolders());
    }

    [Fact]
    public void Progress_reports_the_check_then_the_unpack()
    {
        var phases = new List<bool>();
        Library().Install(Pack(), new SyncProgress<DlcProgress>(p => phases.Add(p.Unpacking)), CancellationToken.None);
        Assert.False(phases[0]);
        Assert.True(phases[^1]);
    }

    [Fact]
    public void A_damaged_replacement_keeps_the_installed_pack()
    {
        Install(Pack());
        var path = Pack();
        var bytes = File.ReadAllBytes(path);
        bytes[TestStfs.DataBlockOffset(3) + 7] ^= 0xFF;
        File.WriteAllBytes(path, bytes);

        Assert.Throws<SetupException>(() => Install(path));
        Assert.Single(Library().Installed());
        Assert.Equal(PackFiles[0].Data, File.ReadAllBytes(Path.Combine(ContentFolder(DlcLibrary.Pack1FileName), "Phase1", "DLCTOC.txt")));
    }
}
