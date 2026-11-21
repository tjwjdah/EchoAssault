using UnrealBuildTool;

public class EchoAssaultEditorTarget : TargetRules
{
    public EchoAssaultEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.Add("EchoAssault");
    }
}
