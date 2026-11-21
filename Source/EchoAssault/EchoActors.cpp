#include "EchoGame.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace
{
bool UsesEnemyPhysics()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyPhysics")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("EchoEnemyBlocking"));
    return bEnabled;
#else
    return true;
#endif
}
}

AEchoDrone::AEchoDrone()
{
    auto* Sphere = CreateDefaultSubobject<USphereComponent>("Hull");
    SetRootComponent(Sphere);
    Sphere->InitSphereRadius(32);
    Sphere->SetCollisionProfileName("OverlapAllDynamic");
    Body = CreateDefaultSubobject<UStaticMeshComponent>("Body");
    Body->SetupAttachment(Sphere);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Arm = CreateDefaultSubobject<USpringArmComponent>("CameraArm");
    Arm->SetupAttachment(Sphere);
    Arm->SetUsingAbsoluteRotation(true);
    Arm->SetRelativeRotation(FRotator(-63, 0, 0));
    Arm->TargetArmLength = 1650;
    Arm->bDoCollisionTest = false;
    auto* Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
    Camera->SetupAttachment(Arm);
    Camera->FieldOfView = 62;
}

AEchoUnit::AEchoUnit()
{
    Sphere = CreateDefaultSubobject<USphereComponent>("Hull");
    SetRootComponent(Sphere);
    Sphere->InitSphereRadius(EchoEnemyCollisionRadius);
    Sphere->SetCollisionProfileName("OverlapAllDynamic");
    Sphere->SetCollisionObjectType(EchoEnemyCollisionChannel);
    Sphere->SetCollisionResponseToChannel(EchoEnemyCollisionChannel, ECR_Block);
    const bool bEnemyPhysics = UsesEnemyPhysics();
    if (bEnemyPhysics)
    {
        Sphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Sphere->SetEnableGravity(false);
    }
    Body = CreateDefaultSubobject<UStaticMeshComponent>("Body");
    Body->SetupAttachment(Sphere);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (bEnemyPhysics) Body->SetUsingAbsoluteRotation(true);
}

void AEchoUnit::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    if (UsesEnemyPhysics())
    {
        Sphere->SetSimulatePhysics(true);
        Sphere->SetConstraintMode(EDOFMode::None);
    }
}

AEchoBolt::AEchoBolt()
{
    
    Sphere = CreateDefaultSubobject<USphereComponent>("Impact");
    SetRootComponent(Sphere);
    Sphere->InitSphereRadius(9);
    Sphere->SetCollisionProfileName("OverlapAllDynamic");
    Sphere->OnComponentBeginOverlap.AddDynamic(this, &AEchoBolt::Contact);
    //Body = CreateDefaultSubobject<UStaticMeshComponent>("Plasma");
    //Body->SetupAttachment(Sphere);
    //Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Flight = CreateDefaultSubobject<UProjectileMovementComponent>("Flight");
    Flight->UpdatedComponent = Sphere;
    Flight->ProjectileGravityScale = 0;
    Flight->bRotationFollowsVelocity = true;
    InitialLifeSpan = 2.6f;
}

void AEchoBolt::Contact(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    auto* Game = GetWorld()->GetAuthGameMode<AEchoGame>();
    if (!Game || Game->Phase != EEchoPhase::Running || IsActorBeingDestroyed()) return;
    if (bEnemy && Cast<AEchoDrone>(Other)) Game->HitPlayer(Damage);
    else if (!bEnemy && Cast<AEchoUnit>(Other)) Game->HitUnit(CastChecked<AEchoUnit>(Other), Damage, OwnerSlot);
    else return;
    Destroy();
}



void AEchoBolt::EndPlay(const EEndPlayReason::Type Reason) {
    auto* Game = GetWorld()->GetAuthGameMode<AEchoGame>();
    if (Game && Game->GetIsmManager()) {
        Game->GetIsmManager()->RemoveInstance(BoltMesh,this ,BoltColor, ID);
    }
    Super::EndPlay(Reason);
}
