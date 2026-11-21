#include "EchoGame.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
// Each test owns a real game world with collision and a local controller for engine pause handling.
// The viewport is suppressed only during this synchronous test; Slate and map travel need separate checks.
struct FEchoTestWorld
{
    TGuardValue<TObjectPtr<UGameViewportClient>> ViewportGuard;
    UGameInstance* Instance = nullptr;
    UWorld* World = nullptr;
    AEchoGame* Game = nullptr;
    AEchoDrone* Drone = nullptr;

    FEchoTestWorld() : ViewportGuard(GEngine->GameViewport, nullptr) {}

    bool Initialize(FAutomationTestBase& Test)
    {
        Instance = NewObject<UGameInstance>(GEngine);
        Instance->AddToRoot();
        Instance->InitializeStandalone();
        World = Instance->GetWorld();
        if (!Test.TestNotNull(TEXT("isolated game world"), World)) return false;
        const FURL URL(nullptr, TEXT("/Game/Echo/Arena?game=/Script/EchoAssault.EchoGame"), TRAVEL_Absolute);
        if (!Test.TestTrue(TEXT("authority game mode created"), World->SetGameMode(URL))) return false;
        World->InitializeActorsForPlay(URL);
        Game = World->GetAuthGameMode<AEchoGame>();
        if (!Test.TestNotNull(TEXT("real Echo Assault game mode"), Game)) return false;
        auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine, GEngine->LocalPlayerClass);
        Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
        auto* Controller = World->SpawnActor<APlayerController>();
        if (!Test.TestNotNull(TEXT("local controller"), Controller)) return false;
        Controller->SetPlayer(LocalPlayer);
        Drone = Spawn<AEchoDrone>(FVector(-600, 0, 50));
        if (!Test.TestNotNull(TEXT("controlled drone"), Drone)) return false;
        Controller->Possess(Drone);
        World->BeginPlay();
        Game->SetActorTickEnabled(false);
        if (!Test.TestTrue(TEXT("test world has begun play"), World->HasBegunPlay())) return false;
        if (!Test.TestNotNull(TEXT("test world has physics scene"), World->GetPhysicsScene())) return false;
        return Test.TestNotNull(TEXT("player state supports actual world pause"), Controller->PlayerState.Get());
    }

    template<typename ActorType>
    ActorType* Spawn(FVector Position)
    {
        FActorSpawnParameters Parameters;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<ActorType>(Position, FRotator::ZeroRotator, Parameters);
    }

    AEchoBolt* OverlapBolt(float Damage)
    {
        const FTransform Transform(FRotator::ZeroRotator, Drone->GetActorLocation());
        auto* Bolt = World->SpawnActorDeferred<AEchoBolt>(AEchoBolt::StaticClass(), Transform, nullptr, nullptr,
                                                      ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Bolt) return nullptr;
        // Deferred spawning has already registered native components; update the actual collision shape.
        Bolt->Sphere->SetSphereRadius(3000);
        Bolt->Damage = Damage;
        Bolt->OwnerSlot = 1;
        Bolt->Flight->Deactivate();
        Bolt->FinishSpawning(Transform);
        return Bolt;
    }

    ~FEchoTestWorld()
    {
        if (World) World->EndPlay(EEndPlayReason::Quit);
        if (Instance) Instance->Shutdown();
        if (World)
        {
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
        if (Instance) Instance->RemoveFromRoot();
    }
};

