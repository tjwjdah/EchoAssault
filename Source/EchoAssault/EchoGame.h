#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "EchoHistory.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EchoGame.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class USpringArmComponent;
class SWidget;

inline constexpr ECollisionChannel EchoEnemyCollisionChannel = ECC_GameTraceChannel1;
inline constexpr float EchoEnemyCollisionRadius = 35.f;

UCLASS()
class AEchoDrone : public APawn
{
    GENERATED_BODY()
public:
    AEchoDrone();
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<USpringArmComponent> Arm;
};

UCLASS()
class AEchoUnit : public AActor
{
    GENERATED_BODY()
public:
    AEchoUnit();
    virtual void PostInitializeComponents() override;
    UPROPERTY() TObjectPtr<USphereComponent> Sphere;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    float Health = 2;
    double NextAttack = 0;
    bool bRanged = false;
};

USTRUCT(BlueprintType)
struct FMC
{
    GENERATED_BODY()

    UPROPERTY()
    UStaticMesh* Mesh = nullptr;
    UPROPERTY()
    FLinearColor Color = FLinearColor::White;;

    bool operator==(const FMC& Other) const
    {
        return Mesh == Other.Mesh && Color == Other.Color;
    }

    friend uint32 GetTypeHash(const FMC& Key)
    {
        uint32 Hash = GetTypeHash(Key.Mesh);
        Hash = HashCombine(Hash, GetTypeHash(Key.Color.R));
        Hash = HashCombine(Hash, GetTypeHash(Key.Color.G));
        Hash = HashCombine(Hash, GetTypeHash(Key.Color.B));
        Hash = HashCombine(Hash, GetTypeHash(Key.Color.A));
        return Hash;
    }
};

UCLASS()
class AIsmManager : public AActor
{
    GENERATED_BODY()

    UPROPERTY()
    TMap<FMC, UInstancedStaticMeshComponent*> ISM;
    UPROPERTY() TArray<TObjectPtr<AEchoBolt>> Bolts;
    TMap<FMC,TArray<FTransform>> BoltTransforms;
    //UPROPERTY()
    //TMap<UStaticMesh*, FIIMap> IMap;

public:
    virtual void Tick(float Delta) override;
    AIsmManager();
    
    FPrimitiveInstanceId AddInstance(UStaticMesh* Mesh, AEchoBolt* Bolt,const FTransform& Transform, FLinearColor Color);
    UInstancedStaticMeshComponent* GetAddISM(UStaticMesh* Mesh, FLinearColor Color);
   
    int32 RemoveInstance(UStaticMesh* Mesh, AEchoBolt* Bolt, FLinearColor Color, FPrimitiveInstanceId InstanceIndex);
    void SetIsmTransform(UStaticMesh* Mesh, FLinearColor Color, FPrimitiveInstanceId ID, const FTransform& Transform);
};


UCLASS()
class AEchoBolt : public AActor
{
    GENERATED_BODY()
public:
    AEchoBolt();
    UPROPERTY() TObjectPtr<USphereComponent> Sphere;
    //UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<UProjectileMovementComponent> Flight;
    
    UPROPERTY() TObjectPtr<UStaticMesh> BoltMesh;
    float Damage = 1;
    bool bEnemy = false;
    int32 OwnerSlot = 0;
    FLinearColor BoltColor;
    FVector IsmScale;
    FPrimitiveInstanceId ID;
    UFUNCTION() void Contact(UPrimitiveComponent* Self, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bSweep, const FHitResult& Hit);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    //virtual void Tick(float Delta) override;
};

enum class EEchoPhase : uint8 { Running, Choosing, Won, Lost };
enum class EEchoUpgrade : uint8 { Blaster, Seeker, Blades, Aura, MoreEcho, Accelerate, Health, Speed, Pulse };

UCLASS()
class AEchoGame : public AGameModeBase
{
    GENERATED_BODY()
public:
    AEchoGame();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void HitUnit(AEchoUnit* Unit, float Damage, int32 OwnerSlot);
    void HitPlayer(float Damage);
    void Choose(EEchoUpgrade Upgrade);
    void Restart();
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    static bool IsReplayCheckEnabled();
    FString ReplayCheckStatus() const;
    int32 GetAliveEnemyCount() const { return Enemies.Num(); }
    int32 GetEnemyPopulationTarget() const;
#endif
    void GetFirstEchoPath(TArray<FEchoPose>& Out, int32 i = 0) const;
    TArray<EEchoUpgrade> Choices;
    EEchoPhase Phase = EEchoPhase::Running;
    double Elapsed = 0;
    float HP = 12, MaxHP = 12, Speed = 490;
    int32 XP = 0, Level = 1, Kills = 0, EchoCount = 1;
    int32 Threshold() const { return Level + 2; }
    int32 EchoKills = 0;
    const AEchoDrone* GetDrone() const { return Drone.Get(); }
    double GetAuraRadius() const;

    AIsmManager* GetIsmManager() { return IsmManager; }
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FEchoSpawnOverlapTest;
    friend class FEchoCurrentWorldCombatTest;
    friend class FEchoEnemyFireTest;
    friend class FEchoEnemyWalkBlockingTest;
    friend class FEchoUpgradeReplayTest;
    friend class FEchoWeaponUpgradeReplayBoundaryTest;
    friend class FEchoAuraTest;
    friend class FEchoProgressionTest;
    friend class FEchoProjectileProfile;
    friend class FEchoEnemyProfile;
#endif
    void MoveAndAim(float Delta);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    void RelocateTrailingEnemies(const FVector& Movement);
    double NextEnemyRelocation = 0;
    double PerfTravelCm = 0;
    int32 PerfEchoShots[3] = {};
    int32 PerfPlayerHits = 0;
#endif
    void RecordWeapons();
    void ReplayEchoes(float Delta);
    void Fire(const FEchoShot& Shot, int32 Slot);
    void UpdateEnemies(float Delta);
    void SpawnEnemy(TOptional<bool> RangedOverride = {});
    void UpdateCrystals(float Delta);
    void Transition(EEchoPhase Next);
    AEchoUnit* Nearest(FVector From) const;
    UPROPERTY() TObjectPtr<AEchoDrone> Drone;
    UPROPERTY() TArray<TObjectPtr<AEchoUnit>> Enemies;
    UPROPERTY() TArray<TObjectPtr<AActor>> Crystals;
    UPROPERTY() TArray<TObjectPtr<AActor>> Echoes;
    
    UPROPERTY() TObjectPtr<AIsmManager> IsmManager;
    FEchoHistory History;
    FEchoClock Clocks[3];
    int32 Ranks[static_cast<int32>(EEchoWeapon::End)] = {1};
    double NextShot[static_cast<int32>(EEchoWeapon::End)] = {};
    int32 Acceleration = 0;
    double NextSpawn = 0, SafeUntil = 0;
    TSharedPtr<SWidget> Overlay;
    float BlinkElapsed = 0;


public:
    int32 GetEchoesNum() { return Echoes.Num(); }
};

UCLASS()
class AEchoHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    void DrawEchoTrail(const TArray<FEchoPose>& Path, FLinearColor Color);
    void DrawAuraRange(const AEchoGame& Game);
};
