using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

/// <summary>The rules a text setting follows; the game applies the same (<c>src/settings/text_rules.h</c>).</summary>
public sealed class TextRulesTests
{
    [Theory]
    [InlineData("A")]
    [InlineData("Owner")]
    [InlineData("Big Show 2")]
    [InlineData("abcdefghijklmno")]
    [InlineData("x1 y2")]
    public void A_player_name_the_gamertag_rule_accepts(string name) =>
        Assert.Null(TextRules.Error(TextRules.Gamertag, name));

    [Theory]
    [InlineData("", "Type a player name.")]
    [InlineData("abcdefghijklmnop", "A player name has at most 15 characters.")]
    [InlineData("1Up", "A player name starts with a letter.")]
    [InlineData(" Owner", "A player name starts with a letter.")]
    [InlineData("Own_er", "A player name has only letters, digits and spaces.")]
    [InlineData("Own-er", "A player name has only letters, digits and spaces.")]
    [InlineData("Own\u00e9r", "A player name has only letters, digits and spaces.")]
    [InlineData("Own\ter", "A player name has only letters, digits and spaces.")]
    [InlineData("Owner ", "A player name does not end with a space.")]
    public void A_refused_player_name_says_why_as_the_game_does(string name, string why) =>
        Assert.Equal(why, TextRules.Error(TextRules.Gamertag, name));

    [Theory]
    [InlineData("allstars.komplexaci.cz")]
    [InlineData("127.0.0.1")]
    [InlineData("localhost")]
    [InlineData("192.168.1.5:36000")]
    [InlineData("my-server.lan:1")]
    [InlineData("https://allstars.komplexaci.cz")]
    [InlineData("http://10.0.0.2:36000")]
    [InlineData("https://example.com/api/")]
    [InlineData("HTTPS://Example.com:443")]
    public void A_game_server_the_rule_accepts(string server) =>
        Assert.Null(TextRules.Error(TextRules.ServerAddress, server));

    [Theory]
    [InlineData("", "Type a game server address.")]
    [InlineData("my server", "A server address has no spaces.")]
    [InlineData(" allstars.komplexaci.cz", "A server address has no spaces.")]
    [InlineData("allstars.komplexaci.cz\t", "A server address has no spaces.")]
    [InlineData("allstars\u00a0komplexaci.cz", BadServer)]  // only ASCII spaces, as the game's isspace
    [InlineData("ftp://example.com", "A server URL starts with http:// or https://.")]
    [InlineData("https://", BadServer)]
    [InlineData("https:///api", BadServer)]
    [InlineData("host:", BadServer)]
    [InlineData("host:0", BadServer)]
    [InlineData("host:65536", BadServer)]
    [InlineData("host:abc", BadServer)]
    [InlineData("ho_st", BadServer)]
    [InlineData("http://ho st", "A server address has no spaces.")]
    [InlineData("host/path", BadServer)]
    [InlineData("user@host", BadServer)]
    public void A_refused_game_server_says_why_as_the_game_does(string server, string why) =>
        Assert.Equal(why, TextRules.Error(TextRules.ServerAddress, server));

    private const string BadServer = "A server address is a host name, host:port (port 1 to 65535), or an http(s) URL.";

    [Fact]
    public void A_rule_this_launcher_does_not_know_refuses_every_value() =>
        Assert.NotNull(TextRules.Error("future_rule", "anything"));
}
