#include "EchoGame.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Algo/RandomShuffle.h"
#include "GameFramework/PlayerInput.h"
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

#define LOCTEXT_NAMESPACE "EchoAssaultUI"

namespace
{
void AddTriangle(TArray<FCanvasUVTri>& Triangles, FVector2D A, FVector2D B, FVector2D C,
                 FLinearColor ColorA, FLinearColor ColorB, FLinearColor ColorC)
{
    FCanvasUVTri Triangle{};
    Triangle.V0_Pos = A; Triangle.V1_Pos = B; Triangle.V2_Pos = C;
    Triangle.V0_Color = ColorA; Triangle.V1_Color = ColorB; Triangle.V2_Color = ColorC;
    Triangles.Add(Triangle);
}

FSlateFontInfo HUDTextFont(int32 Size)
{
    auto Info = FCoreStyle::GetDefaultFontStyle("Regular", Size);
    // UE 5.5 Canvas checks UFont before using the Slate composite font information.
    Info.FontObject = GEngine ? GEngine->GetMediumFont() : nullptr;
    return Info;
}

FText UpgradeName(EEchoUpgrade Choice)
{
    switch (Choice)
    {
    case EEchoUpgrade::Blaster: return LOCTEXT("Blaster", "블래스터 / 피해량과 연사 속도 증가");
    case EEchoUpgrade::Seeker: return LOCTEXT("Seeker", "유도탄 / 무기 획득 또는 강화");
    case EEchoUpgrade::Blades: return LOCTEXT("Blades", "방사형 칼날 / 무기 획득 또는 강화");
    case EEchoUpgrade::Aura: return LOCTEXT("Aura", "분해장 / 본체 주변의 적에게 주기적으로 피해");
    case EEchoUpgrade::MoreEcho: return LOCTEXT("MoreEcho", "분신 추가 / 과거의 이동과 공격을 재현하는 분신 추가");
    case EEchoUpgrade::Accelerate: return LOCTEXT("Accelerate", "시간 가속 / 모든 분신의 재현 지연 1초 감소");
    case EEchoUpgrade::Health: return LOCTEXT("Health", "수리 / 최대 체력 3 증가, 체력 5 회복");
    case EEchoUpgrade::Pulse: return LOCTEXT("Pulse", "기체 개조 / 체력과 이동 속도 증가");
    default: return LOCTEXT("Speed", "추진기 / 이동 속도 증가");
    }
}
}

void AEchoGame::Transition(EEchoPhase Next)
{
    if (IsValid(Drone) && (Next == EEchoPhase::Lost || Next == EEchoPhase::Won))
        Drone->Body->SetVisibility(true);
    Phase = Next;
    Choices.Reset();
    auto* PC = GetWorld()->GetFirstPlayerController();
    if (PC && PC->PlayerInput)
    {
        PC->PlayerInput->FlushPressedKeys();
    }
    if (Next == EEchoPhase::Choosing)
    {
        for (int32 I = 0; I < static_cast<int32>(EEchoWeapon::End); ++I) if (Ranks[I] < 3) Choices.Add(static_cast<EEchoUpgrade>(I));
        if (EchoCount < 3) Choices.Add(EEchoUpgrade::MoreEcho);
        if (Acceleration < 2) Choices.Add(EEchoUpgrade::Accelerate);
        Choices.Add(EEchoUpgrade::Health);
        Choices.Add(EEchoUpgrade::Speed);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
        if (!IsReplayCheckEnabled())
#endif
            Algo::RandomShuffle(Choices);
        Choices.SetNum(FMath::Min(3, Choices.Num()));
    }
    // Hull and thrusters remain eligible. Supply a third distinct fallback if all capped upgrades are exhausted.
    if (Next == EEchoPhase::Choosing && Choices.Num() == 2) Choices.Add(EEchoUpgrade::Pulse);
    UGameplayStatics::SetGamePaused(this, true);
    if (!PC || !GEngine || !GEngine->GameViewport) return;
    const bool bPicking = Next == EEchoPhase::Choosing;
    auto Column = SNew(SVerticalBox);
    Column->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
    [
        SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 23))
        .Text(bPicking ? LOCTEXT("ChooseUpgrade", "강화를 선택하세요") :
            (Next == EEchoPhase::Won ? LOCTEXT("Victory", "승리! 4분 생존 성공") : LOCTEXT("Defeat", "패배 · 기체 파괴")))
    ];
    if (!bPicking)
    {
        Column->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
        [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 16)).AutoWrapText(true)
          .Text(FText::Format(LOCTEXT("Results", "처치 {0} / 분신 처치 {1} / 생존 {2}초"),
              FText::AsNumber(Kills), FText::AsNumber(EchoKills), FText::AsNumber(FMath::RoundToInt(Elapsed)))) ];
    }
    for (const auto Item : Choices)
    {
        Column->AddSlot().AutoHeight().Padding(0, 3)
        [ SNew(SButton).ContentPadding(FMargin(16, 12)).OnClicked_Lambda([this, Item]()
          { Choose(Item); return FReply::Handled(); })
          [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
            .AutoWrapText(true).WrapTextAt(480).Text(UpgradeName(Item)) ] ];
    }
    if (!bPicking)
    {
        Column->AddSlot().AutoHeight()
        [ SNew(SButton).ContentPadding(FMargin(16, 12)).OnClicked_Lambda([this]()
          { Restart(); return FReply::Handled(); })
          [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 18)).Text(LOCTEXT("Restart", "다시 시작")) ] ];
    }
    Overlay = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(SBorder).Padding(24).BorderBackgroundColor(FLinearColor(0.035, 0.06, 0.13, 0.98))
          [ SNew(SBox).WidthOverride(560)[Column] ] ];
    GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 10);
    PC->SetInputMode(FInputModeUIOnly());
}

