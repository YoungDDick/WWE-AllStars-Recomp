using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public class DiscManifestTests
{
    [Fact]
    public void The_supported_manifest_is_the_base_v5_disc()
    {
        var manifest = DiscManifest.Supported;

        Assert.Equal("54510866-60cef505-00000005", manifest.Version);
        Assert.Equal("521c2325d89470ff490efee257885a465360ffb64f9db2491eda6af7ab3a6892", manifest.Executable.Sha256);
        Assert.Equal(5632, manifest.Files.Count);
    }

    [Fact]
    public void Paths_are_found_regardless_of_case()
    {
        var manifest = DiscManifest.Parse(new TestDisc().ManifestText());

        Assert.Equal(5000, manifest.Find("coalesced_int.BIN")?.Size);
        Assert.Null(manifest.Find("missing.bin"));
    }

    [Theory]
    [InlineData("abc\t1\tx.bin\n")]
    [InlineData("manifest_version\tv1\nnot-a-hash\t1\tdefault.xex\n")]
    [InlineData("manifest_version\tv1\n")]
    public void A_malformed_manifest_is_rejected(string text)
    {
        Assert.Throws<InvalidDataException>(() => DiscManifest.Parse(text));
    }
}
