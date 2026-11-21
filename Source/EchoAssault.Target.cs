using UnrealBuildTool;

public class EchoAssaultTarget : TargetRules
{
    public EchoAssaultTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.Add("EchoAssault");
    }
}
