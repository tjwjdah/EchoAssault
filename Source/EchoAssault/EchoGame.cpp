#include "EchoGame.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"
CSV_DEFINE_CATEGORY(EchoCrowd, true);
#endif

namespace
{
    constexpr float ArenaEdge = 1750.f;
    constexpr float FlightHeight = 50.f;
    const FLinearColor Ice(0.12f, 0.85f, 1.f), Ghost(0.52f, 0.26f, 1.f), Red(1.f, 0.16f, 0.10f);
    const FLinearColor Gold(1.f, 0.61f, 0.15f), Mint(0.13f, 1.f, 0.57f);

    int32 GetTargetEnemyCount(double Elapsed) {
        if (Elapsed < 30.f) {
            return FMath::Lerp(0, 100, Elapsed / 30);
        }
        else if (Elapsed >= 30.f && Elapsed < 60.f) {
            return 100.f;
        }
        else if (Elapsed >= 60.f && Elapsed < 90.f) {
            return FMath::Lerp(100, 200, (Elapsed - 60.f) / 30);
        }
        else if (Elapsed >= 90.f && Elapsed < 120.f) {
            return 200.f;
        }
        else if (Elapsed >= 120.f && Elapsed < 150.f) {
            return  FMath::Lerp(200, 300, (Elapsed - 120.f) / 30);
        }
        else if (Elapsed >= 150.f && Elapsed < 180.f) {
            return  300.f;
        }
        else if (Elapsed >= 180.f && Elapsed < 210.f) {
            return  FMath::Lerp(300, 400, (Elapsed - 180.f) / 30);
        }
        return  500.f;
    }
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    constexpr int32 SustainedCrowdSize = 500;
    constexpr int32 PerfRangedTarget = 100;

    bool IsCrowdCombatBenchmark()
    {
        static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyCrowdPreview")) &&
            FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyCrowdBenchmark"));
        return bEnabled;
    }

    bool IsSustainedCrowdBenchmark()
    {
        static const bool bEnabled = IsCrowdCombatBenchmark() && FParse::Param(FCommandLine::Get(), TEXT("EchoCrowdSustain"));
        return bEnabled;
    }

    bool IsPerfCheck()
    {
        static const bool bEnabled = IsSustainedCrowdBenchmark() && FParse::Param(FCommandLine::Get(), TEXT("EchoPerfCheck"));
        return bEnabled;
    }

    FVector CrowdRingOffset(int32 Index, int32 EnemyCount)
    {
        double Radius = 500;
        for (int32 Ring = 0; EnemyCount > 0; ++Ring, Radius += 110)
        {
            const int32 RingCount = FMath::Min(EnemyCount, FMath::FloorToInt(UE_DOUBLE_TWO_PI * Radius / 95));
            if (Index < RingCount)
            {
                const double Angle = (Index + 0.5 * (Ring % 2)) * UE_DOUBLE_TWO_PI / RingCount;
                return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);
            }
            Index -= RingCount;
            EnemyCount -= RingCount;
        }
        return FVector::ZeroVector;
    }
#endif

    void Style(UStaticMeshComponent* Body, const TCHAR* MeshPath, FLinearColor Color, FVector Scale = FVector::OneVector)
    {
        Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
        auto* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Echo/Hull.Hull"));
        if (Base)
        {
            auto* Mat = UMaterialInstanceDynamic::Create(Base, Body);
            Mat->SetVectorParameterValue("Tint", Color);
            Body->SetMaterial(0, Mat);
        }
        Body->SetRelativeScale3D(Scale);
    }

    AStaticMeshActor* Decoration(UWorld* World, FVector Where, const TCHAR* Shape, FLinearColor Color, FVector Scale)
    {
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Actor = World->SpawnActor<AStaticMeshActor>(Where, FRotator::ZeroRotator, Spawn);
        if (!IsValid(Actor)) return nullptr;
        auto* Body = Actor->GetStaticMeshComponent();
        Body->SetMobility(EComponentMobility::Movable);
        Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Style(Body, Shape, Color, Scale);
        return Actor;
    }
    
    AEchoBolt* Launch(UWorld* World, FVector From, FVector Direction, float Speed, float Damage, int32 Slot, FLinearColor Color, FVector Scale)
    {
        FTransform Transform(Direction.Rotation(), From);
        auto* Bolt = World->SpawnActorDeferred<AEchoBolt>(AEchoBolt::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!IsValid(Bolt)) return nullptr;
        Bolt->Damage = Damage;
        Bolt->OwnerSlot = Slot;
        Bolt->bEnemy = Slot < 0;
        Bolt->Flight->bInitialVelocityInLocalSpace = false;
        Bolt->Flight->Velocity = Direction * Speed;
        Bolt->Flight->MaxSpeed = Speed;
        Bolt->BoltColor = Color;
        auto* Game = World->GetAuthGameMode<AEchoGame>();
        //Style(Bolt->Body, TEXT("/Engine/BasicShapes/Sphere.Sphere"), Color, Scale);
        Bolt->FinishSpawning(Transform);
        if (!(IsValid(Bolt) && !Bolt->IsActorBeingDestroyed()))
            return nullptr;
        else
        {
            Bolt->IsmScale = Scale;
            Transform.SetScale3D(Bolt->IsmScale);
            if (Bolt)
                Bolt->ID = Game->GetIsmManager()->AddInstance(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")), Bolt,Transform, Bolt->BoltColor);
            return Bolt;
        }
        
    }
}

