using System.Net;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class GameServerCheckTests
{
    private readonly FakeServer _server = new();

    private GameServerCheck Check(TimeSpan? timeout = null) =>
        new(new HttpClient(_server), timeout ?? TimeSpan.FromSeconds(3));

    private static Dictionary<string, string> Online(string server = "https://allstars.komplexaci.cz", bool on = true) => new()
    {
        [SettingsFile.OnlineKey] = on ? "true" : "false",
        [SettingsFile.GameServerKey] = server,
    };

    private static HttpResponseMessage Answer(string body, HttpStatusCode status = HttpStatusCode.OK) =>
        new(status) { Content = new StringContent(body) };

    [Fact]
    public async Task A_whoami_answer_means_the_server_is_ready()
    {
        _server.ReplyWith(Answer("""{"address":"88.100.182.229"}"""));

        var status = await Check().CheckAsync(Online(), TestContext.Current.CancellationToken);

        Assert.Equal(GameServerStatus.Ready, status);
        var request = _server.Requests[0];
        Assert.Equal(HttpMethod.Get, request.Method);
        Assert.Equal(new Uri("https://allstars.komplexaci.cz/whoami"), request.RequestUri);
    }

    // whoami answers; /version answers with `version` (or the status given).
    private void ServerWithVersion(string? version, HttpStatusCode versionStatus = HttpStatusCode.OK) =>
        _server.Respond = _ => Task.FromResult(_server.Requests[^1].RequestUri!.AbsolutePath == "/version"
            ? Answer(version ?? "", versionStatus)
            : Answer("""{"address":"88.100.182.229"}"""));

    [Fact]
    public async Task A_server_for_this_game_version_is_ready()
    {
        ServerWithVersion("""{"gameVersion":"tu2","message":"Update the port."}""");
        Assert.Equal(GameServerStatus.Ready, await Check().CheckAsync(Online(), TestContext.Current.CancellationToken));
        Assert.Equal(new Uri("https://allstars.komplexaci.cz/version"), _server.Requests[^1].RequestUri);
    }

    [Fact]
    public async Task A_server_for_another_game_version_asks_for_an_update()
    {
        ServerWithVersion("""{"gameVersion":"tu3","message":"Update the port."}""");
        var status = await Check().CheckAsync(Online(), TestContext.Current.CancellationToken);
        Assert.Equal(GameServerStatus.UpdateRequired, status);
        Assert.Contains("update", GameServerCheck.StatusLine(status, "https://allstars.komplexaci.cz"));
    }

    [Fact]
    public async Task A_version_question_that_times_out_still_means_ready()
    {
        // whoami answered; only /version hangs past the deadline.
        _server.Respond = async token =>
        {
            if (_server.Requests[^1].RequestUri!.AbsolutePath != "/version") return Answer("""{"address":"88.100.182.229"}""");
            await Task.Delay(Timeout.Infinite, token);
            throw new InvalidOperationException("unreachable");
        };
        var status = await Check(TimeSpan.FromMilliseconds(200)).CheckAsync(Online(), TestContext.Current.CancellationToken)
            .WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken);
        Assert.Equal(GameServerStatus.Ready, status);
    }

    [Theory]
    [InlineData("""{"gameVersion":""}""", HttpStatusCode.OK)]       // takes every version
    [InlineData("404 page not found", HttpStatusCode.NotFound)]     // a server from before the check
    public async Task A_server_that_takes_any_version_is_ready(string body, HttpStatusCode status)
    {
        ServerWithVersion(body, status);
        Assert.Equal(GameServerStatus.Ready, await Check().CheckAsync(Online(), TestContext.Current.CancellationToken));
    }

    [Fact]
    public async Task Online_play_off_sends_nothing()
    {
        _server.ReplyWith(Answer("""{"address":"88.100.182.229"}"""));

        var status = await Check().CheckAsync(Online(on: false), TestContext.Current.CancellationToken);

        Assert.Equal(GameServerStatus.Off, status);
        Assert.Empty(_server.Requests);
    }

    [Fact]
    public async Task A_server_that_never_answers_is_not_answering_after_the_timeout()
    {
        _server.Respond = async token =>
        {
            await Task.Delay(Timeout.Infinite, token);
            throw new InvalidOperationException("unreachable");
        };

        var status = await Check(TimeSpan.FromMilliseconds(50))
            .CheckAsync(Online(), TestContext.Current.CancellationToken)
            .WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken);

        Assert.Equal(GameServerStatus.NotAnswering, status);
    }

    [Fact]
    public async Task A_refused_connection_is_not_answering()
    {
        _server.Respond = _ => throw new HttpRequestException("No connection could be made because the target machine actively refused it.");

        Assert.Equal(GameServerStatus.NotAnswering, await Check().CheckAsync(Online(), TestContext.Current.CancellationToken));
    }

    [Theory]
    [InlineData(HttpStatusCode.BadGateway)]          // the proxy is up, the game server behind it is not
    [InlineData(HttpStatusCode.ServiceUnavailable)]
    [InlineData(HttpStatusCode.GatewayTimeout)]
    public async Task A_proxy_without_its_game_server_is_not_answering(HttpStatusCode status)
    {
        _server.ReplyWith(Answer("<html>Bad Gateway</html>", status));

        Assert.Equal(GameServerStatus.NotAnswering, await Check().CheckAsync(Online(), TestContext.Current.CancellationToken));
    }

    [Theory]
    [InlineData("<html><body>Welcome to nginx!</body></html>", HttpStatusCode.OK)]
    [InlineData("""{"status":"ok"}""", HttpStatusCode.OK)]
    [InlineData("""{"address":42}""", HttpStatusCode.OK)]
    [InlineData("[]", HttpStatusCode.OK)]
    [InlineData("Not Found", HttpStatusCode.NotFound)]
    public async Task Any_other_answer_is_not_a_game_server(string body, HttpStatusCode status)
    {
        _server.ReplyWith(Answer(body, status));

        Assert.Equal(GameServerStatus.NotAGameServer, await Check().CheckAsync(Online(), TestContext.Current.CancellationToken));
    }

    [Fact]
    public async Task An_address_the_rule_refuses_is_not_a_game_server_and_sends_nothing()
    {
        var status = await Check().CheckAsync(Online("my server"), TestContext.Current.CancellationToken);

        Assert.Equal(GameServerStatus.NotAGameServer, status);
        Assert.Empty(_server.Requests);
    }

    [Theory]
    // As the game's ApiBaseUrl (SDK xlive_json.cpp): a bare host gets http and port 36000, a URL is kept.
    [InlineData("127.0.0.1", "http://127.0.0.1:36000/")]
    [InlineData("allstars.komplexaci.cz", "http://allstars.komplexaci.cz:36000/")]
    [InlineData("192.168.1.5:4000", "http://192.168.1.5:4000/")]
    [InlineData("https://allstars.komplexaci.cz", "https://allstars.komplexaci.cz/")]
    [InlineData("https://example.com/api//", "https://example.com/api/")]
    [InlineData("http://10.0.0.2:36000", "http://10.0.0.2:36000/")]
    public void A_server_address_becomes_the_same_base_URL_as_in_the_game(string server, string baseUrl) =>
        Assert.Equal(baseUrl, GameServerCheck.ApiBaseUrl(server));

    [Fact]
    public async Task A_bare_host_is_asked_on_port_36000_over_http()
    {
        _server.ReplyWith(Answer("""{"address":"127.0.0.1"}"""));

        await Check().CheckAsync(Online("127.0.0.1"), TestContext.Current.CancellationToken);

        Assert.Equal(new Uri("http://127.0.0.1:36000/whoami"), _server.Requests[0].RequestUri);
    }

    [Fact]
    public async Task The_check_returns_to_its_caller_at_once_while_the_server_is_slow()
    {
        var answer = new TaskCompletionSource<HttpResponseMessage>(TaskCreationOptions.RunContinuationsAsynchronously);
        _server.Respond = _ => answer.Task;

        var check = Check().CheckAsync(Online(), TestContext.Current.CancellationToken);

        Assert.False(check.IsCompleted);
        answer.SetResult(Answer("""{"address":"1.2.3.4"}"""));
        Assert.Equal(GameServerStatus.Ready, await check);
    }

    [Theory]
    [InlineData("https://allstars.komplexaci.cz", "allstars.komplexaci.cz")]
    [InlineData("127.0.0.1", "127.0.0.1:36000")]
    [InlineData("http://10.0.0.2:4000/api", "10.0.0.2:4000")]
    public void The_status_line_names_the_servers_host(string server, string host) =>
        Assert.Equal(host, GameServerCheck.HostOf(server));

    [Theory]
    [InlineData(GameServerStatus.Ready, "Online: ready (allstars.komplexaci.cz)")]
    [InlineData(GameServerStatus.NotAnswering, "Online: the game server isn't answering; you can still play offline (allstars.komplexaci.cz)")]
    [InlineData(GameServerStatus.NotAGameServer, "Online: allstars.komplexaci.cz isn't a game server; check Settings > ONLINE")]
    [InlineData(GameServerStatus.Checking, "Online: checking allstars.komplexaci.cz...")]
    [InlineData(GameServerStatus.Off, "")]
    public void Each_status_reads_as_the_spec_says(GameServerStatus status, string line) =>
        Assert.Equal(line, GameServerCheck.StatusLine(status, "https://allstars.komplexaci.cz"));

    /// <summary>Stands in for the game server: records each request and answers with <see cref="Respond"/>.</summary>
    private sealed class FakeServer : HttpMessageHandler
    {
        public List<HttpRequestMessage> Requests { get; } = [];
        public Func<CancellationToken, Task<HttpResponseMessage>> Respond { get; set; } =
            _ => Task.FromResult(new HttpResponseMessage(HttpStatusCode.NotFound));

        public void ReplyWith(HttpResponseMessage response) => Respond = _ => Task.FromResult(response);

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            Requests.Add(request);
            return Respond(cancellationToken);
        }
    }
}
