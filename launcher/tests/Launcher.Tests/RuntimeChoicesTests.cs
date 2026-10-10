using WweAllStars.Launcher.Core;

namespace WweAllStars.Launcher.Tests;

/// <summary>The GPU and language settings, whose choices depend on the player's PC and disc.</summary>
public sealed class RuntimeChoicesTests : IDisposable
{
    private readonly string _gameData = Directory.CreateTempSubdirectory("wwe-gamedata-").FullName;

    public void Dispose() => Directory.Delete(_gameData, recursive: true);

    // The two settings as the game exports them (src/settings/registry.h).
    private const string SchemaJson = """
        {
          "format": 2,
          "settings": [
            {"key": "vulkan_device", "label": "GPU", "tab": "DISPLAY", "type": "int", "default": "-1", "applies_at_startup": true, "native_only": false, "next_start": true, "runtime_choices": "gpus", "choices": [{"value": "-1", "label": "Automatic"}], "help": "Which GPU runs the game."},
            {"key": "user_language", "label": "Language", "tab": "DISPLAY", "type": "int", "default": "1", "applies_at_startup": true, "native_only": false, "next_start": true, "runtime_choices": "languages", "choices": [{"value": "1", "label": "English", "file": "Game/Localization/Coalesced.int"}, {"value": "3", "label": "German", "file": "Game/Localization/Coalesced.deu"}, {"value": "4", "label": "French", "file": "Game/Localization/Coalesced.fra"}, {"value": "6", "label": "Italian", "file": "Game/Localization/Coalesced.ita"}], "help": "The language of menus and commentary."}
          ]
        }
        """;

    private void AddGameFile(string relative)
    {
        var path = Path.Combine(_gameData, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, "x");
    }

    private static IEnumerable<string> Labels(SettingsSchema schema, string key) =>
        schema.Find(key)!.Choices.Select(c => c.Label);

    [Fact]
    public void Language_choices_are_the_languages_whose_Coalesced_files_the_game_data_holds()
    {
        AddGameFile("Game/Localization/Coalesced.int");
        AddGameFile("Game/Localization/Coalesced.fra");
        AddGameFile("Game/Localization/Coalesced.ita");

        var schema = SettingsSchema.Parse(SchemaJson).WithRuntimeChoices(_gameData, []);

        Assert.Equal(["English", "French", "Italian"], Labels(schema, "user_language"));
    }

    [Fact]
    public void Without_game_data_the_language_keeps_only_its_default()
    {
        var schema = SettingsSchema.Parse(SchemaJson).WithRuntimeChoices(Path.Combine(_gameData, "missing"), []);

        Assert.Equal(["English"], Labels(schema, "user_language"));
    }

    [Fact]
    public void Gpu_choices_are_automatic_then_each_gpu_the_game_reports_by_name()
    {
        var schema = SettingsSchema.Parse(SchemaJson)
            .WithRuntimeChoices(_gameData, [new Gpu(0, "Intel(R) Iris(R) Xe Graphics"), new Gpu(1, "NVIDIA GeForce RTX 3060")]);

        Assert.Equal(
            [new SettingChoice("-1", "Automatic"), new SettingChoice("0", "Intel(R) Iris(R) Xe Graphics"),
             new SettingChoice("1", "NVIDIA GeForce RTX 3060")],
            schema.Find("vulkan_device")!.Choices);
    }

    [Fact]
    public void The_settings_file_saves_a_reported_gpu_and_a_present_language_and_refuses_others()
    {
        AddGameFile("Game/Localization/Coalesced.int");
        AddGameFile("Game/Localization/Coalesced.deu");
        var path = Path.Combine(_gameData, "wweallstars.settings.toml");
        var file = new SettingsFile(path,
            SettingsSchema.Parse(SchemaJson).WithRuntimeChoices(_gameData, [new Gpu(0, "Only GPU")]));

        file.Set("vulkan_device", "0");
        file.Set("user_language", "3");
        Assert.Throws<LauncherException>(() => file.Set("vulkan_device", "1"));
        Assert.Throws<LauncherException>(() => file.Set("user_language", "4"));  // no Coalesced.fra

        Assert.Equal("vulkan_device = 0\nuser_language = 3\n", File.ReadAllText(path));
    }
}
