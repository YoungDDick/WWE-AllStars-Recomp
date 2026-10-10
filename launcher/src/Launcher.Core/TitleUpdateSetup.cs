using System.Security.Cryptography;

namespace WweAllStars.Launcher.Core;

/// <summary>
/// The 13 files of Title Update 2 with their sizes and SHA-256s, no data: embedded from
/// <c>launcher/tu2-manifest.tsv</c>, in the disc manifest's format.
/// </summary>
public sealed class TitleUpdateManifest
{
    public const string PatchPath = "default.xexp";

    private readonly Dictionary<string, ManifestEntry> _byPath;

    private TitleUpdateManifest(string version, IReadOnlyList<ManifestEntry> files)
    {
        Version = version;
        Files = files;
        _byPath = files.ToDictionary(file => file.Path, StringComparer.OrdinalIgnoreCase);
        Patch = Find(PatchPath) ?? throw new InvalidDataException("The title update manifest lists no default.xexp.");
    }

    /// <summary>Names the manifest; the update folder's marker records it.</summary>
    public string Version { get; }
    public IReadOnlyList<ManifestEntry> Files { get; }
    /// <summary>The patch to the game exe: its hash tells TU2 from any other update.</summary>
    public ManifestEntry Patch { get; }
    public long TotalBytes => Files.Sum(file => file.Size);

    public ManifestEntry? Find(string path) => _byPath.GetValueOrDefault(path);

    /// <summary>Title Update 2 for media 60CEF505, the update this build is generated from (ADR-0022).</summary>
    public static TitleUpdateManifest Supported { get; } = LoadSupported();

    public static TitleUpdateManifest Parse(string text)
    {
        var (version, files) = DiscManifest.ParseLines(text, "title update manifest");
        return new TitleUpdateManifest(version, files);
    }

    private static TitleUpdateManifest LoadSupported()
    {
        using var stream = typeof(TitleUpdateManifest).Assembly.GetManifestResourceStream("tu2-manifest.tsv")
            ?? throw new InvalidOperationException("The title update manifest is not embedded in the launcher.");
        using var reader = new StreamReader(stream);
        return Parse(reader.ReadToEnd());
    }
}

/// <summary>
/// Title Update 2 in setup, the 0.9.0 upgrade and Repair (tu2-dlc spec, "Setup and upgrade"):
/// check the package (a title update for this disc whose <c>default.xexp</c> is TU2's), check
/// every block, unpack into <c>&lt;updatedata&gt;.partial</c> verifying each file against the
/// manifest, write the marker, rename the folder into place. As in disc setup, the rename is the
/// commit point and a cancelled unpack resumes, keeping the files that verify. The package is
/// only ever read. The update folder sits next to the game data folder; the game mounts it as
/// <c>update:</c>.
/// </summary>
public sealed class TitleUpdateSetup(TitleUpdateManifest manifest, IDiskSpace space, TimeProvider time)
{
    public const string MarkerFileName = ".wweallstars-update";
    public const string UpdateDataFolderName = "updatedata";
    /// <summary>The disc's media ID, which TU2's header carries.</summary>
    public const uint MediaId = 0x60CEF505;

    private const string MarkerKey = "manifest_version=";
    private const string TempSuffix = ".tmp";
    private static readonly TimeSpan ReportInterval = TimeSpan.FromMilliseconds(100);

    /// <summary>What a player without TU2 is told: what it is and where it comes from, no links.</summary>
    public const string WhatTitleUpdate2Is =
        "This version of the port needs Title Update 2, the free \"Compatibility Pack\" update WWE All Stars " +
        "got on Xbox 360; the DLC wrestlers need it, so the port is built on it. It is not included: dump it from your own console (the tu00000005_00000000 file, or a " +
        "TU_ file from the console's Cache folder) and choose it here. A zip, 7z or rar with it inside works too.";

