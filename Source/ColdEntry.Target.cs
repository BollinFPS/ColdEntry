using UnrealBuildTool;
using System.Collections.Generic;

public class ColdEntryTarget : TargetRules
{
	public ColdEntryTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ColdEntry");
	}
}
