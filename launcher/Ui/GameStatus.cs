namespace CoopLauncher.Gui;

/// <summary>The mod button the Session row offers: what it does and whether it can be used now.</summary>
internal readonly record struct ModAction(string Label, bool Enabled);

/// <summary>What the rail, the mod button and the settings view say about a game's mod. Every game ships in one package,
/// so an update names the game's mod version in the package (<see cref="PackageBuild"/>). A game build the profile does not list,
/// or a required file the game folder does not satisfy (UnmetRequirement), is shown instead of the mod state.</summary>
internal sealed record GameStatus(
    ModStatus Mod, bool GameFound, int PackageBuild, int GameBuild = GameBuilds.Unknown, bool BuildSupported = true,
    RequirementProblem? UnmetRequirement = null, bool GameRunning = false, bool Built = false, bool DiscSet = false)
{
    private const string ReplaceLabel = "Replace";

    // Built: the game is one the launcher builds from the player's disc (no mod to install: Install reads Build, an installed mod "Built"). DiscSet: that disc is picked.

    public static readonly GameStatus Unknown = new(new ModStatus(ModState.NotInstalled, 0), false, 0);

    public bool ModInstalled => Mod.State != ModState.NotInstalled;

    public bool UpdateAvailable => Mod.State == ModState.UpdateAvailable;

    /// <summary>The row's line is drawn in the amber state colour.</summary>
    public bool NeedsAttention => HasProblem || GameFound && Mod.State == ModState.UpdateAvailable;

    /// <summary>Something stops or endangers play: a modified mod, an unsupported game build or a missing required file.</summary>
    public bool HasProblem => GameFound && (Mod.State == ModState.Modified || !BuildSupported || UnmetRequirement != null);

    /// <summary>Hosting needs the game, its mod (an update or a repair is applied by the start itself), a supported game build
    /// and every required file.</summary>
    public bool CanHost => GameFound && ModInstalled && BuildSupported && UnmetRequirement == null;

    /// <summary>The rail's line under the game's name.</summary>
    public string Line => !GameFound ? "Game not found"
        : !BuildSupported ? $"Game build {GameBuild} unsupported"
        : UnmetRequirement is { } unmet ? StatusText.Requirement(unmet.Name, unmet.OtherVersion)
        : Mod.State switch
        {
            ModState.NotInstalled => Built ? "Not built" : "Not installed",
            ModState.UpdateAvailable => PackageBuild > 0 ? $"Mod update {PackageBuild}" : "Mod update",
            ModState.Modified => Built ? "Build damaged" : "Mod modified",
            _ => ModLabel,
        };

    /// <summary>The settings view's line under a game's folder: the mod and the game's build (two items, a gap apart on the web page).</summary>
    public string SettingsLine => $"{SettingsMod} / {SettingsGame}";

    private string InstallLabel => Built ? "Build" : "Install";

    private string RepairLabel => Built ? "Rebuild" : "Repair";

    /// <summary>The one mod button the Session row offers, or none when the mod needs nothing. Never usable while the game runs.</summary>
    public ModAction? Action => Mod.State switch
    {
        ModState.NotInstalled => new ModAction(InstallLabel, GameFound && !GameRunning),
        _ when UnmetRequirement is { } unmet => new ModAction(unmet.OtherVersion ? ReplaceLabel : InstallLabel, GameFound && !GameRunning),
        ModState.UpdateAvailable => new ModAction("Update", GameFound && !GameRunning),
        ModState.Modified => new ModAction(RepairLabel, GameFound && !GameRunning),
        _ => null,
    };

    private string ModLabel => Mod.Build > 0 ? $"Mod {Mod.Build}" : "Installed";

    private string BuiltSettingsMod => Mod.State switch
    {
        ModState.NotInstalled => "Not built",
        ModState.Modified => "Build damaged",
        _ => "Built",
    };

    /// <summary>The mod half of the settings line.</summary>
    public string SettingsMod => Built ? BuiltSettingsMod : Mod.State switch
    {
        ModState.NotInstalled => "Mod not installed",
        ModState.Modified => "Mod modified",
        ModState.UpdateAvailable => PackageBuild > 0 ? $"Mod update {PackageBuild}" : "Mod update",
        _ => Mod.Build > 0 ? $"Mod {Mod.Build} installed" : "Mod installed",
    };

    /// <summary>The game half of the settings line.</summary>
    public string SettingsGame => Built ? (DiscSet ? "Disc set" : "Disc not set")
        : !GameFound ? "Game not found"
        : GameBuild == GameBuilds.Unknown ? "Game found"
        : BuildSupported ? $"Game build {GameBuild}"
        : $"Game build {GameBuild} unsupported";

    /// <summary>Reads a game's mod state: the folder, the installed game build and the mod's files (it hashes them, so not on
    /// the UI thread), and whether the game runs now.</summary>
    public static GameStatus Read(GameProfile game)
    {
        var gameDir = GameFolders.Find(game);
        var gameBuild = GameBuilds.Installed(game, gameDir);
        return new GameStatus(
            ModInstaller.Status(game, gameDir), gameDir != null, BuildCheck.LocalModVersion(game.Id), gameBuild, GameBuilds.IsSupported(game, gameBuild),
            gameDir == null ? null : game.UnmetRequirement(gameDir), GameLauncher.IsRunning(game), game.Recomp != null, RecompInstall.DiscPath(game) != null);
    }
}