void AEchoGame::Choose(EEchoUpgrade Upgrade)
{
    if (Phase != EEchoPhase::Choosing || !Choices.Contains(Upgrade)) return;
    switch (Upgrade)
    {
    case EEchoUpgrade::Blaster:
    case EEchoUpgrade::Seeker:
    case EEchoUpgrade::Blades:
    case EEchoUpgrade::Aura:
        ++Ranks[static_cast<int32>(Upgrade)]; break;
    case EEchoUpgrade::MoreEcho: ++EchoCount; break;
    case EEchoUpgrade::Accelerate: ++Acceleration; break;
    case EEchoUpgrade::Health: MaxHP += 3; HP = FMath::Min(MaxHP, HP + 5); break;
    case EEchoUpgrade::Speed: Speed += 35; break;
    case EEchoUpgrade::Pulse: MaxHP += 1; Speed += 15; HP = FMath::Min(MaxHP, HP + 1); break;
    }
    if (Overlay && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
    Overlay.Reset();
    Choices.Reset();
    Phase = EEchoPhase::Running;
    UGameplayStatics::SetGamePaused(this, false);
    if (auto* PC = GetWorld()->GetFirstPlayerController()) PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
}

void AEchoGame::Restart()
{
    if (Phase != EEchoPhase::Won && Phase != EEchoPhase::Lost) return;
    UGameplayStatics::SetGamePaused(this, false);
    UGameplayStatics::OpenLevel(this, FName("/Game/Echo/Arena"));
}

void AEchoGame::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Overlay && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
    Overlay.Reset();
    Super::EndPlay(Reason);
}

void AEchoHUD::DrawEchoTrail(const TArray<FEchoPose>& Path, FLinearColor Color)
{
    if (Path.Num() < 2 || !Canvas->DefaultTexture || !Canvas->DefaultTexture->GetResource()) return;
    TArray<FCanvasUVTri> Triangles;
    Triangles.Reserve((Path.Num() - 1) * 2);
    const double Duration = Path.Last().Time - Path[0].Time;
    const auto ColorAt = [&](double Time)
    {
        const float Age = Duration > 0 ? float((Time - Path[0].Time) / Duration) : 1.f;
        return FLinearColor(Color.R, Color.G, Color.B, FMath::Lerp(0.22f, 0.48f, Age));
    };
    for (int32 I = 1; I < Path.Num(); ++I)
    {
        const FVector Start = Project(Path[I - 1].Position, false), End = Project(Path[I].Position, false);
        if (Start.Z <= 0 || End.Z <= 0) continue;
        const FVector2D A(Start.X, Start.Y), B(End.X, End.Y);
        const FVector2D Direction = (B - A).GetSafeNormal();
        if (Direction.IsNearlyZero()) continue;
        const FVector2D Side(-Direction.Y * 1.5, Direction.X * 1.5);
        const FLinearColor StartColor = ColorAt(Path[I - 1].Time), EndColor = ColorAt(Path[I].Time);
        AddTriangle(Triangles, A - Side, B - Side, B + Side, StartColor, EndColor, EndColor);
        AddTriangle(Triangles, A - Side, B + Side, A + Side, StartColor, EndColor, StartColor);
    }
    if (!Triangles.IsEmpty())
    {
        // Canvas line items are opaque in UE 5.5; translucent triangles retain the trail alpha.
        FCanvasTriangleItem Trail(Triangles, Canvas->DefaultTexture->GetResource());
        Trail.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Trail);
    }
    const FVector Oldest = Project(Path[0].Position, false);
    if (Oldest.Z > 0)
    {
        Color.A = 0.65f;
        FCanvasTileItem Endpoint(FVector2D(Oldest.X - 1.5, Oldest.Y - 1.5), FVector2D(5, 5), Color);
        Endpoint.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Endpoint);
    }
}

