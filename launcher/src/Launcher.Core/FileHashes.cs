using System.Security.Cryptography;

namespace WweAllStars.Launcher.Core;

/// <summary>File hashes the setups compare with their manifests.</summary>
internal static class FileHashes
{
    /// <summary>The SHA-256 of a file in lower-case hex, read in 1 MiB pieces.</summary>
    public static string Sha256(string path, CancellationToken cancel)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20);
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var buffer = new byte[1 << 20];
        int read;
        while ((read = stream.Read(buffer)) > 0)
        {
            cancel.ThrowIfCancellationRequested();
            hash.AppendData(buffer, 0, read);
        }
        return Convert.ToHexStringLower(hash.GetHashAndReset());
    }
}
