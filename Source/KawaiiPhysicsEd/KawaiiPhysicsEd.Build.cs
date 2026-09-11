// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

using UnrealBuildTool;

public class KawaiiPhysicsEd : ModuleRules
{
	public KawaiiPhysicsEd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "KawaiiPhysics",
			"AnimGraph", "BlueprintGraph", "Persona", "UnrealEd", "AnimGraphRuntime",
			"Slate", "SlateCore", "EditorFramework",
			// BaseClass of the edit mode moved into its own module in 5.1.
			"AnimationEditMode",
			// JSON import of cooked AnimBlueprints exported with FModel.
			"Json", "JsonUtilities", "DesktopPlatform", "ToolMenus", "ContentBrowser", "AssetRegistry", "ApplicationCore"
		});

		// StructUtils is a separate plugin up to 5.4; folded into the engine in 5.5.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
	}
}
