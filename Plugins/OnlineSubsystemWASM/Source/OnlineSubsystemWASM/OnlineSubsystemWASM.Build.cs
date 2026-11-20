using UnrealBuildTool;

public class OnlineSubsystemWASM : ModuleRules
{
	public OnlineSubsystemWASM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"UMG", // base classes for the premade widgets (guide 05)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Engine HTTP is used on native platforms (editor, dedicated server). On Wasm it is a null module,
			// so WasmOssHttp.cpp calls the browser's fetch() instead.
			"HTTP",
			"Json",
			"Slate",
			"SlateCore",
		});
	}
}