AEchoGame::AEchoGame()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AEchoDrone::StaticClass();
    HUDClass = AEchoHUD::StaticClass();

    auto* Bounds = CreateDefaultSubobject<USceneComponent>(TEXT("ArenaBounds"));
    Bounds->SetMobility(EComponentMobility::Static);
    SetRootComponent(Bounds);

    constexpr double WallHalfThickness = 60.0;
    constexpr double WallCenter = ArenaEdge + EchoEnemyCollisionRadius + WallHalfThickness;
    constexpr double WallHalfSpan = WallCenter + WallHalfThickness;
    const auto AddWall = [this, Bounds](FName Name, FVector Center, FVector Extent)
    {
        auto* Wall = CreateDefaultSubobject<UBoxComponent>(Name);
        Wall->SetupAttachment(Bounds);
        Wall->SetMobility(EComponentMobility::Static);
        Wall->SetRelativeLocation(Center);
        Wall->InitBoxExtent(Extent);
        Wall->SetCollisionObjectType(EchoEnemyCollisionChannel);
        Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
        Wall->SetCollisionResponseToChannel(EchoEnemyCollisionChannel, ECR_Block);
        Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Wall->SetGenerateOverlapEvents(false);
    };
    AddWall(TEXT("EnemyEastWall"), FVector(WallCenter, 0, FlightHeight), FVector(WallHalfThickness, WallHalfSpan, 200));
    AddWall(TEXT("EnemyWestWall"), FVector(-WallCenter, 0, FlightHeight), FVector(WallHalfThickness, WallHalfSpan, 200));
    AddWall(TEXT("EnemyNorthWall"), FVector(0, WallCenter, FlightHeight), FVector(WallHalfSpan, WallHalfThickness, 200));
    AddWall(TEXT("EnemySouthWall"), FVector(0, -WallCenter, FlightHeight), FVector(WallHalfSpan, WallHalfThickness, 200));
}

void AEchoGame::BeginPlay()
{
    Super::BeginPlay();
    const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
    Decoration(GetWorld(), FVector(0, 0, -15), Cube, FLinearColor(0.045, 0.075, 0.11), FVector(36, 36, 0.25));
    for (int32 Line = -4; Line <= 4; ++Line)
    {
        const float Offset = Line * 440.f;
        const FLinearColor Ink = FMath::Abs(Line) == 4 ? Ice : FLinearColor(0.08, 0.17, 0.21);
        Decoration(GetWorld(), FVector(Offset, 0, 0), Cube, Ink, FVector(0.045, 35.25, 0.018));
        Decoration(GetWorld(), FVector(0, Offset, 0), Cube, Ink, FVector(35.25, 0.045, 0.018));
    }
    IsmManager = GetWorld()->SpawnActor<AIsmManager>(AIsmManager::StaticClass());
     
    for (int32 I = 0; I < 3; ++I) Clocks[I].Delay = (I + 1) * 8.0;
}

