using UnrealBuildTool;

public class ColdEntry : ModuleRules
{
	public ColdEntry(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "DeveloperSettings"
		});
	}
}
