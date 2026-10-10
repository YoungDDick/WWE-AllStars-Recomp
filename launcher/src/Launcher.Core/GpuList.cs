using System.Globalization;
using System.Text.RegularExpressions;

namespace WweAllStars.Launcher.Core;

/// <summary>One GPU as Vulkan reports it: the index the game's <c>vulkan_device</c> setting takes, and its name.</summary>
/// <param name="SuggestedPreset">
/// The Graphics preset the game suggests for it (a preset value of the <c>graphics_preset</c>
/// row, <c>src/settings/gpu_suggestion.h</c>); empty when the line carries none.
/// </param>
/// <param name="Automatic">Whether the game marks it as the GPU Automatic opens.</param>
public sealed record Gpu(int Index, string Name, string SuggestedPreset = "", bool Automatic = false);

/// <summary>
/// The GPUs the game can run on. The launcher asks the game exe (<c>wweallstars.exe --list-gpus</c>,
/// <c>src/settings/gpu_list.h</c>) instead of linking Vulkan itself, so the indices are the ones
/// the game's Vulkan setup uses.
/// </summary>
public static partial class GpuList
{
    /// <summary>The game's switch that prints the GPUs and exits before any window opens.</summary>
    public const string Switch = "--list-gpus";

    /// <summary>
    /// The GPUs in <c>--list-gpus</c> output: one <c>gpu &lt;index&gt;: &lt;name&gt;</c> line each,
    /// optionally followed by a tab and <c>suggested=&lt;preset&gt;</c>, then a tab and
    /// <c>automatic</c> on the GPU Automatic opens; other lines are ignored.
    /// </summary>
    public static IReadOnlyList<Gpu> Parse(string output)
    {
        var gpus = new List<Gpu>();
        foreach (var line in output.Split('\n'))
        {
            var match = GpuLine().Match(line.TrimEnd('\r'));
            if (match.Success &&
                int.TryParse(match.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var index))
                gpus.Add(new Gpu(index, match.Groups[2].Value, match.Groups[3].Value, match.Groups[4].Success));
        }
        return gpus;
    }

    /// <summary>
    /// Asks the game exe for its GPUs through <paramref name="run"/> (which returns the standard
    /// output; <see cref="HeadlessGame.RunAsync"/> in the launcher). A game that is missing,
    /// fails or hangs gives no GPUs, so the Settings screen still offers Automatic.
    /// </summary>
    public static async Task<IReadOnlyList<Gpu>> QueryAsync(
        PortableLayout layout, Func<ProcessStart, CancellationToken, Task<string>> run, CancellationToken cancel)
    {
        try
        {
            return Parse(await run(new ProcessStart(layout.GameExecutable, layout.GameDirectory, [Switch]), cancel));
        }
        catch (Exception error) when (error is LauncherException or TimeoutException or IOException
                                          or System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            return [];
        }
    }

    /// <summary>The <c>vulkan_device</c> value that lets the game pick a GPU itself.</summary>
    public const string Automatic = "-1";

    /// <summary>
    /// The suggested Graphics preset for the GPU <paramref name="vulkanDevice"/> chooses: the GPU
    /// it names, or for Automatic the GPU the game marks as its pick (the first listed from a
    /// game that marks none). Empty
    /// when the GPU is not listed or carries no suggestion.
    /// </summary>
    public static string SuggestionFor(IReadOnlyList<Gpu> gpus, string vulkanDevice)
    {
        if (vulkanDevice == Automatic)
            return (gpus.FirstOrDefault(g => g.Automatic) ?? gpus.FirstOrDefault())?.SuggestedPreset ?? "";
        return int.TryParse(vulkanDevice, NumberStyles.None, CultureInfo.InvariantCulture, out var index)
            ? gpus.FirstOrDefault(g => g.Index == index)?.SuggestedPreset ?? ""
            : "";
    }

    [GeneratedRegex(@"^gpu (\d+): ([^\t]+)(?:\tsuggested=(\w+))?(\tautomatic)?$")]
    private static partial Regex GpuLine();
}