void AEchoGame::Tick(float Delta)
{
    Super::Tick(Delta);
    if (Phase != EEchoPhase::Running || Delta <= 0) return;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    const bool bPerfCheck = IsPerfCheck();
#endif
    if (!Drone)
    {
        Drone = Cast<AEchoDrone>(UGameplayStatics::GetPlayerPawn(this, 0));
        if (!Drone) return;
        Drone->SetActorLocation(FVector(0, 0, FlightHeight));
        Style(Drone->Body, TEXT("/Game/Echo/Scout.Scout"), Ice);
        auto* PC = CastChecked<APlayerController>(Drone->GetController());
        PC->bShowMouseCursor = true;
        PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
        const bool bFinalWaveCheck = !IsCrowdCombatBenchmark() && FParse::Param(FCommandLine::Get(), TEXT("EchoWaveCheckFinal"));
        if (bFinalWaveCheck || FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyCrowdPreview")))
        {
            constexpr int32 DirectionCount = 6;
            constexpr int32 UnitsPerDirection = 4;
            constexpr int32 SmallCrowdCount = DirectionCount * UnitsPerDirection;
            int32 PreviewCount = SmallCrowdCount;
            FParse::Value(FCommandLine::Get(), TEXT("EchoEnemyCrowdCount="), PreviewCount);
            PreviewCount = FMath::Clamp(PreviewCount, SmallCrowdCount, 500);
            const bool bSustainedBenchmark = IsSustainedCrowdBenchmark();
            if (IsCrowdCombatBenchmark())
            {
                FMath::RandInit(17543);
                HP = MaxHP = 512;
                Level = 2048;
                Ranks[static_cast<int32>(EEchoWeapon::Seeker)] = 1;
                Ranks[static_cast<int32>(EEchoWeapon::Blades)] = 1;
            }
            if (bFinalWaveCheck)
            {
                Elapsed = 205.0;
                PreviewCount = GetTargetEnemyCount(Elapsed);
                HP = MaxHP = 60;
                Level = 2048;
            }
            else if (bSustainedBenchmark)
            {
                PreviewCount = SustainedCrowdSize;
                Elapsed = 180;
                Level = 10000; // Keep the level-up menu from pausing a sustained capture.
                EchoCount = UE_ARRAY_COUNT(Clocks);
                Acceleration = 2;
                for (int32& Rank : Ranks) Rank = 3;
            }

            const double PlayTime = Elapsed;
            if (PreviewCount > SmallCrowdCount && !bSustainedBenchmark && !bFinalWaveCheck) Elapsed = 30;
            const double SpawnTimeOffset = Elapsed - PlayTime;
            const int32 BeforePreview = Enemies.Num();
            int32 RangedCount = 0;
            for (int32 Index = 0; Index < PreviewCount; ++Index)
            {
                const int32 BeforeSpawn = Enemies.Num();
                SpawnEnemy(bPerfCheck ? TOptional<bool>(Index % (SustainedCrowdSize / PerfRangedTarget) == 0) : TOptional<bool>());
                if (Enemies.Num() == BeforeSpawn) break;
                FVector Offset;
                if (PreviewCount == SmallCrowdCount)
                {
                    const int32 Direction = Index / UnitsPerDirection;
                    const int32 Member = Index % UnitsPerDirection;
                    const double Angle = Direction * UE_DOUBLE_TWO_PI / DirectionCount + (Member % 2 == 0 ? -0.14 : 0.14);
                    const double Radius = 500.0 + (Direction % 3) * 30.0 + (Member / 2) * 130.0;
                    Offset = FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);
                }
                else
                {
                    Offset = CrowdRingOffset(Index, PreviewCount);
                }
                auto* Unit = Enemies.Last().Get();
                if (Unit->bRanged) ++RangedCount;
                Unit->NextAttack += -SpawnTimeOffset + (PreviewCount > SmallCrowdCount ? (Index % 7) * 0.09 : 0.0);
                Unit->SetActorLocation(Drone->GetActorLocation() + Offset, false, nullptr, ETeleportType::TeleportPhysics);
            }
            Elapsed = PlayTime;
            NextSpawn -= SpawnTimeOffset;
            UE_LOG(LogTemp, Display, TEXT("EchoCrowdPreview: spawned=%d requested=%d ranged=%d layout=%s"),
                Enemies.Num() - BeforePreview, PreviewCount, RangedCount, PreviewCount == SmallCrowdCount ? TEXT("sectors") : TEXT("rings"));
        }
#endif
        History.Poses.Add({Elapsed, Drone->GetActorLocation(), 0});
    }
    Elapsed += Delta;

    
    if (Elapsed < SafeUntil) {
        constexpr float BlinkInterval = 0.1f;
        BlinkElapsed += Delta;

        while (BlinkElapsed >= BlinkInterval)
        {
            BlinkElapsed -= BlinkInterval;
            Drone->Body->SetVisibility(!Drone->Body->IsVisible());
        }
    }
    else
    {
        BlinkElapsed = 0.f;
        if (!Drone->Body->IsVisible())
            Drone->Body->SetVisibility(true);
    }
    if (Elapsed >= 240) { Transition(EEchoPhase::Won); return; }

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    const FVector BeforeMove = Drone->GetActorLocation();
#endif
    MoveAndAim(Delta);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    static const bool bFlankCheck = FParse::Param(FCommandLine::Get(), TEXT("EchoFlankCheck"));
    if (bFlankCheck) RelocateTrailingEnemies(Drone->GetActorLocation() - BeforeMove);
    if (bPerfCheck) PerfTravelCm += FVector::Dist2D(BeforeMove, Drone->GetActorLocation());
#endif
    History.Poses.Add({Elapsed, Drone->GetActorLocation(), static_cast<float>(Drone->GetActorRotation().Yaw)});
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (IsSustainedCrowdBenchmark())
    {
        if (!bPerfCheck && Enemies.Num() < SustainedCrowdSize)
        {
            CSV_SCOPED_TIMING_STAT(EchoCrowd, EnemyRefill);
            SpawnEnemy();
        }
    }
    else
#endif
    if (Elapsed >= NextSpawn && Enemies.Num() < GetTargetEnemyCount(Elapsed)) SpawnEnemy();
    RecordWeapons();
    ReplayEchoes(Delta);
    History.Trim(Elapsed);
    UpdateEnemies(Delta);
    if (Phase == EEchoPhase::Running) UpdateCrystals(Delta);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (IsCrowdCombatBenchmark())
    {
        if (bPerfCheck)
        {
            int32 RangedAlive = 0;
            for (const AEchoUnit* Unit : Enemies)
            {
                if (IsValid(Unit) && Unit->bRanged) ++RangedAlive;
            }
            if (Enemies.Num() < SustainedCrowdSize)
            {
                CSV_SCOPED_TIMING_STAT(EchoCrowd, EnemyRefill);
                const int32 BeforeSpawn = Enemies.Num();
                const bool bSpawnRanged = RangedAlive < PerfRangedTarget;
                SpawnEnemy(bSpawnRanged);
                if (bSpawnRanged && Enemies.Num() > BeforeSpawn) ++RangedAlive;
            }
            int32 Width = 0, Height = 0;
            CastChecked<APlayerController>(Drone->GetController())->GetViewportSize(Width, Height);
            CSV_CUSTOM_STAT(EchoCrowd, ViewportWidth, Width, ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, ViewportHeight, Height, ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, RangedAlive, RangedAlive, ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, Echo1Shots, PerfEchoShots[0], ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, Echo2Shots, PerfEchoShots[1], ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, Echo3Shots, PerfEchoShots[2], ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, TravelCm, static_cast<float>(PerfTravelCm), ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, PlayerHits, PerfPlayerHits, ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, PlayerHP, HP, ECsvCustomStatOp::Set);
            int32 MinRank = 3;
            for (int32 Rank : Ranks) MinRank = FMath::Min(MinRank, Rank);
            CSV_CUSTOM_STAT(EchoCrowd, MinRank, MinRank, ECsvCustomStatOp::Set);
        }
        CSV_CUSTOM_STAT(EchoCrowd, Alive, Enemies.Num(), ECsvCustomStatOp::Set);
        CSV_CUSTOM_STAT(EchoCrowd, SimSeconds, static_cast<float>(Elapsed), ECsvCustomStatOp::Set);
        if (IsSustainedCrowdBenchmark())
        {
            CSV_CUSTOM_STAT(EchoCrowd, Echoes, Echoes.Num(), ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, Kills, Kills, ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, EchoKills, EchoKills, ECsvCustomStatOp::Set);
            CSV_CUSTOM_STAT(EchoCrowd, Crystals, Crystals.Num(), ECsvCustomStatOp::Set);
        }
        static const bool bCheckPlane = FParse::Param(FCommandLine::Get(), TEXT("EchoCrowdCheckPlane"));
        if (bCheckPlane)
        {
            double MaxHeightError = 0;
            for (const AEchoUnit* Unit : Enemies)
            {
                if (IsValid(Unit)) MaxHeightError = FMath::Max(MaxHeightError, FMath::Abs(Unit->GetActorLocation().Z - FlightHeight));
            }
            CSV_CUSTOM_STAT(EchoCrowd, MaxHeightErrorCm, static_cast<float>(MaxHeightError), ECsvCustomStatOp::Set);
        }
    }
#endif
}

