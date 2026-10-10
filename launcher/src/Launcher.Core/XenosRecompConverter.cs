using System.Diagnostics;
using System.Globalization;
using System.Text.RegularExpressions;

namespace WweAllStars.Launcher.Core;

/// <summary>The shader converter this launcher ships with; <c>Id</c> is generated at build time.</summary>
public static partial class ShaderConverter;

/// <summary>
/// Runs XenosRecomp, shipped beside the launcher: <c>XenosRecomp.exe</c> with its DLLs and
/// <c>shader_common.h</c> in one folder (scripts/convert-shaders.ps1 runs it the same way).
/// </summary>
public sealed partial class XenosRecompConverter(string executable) : IShaderConverter
{
    public const string ExecutableName = "XenosRecomp.exe";
    public const string IncludeName = "shader_common.h";

    public string Executable { get; } = Path.GetFullPath(executable);

    public void Convert(string inputDirectory, string outputDirectory, IProgress<double>? progress, CancellationToken cancel)
    {
        var include = Path.Combine(Path.GetDirectoryName(Executable)!, IncludeName);
        if (!File.Exists(Executable) || !File.Exists(include))
            throw new ShaderConverterException(
                $"the shader converter is missing ({(File.Exists(Executable) ? include : Executable)}). Unzip the launcher's release again");

        var start = new ProcessStartInfo(Executable)
        {
            WorkingDirectory = Path.GetDirectoryName(Executable)!,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (var argument in new[]
                 {
                     inputDirectory, Path.Combine(outputDirectory, "shader_cache.cpp"), include, Path.Combine(outputDirectory, "hlsl"),
                 })
            start.ArgumentList.Add(argument);

        using var process = new Process { StartInfo = start };
        var errors = new List<string>();
        process.OutputDataReceived += (_, line) =>
        {
            if (line.Data is not null && Percent().Match(line.Data) is { Success: true } match)
                progress?.Report(double.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture) / 100);
        };
        process.ErrorDataReceived += (_, line) =>
        {
            if (line.Data is null) return;
            lock (errors) errors.Add(line.Data);
        };
        try
        {
            process.Start();
        }
        catch (Exception error) when (error is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            throw new ShaderConverterException($"the shader converter could not start: {error.Message}", error);
        }
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        using (cancel.Register(() =>
               {
                   try { process.Kill(entireProcessTree: true); }
                   // Already gone, or it cannot be ended: then the wait lasts until it finishes.
                   catch (Exception error) when (error is InvalidOperationException or System.ComponentModel.Win32Exception or NotSupportedException) { }
               }))
        {
            process.WaitForExit();
        }
        cancel.ThrowIfCancellationRequested();
        if (process.ExitCode == 0) return;

        string detail;
        lock (errors)
        {
            var failures = errors.Select(line => Failures().Match(line)).FirstOrDefault(match => match.Success);
            detail = failures is not null
                ? $"XenosRecomp could not convert {failures.Groups[1].Value} shaders"
                : $"XenosRecomp stopped with exit code {process.ExitCode}";
        }
        throw new ShaderConverterException(detail);
    }

    // "Recompiling shaders... 42.5%" on stdout; "Recompile failures (3):" on stderr.
    [GeneratedRegex(@"Recompiling shaders\.\.\. ([0-9.]+)%")]
    private static partial Regex Percent();

    [GeneratedRegex(@"^Recompile failures \((\d+)\):")]
    private static partial Regex Failures();
}
