namespace WweAllStars.Launcher.Core;

/// <summary>
/// The rules a text setting's value follows, by the name the schema export gives them. The game
/// applies the same rules with the same messages (<c>src/settings/text_rules.h</c>), so a value
/// the launcher saves is one the game accepts.
/// </summary>
public static class TextRules
{
    /// <summary>Xbox LIVE's gamertags: 1 to 15 letters, digits and spaces, first a letter (the player name).</summary>
    public const string Gamertag = "gamertag";

    /// <summary>A host, host:port or an http(s) URL (the game server), as the game turns it into the API's base URL.</summary>
    public const string ServerAddress = "server_address";

    /// <summary>Why <paramref name="value"/> breaks the rule, or null when it follows it.</summary>
    public static string? Error(string rule, string value) => rule switch
    {
        Gamertag => GamertagError(value),
        ServerAddress => ServerAddressError(value),
        _ => "This launcher does not know this setting's rule; a newer launcher does.",
    };

    private static string? ServerAddressError(string server)
    {
        if (server.Length == 0) return "Type a game server address.";
        // ASCII whitespace only, as the game's std::isspace; anything else fails the host rule.
        if (server.Any(c => c is ' ' or '\t' or '\n' or '\v' or '\f' or '\r')) return "A server address has no spaces.";
        var authority = server;
        var scheme = server.IndexOf("://", StringComparison.Ordinal);
        if (scheme >= 0)
        {
            if (!server.StartsWith("http://", StringComparison.OrdinalIgnoreCase) &&
                !server.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
                return "A server URL starts with http:// or https://.";
            authority = server[(scheme + 3)..];
            var slash = authority.IndexOf('/');
            if (slash >= 0) authority = authority[..slash];
        }
        return HostAndPort(authority)
            ? null
            : "A server address is a host name, host:port (port 1 to 65535), or an http(s) URL.";
    }

    // host or host:port, the port 1 to 65535; a host is letters, digits, dots and hyphens.
    private static bool HostAndPort(string text)
    {
        var colon = text.IndexOf(':');
        var host = colon < 0 ? text : text[..colon];
        if (host.Length == 0 || !host.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '-')) return false;
        if (colon < 0) return true;
        var port = text[(colon + 1)..];
        return port.Length is > 0 and <= 5 && port.All(char.IsAsciiDigit) && int.Parse(port) is >= 1 and <= 65535;
    }

    private static string? GamertagError(string name)
    {
        if (name.Length == 0) return "Type a player name.";
        if (name.Length > 15) return "A player name has at most 15 characters.";
        if (!char.IsAsciiLetter(name[0])) return "A player name starts with a letter.";
        if (!name.All(c => char.IsAsciiLetterOrDigit(c) || c == ' '))
            return "A player name has only letters, digits and spaces.";
        if (name[^1] == ' ') return "A player name does not end with a space.";
        return null;
    }
}