void AEchoGame::MoveAndAim(float Delta)
{
    auto* PC = CastChecked<APlayerController>(Drone->GetController());
    FVector Input(PC->IsInputKeyDown(EKeys::W) - PC->IsInputKeyDown(EKeys::S),
                  PC->IsInputKeyDown(EKeys::D) - PC->IsInputKeyDown(EKeys::A), 0);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (IsCrowdCombatBenchmark())
    {
        static const bool bStationary = IsPerfCheck() && FParse::Param(FCommandLine::Get(), TEXT("EchoPerfCheckStationary"));
        Input = bStationary ? FVector::ZeroVector : FRotator(0, Elapsed * 45 + 90, 0).Vector();
    }
#endif
    FVector Pos = Drone->GetActorLocation() + Input.GetSafeNormal() * Speed * Delta;
    Pos.X = FMath::Clamp(Pos.X, -double(ArenaEdge), double(ArenaEdge));
    Pos.Y = FMath::Clamp(Pos.Y, -double(ArenaEdge), double(ArenaEdge));
    Drone->SetActorLocation(Pos, true);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (IsCrowdCombatBenchmark())
    {
        Drone->SetActorRotation(FRotator(0, Elapsed * 45, 0));
        return;
    }
#endif
    FVector Ray, Dir;
    if (PC->DeprojectMousePositionToWorld(Ray, Dir) && Dir.Z < -UE_SMALL_NUMBER)
    {
        const FVector Aim = Ray + Dir * ((Pos.Z - Ray.Z) / Dir.Z) - Pos;
        if (!Aim.IsNearlyZero()) Drone->SetActorRotation(FRotator(0, Aim.Rotation().Yaw, 0));
    }
}

AEchoUnit* AEchoGame::Nearest(FVector From) const
{
    AEchoUnit* Best = nullptr;
    double BestDist = TNumericLimits<double>::Max();
    for (AEchoUnit* Unit : Enemies)
    {
        if (!IsValid(Unit) || Unit->IsActorBeingDestroyed()) continue;
        const double Dist = FVector::DistSquared(From, Unit->GetActorLocation());
        if (Dist < BestDist) { BestDist = Dist; Best = Unit; }
    }
    return Best;
}

void AEchoGame::RecordWeapons()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    CSV_SCOPED_TIMING_STAT(EchoCrowd, BodyWeapons);
#endif
    constexpr double Cooldowns[] = {0.34, 1.55, 2.7, 0.75};
    static_assert(UE_ARRAY_COUNT(Cooldowns) == static_cast<int32>(EEchoWeapon::End));
    const FVector Position = Drone->GetActorLocation();
    for (int32 I = 0; I < static_cast<int32>(EEchoWeapon::End); ++I)
    {
        const int32 Rank = Ranks[I];
        if (!Rank || Elapsed < NextShot[I]) continue;
        const auto Weapon = static_cast<EEchoWeapon>(I);
        if (Weapon == EEchoWeapon::Aura)
        {
            Fire({Elapsed, Position, FVector::ZeroVector, Weapon, Rank}, 0);
            NextShot[I] = Elapsed + Cooldowns[I];
            continue;
        }
        FVector Direction = Drone->GetActorForwardVector();
        if (Weapon == EEchoWeapon::Seeker)
        {
            const auto* Target = Nearest(Position);
            if (!Target) continue;
            Direction = (Target->GetActorLocation() - Position).GetSafeNormal2D();
        }
        const FVector Origin = Position + Direction * (Weapon == EEchoWeapon::Blades ? 0 : 40);
        const FEchoShot Shot{Elapsed, Origin, Direction, Weapon, Rank};
        History.Shots.Add(Shot);
        Fire(Shot, 0);
        NextShot[I] = Elapsed + Cooldowns[I] / (1 + 0.12 * (Rank - 1));
    }
}

double AEchoGame::GetAuraRadius() const
{
    const int32 Rank = Ranks[static_cast<int32>(EEchoWeapon::Aura)];
    return Rank > 0 ? 180.0 + 40.0 * (Rank - 1) : 0.0;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
int32 AEchoGame::GetEnemyPopulationTarget() const
{
    return GetTargetEnemyCount(Elapsed);
}

bool AEchoGame::IsReplayCheckEnabled()
{
    static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("EchoReplayCheck"));
    return bEnabled;
}