void AEchoHUD::DrawAuraRange(const AEchoGame& Game)
{
    const double Radius = Game.GetAuraRadius();
    const AEchoDrone* Drone = Game.GetDrone();
    if (Radius <= 0 || !IsValid(Drone) || !Canvas->DefaultTexture || !Canvas->DefaultTexture->GetResource()) return;

    constexpr int32 SegmentCount = 48;
    constexpr double RingWidth = 8.0;
    const FLinearColor RangeColor(0.22f, 0.60f, 1.f, 0.58f);
    const FLinearColor RangeColor2(0.22f, 0.60f, 1.f, 0.14f);
    const double InnerRadius = Radius - RingWidth;
    FVector GroundCenter = Drone->GetActorLocation();
    GroundCenter.Z = 0.0;
    FVector ProCentor(Project(GroundCenter,false));
    FVector2D Center(ProCentor.X, ProCentor.Y);
    GroundCenter.Z = 0.0;
    FVector OuterPoints[SegmentCount], InnerPoints[SegmentCount];
    for (int32 I = 0; I < SegmentCount; ++I)
    {
        const double Angle = UE_TWO_PI * I / SegmentCount;
        const FVector RadialDirection(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
        OuterPoints[I] = Project(GroundCenter + RadialDirection * Radius, false);
        InnerPoints[I] = Project(GroundCenter + RadialDirection * InnerRadius, false);
    }

    TArray<FCanvasUVTri> Triangles;
    Triangles.Reserve(SegmentCount * 4);
    for (int32 I = 0; I < SegmentCount; ++I)
    {
        const int32 Next = (I + 1) % SegmentCount;
        if (OuterPoints[I].Z <= 0 || OuterPoints[Next].Z <= 0 || InnerPoints[I].Z <= 0 || InnerPoints[Next].Z <= 0) continue;
        const FVector2D OuterStart(OuterPoints[I]), OuterEnd(OuterPoints[Next]);
        const FVector2D InnerStart(InnerPoints[I]), InnerEnd(InnerPoints[Next]);
        AddTriangle(Triangles, Center, InnerStart, InnerEnd, RangeColor2, RangeColor2, RangeColor2);

        AddTriangle(Triangles, OuterStart, OuterEnd, InnerEnd, RangeColor, RangeColor, RangeColor);
        AddTriangle(Triangles, OuterStart, InnerEnd, InnerStart, RangeColor, RangeColor, RangeColor);
    }
    if (Triangles.IsEmpty()) return;
    FCanvasTriangleItem Ring(Triangles, Canvas->DefaultTexture->GetResource());
    Ring.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Ring);
}

void AEchoHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* Game = GetWorld()->GetAuthGameMode<AEchoGame>();
    if (!Canvas || !Game) return;
    DrawAuraRange(*Game);
    TArray<FEchoPose> Path;
    Game->GetFirstEchoPath(Path);
    DrawEchoTrail(Path, FLinearColor(0.53f, 0.33f, 0.91f));
    if (Game->GetEchoesNum() > 1)
    {
        Game->GetFirstEchoPath(Path, 1);
        DrawEchoTrail(Path, FLinearColor(0.95f, 0.55f, 0.31f));
    }
    if (Game->GetEchoesNum() > 2)
    {
        Game->GetFirstEchoPath(Path, 2);
        DrawEchoTrail(Path, FLinearColor(0.19f, 0.84f, 0.63f));
    }
    const float Scale = FMath::Min(1.f, Canvas->SizeX / 800.f);
    const float X = 24 * Scale, Y = 20 * Scale;
    // Use the runtime composite font (including its Korean fallback), not the offline Canvas font.
    const auto Label = [this, Scale](const FText& Text, float Left, float Top, int32 Size, FLinearColor Color)
    {
        FCanvasTextItem Item(FVector2D(Left, Top), Text, HUDTextFont(Size), Color);
        Item.Scale = FVector2D(Scale);
        Item.EnableShadow(FLinearColor::Black);
        Canvas->DrawItem(Item);
    };
    const int32 T = FMath::Clamp(FMath::CeilToInt(240 - Game->Elapsed), 0, 240);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    static const bool bShowEnemyCount = FParse::Param(FCommandLine::Get(), TEXT("EchoWaveCheck"));
    const float PanelHeight = bShowEnemyCount ? 140.f : 128.f;
