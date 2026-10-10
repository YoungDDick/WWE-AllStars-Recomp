using WweAllStars.Launcher.Core;

// Usage: Launcher.ReleaseCheck <staged release folder>. Exit 0 when it may ship, 1 when not.
if (args.Length != 1 || !Directory.Exists(args[0]))
{
    Console.Error.WriteLine("Usage: Launcher.ReleaseCheck <staged release folder>");
    return 2;
}
var problems = ReleaseCheck.Run(args[0], DiscManifest.Supported, TitleUpdateManifest.Supported);
foreach (var problem in problems) Console.Error.WriteLine($"{problem.Path}: {problem.Reason}");
if (problems.Count > 0)
{
    Console.Error.WriteLine($"The release folder fails the packaging check ({problems.Count} problem{(problems.Count == 1 ? "" : "s")}).");
    return 1;
}
Console.WriteLine($"Release check passed: {Path.GetFullPath(args[0])}");
return 0;