    public static string UpdateDataDirectory(string gameDataDirectory) =>
        Path.Combine(Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(Path.GetFullPath(gameDataDirectory)))!,
            UpdateDataFolderName);

    public static string PartialDirectory(string updateDataDirectory) =>
        Path.TrimEndingDirectorySeparator(Path.GetFullPath(updateDataDirectory)) + ".partial";

    public SetupStatus StatusOf(string updateDataDirectory)
    {
        if (ReadMarker(updateDataDirectory) == manifest.Version) return SetupStatus.Done;
        return Directory.Exists(PartialDirectory(updateDataDirectory)) ? SetupStatus.Partial : SetupStatus.NeedsSetup;
    }

    /// <summary>
    /// The title update among intake findings, or a <see cref="SetupException"/> that says what
    /// was found instead (DLC, another game's package, nothing).
    /// </summary>
    public static string Choose(PackageIntakeResult intake)
    {
        if (intake.Items.FirstOrDefault(item => item.Kind == PackageKind.TitleUpdate) is { PackagePath: { } path }) return path;
        if (intake.Items.FirstOrDefault(item => item.Kind == PackageKind.Dlc) is { } dlc)
            throw new SetupException(SetupError.WrongPackage,
                $"That is DLC ({dlc.Header!.DisplayName}), not the title update. Add DLC after setup; " +
                "setup needs Title Update 2 here.");
        if (intake.Items.Any(item => item.Kind == PackageKind.OtherGame))
            throw new SetupException(SetupError.WrongPackage,
                "That package is for another game. Choose Title Update 2 for WWE All Stars.");
        if (intake.Items.Any(item => item.Kind == PackageKind.OtherContent))
            throw new SetupException(SetupError.WrongPackage,
                "That is WWE All Stars content, but not a title update. " + WhatTitleUpdate2Is);
        throw new SetupException(SetupError.NoTitleUpdate, "No title update found there. " + WhatTitleUpdate2Is);
    }

    /// <summary>
    /// Checks the package without unpacking it: a title update for this disc whose
    /// <c>default.xexp</c> matches TU2's. Does not check every block (<see cref="Install"/> does).
    /// </summary>
    public void Check(string packagePath)
    {
        using var package = Open(packagePath);
        CheckHeader(package);
        CheckPatch(package, CancellationToken.None);
    }

    /// <summary>Checks the package and every block of it, unpacking nothing (headless <c>--verify</c>).</summary>
    public void Verify(string packagePath, IProgress<SetupProgress>? progress, CancellationToken cancel)
    {
        using var package = Open(packagePath);
        CheckHeader(package);
        VerifyBlocks(package, new Reporter(progress, time), cancel);
        CheckPatch(package, cancel);
    }

    /// <summary>Checks, verifies and unpacks TU2 into <paramref name="updateDataDirectory"/>.</summary>
    public void Install(string packagePath, string updateDataDirectory, SetupStart start,
        IProgress<SetupProgress>? progress, CancellationToken cancel)
    {
        var update = Path.TrimEndingDirectorySeparator(Path.GetFullPath(updateDataDirectory));
        var partial = PartialDirectory(update);
        var reporter = new Reporter(progress, time);
        try
        {
            if (StatusOf(update) == SetupStatus.Done)
            {
                reporter.Report(SetupStep.Done, manifest.TotalBytes, manifest.TotalBytes, null, force: true);
                return;
            }
            using var package = Open(packagePath);
            CheckHeader(package);
            VerifyBlocks(package, reporter, cancel);
            CheckPatch(package, cancel);

            if (start == SetupStart.Restart && Directory.Exists(partial)) Directory.Delete(partial, recursive: true);
            var kept = Directory.Exists(partial) ? KeepVerifiedFiles(partial, cancel) : [];
            var pending = manifest.Files.Where(entry => !kept.Contains(entry.Path)).ToList();
            var needed = pending.Sum(entry => entry.Size);
            var parent = Path.GetDirectoryName(update)!;
            Directory.CreateDirectory(parent);
            var free = space.FreeBytes(parent);
            if (free < needed + GameSetup.SpareBytes)
                throw new SetupException(SetupError.DiskFull,
                    $"Not enough disk space for Title Update 2: it needs {needed / 1e6:0} MB and some room to spare " +
                    $"on {Path.GetPathRoot(update)}. Free some space and try again.");

            Directory.CreateDirectory(partial);
            var done = manifest.TotalBytes - needed;
            foreach (var entry in pending) done = Unpack(package, entry, partial, reporter, done, cancel);

            WriteMarker(partial);
            if (Directory.Exists(update)) Directory.Delete(update, recursive: true);
            Directory.Move(partial, update);
            reporter.Report(SetupStep.Done, manifest.TotalBytes, manifest.TotalBytes, null, force: true);
        }
        catch (UnauthorizedAccessException error)
        {
            throw new SetupException(SetupError.NoPermission,
                $"Setup is not allowed to write to {Path.GetDirectoryName(update)}. Choose a game data folder in a place you can write to.", error);
        }
        catch (IOException error) when ((error.HResult & 0xFFFF) is 0x70 or 0x27)
        {
            throw new SetupException(SetupError.DiskFull,
                $"The disk ran out of space while unpacking Title Update 2 to {update}. Free some space and try again.", error);
        }
    }

    /// <summary>Repair's first half: re-hashes the update folder's files against the manifest.</summary>
    public RepairCheck CheckInstalled(string updateDataDirectory, IProgress<SetupProgress>? progress, CancellationToken cancel)
    {
        var reporter = new Reporter(progress, time);
        List<string> missing = [], damaged = [];
        long done = 0;
        foreach (var entry in manifest.Files)
        {
            cancel.ThrowIfCancellationRequested();
            reporter.Report(SetupStep.VerifyingTitleUpdateFiles, done, manifest.TotalBytes, entry.Path);
            var path = Path.Combine(updateDataDirectory, entry.Path);
            if (!File.Exists(path)) missing.Add(entry.Path);
            else if (new FileInfo(path).Length != entry.Size || HashFile(path, cancel) != entry.Sha256) damaged.Add(entry.Path);
            done += entry.Size;
        }
        reporter.Report(SetupStep.VerifyingTitleUpdateFiles, done, manifest.TotalBytes, null, force: true);
        return new RepairCheck(missing, damaged);
    }

    /// <summary>
    /// Marks an update folder whose files all verify (<see cref="CheckInstalled"/> found nothing):
    /// a folder that lost its marker needs no package to be set up again.
    /// </summary>
    public void MarkVerified(string updateDataDirectory, RepairCheck check)
    {
        if (!check.NothingToRepair) throw new InvalidOperationException("Only a folder whose files all verify is marked.");
        WriteMarker(updateDataDirectory);
    }

    /// <summary>
    /// Repair's second half: re-unpacks only the files <paramref name="check"/> lists from a
    /// checked and verified TU2 package, each verified and swapped in through a temp file, then
    /// (re)writes the marker. Returns the repaired paths.
    /// </summary>
    public IReadOnlyList<string> Repair(string packagePath, string updateDataDirectory, RepairCheck check,
        IProgress<SetupProgress>? progress, CancellationToken cancel)
    {
        var update = Path.TrimEndingDirectorySeparator(Path.GetFullPath(updateDataDirectory));
        var bad = new HashSet<string>(check.Missing.Concat(check.Damaged), StringComparer.OrdinalIgnoreCase);
        var reporter = new Reporter(progress, time);
        using var package = Open(packagePath);
        CheckHeader(package);
        VerifyBlocks(package, reporter, cancel);
        CheckPatch(package, cancel);
        Directory.CreateDirectory(update);
        var entries = manifest.Files.Where(entry => bad.Contains(entry.Path)).ToList();
        long done = 0;
        foreach (var entry in entries) done = Unpack(package, entry, update, reporter, done, cancel);
        WriteMarker(update);
        reporter.Report(SetupStep.Done, done, done, null, force: true);
        return entries.Select(entry => entry.Path).ToList();
    }

    private static StfsPackage Open(string packagePath)
    {
        if (!File.Exists(packagePath))
            throw new SetupException(SetupError.NoTitleUpdate, $"The title update was not found: {packagePath}");
        try
        {
            return StfsPackage.Open(packagePath);
        }
        catch (InvalidDataException error)
        {
            throw new SetupException(SetupError.DamagedFile,
                "The title update package is damaged: " + error.Message + " Dump it from your console again.", error);
        }
    }

    // The header: a title update for this disc. Cheap, so it comes before the block check.
    private static void CheckHeader(StfsPackage package)
    {
        var kind = PackageIntake.Classify(package.Header);
        if (kind == PackageKind.Dlc)
            throw new SetupException(SetupError.WrongPackage,
                $"That is DLC ({package.DisplayName}), not the title update. Add DLC after setup.");
        if (kind == PackageKind.OtherGame)
            throw new SetupException(SetupError.WrongPackage, "That package is for another game. Choose Title Update 2 for WWE All Stars.");
        if (kind != PackageKind.TitleUpdate)
            throw new SetupException(SetupError.WrongPackage, "That is WWE All Stars content, but not a title update.");
        if (package.MediaId != MediaId)
            throw new SetupException(SetupError.WrongPackage,
                $"That title update is for another version of the game (media ID {package.MediaId:X8}); " +
                "this port supports the USA/Europe disc, media ID 60CEF505.");
    }

    // The patch inside: TU2's default.xexp (the header says version 5 for every update). After the
    // block check a mismatch means another update, not damage; before it (the quick check when a
    // file is chosen) damage reads as damage too.
    private void CheckPatch(StfsPackage package, CancellationToken cancel)
    {
        var patch = package.Find(TitleUpdateManifest.PatchPath);
        string? hash;
        try
        {
            hash = patch is null || patch.Size != manifest.Patch.Size ? null : Hash(package, patch, cancel);
        }
        catch (InvalidDataException error)
        {
            throw new SetupException(SetupError.DamagedFile,
                "The title update package is damaged: " + error.Message + " Dump it from your console again.", error);
        }
        if (hash != manifest.Patch.Sha256)
            throw new SetupException(SetupError.WrongPackage,
                "That is a title update for WWE All Stars, but not Title Update 2 (its default.xexp is different). " +
                "Only Title Update 2 works with this version of the port.");
    }

    private static void VerifyBlocks(StfsPackage package, Reporter reporter, CancellationToken cancel)
    {
        try
        {
            package.Verify(new ProgressRelay<StfsProgress>(p =>
                reporter.Report(SetupStep.VerifyingTitleUpdate, p.Done, p.Total, null)), cancel);
        }
        catch (InvalidDataException error)
        {
            throw new SetupException(SetupError.DamagedFile,
                "The title update package is damaged: " + error.Message + " Dump it from your console again.", error);
        }
    }

    // Unpacks one manifest file through a temp file, verifying its hash; returns the bytes done.
    private long Unpack(StfsPackage package, ManifestEntry entry, string folder, Reporter reporter, long done, CancellationToken cancel)
    {
        var file = package.Find(entry.Path) ?? throw new SetupException(SetupError.WrongPackage,
            $"The title update has no {entry.Path}, so it is not Title Update 2.");
        var target = SafePath.Inside(folder, entry.Path) ?? throw new InvalidDataException("A bad path in the title update manifest.");
        var temp = target + TempSuffix;
        try
        {
            reporter.Report(SetupStep.UnpackingTitleUpdate, done, manifest.TotalBytes, entry.Path, force: true);
            using (var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256))
            using (var output = new FileStream(temp, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16))
            {
                package.CopyTo(file, block =>
                {
                    hash.AppendData(block);
                    output.Write(block);
                    done += block.Length;
                    reporter.Report(SetupStep.UnpackingTitleUpdate, done, manifest.TotalBytes, entry.Path);
                }, cancel);
                output.Flush(flushToDisk: true);
                if (Convert.ToHexStringLower(hash.GetHashAndReset()) != entry.Sha256)
                    throw new SetupException(SetupError.DamagedFile,
                        $"A Title Update 2 file failed verification: {entry.Path}. Dump the update from your console again.");
            }
            File.Move(temp, target, overwrite: true);
            return done;
        }
        finally
        {
            if (File.Exists(temp)) File.Delete(temp);
        }
    }

    private HashSet<string> KeepVerifiedFiles(string partial, CancellationToken cancel)
    {
        var kept = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in Directory.EnumerateFiles(partial, "*", SearchOption.AllDirectories).ToList())
        {
            cancel.ThrowIfCancellationRequested();
            var relative = Path.GetRelativePath(partial, path).Replace(Path.DirectorySeparatorChar, '/');
            var entry = manifest.Find(relative);
            if (entry is not null && new FileInfo(path).Length == entry.Size && HashFile(path, cancel) == entry.Sha256)
                kept.Add(entry.Path);
            else File.Delete(path);
        }
        return kept;
    }

    private static string Hash(StfsPackage package, StfsFile file, CancellationToken cancel)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        package.CopyTo(file, hash.AppendData, cancel);
        return Convert.ToHexStringLower(hash.GetHashAndReset());
    }

    private static string HashFile(string path, CancellationToken cancel) => FileHashes.Sha256(path, cancel);

    private void WriteMarker(string folder) =>
        File.WriteAllText(Path.Combine(folder, MarkerFileName), MarkerKey + manifest.Version + "\n");

    private static string? ReadMarker(string folder)
    {
        try
        {
            var text = File.ReadAllText(Path.Combine(folder, MarkerFileName)).Trim();
            return text.StartsWith(MarkerKey, StringComparison.Ordinal) ? text[MarkerKey.Length..] : null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    /// <summary>Progress reports at most every 100 ms (the first of each file always).</summary>
    private sealed class Reporter(IProgress<SetupProgress>? progress, TimeProvider time)
    {
        private long _last = long.MinValue;

        public void Report(SetupStep step, long done, long total, string? file, bool force = false)
        {
            if (progress is null) return;
            var now = time.GetTimestamp();
            if (!force && _last != long.MinValue && time.GetElapsedTime(_last, now) < ReportInterval) return;
            _last = now;
            progress.Report(new SetupProgress(step, done, total, file, null));
        }
    }
}
