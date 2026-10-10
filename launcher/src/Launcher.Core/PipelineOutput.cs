using System.Globalization;

namespace WweAllStars.Launcher.Core;

/// <summary>The prepare's stages, in the order it runs them.</summary>
public enum PipelineStage
{
    /// <summary>The shader pack's modules.</summary>
    Modules,
    /// <summary>On GPUs with pipeline libraries: a library per shader and spec-constant value (ticket 09).</summary>
    Libraries,
    /// <summary>The planned pipelines.</summary>
    Pipelines,
}

/// <summary>
/// One progress line of the prepare: a stage and how much of it is done. <see cref="AfterLibraries"/>:
/// the run had a libraries stage before these pipelines.
/// </summary>
public sealed record PipelineProgress(PipelineStage Stage, int Done, int Total, bool AfterLibraries = false)
{
    // On the owner's machine: modules about 0.1 s; pipelines about 0.5 s without libraries;
    // with them 8.8 s of libraries on 16 threads, then 0.3 s of pipelines (ticket 09).
    private const double ModulesEnd = 0.1;
    private const double LibrariesEnd = 0.9;

    /// <summary>The whole prepare's fraction done, 0 to 1.</summary>
    public double Fraction
    {
        get
        {
            var stage = Total > 0 ? Math.Clamp((double)Done / Total, 0, 1) : 1;
            var (start, end) = Stage switch
            {
                PipelineStage.Modules => (0.0, ModulesEnd),
                PipelineStage.Libraries => (ModulesEnd, LibrariesEnd),
                _ => (AfterLibraries ? LibrariesEnd : ModulesEnd, 1.0),
            };
            return start + (end - start) * stage;
        }
    }
}

public enum PipelineOutcome
{
    /// <summary>Every planned pipeline was built.</summary>
    Ready,
    /// <summary>The driver refused some; the store and the driver cache were still written.</summary>
    Partial,
    /// <summary>The mode could not run (no shader pack, no Vulkan, ...); <see cref="PipelineResult.Reason"/> says why.</summary>
    Failed,
    /// <summary>The exe failed another way: an exit code or output the format does not allow.</summary>
    Crashed,
}

/// <summary>How a prepare ended, with what the launcher tells the player.</summary>
public sealed record PipelineResult(PipelineOutcome Outcome, int ExitCode, int Pipelines, int Failed, string? Reason)
{
    /// <summary>The store and driver cache were written: the next start needs no rebuild.</summary>
    public bool Usable => Outcome is PipelineOutcome.Ready or PipelineOutcome.Partial;

    /// <summary>Why the exe did not start, when it did not.</summary>
    public string? StartError { get; init; }

    private const string MayStutter = "The game may stutter the first time it meets each effect.";

    public string Message => Outcome switch
    {
        PipelineOutcome.Ready => "Pipelines ready.",
        PipelineOutcome.Partial =>
            $"Pipelines ready, but the graphics driver refused {Failed} of them; the game builds those while it runs.",
        // Every reason the mode gives is one that also sends a normal start to Emulated.
        PipelineOutcome.Failed => $"Pipelines could not be prepared: {ReasonText(Reason)}, so the game runs on Emulated.",
        _ when StartError is not null => $"Pipelines could not be prepared: {StartError.TrimEnd('.')}.",
        _ => $"Pipelines could not be prepared: the game exited with code 0x{ExitCode:X8}. {MayStutter} " +
             "Its log is logs/prepare-pipelines.log.",
    };

    private static string ReasonText(string? reason) => reason switch
    {
        "missing_shader_pack" => "the Native shaders are not prepared",
        "stale_shader_pack" or "wrong_shader_pack_version" => "the Native shaders are out of date",
        "truncated_shader_pack" or "bad_shader_pack_magic" or "corrupt_shader_pack" => "the Native shaders are damaged",
        "vulkan_setup" => "Vulkan could not start on this graphics card",
        "native_off" => "the Native renderer is off",
        _ => $"the game reported {reason ?? "no reason"}",
    };
}

/// <summary>
/// Reads the game's prepare-pipelines output (ticket 06; src/native/prepare_output.h): a
/// <c>wwe-prepare-pipelines 1</c> header, <c>progress</c> lines, then one <c>result</c> line,
/// and an exit code (0 ok, 3 partial, 2 failed, anything else a crash). Lines it does not know
/// are ignored. Feed it line by line with <see cref="Read"/>, then call <see cref="Finish"/>.
/// </summary>
public sealed class PipelineOutput
{
    public const string Header = "wwe-prepare-pipelines";
    public const int FormatVersion = 1;

    private bool _header;
    private bool _libraries;
    private (string Status, int Pipelines, int Failed, string? Reason)? _result;

    /// <summary>Reads one line; returns the progress it reports, or null.</summary>
    public PipelineProgress? Read(string line)
    {
        var words = line.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (words.Length == 0) return null;
        if (words[0] == Header)
        {
            _header = words.Length == 2 && words[1] == FormatVersion.ToString(CultureInfo.InvariantCulture);
            return null;
        }
        var fields = Fields(words);
        if (words[0] == "result" && fields.TryGetValue("status", out var status))
        {
            _result = (status, Number(fields, "pipelines") ?? 0, Number(fields, "failed") ?? 0, fields.GetValueOrDefault("reason"));
            return null;
        }
        if (!_header || words[0] != "progress") return null;
        PipelineStage? stage = fields.GetValueOrDefault("stage") switch
        {
            "modules" => PipelineStage.Modules,
            "libraries" => PipelineStage.Libraries,
            "pipelines" => PipelineStage.Pipelines,
            _ => null,
        };
        if (stage is not { } known || Number(fields, "done") is not { } done || Number(fields, "total") is not { } total) return null;
        _libraries |= known == PipelineStage.Libraries;
        return new PipelineProgress(known, done, total, known == PipelineStage.Pipelines && _libraries);
    }

    /// <summary>The run's result, from the result line and the exit code together.</summary>
    public PipelineResult Finish(int exitCode)
    {
        PipelineOutcome? outcome = (_header ? _result?.Status : null, exitCode) switch
        {
            ("ok", 0) => PipelineOutcome.Ready,
            ("partial", 3) => PipelineOutcome.Partial,
            ("failed", 2) => PipelineOutcome.Failed,
            _ => null,
        };
        return outcome is { } known
            ? new PipelineResult(known, exitCode, _result!.Value.Pipelines, _result.Value.Failed, _result.Value.Reason)
            : new PipelineResult(PipelineOutcome.Crashed, exitCode, 0, 0, null);
    }

    private static Dictionary<string, string> Fields(string[] words)
    {
        var fields = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var word in words.Skip(1))
        {
            var equals = word.IndexOf('=');
            if (equals > 0) fields[word[..equals]] = word[(equals + 1)..];
        }
        return fields;
    }

    private static int? Number(Dictionary<string, string> fields, string key) =>
        fields.TryGetValue(key, out var text) && int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out var value)
            ? value
            : null;
}
