using UnrealBuildTool;

public class DroneSim : ModuleRules
{
	public DroneSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "Sockets", "Networking", "ProceduralMeshComponent"
		});
	}
}