void QueueProjectileFrames(TSharedRef<FEchoTestWorld> Fixture, int32 FrameCount = 12)
{
    // Tick functions are queued at most once per GFrameCounter. Wait for distinct engine frames.
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(
        [Fixture, Remaining = FrameCount, LastFrame = GFrameCounter]() mutable
        {
            if (LastFrame == GFrameCounter) return false;
            LastFrame = GFrameCounter;
            Fixture->World->Tick(LEVELTICK_All, 1.f / 60);
            return --Remaining == 0;
        }));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoSpawnOverlapTest, "EchoAssault.Gameplay.SpawnOverlap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoSpawnOverlapTest::RunTest(const FString&)
{
    FEchoTestWorld Fixture;
    if (!Fixture.Initialize(*this)) return false;
    auto* Game = Fixture.Game;
    Game->Drone = Fixture.Drone;
    Game->Elapsed = 120;

    // Any generated position is inside this real collision sphere. Health must be set before overlap delivery.
    auto* FirstBolt = Fixture.OverlapBolt(2);
    if (!TestNotNull(TEXT("overlapping projectile spawned"), FirstBolt)) return false;
    Game->SpawnEnemy();
    TestTrue(TEXT("spawn overlap consumes projectile"), FirstBolt->IsActorBeingDestroyed());
    if (!TestEqual(TEXT("surviving initialized enemy registered once"), Game->Enemies.Num(), 1)) return false;
    auto* Unit = Game->Enemies[0].Get();
    TestEqual(TEXT("120 second enemy has configured health before damage"), Unit->Health, 1.f);
    TestEqual(TEXT("attack time configured before registration"), Unit->NextAttack, 121.7);
    TestTrue(TEXT("next spawn is scheduled"), Game->NextSpawn > Game->Elapsed);
    TestEqual(TEXT("nonlethal overlap gives no kill"), Game->Kills, 0);
    TestTrue(TEXT("nonlethal overlap gives no crystal"), Game->Crystals.IsEmpty());

    Unit->Destroy();
    Game->HitUnit(Unit, 100, 1);
    Game->HitUnit(nullptr, 100, 1);
    Game->UpdateEnemies(1.f / 60);
    TestTrue(TEXT("destroying enemy is removed from active list"), Game->Enemies.IsEmpty());
    TestEqual(TEXT("destroying enemy cannot award kill"), Game->Kills, 0);
    TestTrue(TEXT("destroying enemy cannot award crystal"), Game->Crystals.IsEmpty());

    auto* LethalBolt = Fixture.OverlapBolt(100);
    if (!TestNotNull(TEXT("lethal overlap projectile spawned"), LethalBolt)) return false;
    Game->SpawnEnemy();
    TestTrue(TEXT("lethal spawn overlap consumes projectile"), LethalBolt->IsActorBeingDestroyed());
    TestTrue(TEXT("enemy killed during FinishSpawning is never registered"), Game->Enemies.IsEmpty());
    TestEqual(TEXT("spawn overlap awards one kill"), Game->Kills, 1);
    TestEqual(TEXT("spawn overlap preserves echo ownership"), Game->EchoKills, 1);
    TestEqual(TEXT("spawn overlap awards one crystal"), Game->Crystals.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoCurrentWorldCombatTest, "EchoAssault.Gameplay.CurrentWorldCombat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoCurrentWorldCombatTest::RunTest(const FString&)
{
    auto Fixture = MakeShared<FEchoTestWorld>();
    if (!Fixture->Initialize(*this)) return false;
    auto* Game = Fixture->Game;
    Game->Drone = Fixture->Drone;
    const FVector TargetPosition(150, 0, 50);
    auto* Unit = Fixture->Spawn<AEchoUnit>(TargetPosition);
    if (!TestNotNull(TEXT("current world enemy"), Unit)) return false;
    Game->Enemies.Add(Unit);
    Game->History.Shots.Add({0, FVector(0, 0, 50), FVector::ForwardVector, EEchoWeapon::Blaster, 1});
    Game->Ranks[0] = 3;
    const FEchoShot Snapshot = Game->History.Shots[0];
    Game->Fire(Snapshot, 1);
    AEchoBolt* Bolt = nullptr;
    for (TActorIterator<AEchoBolt> It(Fixture->World); It; ++It) if (!It->IsActorBeingDestroyed()) Bolt = *It;
    if (!TestNotNull(TEXT("replayed real projectile"), Bolt)) return false;
    TestEqual(TEXT("past rank determines damage after current upgrade"), Bolt->Damage, 1.f);
    TestEqual(TEXT("past firing origin is retained"), Bolt->GetActorLocation(), Snapshot.Origin);
    TestEqual(TEXT("past firing direction is retained"), Bolt->Flight->Velocity.GetSafeNormal(), Snapshot.Direction);
    TestEqual(TEXT("replayed projectile belongs to echo"), Bolt->OwnerSlot, 1);
    TestTrue(TEXT("enemy collision body is registered"), Unit->Sphere->IsRegistered() && Unit->Sphere->IsPhysicsStateCreated());
    TestTrue(TEXT("projectile collision body is registered"), Bolt->Sphere->IsRegistered() && Bolt->Sphere->IsPhysicsStateCreated());
    const TWeakObjectPtr<AEchoUnit> WeakUnit(Unit);
    const TWeakObjectPtr<AEchoBolt> WeakBolt(Bolt);
    QueueProjectileFrames(Fixture);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([this, Fixture, WeakUnit, WeakBolt, Snapshot]()
    {
        auto* Game = Fixture->Game;
        if (!TestTrue(TEXT("nonlethal hit keeps current enemy alive"), WeakUnit.IsValid())) return true;
        TestEqual(TEXT("engine projectile hits current enemy"), WeakUnit->Health, 1.f);
        TestTrue(TEXT("actual overlap destroys projectile"), !WeakBolt.IsValid() || WeakBolt->IsActorBeingDestroyed());
        TestEqual(TEXT("damage alone gives no XP"), Game->XP, 0);
        TestEqual(TEXT("damage alone gives no kill"), Game->Kills, 0);
        Game->Fire(Snapshot, 1);
        return true;
    }));
    QueueProjectileFrames(Fixture);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([this, Fixture, WeakUnit, TargetPosition]()
    {
        auto* Game = Fixture->Game;
        TestTrue(TEXT("second historical shot kills current enemy"), !WeakUnit.IsValid() || WeakUnit->IsActorBeingDestroyed());
        TestEqual(TEXT("current enemy death awards one kill"), Game->Kills, 1);
        TestEqual(TEXT("current enemy death awards one echo kill"), Game->EchoKills, 1);
        TestTrue(TEXT("dead enemy removed immediately"), Game->Enemies.IsEmpty());
        if (!TestEqual(TEXT("current enemy death creates one crystal"), Game->Crystals.Num(), 1)) return true;
        auto* Crystal = Game->Crystals[0].Get();
        if (!TestNotNull(TEXT("actual collectible actor"), Crystal)) return true;
        TestEqual(TEXT("crystal uses current enemy location"), Crystal->GetActorLocation(), TargetPosition);
        Game->HitUnit(WeakUnit.Get(), 100, 1);
        TestEqual(TEXT("duplicate hit cannot award another kill"), Game->Kills, 1);
        TestEqual(TEXT("duplicate hit cannot award another crystal"), Game->Crystals.Num(), 1);
        TestEqual(TEXT("XP waits for pickup"), Game->XP, 0);
        Fixture->Drone->SetActorLocation(Crystal->GetActorLocation());
        Game->UpdateCrystals(1.f / 60);
        TestEqual(TEXT("real crystal pickup grants one XP"), Game->XP, 1);
        TestTrue(TEXT("picked crystal destroyed"), Crystal->IsActorBeingDestroyed());
        TestTrue(TEXT("picked crystal removed from collection"), Game->Crystals.IsEmpty());

        // Reverse the creation order: FinishSpawning can destroy a new seeker before homing setup.
        auto* OverlappedUnit = Fixture->Spawn<AEchoUnit>(FVector(0, 400, 50));
        if (!TestNotNull(TEXT("enemy already at new projectile origin"), OverlappedUnit)) return true;
        Game->Enemies.Add(OverlappedUnit);
        const FEchoShot Seeker{0, OverlappedUnit->GetActorLocation(), FVector::ForwardVector, EEchoWeapon::Seeker, 1};
        Game->Fire(Seeker, 1);
        TestTrue(TEXT("new projectile overlap kills existing enemy during creation"), OverlappedUnit->IsActorBeingDestroyed());
        TestTrue(TEXT("new projectile overlap removes current enemy"), Game->Enemies.IsEmpty());
        TestEqual(TEXT("new projectile overlap awards one additional kill"), Game->Kills, 2);
        TestEqual(TEXT("new projectile overlap preserves ownership"), Game->EchoKills, 2);
        TestEqual(TEXT("new projectile overlap drops one new crystal"), Game->Crystals.Num(), 1);
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoEnemyFireTest, "EchoAssault.Gameplay.EnemyFire", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoEnemyFireTest::RunTest(const FString&)
{
    auto Fixture = MakeShared<FEchoTestWorld>();
    if (!Fixture->Initialize(*this)) return false;
    auto* Game = Fixture->Game;
    Game->Drone = Fixture->Drone;
    const FVector PlayerStart = Fixture->Drone->GetActorLocation();
    const FVector LancerStart = PlayerStart + FVector(480, 0, 0);
    const FVector HunterStart = PlayerStart + FVector(0, -150, 0);
    auto* Lancer = Fixture->Spawn<AEchoUnit>(LancerStart);
    auto* Hunter = Fixture->Spawn<AEchoUnit>(HunterStart);
    if (!TestNotNull(TEXT("ranged enemy"), Lancer) || !TestNotNull(TEXT("close-range enemy"), Hunter)) return false;
    Lancer->bRanged = true;
    Game->Elapsed = 30;
    Lancer->NextAttack = Game->Elapsed;
    Game->Enemies = {Hunter, Lancer};
    for (int32 Frame = 0; Frame < 4; ++Frame)
    {
        Game->UpdateEnemies(1.f / 60);
        Fixture->World->Tick(LEVELTICK_All, 1.f / 60);
    }
    TestTrue(TEXT("ranged enemy holds its firing distance"), Lancer->GetActorLocation().Equals(LancerStart));
    TestTrue(TEXT("close-range enemy moves toward the drone"), Hunter->GetActorLocation().Y > HunterStart.Y);

    Game->UpdateEnemies(1.f / 60);
    TWeakObjectPtr<AEchoBolt> FirstShot;
    int32 EnemyShots = 0;
    for (TActorIterator<AEchoBolt> It(Fixture->World); It; ++It)
        if (It->bEnemy && !It->IsActorBeingDestroyed())
        {
            FirstShot = *It;
            ++EnemyShots;
        }
    if (!TestEqual(TEXT("only the ranged enemy fires, once before cooldown"), EnemyShots, 1)) return false;
    TestTrue(TEXT("enemy projectile aims at the drone's original position"),
             FirstShot->Flight->Velocity.GetSafeNormal().Equals(-FVector::ForwardVector));
    const float HealthBefore = Game->HP;
    const TWeakObjectPtr<AEchoUnit> RangedEnemy(Lancer);
    Fixture->Drone->SetActorLocation(PlayerStart + FVector(0, 180, 0), true);
    QueueProjectileFrames(Fixture, 100);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([this, Fixture, FirstShot, RangedEnemy, PlayerStart, HealthBefore]()
    {
        if (TestTrue(TEXT("dodged projectile stays in flight"), FirstShot.IsValid()))
            TestTrue(TEXT("dodged projectile passes the old player position"), FirstShot->GetActorLocation().X < PlayerStart.X - 40);
        TestEqual(TEXT("stepping off the firing line avoids damage"), Fixture->Game->HP, HealthBefore);
        if (!TestTrue(TEXT("ranged enemy survives its first shot"), RangedEnemy.IsValid())) return true;
        Fixture->Drone->SetActorLocation(PlayerStart, true);
        Fixture->Game->Elapsed = RangedEnemy->NextAttack;
        Fixture->Game->UpdateEnemies(1.f / 60);
        return true;
    }));
    QueueProjectileFrames(Fixture, 100);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([this, Fixture, HealthBefore]()
    {
        TestEqual(TEXT("staying on the firing line takes one real collision hit"), Fixture->Game->HP, HealthBefore - 2.f);
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoEnemyArenaBoundsTest, "EchoAssault.Gameplay.EnemyArenaBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEchoEnemyArenaBoundsTest::RunTest(const FString&)
{
    auto Fixture = MakeShared<FEchoTestWorld>();
    if (!Fixture->Initialize(*this)) return false;
    const TArray<FVector> Directions = {FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector};
    TArray<AEchoUnit*> Units;
    for (const FVector& Direction : Directions)
    {
        auto* Unit = Fixture->Spawn<AEchoUnit>(Direction * 1725 + FVector(0, 0, 50));
        if (!TestNotNull(TEXT("enemy near arena edge"), Unit)) return false;
        if (Unit->Sphere->IsSimulatingPhysics()) Unit->Sphere->SetPhysicsLinearVelocity(Direction * 600);
        Units.Add(Unit);
    }

    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(
        [this, Fixture, Directions, Units, Farthest = TArray<double>{1725, 1725, 1725, 1725},
         Frame = 0, LastFrame = GFrameCounter]() mutable
        {
            if (LastFrame == GFrameCounter) return false;
            LastFrame = GFrameCounter;
            for (int32 I = 0; I < Units.Num(); ++I)
                if (!Units[I]->Sphere->IsSimulatingPhysics()) Units[I]->AddActorWorldOffset(Directions[I] * 10, true);
            Fixture->World->Tick(LEVELTICK_All, 1.f / 60);
            for (int32 I = 0; I < Units.Num(); ++I)
                Farthest[I] = FMath::Max(Farthest[I], FVector::DotProduct(Units[I]->GetActorLocation(), Directions[I]));
            if (++Frame < 24) return false;

            for (int32 I = 0; I < Units.Num(); ++I)
            {
                AddInfo(FString::Printf(TEXT("ECHO_ENEMY_BOUND edge=%d farthest_cm=%.3f"), I, Farthest[I]));
                TestTrue(TEXT("outward motion reaches the arena edge"), Farthest[I] > 1740);
                TestTrue(TEXT("enemy center stays within the arena edge"), Farthest[I] <= 1752);
            }
            return true;
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoEnemyWalkBlockingTest, "EchoAssault.Gameplay.EnemyWalkBlocking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEchoEnemyWalkBlockingTest::RunTest(const FString&)
{
    auto Fixture = MakeShared<FEchoTestWorld>();
    if (!Fixture->Initialize(*this)) return false;
    auto* Game = Fixture->Game;
    Game->Drone = Fixture->Drone;
    Fixture->Drone->SetActorLocation(FVector(0, 0, 50));
    auto* Front = Fixture->Spawn<AEchoUnit>(FVector(480, 0, 50));
    auto* Rear = Fixture->Spawn<AEchoUnit>(FVector(620, 0, 50));
    if (!TestNotNull(TEXT("front ranged enemy"), Front) || !TestNotNull(TEXT("rear pursuing enemy"), Rear)) return false;
    Front->bRanged = true;
    Front->NextAttack = 240;
    Game->Enemies = {Front, Rear};

    const bool bBlocking = FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyBlocking"));
    const bool bPhysics = !bBlocking || FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyPhysics"));
    TestEqual(TEXT("front body uses the requested physics mode"), Front->Sphere->IsSimulatingPhysics(), bPhysics);
    TestEqual(TEXT("rear body uses the requested physics mode"), Rear->Sphere->IsSimulatingPhysics(), bPhysics);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(
        [this, Fixture, Front, Rear, bBlocking, bPhysics, Frame = 0, LastFrame = GFrameCounter]() mutable
        {
            if (LastFrame == GFrameCounter) return false;
            LastFrame = GFrameCounter;
            Fixture->Game->UpdateEnemies(1.f / 60);
            Fixture->World->Tick(LEVELTICK_All, 1.f / 60);
            if (++Frame < 90) return false;

            const FVector FrontLocation = Front->GetActorLocation();
            const FVector RearLocation = Rear->GetActorLocation();
            AddInfo(FString::Printf(TEXT("ECHO_ENEMY_WALK blocking=%d physics=%d front_x=%.3f rear_x=%.3f"),
                bBlocking, bPhysics, FrontLocation.X, RearLocation.X));
            if (bPhysics)
            {
                TestTrue(TEXT("rear enemy pushes the stationary front enemy toward the drone"), FrontLocation.X < 470);
                TestTrue(TEXT("the two physics enemies keep their order and distance"), RearLocation.X - FrontLocation.X >= 65);
                TestTrue(TEXT("both enemies remain on the play plane"),
                         FMath::IsNearlyEqual(FrontLocation.Z, 50.0, 1.0) && FMath::IsNearlyEqual(RearLocation.Z, 50.0, 1.0));
            }
            else
            {
                TestTrue(TEXT("front enemy holds at ranged distance"), FMath::IsNearlyEqual(FrontLocation.X, 480.0));
                TestTrue(TEXT("rear enemy reaches the front enemy without passing through it"), RearLocation.X >= 549 && RearLocation.X <= 554);
            }
            return true;
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoUpgradeReplayTest, "EchoAssault.Gameplay.UpgradeReplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoUpgradeReplayTest::RunTest(const FString&)
{
    FEchoTestWorld Fixture;
    if (!Fixture.Initialize(*this)) return false;
    auto* Game = Fixture.Game;
    Game->Drone = Fixture.Drone;
    auto* Target = Fixture.Spawn<AEchoUnit>(FVector(1200, 1000, 50));
    if (!TestNotNull(TEXT("current homing target"), Target)) return false;
    Game->Enemies.Add(Target);

    // Supply fixed offers while using the real selection, replay and projectile paths.
    const auto Choose = [Game](EEchoUpgrade Upgrade)
    {
        Game->Transition(EEchoPhase::Choosing);
        Game->Choices = {Upgrade};
        Game->Choose(Upgrade);
    };
    Game->Elapsed = 29;
    Game->History.Poses = {{0, FVector(0, 0, 50), 0}, {40, FVector(800, 0, 50), 0}};
    const FEchoShot OldBlades{10, FVector(200, -400, 50), FVector::ForwardVector, EEchoWeapon::Blades, 1};
    const FEchoShot OldSeeker{18, FVector(360, -400, 50), FVector::RightVector, EEchoWeapon::Seeker, 2};
    const FEchoShot OldBlaster{26, FVector(520, -400, 50), FVector::ForwardVector, EEchoWeapon::Blaster, 1};
    Game->History.Shots = {OldBlades, OldSeeker, OldBlaster};
    Game->ReplayEchoes(0.25f);
    TestEqual(TEXT("only one echo exists before unlocks"), Game->Echoes.Num(), 1);
    Choose(EEchoUpgrade::MoreEcho);
    Choose(EEchoUpgrade::MoreEcho);
    for (int32 Rank = 1; Rank <= 3; ++Rank)
    {
        if (Rank > 1) Choose(EEchoUpgrade::Blaster);
        Choose(EEchoUpgrade::Seeker);
        Choose(EEchoUpgrade::Blades);
    }

    const auto Advance = [Game](int32 Steps)
    {
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            Game->Elapsed += 0.25;
            Game->ReplayEchoes(0.25f);
        }
    };
    Choose(EEchoUpgrade::Accelerate);
    Advance(1);
    if (!TestEqual(TEXT("two unlocks create three actors"), Game->Echoes.Num(), 3)) return false;
    if (!TestNotNull(TEXT("first echo actor"), Game->Echoes[0].Get())) return false;
    TestTrue(TEXT("first acceleration advances the actor part of a second"),
             Game->Echoes[0]->GetActorLocation().Equals(FVector(426.25, 0, 50), 0.01));
    Advance(15);
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        if (!TestNotNull(TEXT("echo actor after first acceleration"), Game->Echoes[Slot].Get())) return false;
        TestTrue(TEXT("echo uses its own accelerated historical position"),
                 Game->Echoes[Slot]->GetActorLocation().Equals(FVector(520 - 160 * Slot, 0, 50), 0.01));
    }
    Choose(EEchoUpgrade::Accelerate);
    Advance(16);
    for (int32 Slot = 0; Slot < 3; ++Slot)
        TestTrue(TEXT("second acceleration shifts each echo another second"),
                 Game->Echoes[Slot]->GetActorLocation().Equals(FVector(620 - 160 * Slot, 0, 50), 0.01));

    int32 ReplayCounts[3] = {};
    const FEchoShot PastBySlot[] = {OldBlaster, OldSeeker, OldBlades};
    for (TActorIterator<AEchoBolt> It(Fixture.World); It; ++It)
    {
        const int32 Slot = It->OwnerSlot - 1;
        if (!TestTrue(TEXT("replayed bolt belongs to an unlocked echo"), Slot >= 0 && Slot < 3)) continue;
        ++ReplayCounts[Slot];
        TestEqual(TEXT("echo kept the firing origin"), It->GetActorLocation(), PastBySlot[Slot].Origin);
        TestEqual(TEXT("echo uses past damage after current weapons grow"), It->Damage, Slot == 1 ? 3.f : 1.f);
        if (Slot < 2) TestTrue(TEXT("echo kept the firing direction"), It->Flight->Velocity.GetSafeNormal().Equals(PastBySlot[Slot].Direction));
        if (Slot == 1) TestTrue(TEXT("past seeker homes on a current target"), It->Flight->HomingTargetComponent.Get() == Target->Sphere.Get());
    }
    TestEqual(TEXT("past blaster fired once"), ReplayCounts[0], 1);
    TestEqual(TEXT("past seeker fired its rank-two single bolt once"), ReplayCounts[1], 1);
    TestEqual(TEXT("past blades fired their rank-one six-bolt volley once"), ReplayCounts[2], 6);

    Game->RecordWeapons();
    int32 LiveBolts = 0;
    int32 LiveSeekers = 0;
    for (TActorIterator<AEchoBolt> It(Fixture.World); It; ++It)
        if (It->OwnerSlot == 0)
        {
            ++LiveBolts;
            if (It->Flight->bIsHomingProjectile) ++LiveSeekers;
        }
    TestEqual(TEXT("upgraded live weapons fire one blaster, two seekers and ten blades"), LiveBolts, 13);
    TestEqual(TEXT("rank-three live seekers both home"), LiveSeekers, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoWeaponUpgradeReplayBoundaryTest, "EchoAssault.Gameplay.WeaponUpgradeReplayBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoWeaponUpgradeReplayBoundaryTest::RunTest(const FString&)
{
    FEchoTestWorld Fixture;
    if (!Fixture.Initialize(*this)) return false;
    auto* Game = Fixture.Game;
    Game->Drone = Fixture.Drone;
    Game->NextShot[static_cast<int32>(EEchoWeapon::Blaster)] = 1000;
    Game->History.Poses.Add({0, Fixture.Drone->GetActorLocation(), 0});
    Game->Elapsed = 9.75;

    const auto ChooseBlades = [Game]()
    {
        Game->Transition(EEchoPhase::Choosing);
        Game->Choices = {EEchoUpgrade::Blades};
        Game->Choose(EEchoUpgrade::Blades);
    };
    const auto AdvanceReplayTo = [Game](double Time)
    {
        // Advance the replay clock while keeping projectile actors available to count.
        while (Game->Elapsed < Time)
        {
            Game->Elapsed += 0.25;
            Game->ReplayEchoes(0.25f);
        }
    };
    const auto ExpectBlades = [this, World = Fixture.World](const TCHAR* Context, int32 Slot, int32 RankOne, int32 RankTwo)
    {
        int32 Total = 0, RankOneDamage = 0, RankTwoDamage = 0;
        for (TActorIterator<AEchoBolt> It(World); It; ++It)
        {
            if (It->OwnerSlot != Slot || It->IsActorBeingDestroyed()) continue;
            ++Total;
            if (It->Damage == 1.f) ++RankOneDamage;
            if (It->Damage == 2.f) ++RankTwoDamage;
        }
        TestEqual(FString::Printf(TEXT("%s total bolts"), Context), Total, RankOne + RankTwo);
        TestEqual(FString::Printf(TEXT("%s rank-one bolts"), Context), RankOneDamage, RankOne);
        TestEqual(FString::Printf(TEXT("%s rank-two bolts"), Context), RankTwoDamage, RankTwo);
    };

    ChooseBlades();
    AdvanceReplayTo(10);
    Game->RecordWeapons();
    ExpectBlades(TEXT("live at 10s"), 0, 6, 0);
    AdvanceReplayTo(11);
    ChooseBlades();
    AdvanceReplayTo(13);
    Game->RecordWeapons();
    ExpectBlades(TEXT("live after the 13s volley"), 0, 6, 8);

    AdvanceReplayTo(17.75);
    ExpectBlades(TEXT("echo before the first volley is due"), 1, 0, 0);
    AdvanceReplayTo(18);
    ExpectBlades(TEXT("echo at the 18s replay boundary"), 1, 6, 0);
    AdvanceReplayTo(20.75);
    ExpectBlades(TEXT("echo before the upgraded volley is due"), 1, 6, 0);
    AdvanceReplayTo(21);
    ExpectBlades(TEXT("echo at the 21s replay boundary"), 1, 6, 8);
    AdvanceReplayTo(21.25);
    ExpectBlades(TEXT("echo in the frame after the upgraded volley"), 1, 6, 8);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoAuraTest, "EchoAssault.Gameplay.Aura", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEchoAuraTest::RunTest(const FString&)
{
    FEchoTestWorld Fixture;
    if (!Fixture.Initialize(*this)) return false;
    auto* Game = Fixture.Game;
    Game->Drone = Fixture.Drone;
    Game->NextShot[static_cast<int32>(EEchoWeapon::Blaster)] = 1000;
    Game->Elapsed = 10.0;

    const FVector Center = Fixture.Drone->GetActorLocation();
    auto* NearLeft = Fixture.Spawn<AEchoUnit>(Center + FVector(100, -60, 0));
    auto* NearRight = Fixture.Spawn<AEchoUnit>(Center + FVector(100, 60, 0));
    auto* Far = Fixture.Spawn<AEchoUnit>(Center + FVector(200, 0, 0));
    if (!TestNotNull(TEXT("near left target"), NearLeft) || !TestNotNull(TEXT("near right target"), NearRight) ||
        !TestNotNull(TEXT("far target"), Far)) return false;
    NearLeft->Health = NearRight->Health = 1.f;
    Far->Health = 3.f;
    Game->Enemies = {NearLeft, NearRight, Far};

    const auto ChooseAura = [Game]()
    {
        Game->Transition(EEchoPhase::Choosing);
        Game->Choices = {EEchoUpgrade::Aura};
        Game->Choose(EEchoUpgrade::Aura);
    };
    TestEqual(TEXT("locked aura has no range"), Game->GetAuraRadius(), 0.0);
    ChooseAura();
    TestEqual(TEXT("first rank range is available before the first hit"), Game->GetAuraRadius(), 180.0);
    Game->RecordWeapons();
    TestEqual(TEXT("one pulse kills both nearby enemies"), Game->Kills, 2);
    TestEqual(TEXT("first rank misses the far enemy"), Far->Health, 3.f);

    ChooseAura();
    TestEqual(TEXT("second rank updates the range immediately"), Game->GetAuraRadius(), 220.0);
    Game->Elapsed = 10.70;
    Game->RecordWeapons();
    TestEqual(TEXT("upgrading does not shorten the current cooldown"), Far->Health, 3.f);
    Game->Elapsed = 10.75;
    Game->RecordWeapons();
    TestEqual(TEXT("next pulse reaches the far enemy with two damage"), Far->Health, 1.f);
    TestTrue(TEXT("live aura pulses leave no replay events"), Game->History.Shots.IsEmpty());

    Game->Fire({10.75, Center, FVector::ZeroVector, EEchoWeapon::Aura, 2}, 1);
    TestEqual(TEXT("echo slots cannot apply aura damage"), Far->Health, 1.f);
    TestEqual(TEXT("both kills are credited to the player"), Game->EchoKills, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoProgressionTest, "EchoAssault.Gameplay.ProgressionAndOutcomes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoProgressionTest::RunTest(const FString&)
{
    FEchoTestWorld Fixture;
    if (!Fixture.Initialize(*this)) return false;
    auto* Game = Fixture.Game;
    Game->Drone = Fixture.Drone;
    auto* PC = Fixture.World->GetFirstPlayerController();
    if (!TestNotNull(TEXT("controller for UI input transition"), PC)) return false;
    if (!TestNotNull(TEXT("real player input for held key regression"), PC->PlayerInput.Get())) return false;
    const TArray<UInputComponent*> InputStack;
    for (const FKey Key : {EKeys::W, EKeys::A, EKeys::S, EKeys::D})
        PC->InputKey(FInputKeyParams(Key, IE_Pressed, 1.0));
    PC->PlayerInput->ProcessInputStack(InputStack, 1.f / 60, false);
    for (const FKey Key : {EKeys::W, EKeys::A, EKeys::S, EKeys::D})
        TestTrue(TEXT("movement key is held before level up"), PC->IsInputKeyDown(Key));
    Game->XP = Game->Threshold() - 1;
    auto* Unit = Fixture.Spawn<AEchoUnit>(Fixture.Drone->GetActorLocation() + FVector(100, 0, 0));
    if (!TestNotNull(TEXT("level-up enemy"), Unit)) return false;
    Game->HitUnit(Unit, 100, 0);
    if (!TestEqual(TEXT("level-up crystal exists"), Game->Crystals.Num(), 1)) return false;
    Fixture.Drone->SetActorLocation(Game->Crystals[0]->GetActorLocation());
    Game->UpdateCrystals(1.f / 60);
    TestTrue(TEXT("threshold enters choice phase"), Game->Phase == EEchoPhase::Choosing);
    TestTrue(TEXT("choice phase pauses actual game world"), UGameplayStatics::IsGamePaused(Game));
    TestEqual(TEXT("threshold advances level"), Game->Level, 2);
    TestEqual(TEXT("threshold consumes XP"), Game->XP, 0);
    TestEqual(TEXT("level-up offers three choices"), Game->Choices.Num(), 3);
    // No release events are delivered: emulate releasing gameplay keys while UI owns input.
    PC->PlayerInput->ProcessInputStack(InputStack, 1.f / 60, true);
    for (const FKey Key : {EKeys::W, EKeys::A, EKeys::S, EKeys::D})
        TestFalse(TEXT("choice transition clears held movement keys"), PC->IsInputKeyDown(Key));
    Game->Choose(EEchoUpgrade::Pulse);
    TestTrue(TEXT("unoffered upgrade cannot resume game"), Game->Phase == EEchoPhase::Choosing);
    const double BeforePause = Game->Elapsed;
    Game->Tick(1);
    TestEqual(TEXT("choice phase freezes gameplay clock"), Game->Elapsed, BeforePause);

    // At all caps the three effective, distinct choices must be repair, speed and refit.
    for (int32& Rank : Game->Ranks) Rank = 3;
    Game->EchoCount = 3;
    Game->Acceleration = 2;
    Game->Transition(EEchoPhase::Choosing);
    TestEqual(TEXT("capped upgrades still offer three choices"), Game->Choices.Num(), 3);
    for (EEchoUpgrade Choice : {EEchoUpgrade::Health, EEchoUpgrade::Speed, EEchoUpgrade::Pulse})
        TestTrue(TEXT("capped pool contains each distinct effective upgrade"), Game->Choices.Contains(Choice));
    const float PreviousMaxHP = Game->MaxHP, PreviousSpeed = Game->Speed;
    Game->Choose(EEchoUpgrade::Pulse);
    TestEqual(TEXT("refit changes maximum health"), Game->MaxHP, PreviousMaxHP + 1);
    TestEqual(TEXT("refit changes speed"), Game->Speed, PreviousSpeed + 15);
    TestTrue(TEXT("choice resumes running phase"), Game->Phase == EEchoPhase::Running);
    TestFalse(TEXT("choice unpauses actual game world"), UGameplayStatics::IsGamePaused(Game));
    TestTrue(TEXT("consumed choices cleared"), Game->Choices.IsEmpty());
    const FVector AfterChoice = Fixture.Drone->GetActorLocation();
    PC->PlayerInput->ProcessInputStack(InputStack, 1.f / 60, false);
    Game->MoveAndAim(1.f / 60);
    TestTrue(TEXT("resuming without new input does not move the drone"), Fixture.Drone->GetActorLocation().Equals(AfterChoice));
    PC->InputKey(FInputKeyParams(EKeys::W, IE_Pressed, 1.0));
    PC->PlayerInput->ProcessInputStack(InputStack, 1.f / 60, false);
    TestTrue(TEXT("fresh movement press works after choosing"), PC->IsInputKeyDown(EKeys::W));
    Game->MoveAndAim(1.f / 60);
    TestTrue(TEXT("fresh movement press moves the drone"), Fixture.Drone->GetActorLocation().X > AfterChoice.X);
    PC->InputKey(FInputKeyParams(EKeys::W, IE_Released, 0.0));
    PC->PlayerInput->ProcessInputStack(InputStack, 1.f / 60, false);
    TestFalse(TEXT("fresh movement release works after choosing"), PC->IsInputKeyDown(EKeys::W));
    Game->Choose(EEchoUpgrade::Pulse);
    TestEqual(TEXT("duplicate choice cannot apply upgrade again"), Game->MaxHP, PreviousMaxHP + 1);

    // Drive the real gameplay Tick without unrelated spawns or shots obscuring visibility assertions.
    Game->Elapsed = 2;
    Game->HP = Game->MaxHP;
    Game->NextSpawn = 1000;
    for (double& NextShot : Game->NextShot) NextShot = 1000;
    Fixture.Drone->Body->SetVisibility(true);
    Game->HitPlayer(1);
    Game->Tick(0.04f);
    TestTrue(TEXT("drone stays visible before the first blink interval"), Fixture.Drone->Body->IsVisible());
    Game->Tick(0.07f);
    TestFalse(TEXT("damage blink hides the drone after one interval"), Fixture.Drone->Body->IsVisible());
    TestTrue(TEXT("blink preserves fractional interval remainder"), FMath::IsNearlyEqual(Game->BlinkElapsed, 0.01f, 0.0001f));
    Game->Tick(0.25f);
    TestFalse(TEXT("long frame consumes two blink intervals"), Fixture.Drone->Body->IsVisible());
    TestTrue(TEXT("long frame preserves the remaining blink time"), FMath::IsNearlyEqual(Game->BlinkElapsed, 0.06f, 0.0001f));

    Game->Transition(EEchoPhase::Choosing);
    const double PausedBlinkTime = Game->Elapsed;
    const float PausedBlinkRemainder = Game->BlinkElapsed;
    Game->Tick(1);
    TestEqual(TEXT("choosing freezes the invulnerability clock"), Game->Elapsed, PausedBlinkTime);
    TestEqual(TEXT("choosing preserves the blink remainder"), Game->BlinkElapsed, PausedBlinkRemainder);
    TestFalse(TEXT("choosing preserves the hidden blink phase by design"), Fixture.Drone->Body->IsVisible());
    Game->Choose(EEchoUpgrade::Health);
    Game->Tick(0.05f);
    TestTrue(TEXT("blink resumes from the preserved phase after choosing"), Fixture.Drone->Body->IsVisible());

    Fixture.Drone->Body->SetVisibility(false);
    Game->BlinkElapsed = 0.03f;
    Game->Elapsed = Game->SafeUntil - 0.125;
    Game->Tick(0.125f);
    TestEqual(TEXT("tick reaches the exact invulnerability endpoint"), Game->Elapsed, Game->SafeUntil);
    TestTrue(TEXT("invulnerability endpoint restores drone visibility"), Fixture.Drone->Body->IsVisible());
    TestEqual(TEXT("invulnerability endpoint clears blink remainder"), Game->BlinkElapsed, 0.f);

    Game->HP = 4;
    Game->Elapsed = 10;
    Game->HitPlayer(2);
    Game->HitPlayer(2);
    TestEqual(TEXT("same-time damage respects invulnerability"), Game->HP, 2.f);
    Game->Elapsed = Game->SafeUntil;
    Fixture.Drone->Body->SetVisibility(false);
    Game->HitPlayer(2);
    TestEqual(TEXT("lethal hit clamps health to zero"), Game->HP, 0.f);
    TestTrue(TEXT("lethal hit enters loss phase"), Game->Phase == EEchoPhase::Lost);
    TestTrue(TEXT("loss pauses actual game world"), UGameplayStatics::IsGamePaused(Game));
    TestTrue(TEXT("loss restores a hidden drone"), Fixture.Drone->Body->IsVisible());
    Game->Tick(240);
    TestTrue(TEXT("loss cannot advance into victory"), Game->Phase == EEchoPhase::Lost);
    Game->HitPlayer(2);
    TestEqual(TEXT("damage after loss leaves health unchanged"), Game->HP, 0.f);

    // Isolate the time boundary without simulating or claiming a human four-minute run.
    Game->Phase = EEchoPhase::Running;
    Game->HP = Game->MaxHP;
    UGameplayStatics::SetGamePaused(Game, false);
    Game->Elapsed = 0;
    Game->NextSpawn = 0;
    Game->Tick(0.125f);
    TestEqual(TEXT("opening target starts at zero"), Game->Enemies.Num(), 0);
    Game->Tick(0.25f);
    TestEqual(TEXT("opening target grows to one"), Game->Enemies.Num(), 1);
    Game->Tick(0.125f);
    if (TestEqual(TEXT("a due spawn cannot exceed the live enemy target"), Game->Enemies.Num(), 1))
        Game->HitUnit(Game->Enemies[0], 100, 0);
    TestEqual(TEXT("defeating an enemy frees its place"), Game->Enemies.Num(), 0);
    Game->Tick(0.0625f);
    TestEqual(TEXT("the vacant place is refilled while the target stays at one"), Game->Enemies.Num(), 1);

    Game->Elapsed = 209.75;
    Game->NextSpawn = 209.875;
    Game->Tick(0.125f);
    TestEqual(TEXT("regular wave spawns just before 210 seconds"), Game->Enemies.Num(), 2);
    TestTrue(TEXT("regular wave schedules the next refill in 0.03 seconds"),
             FMath::IsNearlyEqual(Game->NextSpawn - Game->Elapsed, 0.03));
    Game->NextSpawn = 210;
    Game->Tick(0.125f);
    TestEqual(TEXT("final wave spawn runs at the exact time boundary"), Game->Enemies.Num(), 3);
    TestTrue(TEXT("final wave schedules the next refill in 0.015 seconds"),
             FMath::IsNearlyEqual(Game->NextSpawn - Game->Elapsed, 0.015));
    Game->Elapsed = 239.5;
    Game->Tick(0.5);
    TestTrue(TEXT("240 second boundary enters victory"), Game->Phase == EEchoPhase::Won);
    TestTrue(TEXT("victory pauses actual game world"), UGameplayStatics::IsGamePaused(Game));
    Fixture.Drone->Body->SetVisibility(false);
    Game->Transition(EEchoPhase::Won);
    TestTrue(TEXT("victory transition restores a hidden drone"), Fixture.Drone->Body->IsVisible());
    Game->HitPlayer(100);
    TestTrue(TEXT("damage after victory cannot replace outcome"), Game->Phase == EEchoPhase::Won);
    return true;
}
// Run explicitly with "Automation RunTests EchoPerf.Projectiles".
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEchoProjectileProfile, "EchoPerf.Projectiles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEchoProjectileProfile::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    Names = {TEXT("Rate40"), TEXT("Rate160"), TEXT("SpawnImpact")};
    Commands = {TEXT("2"), TEXT("8"), TEXT("SpawnImpact")};
}

bool FEchoProjectileProfile::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<FEchoTestWorld>();
    if (!Fixture->Initialize(*this)) return false;
    Fixture->Game->Drone = Fixture->Drone;

    if (Parameters == TEXT("SpawnImpact"))
    {
        const FVector Origin(0, 200, 50);
        auto* Target = Fixture->Spawn<AEchoUnit>(Origin);
        if (!TestNotNull(TEXT("spawn impact target"), Target)) return false;
        Target->Health = 2.f;
        Fixture->Game->Fire({0, Origin, FVector::ForwardVector, EEchoWeapon::Blaster, 1}, 0);
        TestEqual(TEXT("the bolt hits the target during spawning"), Target->Health, 1.f);
        TestFalse(TEXT("no bolt survives the spawn impact"), static_cast<bool>(TActorIterator<AEchoBolt>(Fixture->World)));

        int32 InstanceCount = 0;
        for (TActorIterator<AActor> It(Fixture->World); It; ++It)
        {
            for (const auto* Component : TInlineComponentArray<UInstancedStaticMeshComponent*>(*It))
                InstanceCount += Component->GetInstanceCount();
        }
        TestEqual(TEXT("the spawn impact leaves no projectile instance"), InstanceCount, 0);
        return true;
    }

    constexpr int32 WarmupFrames = 180;
    constexpr int32 SampleFrames = 360;
    constexpr int32 DrainFrames = 180;
    constexpr int32 FramesPerVolley = 3;
    constexpr float TargetHealth = 10000.f;
    const int32 LaneCount = FCString::Atoi(*Parameters);
    TArray<FVector> Origins;
    TArray<AEchoUnit*> Targets;
    for (int32 Lane = 0; Lane < LaneCount; ++Lane)
    {
        const FVector Origin(0, 200 + 100 * Lane, 50);
        Origins.Add(Origin);
        // Half the lanes hit a fixed target; the others run through the normal lifespan.
        if (Lane % 2 != 0) continue;
        auto* Target = Fixture->Spawn<AEchoUnit>(Origin + FVector(720, 0, 0));
        if (!TestNotNull(TEXT("projectile profile target"), Target)) return false;
        Target->Health = TargetHealth;
        Targets.Add(Target);
    }

    const bool bCheckInstances = FParse::Param(FCommandLine::Get(), TEXT("EchoCheckProjectileInstances"));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(
        [this, Fixture, Origins = MoveTemp(Origins), Targets = MoveTemp(Targets), bCheckInstances, Frame = 0, LastFrame = GFrameCounter]() mutable
        {
            if (LastFrame == GFrameCounter) return false;
            LastFrame = GFrameCounter;
            const auto Step = [&]()
            {
                if (Frame < WarmupFrames + SampleFrames && Frame % FramesPerVolley == 0)
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE(EchoProjectileFireBatch);
                    for (int32 Lane = 0; Lane < Origins.Num(); ++Lane)
                    {
                        Fixture->Game->Fire({Frame / 60.0, Origins[Lane], FVector::ForwardVector, EEchoWeapon::Blaster, 1}, Lane % 4);
                    }
                }
                TRACE_CPUPROFILER_EVENT_SCOPE(EchoProjectileWorldTick);
                Fixture->World->Tick(LEVELTICK_All, 1.f / 60);
            };
            if (Frame >= WarmupFrames && Frame < WarmupFrames + SampleFrames)
            {
                TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Origins.Num() == 2 ? TEXT("EchoProjectileSample40") : TEXT("EchoProjectileSample160"));
                Step();
            }
            else
            {
                Step();
            }

            const bool bDrained = Frame + 1 == WarmupFrames + SampleFrames + DrainFrames;
            const bool bCheckFlightPosition = Frame == FramesPerVolley - 1;
            if (bCheckInstances && (Frame == 0 || bCheckFlightPosition || bDrained))
            {
                // The isolated world only uses ISM for projectile visuals.
                int32 InstanceCount = 0;
                TArray<FVector> InstanceLocations;
                for (TActorIterator<AActor> It(Fixture->World); It; ++It)
                {
                    for (const auto* Component : TInlineComponentArray<UInstancedStaticMeshComponent*>(*It))
                    {
                        const int32 ComponentCount = Component->GetInstanceCount();
                        InstanceCount += ComponentCount;
                        if (Frame == 0 && ComponentCount > 0)
                        {
                            TestTrue(TEXT("projectile visual instances have collision disabled"),
                                     Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
                        }
                        if (bDrained) continue;
                        auto* Material = Frame == 0 ? Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0)) : nullptr;
                        if (Frame == 0 && ComponentCount > 0)
                        {
                            TestNotNull(TEXT("projectile visuals use a tinted material"), Material);
                            const UMaterial* BaseMaterial = Material ? Material->GetMaterial() : nullptr;
                            TestTrue(TEXT("base material supports instanced static mesh rendering"),
                                     BaseMaterial && BaseMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
                        }
                        for (int32 Index = 0; Index < ComponentCount; ++Index)
                        {
                            FTransform InstanceTransform;
                            if (TestTrue(TEXT("projectile instance transform is readable"), Component->GetInstanceTransform(Index, InstanceTransform, true)))
                            {
                                TestTrue(TEXT("blaster visual retains its original scale"),
                                         InstanceTransform.GetScale3D().Equals(FVector(0.35, 0.14, 0.14), 0.001));
                                InstanceLocations.Add(InstanceTransform.GetLocation());
                                if (Material)
                                {
                                    const double Y = InstanceTransform.GetLocation().Y;
                                    const int32 Lane = Origins.IndexOfByPredicate([Y](const FVector& Origin) { return FMath::IsNearlyEqual(Origin.Y, Y, 0.1); });
                                    if (!TestTrue(TEXT("the visual belongs to a launch lane"), Lane != INDEX_NONE)) continue;
                                    const FLinearColor Expected = Lane % 4 == 0 ? FLinearColor(0.12f, 0.85f, 1.f) : FLinearColor(0.52f, 0.26f, 1.f);
                                    const FLinearColor Tint = Material->K2_GetVectorParameterValue(TEXT("Tint"));
                                    TestTrue(FString::Printf(TEXT("lane %d visual tint: expected %s, got %s"), Lane, *Expected.ToString(), *Tint.ToString()),
                                             Tint.Equals(Expected));
                                }
                            }
                        }
                    }
                }
                if (bCheckFlightPosition)
                {
                    TArray<FVector> BoltLocations;
                    for (TActorIterator<AEchoBolt> It(Fixture->World); It; ++It)
                    {
                        TestEqual(TEXT("blaster collision sphere retains its 9 cm radius"), It->Sphere->GetScaledSphereRadius(), 9.f, 0.01f);
                        BoltLocations.Add(It->GetActorLocation());
                    }
                    if (TestEqual(TEXT("first volley has a location for every instance"), InstanceLocations.Num(), Origins.Num()) &&
                        TestEqual(TEXT("first volley has a location for every bolt"), BoltLocations.Num(), Origins.Num()))
                    {
                        const auto ByLane = [](const FVector& A, const FVector& B) { return A.Y < B.Y; };
                        InstanceLocations.Sort(ByLane);
                        BoltLocations.Sort(ByLane);
                        AddInfo(FString::Printf(TEXT("First lane position after flight: visual X %.2f, bolt X %.2f"),
                                                InstanceLocations[0].X, BoltLocations[0].X));
                        for (int32 Lane = 0; Lane < Origins.Num(); ++Lane)
                        {
                            TestTrue(TEXT("bolt moved from its launch position"), BoltLocations[Lane].X > Origins[Lane].X);
                            TestTrue(FString::Printf(TEXT("lane %d instance matches the bolt after the same world tick"), Lane),
                                     InstanceLocations[Lane].Equals(BoltLocations[Lane], 0.1));
                        }
                    }
                }
                else
                {
                    TestEqual(bDrained ? TEXT("all hit and expired projectile instances were removed") : TEXT("the first volley created one instance per bolt"),
                              InstanceCount, bDrained ? 0 : Origins.Num());
                    AddInfo(FString::Printf(TEXT("Projectile instances %s: %d"), bDrained ? TEXT("after drain") : TEXT("after first volley"), InstanceCount));
                }
            }

            if (++Frame < WarmupFrames + SampleFrames + DrainFrames) return false;
            for (const AEchoUnit* Target : Targets)
            {
                TestEqual(TEXT("each shot in a target lane hits once"), TargetHealth - Target->Health,
                          float((WarmupFrames + SampleFrames) / FramesPerVolley));
            }
            int32 ActiveBolts = 0;
            for (TActorIterator<AEchoBolt> It(Fixture->World); It; ++It)
            {
                if (It->Flight->IsComponentTickEnabled()) ++ActiveBolts;
            }
            TestEqual(TEXT("no flight ticks remain after draining hit and expired bolts"), ActiveBolts, 0);
            return true;
        }));
    return true;
}

// Run explicitly with "Automation RunTests EchoPerf.Enemies".
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FEchoEnemyProfile, "EchoPerf.Enemies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FEchoEnemyProfile::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    Names = {TEXT("Count100"), TEXT("Count300"), TEXT("Count500")};
    Commands = {TEXT("100"), TEXT("300"), TEXT("500")};
}

bool FEchoEnemyProfile::RunTest(const FString& Parameters)
{
    constexpr float Delta = 1.f / 60;
    constexpr int32 WarmupFrames = 120;
    constexpr int32 SampleFrames = 240;
    constexpr int32 QueriesPerFrame = 8;
    const int32 EnemyCount = FCString::Atoi(*Parameters);
    const bool bDisableCollision = FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyProfileNoCollision"));
    const bool bGridLayout = FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyProfileGrid"));
    const bool bBlocking = FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyBlocking"));
    const bool bPhysics = !bBlocking || FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyPhysics"));
    auto Fixture = MakeShared<FEchoTestWorld>();
    if (!Fixture->Initialize(*this)) return false;
    auto* Game = Fixture->Game;
    Game->Drone = Fixture->Drone;
    Game->Elapsed = 30;
    Game->SafeUntil = 240;
    Fixture->Drone->SetActorLocation(FVector(0, 0, 50));

    // Resolve shared assets before taking measurements of the real spawn path.
    if (!TestNotNull(TEXT("hunter mesh"), LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Echo/Hunter.Hunter"))) ||
        !TestNotNull(TEXT("lancer mesh"), LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Echo/Lancer.Lancer"))) ||
        !TestNotNull(TEXT("shared material"), LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Echo/Hull.Hull")))) return false;

    FMath::RandInit(17543);
    TArray<double> SpawnTimes;
    SpawnTimes.Reserve(EnemyCount);
    for (int32 Index = 0; Index < EnemyCount; ++Index)
    {
        const uint64 Start = FPlatformTime::Cycles64();
        Game->SpawnEnemy();
        SpawnTimes.Add(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start));
    }
    if (!TestEqual(TEXT("real spawns deliver the requested enemy count"), Game->Enemies.Num(), EnemyCount)) return false;
    int32 RangedCount = 0;
    uint32 InitialPositionHash = 0;
    constexpr int32 Columns = 12;
    const int32 Rows = FMath::DivideAndRoundUp(EnemyCount, Columns);
    for (int32 Index = 0; Index < EnemyCount; ++Index)
    {
        auto* Unit = Game->Enemies[Index].Get();
        if (bGridLayout)
        {
            const FVector Position(500 + (Index % Columns) * 82, (Index / Columns - (Rows - 1) * 0.5) * 82, 50);
            Unit->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
        }
        InitialPositionHash = HashCombine(InitialPositionHash, GetTypeHash(Unit->GetActorLocation()));
        if (Unit->bRanged) ++RangedCount;
        Unit->NextAttack = 240;
        if (bDisableCollision) Unit->Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    TArray<double> MoveTimes, SearchTimes, WorldTimes, UpdateTimes;
    MoveTimes.Reserve(SampleFrames);
    SearchTimes.Reserve(SampleFrames);
    WorldTimes.Reserve(SampleFrames);
    UpdateTimes.Reserve(SampleFrames);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(
        [this, Fixture, EnemyCount, RangedCount, InitialPositionHash, bDisableCollision, bGridLayout, bBlocking, bPhysics,
         SpawnTimes = MoveTemp(SpawnTimes), MoveTimes = MoveTemp(MoveTimes),
         SearchTimes = MoveTemp(SearchTimes), WorldTimes = MoveTemp(WorldTimes), UpdateTimes = MoveTemp(UpdateTimes),
         Frame = 0, SampleHits = 0, LastFrame = GFrameCounter]() mutable
        {
            if (LastFrame == GFrameCounter) return false;
            LastFrame = GFrameCounter;
            auto* Mode = Fixture->Game;
            Mode->Elapsed += Delta;
            const double Angle = Frame * Delta * UE_DOUBLE_TWO_PI / 6.0;
            const FVector Player(400.0 * (FMath::Cos(Angle) - 1.0), 400.0 * FMath::Sin(Angle), 50);
            Fixture->Drone->SetActorLocation(Player, true);

            uint64 Start = FPlatformTime::Cycles64();
            Mode->UpdateEnemies(Delta);
            const double MoveMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);

            int32 Hits = 0;
            Start = FPlatformTime::Cycles64();
            for (int32 Query = 0; Query < QueriesPerFrame; ++Query)
            {
                const FVector Offset((Query - 3) * 80, ((Frame + Query * 29) % 31 - 15) * 35, 0);
                if (Mode->Nearest(Player + Offset)) ++Hits;
            }
            const double SearchMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);

            Start = FPlatformTime::Cycles64();
            Fixture->World->Tick(LEVELTICK_All, Delta);
            const double WorldMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);
            if (Frame >= WarmupFrames)
            {
                SampleHits += Hits;
                MoveTimes.Add(MoveMs);
                SearchTimes.Add(SearchMs);
                WorldTimes.Add(WorldMs);
                UpdateTimes.Add(MoveMs + WorldMs);
            }

            if (++Frame < WarmupFrames + SampleFrames) return false;
            TestEqual(TEXT("enemy count is constant after all frames"), Mode->Enemies.Num(), EnemyCount);
            TestEqual(TEXT("every sampled search found a target"), SampleHits, QueriesPerFrame * SampleFrames);
            TestTrue(TEXT("the sampled frames remain in play"), Mode->Phase == EEchoPhase::Running);
            uint32 PositionHash = 0;
            for (const AEchoUnit* Unit : Mode->Enemies) PositionHash = HashCombine(PositionHash, GetTypeHash(Unit->GetActorLocation()));

            const auto Stats = [](TArray<double>& Times)
            {
                Times.Sort();
                double Total = 0;
                for (const double Value : Times) Total += Value;
                return FVector(Total, Total / Times.Num(), Times[FMath::CeilToInt(0.95 * Times.Num()) - 1]);
            };
            const FVector Spawn = Stats(SpawnTimes), Move = Stats(MoveTimes), Search = Stats(SearchTimes);
            const FVector World = Stats(WorldTimes), Update = Stats(UpdateTimes);
            AddInfo(FString::Printf(TEXT("ECHO_ENEMY_PROFILE count=%d ranged=%d collision=%d grid=%d blocking=%d physics=%d frames=%d queries_per_frame=%d initial_position_hash=%u position_hash=%u "
                "spawn_total_ms=%.6f spawn_mean_ms=%.6f spawn_p95_ms=%.6f move_mean_ms=%.6f move_p95_ms=%.6f "
                "search_batch_mean_ms=%.6f search_batch_p95_ms=%.6f world_mean_ms=%.6f world_p95_ms=%.6f update_mean_ms=%.6f update_p95_ms=%.6f"),
                EnemyCount, RangedCount, !bDisableCollision, bGridLayout, bBlocking, bPhysics, SampleFrames, QueriesPerFrame, InitialPositionHash, PositionHash,
                Spawn.X, Spawn.Y, Spawn.Z, Move.Y, Move.Z, Search.Y, Search.Z, World.Y, World.Z, Update.Y, Update.Z));
            return true;
        }));
    return true;
}
#endif
