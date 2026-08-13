using UnrealBuildTool;

// Isolates the nDisplay/DisplayCluster dependency to its own module (Objective 19: keep the
// tram/view synchronization architecture independent enough that the rendering backend can be
// changed if necessary). The core TramSystem module never references DisplayCluster; a project
// that doesn't use nDisplay can simply never enable/build this module without affecting
// TramSystem at all.
public class TramSystemDisplayCluster : ModuleRules
{
	public TramSystemDisplayCluster(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"TramSystem",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DisplayCluster",
		});
	}
}