FString AEchoGame::ReplayCheckStatus() const
{
    FString DelayText;
    for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Clocks); ++Slot)
    {
        if (Slot > 0) DelayText += TEXT(" / ");
        DelayText += Clocks[Slot].bStarted ? FString::Printf(TEXT("%.2f"), Clocks[Slot].Delay) : TEXT("-");
    }
    return FString::Printf(TEXT("블래스터 %d   유도탄 %d   칼날 %d   |   가속 %d   지연 %s초"),
        Ranks[static_cast<int32>(EEchoWeapon::Blaster)], Ranks[static_cast<int32>(EEchoWeapon::Seeker)],
        Ranks[static_cast<int32>(EEchoWeapon::Blades)], Acceleration, *DelayText);
}
#endif

void AEchoGame::Fire(const FEchoShot& Shot, int32 Slot)
{
    if (Shot.Weapon == EEchoWeapon::Aura)
    {
        if (Slot != 0) return;
        const double Radius = GetAuraRadius();
        if (Radius <= 0) return;
        const double RadiusSquared = FMath::Square(Radius);
        TArray<AEchoUnit*> Targets;
        for (AEchoUnit* Unit : Enemies)
        {
            if (!IsValid(Unit) || Unit->IsActorBeingDestroyed()) continue;
            if (FVector::DistSquared(Shot.Origin, Unit->GetActorLocation()) < RadiusSquared) Targets.Add(Unit);
        }
        // Lethal hits remove entries from Enemies, so collect targets before applying damage.
        for (AEchoUnit* Unit : Targets) HitUnit(Unit, Shot.Rank, Slot);
        return;
    }
    const bool bBlades = Shot.Weapon == EEchoWeapon::Blades;
    const bool bSeek = Shot.Weapon == EEchoWeapon::Seeker;
    const int32 Count = bBlades ? 4 + Shot.Rank * 2 : (bSeek && Shot.Rank == 3 ? 2 : 1);
    const int32 Damage = Shot.Rank + (bSeek ? 1 : 0);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (IsPerfCheck() && Slot > 0 && Slot <= UE_ARRAY_COUNT(PerfEchoShots)) ++PerfEchoShots[Slot - 1];
    if (IsReplayCheckEnabled())
    {
        UE_LOG(LogTemp, Display, TEXT("EchoReplayCheck shot=%.6f now=%.6f slot=%d weapon=%d rank=%d origin=(%.3f,%.3f,%.3f) aim=(%.6f,%.6f,%.6f) count=%d damage=%d"),
            Shot.Time, Elapsed, Slot, static_cast<int32>(Shot.Weapon), Shot.Rank,
            Shot.Origin.X, Shot.Origin.Y, Shot.Origin.Z, Shot.Direction.X, Shot.Direction.Y, Shot.Direction.Z, Count, Damage);
    }
#endif
    const FLinearColor Color = Slot ? Ghost : (bBlades ? Gold : (bSeek ? Mint : Ice));
    for (int32 I = 0; I < Count; ++I)
    {
        const float Angle = bBlades ? 360.f * I / Count : (Count == 2 ? I * 20.f - 10.f : 0.f);
        const FVector Direction = Shot.Direction.RotateAngleAxis(Angle, FVector::UpVector);
        AEchoBolt* Bolt = Launch(GetWorld(), Shot.Origin, Direction, bSeek ? 600 : 930, Damage, Slot, Color,
            bBlades ? FVector(0.27, 0.24, 0.08) : FVector(0.35, 0.14, 0.14));
        if (bSeek && IsValid(Bolt))
        {
            if (auto* Current = Nearest(Shot.Origin))
            {
                Bolt->Flight->HomingTargetComponent = Current->Sphere;
                Bolt->Flight->bIsHomingProjectile = true;
                Bolt->Flight->HomingAccelerationMagnitude = 3200;
            }
        }
    }
}

void AEchoGame::GetFirstEchoPath(TArray<FEchoPose>& Out,int32 i) const
{
    const auto& Clock = Clocks[i];
    const double From = Clock.bStarted ? Clock.Previous : FMath::Max(0.0, Elapsed - Clock.Delay);
    if(i==0)
    History.PathBetween(From, Elapsed, Out);
    else
        History.PathBetween(From, Elapsed- Clocks[i-1].Delay, Out);
}

void AEchoGame::ReplayEchoes(float Delta)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    CSV_SCOPED_TIMING_STAT(EchoCrowd, EchoReplay);
#endif
    double Farthest = 0;
    for (int32 I = 0; I < EchoCount; ++I)
    {
        if (I == 0 && Elapsed < 8.0) continue;
        auto& Clock = Clocks[I];
        Clock.Advance(History, Elapsed, Delta, 8.0 * (I + 1) - Acceleration, [&](const FEchoShot& Shot) { Fire(Shot, I + 1); });
        if (!Clock.bStarted) continue;
        const auto Pose = History.PoseAt(Clock.Previous);
        if (Echoes.Num() <= I) Echoes.SetNum(I + 1);
        if (!Echoes[I]) Echoes[I] = Decoration(GetWorld(), Pose.Position, TEXT("/Game/Echo/Scout.Scout"), Ghost, FVector(0.92));
        if (!IsValid(Echoes[I])) continue;
        Echoes[I]->SetActorLocationAndRotation(Pose.Position, FRotator(0, Pose.Yaw, 0));
        Farthest = FMath::Max(Farthest, FVector::Dist(Pose.Position, Drone->GetActorLocation()));
    }
    const float Distance = 1650.f + FMath::Clamp(float(Farthest - 400) * 0.35f, 0.f, 330.f);
    Drone->Arm->TargetArmLength = FMath::FInterpTo(Drone->Arm->TargetArmLength, Distance, Delta, 1.8f);
}


