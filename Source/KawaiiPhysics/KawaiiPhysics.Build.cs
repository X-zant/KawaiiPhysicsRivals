// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

using UnrealBuildTool;

public class KawaiiPhysics : ModuleRules
{
	public KawaiiPhysics(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"AnimGraphRuntime",
				"GameplayTags"
			}
		);

		// StructUtils is a separate plugin up to 5.4; it was folded into the engine in 5.5.
		// The target here is 5.3.2, so this resolves to the plugin module.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore"
			}
		);

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.Add("UnrealEd");
		}
	}
}
