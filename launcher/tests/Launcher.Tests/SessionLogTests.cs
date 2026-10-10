using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class SessionLogTests : IDisposable
{
    private readonly string _root = Directory.CreateTempSubdirectory("wwe-launcher-").FullName;

    public void Dispose() => Directory.Delete(_root, recursive: true);

    private string Session(string name)
    {
        var log = Path.Combine(_root, "logs", name, "runtime.log");
        Directory.CreateDirectory(Path.GetDirectoryName(log)!);
        File.WriteAllText(log, "");
        return log;
    }

    [Fact]
    public void The_latest_session_is_the_newest_folder_counting_same_second_suffixes()
    {
        Session("20261003-235959");
        Session("20261004-123005");
        Session("20261004-123005-2");
        var latest = Session("20261004-123005-10");
        Directory.CreateDirectory(Path.Combine(_root, "logs", "not-a-session"));

        Assert.Equal(latest, new PortableLayout(_root).LatestSessionLogPath());
    }

    [Fact]
    public void A_session_folder_without_a_log_is_skipped()
    {
        var latest = Session("20261004-120000");
        Directory.CreateDirectory(Path.Combine(_root, "logs", "20261004-130000"));

        Assert.Equal(latest, new PortableLayout(_root).LatestSessionLogPath());
    }

    [Fact]
    public void Open_logs_opens_the_latest_session_folder()
    {
        Session("20261004-120000");
        var latest = Session("20261004-130000");

        Assert.Equal(Path.GetDirectoryName(latest), new PortableLayout(_root).LogsFolderToOpen());
    }

    [Fact]
    public void Open_logs_with_no_session_yet_opens_the_logs_folder_and_creates_it()
    {
        var layout = new PortableLayout(_root);

        Assert.Null(layout.LatestSessionLogPath());
        Assert.Equal(Path.Combine(_root, "logs"), layout.LogsFolderToOpen());
        Assert.True(Directory.Exists(Path.Combine(_root, "logs")));
    }
}
