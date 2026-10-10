using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class ReleaseVersionTests
{
    [Theory]
    // The SDK appends the commit to the informational version; About shows its short form, so
    // two players can see whether they run the same build (launcher-online spec, "Build mismatch").
    [InlineData("0.1.0+42a65e4e8d8770289d18a32598e626a25624ce2c", "0.1.0 (commit 42a65e4)")]
    [InlineData("0.2.0-rc1+abc", "0.2.0-rc1 (commit abc)")]
    [InlineData("0.1.0", "0.1.0")]
    [InlineData("0.1.0+", "0.1.0")]
    public void About_shows_the_version_and_the_short_commit(string informational, string shown) =>
        Assert.Equal(shown, ReleaseVersion.Describe(informational));
}
