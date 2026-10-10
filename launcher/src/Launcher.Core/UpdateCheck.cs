using System.Net.Http.Headers;
using System.Text.Json;

namespace WweAllStars.Launcher.Core;

/// <summary>A release newer than the running launcher: its tag and its page on GitHub.</summary>
public sealed record AvailableUpdate(string Tag, Uri ReleasePage);

/// <summary>
/// Asks GitHub Releases whether a newer release exists (spec, "Update check"). It only reports
/// a link; it never downloads anything. Every failure (no network, timeout, rate limit, an
/// unexpected answer) is silent: the result is simply "no update".
/// </summary>
public sealed class UpdateChecker(HttpClient http, string repository, Version current, TimeSpan timeout)
{
    /// <summary>
    /// The GitHub repository ("owner/name") whose releases the launcher checks: a build constant.
    /// Empty turns the check off (as while the project was private, ADR-0014).
    /// </summary>
    public const string ProjectRepository = "shaneomac1337/WWE-AllStars-Recomp";

    /// <summary>How long the check waits for GitHub before giving up silently.</summary>
    public static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(5);

    /// <summary>The checker the launcher uses: this project's releases, the default timeout.</summary>
    public static UpdateChecker ForProject(HttpClient http, Version current) =>
        new(http, ProjectRepository, current, DefaultTimeout);

    /// <summary>False while no repository is set: the check sends nothing and the setting is hidden.</summary>
    public bool IsAvailable => repository.Length > 0;

    private readonly Lock _lock = new();
    private Task<AvailableUpdate?>? _check;

    /// <summary>
    /// The newest release if it is newer than <c>current</c>, else null. Disabled sends nothing;
    /// enabled sends one request for the checker's lifetime and later calls share its answer.
    /// </summary>
    public Task<AvailableUpdate?> CheckAsync(bool enabled, CancellationToken cancellationToken = default)
    {
        if (!enabled || !IsAvailable) return Task.FromResult<AvailableUpdate?>(null);
        lock (_lock) return _check ??= FetchAsync(cancellationToken);
    }

    private async Task<AvailableUpdate?> FetchAsync(CancellationToken cancellationToken)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout);
        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Get,
                $"https://api.github.com/repos/{repository}/releases/latest");
            // GitHub refuses API requests without a User-Agent.
            request.Headers.UserAgent.Add(
                new ProductInfoHeaderValue("WWEAllStarsLauncher", ReleaseVersion.Normalize(current).ToString(3)));
            request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
            using var response = await http.SendAsync(request, deadline.Token);
            if (!response.IsSuccessStatusCode) return null;
            using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync(deadline.Token));
            return Read(json.RootElement);
        }
        catch (Exception)
        {
            // Offline, firewalled, rate limited or a changed API: the player just plays.
            return null;
        }
    }

    private AvailableUpdate? Read(JsonElement release)
    {
        if (release.ValueKind != JsonValueKind.Object
            || !release.TryGetProperty("tag_name", out var tagValue) || tagValue.ValueKind != JsonValueKind.String
            || !release.TryGetProperty("html_url", out var pageValue) || pageValue.ValueKind != JsonValueKind.String)
            return null;
        var tag = tagValue.GetString()!;
        // The link opens in the player's browser, so it must be a GitHub page.
        if (!Uri.TryCreate(pageValue.GetString(), UriKind.Absolute, out var page)
            || page.Scheme != Uri.UriSchemeHttps || page.Host != "github.com")
            return null;
        if (ReleaseVersion.Parse(tag) is not { } latest || latest <= ReleaseVersion.Normalize(current)) return null;
        return new AvailableUpdate(tag, page);
    }
}

/// <summary>Release tags such as <c>v0.3.0</c>, <c>1.0</c> or <c>v0.3.0-rc1</c> as comparable versions.</summary>
public static class ReleaseVersion
{
    /// <summary>
    /// The tag's version with missing parts as zero, or null if the tag is not a version. A
    /// suffix after <c>-</c> or <c>+</c> is dropped, so a pre-release counts as its release.
    /// </summary>
    public static Version? Parse(string tag)
    {
        var text = tag.StartsWith('v') || tag.StartsWith('V') ? tag[1..] : tag;
        var end = text.IndexOfAny(['-', '+']);
        if (end >= 0) text = text[..end];
        return Version.TryParse(text, out var version) ? Normalize(version) : null;
    }

    /// <summary>
    /// The informational version as About shows it: the version, then the build's short commit
    /// (the SDK appends <c>+&lt;commit&gt;</c>), so players can tell whether they run the same build.
    /// </summary>
    public static string Describe(string informationalVersion)
    {
        var plus = informationalVersion.IndexOf('+');
        if (plus < 0) return informationalVersion;
        var commit = informationalVersion[(plus + 1)..];
        var version = informationalVersion[..plus];
        return commit.Length == 0 ? version : $"{version} (commit {commit[..Math.Min(7, commit.Length)]})";
    }

    /// <summary>The version with unset parts (-1) as zero, so 0.2 equals 0.2.0.0.</summary>
    public static Version Normalize(Version version) =>
        new(version.Major, version.Minor, Math.Max(version.Build, 0), Math.Max(version.Revision, 0));
}
