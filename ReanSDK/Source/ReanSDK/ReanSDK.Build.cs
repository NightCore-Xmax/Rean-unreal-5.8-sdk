// Copyright (c) REAN Studios. MIT License.

using UnrealBuildTool;

public class ReanSDK : ModuleRules
{
	public ReanSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"ReanCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"HTTP",
			"Sockets",
		});

		// Browser sign-in listens on 127.0.0.1 for the redirect: desktop only.
		bool bDesktop = Target.Platform == UnrealTargetPlatform.Win64
			|| Target.Platform == UnrealTargetPlatform.Mac
			|| Target.Platform == UnrealTargetPlatform.Linux;
		PrivateDefinitions.Add("REAN_WITH_LOOPBACK=" + (bDesktop ? "1" : "0"));

		// The default token store encrypts with DPAPI on Windows (CryptProtectData).
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("crypt32.lib");
		}
	}
}
