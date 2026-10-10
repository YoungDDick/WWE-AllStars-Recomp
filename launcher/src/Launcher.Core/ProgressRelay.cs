namespace WweAllStars.Launcher.Core;

/// <summary>
/// An <see cref="IProgress{T}"/> that reports straight away, on the reporting thread, one report
/// at a time (the shader converter reports from its output reader's thread).
/// </summary>
internal sealed class ProgressRelay<T>(Action<T> report) : IProgress<T>
{
    private readonly Lock _lock = new();

    public void Report(T value)
    {
        lock (_lock) report(value);
    }
}
