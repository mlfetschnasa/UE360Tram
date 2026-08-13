using UnrealBuildTool;

// Isolates the nDisplay/DisplayCluster dependency to its own plugin (Objective 19: keep the
// tram/view synchronization architecture independent enough that the rendering backend can be
// changed if necessary). The core TramSystem plugin/module never references DisplayCluster; a
// project that doesn't use nDisplay can simply never enable this plugin at all. Being a
// separate .uplugin (not just a second module in TramSystem.uplugin) matters here beyond
// tidiness: it's what lets TramSystemDisplayCluster.uplugin declare a real plugin-level
// dependency on "DisplayCluster" (see that file's "Plugins" array), which is the only way to
// guarantee DisplayCluster's own modules are enabled and loaded before this one tries to load -
// a per-module Build.cs dependency alone only affects compilation, not runtime module load
// order/enablement.
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
