using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher;

public partial class App : Application
{
    /// <summary>The parsed command line; set by Program before Avalonia starts.</summary>
    internal static LauncherOptions Options { get; set; } =
        LauncherOptions.Parse([], AppContext.BaseDirectory);

    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            desktop.MainWindow = new MainWindow();
        base.OnFrameworkInitializationCompleted();
    }
}
