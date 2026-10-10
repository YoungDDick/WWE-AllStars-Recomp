using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

public sealed class HomeJobsTests
{
    private readonly HomeJobs _jobs = new();

    [Fact]
    public void With_no_job_running_Play_waits_only_for_the_game()
    {
        Assert.True(_jobs.CanPlay(gameRunning: false));
        Assert.False(_jobs.CanPlay(gameRunning: true));
    }

    [Theory]
    [InlineData(HomeJob.Repair)]
    [InlineData(HomeJob.PrepareShaders)]
    [InlineData(HomeJob.AddDlc)]
    public void Play_is_off_while_a_job_runs_and_back_once_it_ends(HomeJob job)
    {
        Assert.True(_jobs.TryStart(job, out _));

        Assert.True(_jobs.IsRunning(job));
        Assert.False(_jobs.CanPlay(gameRunning: false));

        _jobs.End(job);

        Assert.False(_jobs.IsRunning(job));
        Assert.True(_jobs.CanPlay(gameRunning: false));
    }

    [Fact]
    public void Play_stays_off_until_every_job_has_ended()
    {
        _jobs.TryStart(HomeJob.Repair, out _);
        _jobs.TryStart(HomeJob.PrepareShaders, out _);

        _jobs.End(HomeJob.PrepareShaders);

        Assert.False(_jobs.CanPlay(gameRunning: false));
        _jobs.End(HomeJob.Repair);
        Assert.True(_jobs.CanPlay(gameRunning: false));
    }

    [Fact]
    public void A_job_already_running_is_not_started_twice()
    {
        Assert.True(_jobs.TryStart(HomeJob.Repair, out var first));

        Assert.False(_jobs.TryStart(HomeJob.Repair, out _));
        _jobs.Cancel(HomeJob.Repair);
        Assert.True(first.IsCancellationRequested);
    }

    [Fact]
    public void Cancel_stops_only_that_job_and_ignores_one_that_is_not_running()
    {
        _jobs.TryStart(HomeJob.Repair, out var repair);
        _jobs.TryStart(HomeJob.PrepareShaders, out var shaders);

        _jobs.Cancel(HomeJob.PrepareShaders);
        _jobs.End(HomeJob.PrepareShaders);
        _jobs.Cancel(HomeJob.PrepareShaders);

        Assert.True(shaders.IsCancellationRequested);
        Assert.False(repair.IsCancellationRequested);
    }

    [Fact]
    public void Closing_the_launcher_cancels_every_running_job()
    {
        _jobs.TryStart(HomeJob.Repair, out var repair);
        _jobs.TryStart(HomeJob.PrepareShaders, out var shaders);

        _jobs.CancelAll();

        Assert.True(repair.IsCancellationRequested);
        Assert.True(shaders.IsCancellationRequested);
    }
}
