using UnrealBuildTool;

public class DroneSimEditorTarget : TargetRules
{
	public DroneSimEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("DroneSim");
	}
}