#else
    constexpr float PanelHeight = 128.f;
#endif
    DrawRect(FLinearColor(0.012, 0.023, 0.045, 0.88), X-10*Scale, Y-8*Scale, 520*Scale, PanelHeight*Scale);
    Label(FText::Format(LOCTEXT("Time", "에코 어썰트   남은 시간 {0}"), FText::AsTimespan(FTimespan::FromSeconds(T))), X, Y, 20, FLinearColor::White);
    DrawRect(FLinearColor(0.18,0.22,0.28), X, Y+38*Scale, 490*Scale, 6*Scale);
    DrawRect(FLinearColor(0.1,0.85,0.9), X, Y+38*Scale, 490*Scale * Game->HP / Game->MaxHP, 6*Scale);
    Label(FText::Format(LOCTEXT("Stats", "체력 {0}/{1}   레벨 {2}   경험치 {3}/{4}"),
        FText::AsNumber(FMath::RoundToInt(Game->HP)), FText::AsNumber(FMath::RoundToInt(Game->MaxHP)),
        FText::AsNumber(Game->Level), FText::AsNumber(Game->XP), FText::AsNumber(Game->Threshold())), X, Y+52*Scale, 14, FLinearColor::White);
    Label(FText::Format(LOCTEXT("Kills", "처치 {0}   분신 처치 {1}"), FText::AsNumber(Game->Kills), FText::AsNumber(Game->EchoKills)),
        X, Y+84*Scale, 14, FLinearColor(0.7,0.85,1));
    if (Game->Elapsed < 8) Label(LOCTEXT("Controls", "WASD 이동 / 마우스 조준 / 자동 사격"), X, Canvas->SizeY-45*Scale, 16, FLinearColor::White);
    if (Game->Elapsed >= 210 && Game->Phase == EEchoPhase::Running) Label(LOCTEXT("FinalWave", "최종 웨이브"), X, Y+135*Scale, 22, FLinearColor(1,0.55,0.12));
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (bShowEnemyCount)
    {
        Label(FText::Format(LOCTEXT("WaveCheckEnemyCount", "생존 적 {0} / 목표 {1}"),
            FText::AsNumber(Game->GetAliveEnemyCount()), FText::AsNumber(Game->GetEnemyPopulationTarget())),
            X, Y+104*Scale, 14, FLinearColor(0.7,0.85,1));
    }
    if (AEchoGame::IsReplayCheckEnabled())
    {
        const float CheckY = Canvas->SizeY - 98 * Scale;
        DrawRect(FLinearColor(0.012, 0.023, 0.045, 0.88), X-10*Scale, CheckY-5*Scale, 620*Scale, 36*Scale);
        Label(FText::FromString(Game->ReplayCheckStatus()), X, CheckY, 15, FLinearColor::White);
    }
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoHUDCanvasFontTest, "EchoAssault.UI.CanvasFont", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoHUDCanvasFontTest::RunTest(const FString&)
{
    const auto Info = HUDTextFont(14);
    FCanvasTextItem Item(FVector2D::ZeroVector, LOCTEXT("FontProbe", "체력 레벨 경험치"), Info, FLinearColor::White);
    if (!TestNotNull(TEXT("Canvas requires a UFont to accept HUD text"), Item.Font)) return false;
    TestTrue(TEXT("HUD font uses runtime glyph caching for Korean fallback"), Item.Font->FontCacheType == EFontCacheType::Runtime);
    TestTrue(TEXT("HUD retains the Slate composite font"), Info.GetCompositeFont() != nullptr);
    return true;
}
#endif

#undef LOCTEXT_NAMESPACE