void AEchoGame::SpawnEnemy(TOptional<bool> RangedOverride)
{
    const FVector Player = Drone->GetActorLocation();
    FVector Pos;
    for (int32 Attempt = 0; Attempt < 8; ++Attempt)
    {
        const float Angle = FMath::FRandRange(0, 2 * PI);
        Pos = Player + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 1030;
        Pos.X = FMath::Clamp(Pos.X, -double(ArenaEdge), double(ArenaEdge));
        Pos.Y = FMath::Clamp(Pos.Y, -double(ArenaEdge), double(ArenaEdge));
        if (FVector::DistSquared(Pos, Player) > FMath::Square(650.f)) break;
    }
    const FTransform Transform((Player - Pos).Rotation(), Pos);
    auto* Unit = GetWorld()->SpawnActorDeferred<AEchoUnit>(AEchoUnit::StaticClass(), Transform, nullptr, nullptr,
                                                         ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!IsValid(Unit))
    {
        UE_LOG(LogTemp, Warning, TEXT("Echo Assault: enemy spawn failed at %s; retry in 0.5 seconds."), *Pos.ToString());
        NextSpawn = Elapsed + 0.5;
        return;
    }
    Unit->bRanged = RangedOverride.IsSet() ? RangedOverride.GetValue() : Elapsed > 25 && FMath::FRand() < 0.23f;
    Unit->Health = Unit->bRanged ? 3 : 2 + FMath::FloorToInt(Elapsed / 100);
    Unit->NextAttack = Elapsed + 1.7;
    Style(Unit->Body, Unit->bRanged ? TEXT("/Game/Echo/Lancer.Lancer") : TEXT("/Game/Echo/Hunter.Hunter"), Unit->bRanged ? Gold : Red);
    // Finishing the spawn can immediately overlap an existing projectile and kill this unit.
    Unit->FinishSpawning(Transform);
    if (IsValid(Unit) && !Unit->IsActorBeingDestroyed()) Enemies.Add(Unit);
    NextSpawn = Elapsed + (Elapsed >= 210.0 ? 0.015 : 0.03);
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void AEchoGame::RelocateTrailingEnemies(const FVector& Movement)
{
    if (Elapsed < 60.0 || Elapsed < NextEnemyRelocation || Movement.IsNearlyZero()) return;
    NextEnemyRelocation = Elapsed + 0.5;

    const auto* PC = CastChecked<APlayerController>(Drone->GetController());
    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    if (Width <= 0 || Height <= 0) return;

    const FVector2D Margin(Width * 0.08, Height * 0.08);
    const FVector2D ScreenLimit = FVector2D(Width, Height) + Margin;
    const auto IsOffscreen = [PC, Margin, ScreenLimit](const FVector& Position)
    {
        FVector2D Screen;
        return PC->ProjectWorldLocationToScreen(Position, Screen, true) &&
            (Screen.X < -Margin.X || Screen.Y < -Margin.Y || Screen.X > ScreenLimit.X || Screen.Y > ScreenLimit.Y);
    };

    constexpr int32 MaxRelocations = 8;
    constexpr double RearDistance = 1400.0;
    constexpr double FlankDistance = 1500.0;
    constexpr double FlankAngles[] = {35, -35, 60, -60, 78, -78};
    const double FirstSide = FMath::RandBool() ? 1.0 : -1.0;
    const FVector Player = Drone->GetActorLocation();
    const FVector Direction = Movement.GetSafeNormal2D();
    const FCollisionObjectQueryParams EnemyObjects(EchoEnemyCollisionChannel);
    const FCollisionShape Space = FCollisionShape::MakeSphere(EchoEnemyCollisionRadius + 5.f);
    int32 Moved = 0, Candidate = 0;
    for (int32 I = Enemies.Num() - 1; I >= 0 && Moved < MaxRelocations && Candidate < UE_ARRAY_COUNT(FlankAngles); --I)
    {
        if (!Enemies.IsValidIndex(I)) continue;
        auto* Unit = Enemies[I].Get();
        if (!IsValid(Unit) || Unit->IsActorBeingDestroyed()) continue;
        const FVector OldPosition = Unit->GetActorLocation();
        const double Behind = FVector::DotProduct(Player - OldPosition, Direction);
        if (Behind < RearDistance || !IsOffscreen(OldPosition)) continue;

        while (Candidate < UE_ARRAY_COUNT(FlankAngles))
        {
            const double Angle = FlankAngles[Candidate++] * FirstSide;
            FVector Destination = Player + Direction.RotateAngleAxis(Angle, FVector::UpVector) * FlankDistance;
            Destination.Z = FlightHeight;
            if (FMath::Abs(Destination.X) > ArenaEdge || FMath::Abs(Destination.Y) > ArenaEdge || !IsOffscreen(Destination)) continue;
            if (GetWorld()->OverlapAnyTestByObjectType(Destination, FQuat::Identity, EnemyObjects, Space)) continue;
            if (!Unit->SetActorLocation(Destination, false, nullptr, ETeleportType::TeleportPhysics)) break;
            if (IsValid(Unit) && !Unit->IsActorBeingDestroyed() && Unit->Sphere->IsSimulatingPhysics())
                Unit->Sphere->SetPhysicsLinearVelocity(FVector::ZeroVector);
            ++Moved;
            UE_LOG(LogTemp, Display, TEXT("EchoFlankCheck: time=%.3f rear_cm=%.1f angle=%.0f destination=(%.1f,%.1f)"),
                Elapsed, Behind, Angle, Destination.X, Destination.Y);
            break;
        }
    }
}
#endif

void AEchoGame::UpdateEnemies(float Delta)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    CSV_SCOPED_TIMING_STAT(EchoCrowd, EnemyUpdate);
#endif
    const FVector Player = Drone->GetActorLocation();
    for (int32 I = Enemies.Num() - 1; I >= 0 && Phase == EEchoPhase::Running; --I)
    {
        // Projectile overlaps can remove an enemy synchronously during movement.
        if (!Enemies.IsValidIndex(I)) continue;
        auto* Unit = Enemies[I].Get();
        if (!IsValid(Unit) || Unit->IsActorBeingDestroyed()) { Enemies.RemoveAtSwap(I); continue; }
        const FVector Offset = Player - Unit->GetActorLocation();
        const FVector Dir = Offset.GetSafeNormal2D();
        const float Range = Offset.Size2D();
        const float SpeedNow = Unit->bRanged ? 125 : 145 + Elapsed * 0.22;
        const float Step = FMath::Min(SpeedNow * Delta, FMath::Max(0.f, Range - (Unit->bRanged ? 480.f : 28.f)));
        if (Unit->Sphere->IsSimulatingPhysics())
        {
            constexpr double SpeedCorrectionRate = 8.0;
            constexpr double MaxDriveAcceleration = 900.0;
            const FVector TargetVelocity = Delta > 0.f ? Dir * (Step / Delta) : FVector::ZeroVector;
            const FVector VelocityError = TargetVelocity - Unit->Sphere->GetPhysicsLinearVelocity();
            Unit->Sphere->AddForce((VelocityError * SpeedCorrectionRate).GetClampedToMaxSize(MaxDriveAcceleration), NAME_None, true);
            Unit->Body->SetWorldRotation(Dir.Rotation());
        }
        else
        {
            Unit->SetActorRotation(Dir.Rotation());
            Unit->AddActorWorldOffset(Dir * Step, true);
        }
        if (!IsValid(Unit) || Unit->IsActorBeingDestroyed()) continue;
        if (!Unit->bRanged && Range < 65) HitPlayer(2);
        if (Unit->bRanged && Range < 840 && Elapsed >= Unit->NextAttack)
        {

            Launch(GetWorld(), Unit->GetActorLocation() + Dir * 40, Dir, 340, 2, -1, Red, FVector(0.25));
            Unit->NextAttack = Elapsed + 2.5;
        }
    }
}

