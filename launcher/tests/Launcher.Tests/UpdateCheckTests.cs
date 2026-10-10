using System.Net;
using System.Text;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class UpdateCheckTests
{
    private const string Repository = "example/wwe-allstars-recomp";
    private static readonly Version Current = new(0, 2, 0);

    private readonly FakeGitHub _github = new();

    private UpdateChecker Checker(Version? current = null, TimeSpan? timeout = null) =>
        new(new HttpClient(_github), Repository, current ?? Current, timeout ?? TimeSpan.FromSeconds(5));

    private static HttpResponseMessage Latest(string tag, string? page = null) => new(HttpStatusCode.OK)
    {
        Content = new StringContent(
            $$"""{"tag_name":"{{tag}}","html_url":"{{page ?? $"https://github.com/{Repository}/releases/tag/{tag}"}}","draft":false}""",
            Encoding.UTF8, "application/json"),
    };

    [Fact]
    public async Task A_newer_tag_offers_the_release_page()
    {
        _github.ReplyWith(Latest("v0.3.0"));

        var update = await Checker().CheckAsync(enabled: true, TestContext.Current.CancellationToken);

        Assert.NotNull(update);
        Assert.Equal("v0.3.0", update.Tag);
        Assert.Equal(new Uri($"https://github.com/{Repository}/releases/tag/v0.3.0"), update.ReleasePage);
    }

    [Theory]
    [InlineData("v0.2.0")]
    [InlineData("0.2")]
    [InlineData("v0.1.9")]
    [InlineData("v0.2.0-rc1")]
    [InlineData("nightly")]
    public async Task The_same_an_older_or_an_unreadable_tag_offers_nothing(string tag)
    {
        _github.ReplyWith(Latest(tag));

        Assert.Null(await Checker().CheckAsync(enabled: true, TestContext.Current.CancellationToken));
    }

    [Theory]
    [InlineData("v0.10.0")]
    [InlineData("1.0")]
    [InlineData("v0.2.1-beta")]
    public async Task Versions_compare_by_number_not_by_text(string tag)
    {
        _github.ReplyWith(Latest(tag));

        Assert.NotNull(await Checker().CheckAsync(enabled: true, TestContext.Current.CancellationToken));
    }

    [Fact]
    public async Task Disabled_sends_no_request()
    {
        _github.ReplyWith(Latest("v9.0.0"));

        Assert.Null(await Checker().CheckAsync(enabled: false, TestContext.Current.CancellationToken));
        Assert.Empty(_github.Requests);
    }

    [Fact]
    public async Task With_no_repository_set_the_check_is_off_and_sends_no_request()
    {
        // ADR-0014: private through M5, so no request goes to a repo that does not exist yet.
        _github.ReplyWith(Latest("v9.0.0"));
        var checker = new UpdateChecker(new HttpClient(_github), "", Current, TimeSpan.FromSeconds(5));

        Assert.False(checker.IsAvailable);
        Assert.Null(await checker.CheckAsync(enabled: true, TestContext.Current.CancellationToken));
        Assert.Empty(_github.Requests);
    }

    [Fact]
    public async Task One_launch_sends_at_most_one_request()
    {
        _github.ReplyWith(Latest("v0.3.0"));
        var checker = Checker();

        var first = await checker.CheckAsync(enabled: true, TestContext.Current.CancellationToken);
        var second = await checker.CheckAsync(enabled: true, TestContext.Current.CancellationToken);

        Assert.Single(_github.Requests);
        Assert.Equal(first, second);
    }

    [Fact]
    public async Task The_request_asks_the_releases_api_for_the_latest_release_as_github_requires()
    {
        _github.ReplyWith(Latest("v0.3.0"));

        await Checker().CheckAsync(enabled: true, TestContext.Current.CancellationToken);

        var request = Assert.Single(_github.Requests);
        Assert.Equal(HttpMethod.Get, request.Method);
        Assert.Equal(new Uri($"https://api.github.com/repos/{Repository}/releases/latest"), request.RequestUri);
        // The API refuses requests without a User-Agent.
        Assert.Equal("WWEAllStarsLauncher/0.2.0", request.Headers.UserAgent.ToString());
        Assert.Contains(request.Headers.Accept, accept => accept.MediaType == "application/vnd.github+json");
    }

    [Fact]
    public async Task A_two_part_launcher_version_still_checks()
    {
        _github.ReplyWith(Latest("v0.3.0"));

        var update = await Checker(current: new Version(0, 2)).CheckAsync(enabled: true, TestContext.Current.CancellationToken);

        Assert.NotNull(update);
        Assert.Equal("WWEAllStarsLauncher/0.2.0", Assert.Single(_github.Requests).Headers.UserAgent.ToString());
    }

    [Fact]
    public async Task The_check_returns_to_its_caller_at_once_while_the_server_is_slow()
    {
        var answer = new TaskCompletionSource<HttpResponseMessage>(TaskCreationOptions.RunContinuationsAsynchronously);
        _github.Respond = _ => answer.Task;

        var check = Checker().CheckAsync(enabled: true, TestContext.Current.CancellationToken);

        Assert.False(check.IsCompleted);
        answer.SetResult(Latest("v0.3.0"));
        Assert.NotNull(await check);
    }

    [Fact]
    public async Task A_server_that_never_answers_is_silent_after_the_timeout()
    {
        _github.Respond = async token =>
        {
            await Task.Delay(Timeout.Infinite, token);
            throw new InvalidOperationException("unreachable");
        };

        var update = await Checker(timeout: TimeSpan.FromMilliseconds(50))
            .CheckAsync(enabled: true, TestContext.Current.CancellationToken)
            .WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken);

        Assert.Null(update);
    }

    [Fact]
    public async Task A_network_error_is_silent()
    {
        _github.Respond = _ => throw new HttpRequestException("No such host is known.");

        Assert.Null(await Checker().CheckAsync(enabled: true, TestContext.Current.CancellationToken));
    }

    [Theory]
    [InlineData(HttpStatusCode.Forbidden)]       // GitHub's rate limit: 403 with X-RateLimit-Remaining: 0
    [InlineData(HttpStatusCode.TooManyRequests)] // its secondary rate limit
    [InlineData(HttpStatusCode.NotFound)]        // no release published yet
    [InlineData(HttpStatusCode.InternalServerError)]
    public async Task An_error_status_or_rate_limit_is_silent(HttpStatusCode status)
    {
        var response = new HttpResponseMessage(status) { Content = new StringContent("""{"message":"API rate limit exceeded"}""") };
        response.Headers.Add("X-RateLimit-Remaining", "0");
        _github.ReplyWith(response);

        Assert.Null(await Checker().CheckAsync(enabled: true, TestContext.Current.CancellationToken));
    }

    [Theory]
    [InlineData("not json")]
    [InlineData("""{"html_url":"https://github.com/example/wwe-allstars-recomp/releases/tag/v9.0.0"}""")]
    [InlineData("""{"tag_name":"v9.0.0"}""")]
    [InlineData("""{"tag_name":"v9.0.0","html_url":"http://github.com/x"}""")]
    [InlineData("""{"tag_name":"v9.0.0","html_url":"https://evil.example/releases"}""")]
    [InlineData("""{"tag_name":9,"html_url":"https://github.com/x"}""")]
    [InlineData("[]")]
    public async Task An_unexpected_answer_is_silent(string body)
    {
        _github.ReplyWith(new HttpResponseMessage(HttpStatusCode.OK) { Content = new StringContent(body) });

        Assert.Null(await Checker().CheckAsync(enabled: true, TestContext.Current.CancellationToken));
    }

    /// <summary>Stands in for api.github.com: records each request and answers with <see cref="Respond"/>.</summary>
    private sealed class FakeGitHub : HttpMessageHandler
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
