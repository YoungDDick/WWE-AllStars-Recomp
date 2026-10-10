using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

/// <summary>
/// The game's prepare-pipelines output (ticket 06, src/native/prepare_output.h): lines in,
/// progress and a result out.
/// </summary>
public sealed class PipelineOutputTests
{
    private static (PipelineOutput Output, List<PipelineProgress> Progress) Feed(params string[] lines)
    {
        var output = new PipelineOutput();
        var progress = new List<PipelineProgress>();
        foreach (var line in lines)
            if (output.Read(line) is { } report) progress.Add(report);
        return (output, progress);
    }

    [Fact]
    public void A_full_run_reports_both_stages_and_ends_ready()
    {
        var (output, progress) = Feed(
            "wwe-prepare-pipelines 1",
            "progress stage=modules done=867 total=2024",
            "progress stage=modules done=2024 total=2024",
            "progress stage=pipelines done=24 total=165",
            "progress stage=pipelines done=165 total=165",
            "result status=ok modules=2024 pipelines=165 failed=0 ms=561");

        Assert.Equal(
            [
                new PipelineProgress(PipelineStage.Modules, 867, 2024),
                new PipelineProgress(PipelineStage.Modules, 2024, 2024),
                new PipelineProgress(PipelineStage.Pipelines, 24, 165),
                new PipelineProgress(PipelineStage.Pipelines, 165, 165),
            ], progress);
        var result = output.Finish(exitCode: 0);
        Assert.Equal(PipelineOutcome.Ready, result.Outcome);
        Assert.Equal(165, result.Pipelines);
        Assert.Equal(0, result.Failed);
        Assert.True(result.Usable);
    }

    [Fact]
    public void Overall_progress_runs_from_modules_into_pipelines()
    {
        var modulesHalf = new PipelineProgress(PipelineStage.Modules, 1, 2).Fraction;
        var modulesDone = new PipelineProgress(PipelineStage.Modules, 2, 2).Fraction;
        var pipelinesHalf = new PipelineProgress(PipelineStage.Pipelines, 50, 100).Fraction;

        Assert.InRange(modulesHalf, 0.01, modulesDone - 0.01);
        Assert.InRange(pipelinesHalf, modulesDone + 0.01, 0.99);
        Assert.Equal(1, new PipelineProgress(PipelineStage.Pipelines, 165, 165).Fraction);
        Assert.Equal(1, new PipelineProgress(PipelineStage.Pipelines, 0, 0).Fraction);
    }

    [Fact]
    public void On_the_library_path_the_libraries_stage_holds_most_of_the_bar()
    {
        var (output, progress) = Feed(
            "wwe-prepare-pipelines 1",
            "progress stage=modules done=2024 total=2024",
            "progress stage=libraries done=2222 total=4444",
            "progress stage=libraries done=4444 total=4444",
            "progress stage=pipelines done=0 total=125",
            "result status=ok modules=2024 pipelines=125 failed=0 ms=7000 libraries=4444 libraries_failed=0");

        Assert.Equal([PipelineStage.Modules, PipelineStage.Libraries, PipelineStage.Libraries, PipelineStage.Pipelines],
            progress.Select(p => p.Stage));
        Assert.InRange(progress[1].Fraction, progress[0].Fraction + 0.3, progress[2].Fraction - 0.3);
        // Pipelines after the libraries start where the libraries ended, not back at the modules' end.
        Assert.True(progress[3].Fraction >= progress[2].Fraction);
        Assert.Equal(PipelineOutcome.Ready, output.Finish(exitCode: 0).Outcome);
    }

    [Fact]
    public void Refused_pipelines_end_partial_and_are_still_usable()
    {
        var (output, _) = Feed(
            "wwe-prepare-pipelines 1",
            "result status=partial modules=2024 pipelines=160 failed=5 ms=700");

        var result = output.Finish(exitCode: 3);

        Assert.Equal(PipelineOutcome.Partial, result.Outcome);
        Assert.Equal(5, result.Failed);
        Assert.True(result.Usable);
        Assert.Contains("5", result.Message);
    }

    [Fact]
    public void A_mode_that_could_not_run_names_its_reason()
    {
        var (output, _) = Feed(
            "wwe-prepare-pipelines 1",
            "result status=failed reason=vulkan_setup ms=11");

        var result = output.Finish(exitCode: 2);

        Assert.Equal(PipelineOutcome.Failed, result.Outcome);
        Assert.Equal("vulkan_setup", result.Reason);
        Assert.False(result.Usable);
        Assert.Contains("Vulkan", result.Message);
        Assert.Contains("Emulated", result.Message);
        Assert.DoesNotContain("on Native", result.Message);
    }

    [Fact]
    public void An_exit_without_a_result_line_is_a_crash_with_its_code()
    {
        var (output, _) = Feed(
            "wwe-prepare-pipelines 1",
            "progress stage=modules done=10 total=2024");

        var result = output.Finish(exitCode: unchecked((int)0xC0000005));

        Assert.Equal(PipelineOutcome.Crashed, result.Outcome);
        Assert.False(result.Usable);
        Assert.Contains("C0000005", result.Message);
        Assert.Contains("prepare-pipelines.log", result.Message);
    }

    [Fact]
    public void A_result_that_disagrees_with_the_exit_code_is_a_crash()
    {
        var (output, _) = Feed("result status=ok modules=1 pipelines=1 failed=0 ms=1");

        Assert.Equal(PipelineOutcome.Crashed, output.Finish(exitCode: 1).Outcome);
    }

    [Fact]
    public void Lines_it_does_not_know_are_ignored()
    {
        var (output, progress) = Feed(
            "wwe-prepare-pipelines 1",
            "[info] something the runtime printed",
            "progress stage=shaders done=1 total=2",
            "progress stage=modules done=x total=2",
            "progress stage=modules done=1 total=2 extra=7",
            "result status=ok modules=2 pipelines=0 failed=0 ms=5");

        Assert.Equal([new PipelineProgress(PipelineStage.Modules, 1, 2)], progress);
        Assert.Equal(PipelineOutcome.Ready, output.Finish(0).Outcome);
    }

    [Fact]
    public void A_newer_format_version_is_not_trusted()
    {
        var (output, progress) = Feed(
            "wwe-prepare-pipelines 2",
            "progress stage=modules done=1 total=2",
            "result status=ok modules=2 pipelines=0 failed=0 ms=5");

        Assert.Empty(progress);
        Assert.Equal(PipelineOutcome.Crashed, output.Finish(0).Outcome);
    }
}