void AEchoGame::HitUnit(AEchoUnit* Unit, float Damage, int32 Slot)
{
    if (Phase != EEchoPhase::Running || !IsValid(Unit) || Unit->IsActorBeingDestroyed() || Unit->Health <= 0) return;
    Unit->Health -= Damage;
    if (Unit->Health > 0) return;
    Crystals.Add(Decoration(GetWorld(), Unit->GetActorLocation(), TEXT("/Game/Echo/Crystal.Crystal"), Mint, FVector(1)));
    Enemies.RemoveSingleSwap(Unit);
    ++Kills;
    if (Slot > 0) ++EchoKills;
    Unit->Destroy();
}

void AEchoGame::HitPlayer(float Damage)
{
    if (Phase != EEchoPhase::Running || Elapsed < SafeUntil) return;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (IsPerfCheck())
    {
        ++PerfPlayerHits;
        Damage = 0;
    }
#endif
    HP = FMath::Max(0.f, HP - Damage);
    SafeUntil = Elapsed + 0.85;
    if (HP <= 0) Transition(EEchoPhase::Lost);
}

void AEchoGame::UpdateCrystals(float Delta)
{
    const FVector Player = Drone->GetActorLocation();
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    static const bool bDebugLevelUp = FParse::Param(FCommandLine::Get(), TEXT("EchoDebugLevelUp"));
    if (bDebugLevelUp)
    {
        auto* PC = Cast<APlayerController>(Drone->GetController());
        if (PC && PC->WasInputKeyJustReleased(EKeys::F6))
        {
            if (auto* Crystal = Decoration(GetWorld(), Player, TEXT("/Game/Echo/Crystal.Crystal"), Mint, FVector(1)))
            {
                UE_LOG(LogTemp, Display, TEXT("EchoDebugLevelUp: F6 released, entering level %d"), Level + 1);
                XP = Threshold() - 1;
                Crystals.Add(Crystal);
            }
        }
    }
#endif
    for (int32 I = Crystals.Num() - 1; I >= 0; --I)
    {
        AActor* Gem = Crystals[I];
        if (!IsValid(Gem))
        {
            Crystals.RemoveAtSwap(I);
            continue;
        }
        const FVector Offset = Player - Gem->GetActorLocation();
        if (Offset.SizeSquared() > FMath::Square(190.f)) continue;
        if (Offset.Size2D() > 36)
        {
            Gem->AddActorWorldOffset(Offset.GetClampedToMaxSize(730 * Delta));
            continue;
        }
        Gem->Destroy();
        Crystals.RemoveAtSwap(I);
        ++XP;
        if (XP >= Threshold())
        {
            XP -= Threshold();
            ++Level;

            Transition(EEchoPhase::Choosing);

            break;
        }
    }
}

