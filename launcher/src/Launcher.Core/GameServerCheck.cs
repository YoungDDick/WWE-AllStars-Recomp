using System.Net;
using System.Text.Json;

namespace WweAllStars.Launcher.Core;

/// <summary>What the home screen says about the game server (launcher-online spec, "Server status check").</summary>
public enum GameServerStatus
{
    /// <summary>Online play is off: nothing was asked and the line is hidden.</summary>
    Off,

    /// <summary>The check is running.</summary>
    Checking,

    /// <summary>The server answered <c>whoami</c>.</summary>
    Ready,

    /// <summary>No answer in time, a refused connection, or a proxy without its game server.</summary>
    NotAnswering,

    /// <summary>Something answered, but not as the game server does (or the address breaks the rule).</summary>
    NotAGameServer,

    /// <summary>The server takes another game version (<c>GET /version</c>): this port needs an update to play online.</summary>
    UpdateRequired,
}

/// <summary>
/// Asks the configured game server's <c>whoami</c>, the same first request the game's sign-in makes,
/// with a short timeout. It sends no identity (whoami needs none) and nothing while online play is
/// off. Play works in every status: a server outage never stops offline play.
/// </summary>
public sealed class GameServerCheck(HttpClient http, TimeSpan timeout)
{
    /// <summary>The game version this port is built on, as the game sends it at sign-in (Title Update 2).</summary>
    public const string GameVersion = "tu2";

    /// <summary>How long the check waits before calling the server not answering.</summary>
    public static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(3);

    /// <summary>The status for the saved settings (<see cref="SettingsFile.Read"/>).</summary>
    public async Task<GameServerStatus> CheckAsync(IReadOnlyDictionary<string, string> values,
                                                   CancellationToken cancellationToken = default)
    {
        if (values.GetValueOrDefault(SettingsFile.OnlineKey) != "true") return GameServerStatus.Off;
        var server = values.GetValueOrDefault(SettingsFile.GameServerKey, "");
        if (TextRules.Error(TextRules.ServerAddress, server) is not null ||
            !Uri.TryCreate(ApiBaseUrl(server) + "whoami", UriKind.Absolute, out var whoami))
            return GameServerStatus.NotAGameServer;
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout);
        try
        {
            using var response = await http.GetAsync(whoami, deadline.Token);
            if (response.StatusCode is HttpStatusCode.BadGateway or HttpStatusCode.ServiceUnavailable
                or HttpStatusCode.GatewayTimeout)
                return GameServerStatus.NotAnswering;
            if (response.StatusCode != HttpStatusCode.OK) return GameServerStatus.NotAGameServer;
            using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync(deadline.Token));
            // As the SDK's ParseWhoami: an object with the caller's address as a string.
            if (json.RootElement.ValueKind != JsonValueKind.Object ||
                !json.RootElement.TryGetProperty("address", out var address) ||
                address.ValueKind != JsonValueKind.String)
                return GameServerStatus.NotAGameServer;
            return await TakesThisVersion(new Uri(ApiBaseUrl(server) + "version"), deadline.Token, cancellationToken)
                ? GameServerStatus.Ready
                : GameServerStatus.UpdateRequired;
        }
        catch (JsonException)
        {
            return GameServerStatus.NotAGameServer;
        }
        catch (Exception error) when (error is HttpRequestException or OperationCanceledException)
        {
            if (cancellationToken.IsCancellationRequested) throw;
            return GameServerStatus.NotAnswering;
        }
    }

    // The server's GET /version: the game version it takes. A server from before the check (no
    // /version), one that takes any version, and one whose /version doesn't answer (whoami just
    // did) are fine; only a stated other version asks for an update.
    private async Task<bool> TakesThisVersion(Uri version, CancellationToken cancel, CancellationToken caller)
    {
        try
        {
            using var response = await http.GetAsync(version, cancel);
            if (response.StatusCode != HttpStatusCode.OK) return true;
            using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync(cancel));
            return json.RootElement.ValueKind != JsonValueKind.Object ||
                   !json.RootElement.TryGetProperty("gameVersion", out var required) ||
                   required.ValueKind != JsonValueKind.String || required.GetString() is null or "" ||
                   required.GetString() == GameVersion;
        }
        catch (JsonException)
        {
            return true;
        }
        catch (Exception error) when (error is HttpRequestException or OperationCanceledException)
        {
            if (caller.IsCancellationRequested) throw;
            return true;
        }
    }

    /// <summary>
    /// The API's base URL for a Game server value, as the game builds it (SDK <c>ApiBaseUrl</c>,
    /// <c>src/kernel/xam/live/xlive_json.cpp</c>): a bare host gets <c>http://</c> and port 36000,
    /// a URL is kept; one trailing slash.
    /// </summary>
    public static string ApiBaseUrl(string server)
    {
        var url = server;
        if (!url.Contains("://", StringComparison.Ordinal))
        {
            url = "http://" + url;
            var host = url.IndexOf("://", StringComparison.Ordinal) + 3;
            if (url.IndexOf(':', host) < 0)
            {
                var slash = url.IndexOf('/', host);
                url = url.Insert(slash < 0 ? url.Length : slash, ":36000");
            }
        }
        return url.TrimEnd('/') + "/";
    }

    /// <summary>The server's host (and port, unless the scheme's default), for the status line.</summary>
    public static string HostOf(string server) =>
        Uri.TryCreate(ApiBaseUrl(server), UriKind.Absolute, out var url) ? url.Authority : server;

    /// <summary>The home screen's line for a status; empty when online play is off.</summary>
    public static string StatusLine(GameServerStatus status, string server) => status switch
    {
        GameServerStatus.Checking => $"Online: checking {HostOf(server)}...",
        GameServerStatus.Ready => $"Online: ready ({HostOf(server)})",
        GameServerStatus.NotAnswering =>
            $"Online: the game server isn't answering; you can still play offline ({HostOf(server)})",
        GameServerStatus.NotAGameServer => $"Online: {HostOf(server)} isn't a game server; check Settings > ONLINE",
        GameServerStatus.UpdateRequired =>
            $"Online: {HostOf(server)} needs a newer version of the port; update to play online (offline play works)",
        _ => "",
    };
}
