// Copyright (c) REAN Studios. MIT License.

using UnrealBuildTool;

// The engine-independent core of the REAN SDK: plain C++17, no Unreal headers
// (only Private/ReanCoreModule.cpp, the module glue, includes Unreal). The same
// sources are built and unit-tested outside Unreal with CMake (ReanCore/ in the
// repository root).
public class ReanCore : ModuleRules
{
	public ReanCore(ReadOnlyTargetRules Target) : base(Target)
	{
		// No shared PCH: keeps Unreal's macros (check, verify, TEXT...) out of the plain C++ files.
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = false;

		PublicDependencyModuleNames.Add("Core");

		// Export macro (see Public/Rean/ReanExport.h): a DLL in modular builds (editor), static in monolithic ones.
		bool bShared = Target.LinkType == TargetLinkType.Modular;
		PublicDefinitions.Add("REAN_CORE_SHARED=" + (bShared ? "1" : "0"));
		PrivateDefinitions.Add("REAN_CORE_BUILDING=1");
	}
}
