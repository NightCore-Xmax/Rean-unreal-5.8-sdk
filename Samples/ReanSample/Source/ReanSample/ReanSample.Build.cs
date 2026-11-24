// Copyright (c) REAN Studios. MIT License.

using UnrealBuildTool;

public class ReanSample : ModuleRules
{
	public ReanSample(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore", "ReanSDK" });
	}
}
