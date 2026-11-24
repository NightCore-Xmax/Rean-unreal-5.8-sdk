// Copyright (c) REAN Studios. MIT License.

using UnrealBuildTool;

public class ReanSampleTarget : TargetRules
{
	public ReanSampleTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ReanSample");
	}
}
