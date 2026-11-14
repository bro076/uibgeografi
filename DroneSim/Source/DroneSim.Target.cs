using UnrealBuildTool;

public class DroneSimTarget : TargetRules
{
	public DroneSimTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("DroneSim");
	}
}
