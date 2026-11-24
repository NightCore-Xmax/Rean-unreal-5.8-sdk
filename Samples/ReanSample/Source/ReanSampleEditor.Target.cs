// Copyright (c) REAN Studios. MIT License.

using UnrealBuildTool;

public class ReanSampleEditorTarget : TargetRules
{
	public ReanSampleEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ReanSample");
	}
}