void AIsmManager::Tick(float Delta)
{

  Super::Tick(Delta);
  int32 I=0;
  for (const auto& Pair : ISM)
  {
      BoltTransforms[Pair.Key].SetNum(Pair.Value->GetInstanceCount(), EAllowShrinking::No);
  }
  
  auto* Game = GetWorld()->GetAuthGameMode<AEchoGame>();
  UInstancedStaticMeshComponent* ISMComponent;
  for (AEchoBolt* Bolt : Bolts) {

      if (!Bolt)continue;
      FTransform Trans = Bolt->GetTransform();
      Trans.SetScale3D(Bolt->IsmScale);
      ISMComponent = ISM[{Bolt->BoltMesh, Bolt->BoltColor}];
      int32 Index = ISMComponent->GetInstanceIndexForId(Bolt->ID);
      BoltTransforms[{Bolt->BoltMesh, Bolt->BoltColor}][Index] = Trans;

      //Game->GetIsmManager()->SetIsmTransform(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")), Bolt->BoltColor, Bolt->ID, Trans);
  }
  for (const auto& Pair : ISM)
  {
      Pair.Value->BatchUpdateInstancesTransforms(0, BoltTransforms[Pair.Key], true);
  }
  
}

AIsmManager::AIsmManager()
{
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    PrimaryActorTick.bCanEverTick = true;
}

FPrimitiveInstanceId AIsmManager::AddInstance(UStaticMesh* Mesh, AEchoBolt* Bolt,const FTransform& Transform, FLinearColor Color)
{
    UInstancedStaticMeshComponent* ISMComponent = GetAddISM(Mesh,Color);
    if (!ISMComponent)return FPrimitiveInstanceId();
    FPrimitiveInstanceId InstanceId = ISMComponent->AddInstanceById(Transform);
    int32 Index = ISMComponent->GetInstanceIndexForId(InstanceId);
    Bolts.Add(Bolt); 
    Bolt->BoltMesh = Mesh;
    //TMap<UStaticMesh*, FIIMap>& RegistryMap = IMap;
    
    //if (!RegistryMap.Contains(Mesh))
    //{
    //    RegistryMap.Add(Mesh, FIIMap());
   /// }
    //(RegistryMap)[Mesh].Map.Add(ID, Index);
    return InstanceId;
}

UInstancedStaticMeshComponent* AIsmManager::GetAddISM(UStaticMesh* Mesh, FLinearColor Color)
{
    if (!Mesh)
        return nullptr;

    
    if (ISM.Contains({Mesh,Color}))
        return (ISM)[{Mesh, Color}];

    UInstancedStaticMeshComponent* NewISM = NewObject<UInstancedStaticMeshComponent>(this);
    NewISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewISM->RegisterComponent();
    NewISM->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
    NewISM->SetStaticMesh(Mesh);

    TArray<FTransform> Transforms;
    BoltTransforms.Add({ Mesh, Color }, Transforms);
    ISM.Add({ Mesh, Color}, NewISM);
    if (auto* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Echo/Hull.Hull")))
    {
        auto* Material = UMaterialInstanceDynamic::Create(Base, NewISM);
        Material->SetVectorParameterValue(TEXT("Tint"), Color);
        NewISM->SetMaterial(0, Material);
    }
    return NewISM;
}
/**
int32 AIsmManager::GetID(UStaticMesh* Mesh, int32 ID)
{
    if (IMap.Contains(Mesh))
        return -1;
    
    FIIMap M = IMap[Mesh];
    return M.Map[ID];

}
*/
int32 AIsmManager::RemoveInstance(UStaticMesh* Mesh, AEchoBolt* Bolt,FLinearColor Color, FPrimitiveInstanceId ID)
{

    if (!ISM.Contains({ Mesh, Color })) return-1;
    UInstancedStaticMeshComponent* ISMComponent = ISM[{Mesh, Color}];

    /*TMap<int32, int32>& M = IMap[Mesh].Map;

    if (!M.Contains(ID))
        return-1;
    int32 Index = M[ID];
    if (!ISMComponent->RemoveInstance(M[ID]))
        return-1;

    int32 LastIndex = ISMComponent->GetInstanceCount();

    if (Index == LastIndex) return -1;

    
    for (const auto& Pair : M)
    {
        if (Pair.Value == LastIndex)
        {
            M.Remove(Pair.Key);
            break;
        }
    }
    M[ID] = LastIndex;
    /*
    if (M.Contains(LastIndex)) {
        int32 ID = M[LastIndex];
        M.Remove(LastIndex);
        M.Add(InstanceIndex, ID);
        return ID;
    }
    */
    if (ISMComponent->IsValidId(ID))
    {
        ISMComponent->RemoveInstanceById(ID);
        Bolts.Remove(Bolt);
    }
    return -1;
}

void AIsmManager::SetIsmTransform(UStaticMesh* Mesh, FLinearColor Color, FPrimitiveInstanceId ID, const FTransform& Transform)
{
    UInstancedStaticMeshComponent* ISMComponent = GetAddISM(Mesh, Color);
   if (!ISMComponent)return;
   int32 Index = ISMComponent->GetInstanceIndexForId(ID);
 
   ISMComponent->UpdateInstanceTransform(Index,Transform);
   
}
