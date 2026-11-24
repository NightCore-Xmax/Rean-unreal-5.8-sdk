// Copyright (c) REAN Studios. MIT License.

using UnrealBuildTool;

public class ReanSDKEditor : ModuleRules
{
	public ReanSDKEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"PropertyEditor",
			"ReanCore",
			"ReanSDK",
		});
	}
}
