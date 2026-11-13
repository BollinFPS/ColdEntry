#include "CEGadgets.h"
#include "ColdEntry.h"
#include "CEAssets.h"
#include "CESound.h"
#include "CETuning.h"
#include "CECharacter.h"
#include "CEEntities.h"
#include "CEGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"

static ACEGameMode* GMOf(const UObject* O) { return O && O->GetWorld() ? O->GetWorld()->GetAuthGameMode<ACEGameMode>() : nullptr; }

// =============================================================================================
ACEGadget::ACEGadget()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tags.Add(TEXT("CERound"));
}

void ACEGadget::BeginPlay()
{
	Super::BeginPlay();
	BuildVisual();
}

UStaticMeshComponent* ACEGadget::AddPart(USceneComponent* Parent, const FVector& Loc, const FVector& Half, ECESurf Surf)
{
	UCEAssets* A = UCEAssets::Get(this);
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent);
	C->SetStaticMesh(A->Cube);
	C->SetMaterial(0, A->SurfMoving(Surf));
	C->SetRelativeLocation(Loc);
	C->SetAbsolute(false, false, true);
	C->SetWorldScale3D(Half / 50.f);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	AddInstanceComponent(C);
	return C;
}

void ACEGadget::MakeShootable(UPrimitiveComponent* C)
{
	C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	C->SetCollisionObjectType(ECC_WorldDynamic);
	C->SetCollisionResponseToAllChannels(ECR_Ignore);
	C->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Block);
}

float ACEGadget::TakeDamage(float Damage, FDamageEvent const& Event, AController* Inst, AActor* Causer)
{
	if (bWrecked) return 0.f;
	Health -= Damage;
	if (Health <= 0.f) Wreck();
	return Damage;
}

void ACEGadget::Wreck()
{
	if (bWrecked) return;
	bWrecked = true;
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::ImpactMetal, GetActorLocation(), 0.8f);
	if (ACEFx* Fx = ACEFx::Get(GetWorld())) Fx->Burst(GetActorLocation(), 10, 400.f, 0.8f, true, 0.4f);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(0.5f);
}

// =============================================================================================
// Grenade
// =============================================================================================
ACEGrenade::ACEGrenade()
{
	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(5.f);
	Sphere->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Ignore);
	RootComponent = Sphere;
	Mesh->SetupAttachment(Sphere);
	Move = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Move"));
	Move->UpdatedComponent = Sphere;
	Move->bShouldBounce = true;
	Move->Bounciness = 0.3f;
	Move->Friction = 0.45f;
	Move->ProjectileGravityScale = 1.f;
	Move->InitialSpeed = 0.f;
	Move->MaxSpeed = 3000.f;
	Move->bAutoActivate = false;
}

void ACEGrenade::BuildVisual()
{
	UCEAssets* A = UCEAssets::Get(this);
	Mesh->SetStaticMesh(A->Cylinder);
	Mesh->SetMaterial(0, A->SurfMoving(Type == ECEGadget::Flash ? ECESurf::Olive : ECESurf::Charcoal));
	Mesh->SetRelativeScale3D(FVector(0.065f, 0.065f, 0.11f));
}

void ACEGrenade::Launch(ECEGadget InType, const FVector& Velocity)
{
	Type = InType;
	Fuse = Type == ECEGadget::Flash ? UCETuning::Get()->FlashFuse : 0.9f;
	BuildVisual();
	Move->Velocity = Velocity;
	Move->Activate(true);
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Throw, GetActorLocation(), 0.8f);
}

void ACEGrenade::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDone) return;
	Fuse -= DeltaTime;
	if (Fuse <= 0.f)
	{
		bDone = true;
		if (ACEGameMode* G = GMOf(this)) G->GrenadeDetonate(this);
		SetActorHiddenInGame(true);
		Move->StopMovementImmediately();
		SetLifeSpan(0.2f);
	}
}

// =============================================================================================
// Smoke
// =============================================================================================
ACESmoke::ACESmoke()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Tags.Add(TEXT("CERound"));
}

void ACESmoke::BeginPlay()
{
	Super::BeginPlay();
	UCEAssets* A = UCEAssets::Get(this);
	Radius = UCETuning::Get()->SmokeRadius;
	Life = UCETuning::Get()->SmokeDuration;
	Mat = A->MakeTranslucent(FLinearColor(0.5f, 0.5f, 0.49f), 0.0f);
	for (int32 i = 0; i < 26; ++i)
	{
		UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(this);
		P->SetupAttachment(RootComponent);
		P->SetStaticMesh(A->Sphere);
		P->SetMaterial(0, A->HasTranslucency() ? (UMaterialInterface*)Mat : A->SurfMoving(ECESurf::Ceiling));
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetCastShadow(false);
		P->RegisterComponent();
		AddInstanceComponent(P);
		const FVector2D R2 = FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal() * FMath::Sqrt(FMath::FRand()) * Radius * 0.8f;
		Offsets.Add(FVector(R2.X, R2.Y, FMath::FRandRange(20.f, 220.f)));
		Puffs.Add(P);
	}
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Throw, GetActorLocation(), 1.f);
}

bool ACESmoke::Blocks(const FVector& A, const FVector& B) const
{
	if (Age < 0.8f || Age > Life - 1.0f) return false;
	const FVector C = GetActorLocation() + FVector(0, 0, 120.f);
	const float R = Radius * 0.85f;
	const FVector D = B - A;
	const float T = FMath::Clamp(FVector::DotProduct(C - A, D) / FMath::Max(1.f, D.SizeSquared()), 0.f, 1.f);
	return FVector::DistSquared(A + D * T, C) < R * R;
}

void ACESmoke::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
	const float Grow = FMath::Clamp(Age / 1.6f, 0.f, 1.f);
	const float Fade = FMath::Clamp((Life - Age) / 2.f, 0.f, 1.f);
	if (Mat) Mat->SetScalarParameterValue(TEXT("Opacity"), 0.55f * Fade * FMath::Min(1.f, Age * 2.f));
	for (int32 i = 0; i < Puffs.Num(); ++i)
	{
		const float S = (1.6f + 0.6f * FMath::Sin(Age * 0.4f + i)) * Grow;
		Puffs[i]->SetRelativeLocation(Offsets[i] * (0.4f + 0.6f * Grow) + FVector(0, 0, Age * 4.f));
		Puffs[i]->SetRelativeScale3D(FVector(S));
	}
	if (Age >= Life) Destroy();
}

// =============================================================================================
// Breach charge
// =============================================================================================
ACEBreachCharge::ACEBreachCharge()
{
	Led = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Led"));
	Led->SetupAttachment(Mesh);
	Led->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACEBreachCharge::BuildVisual()
{
	UCEAssets* A = UCEAssets::Get(this);
	Mesh->SetStaticMesh(A->Cube);
	Mesh->SetMaterial(0, A->SurfMoving(ECESurf::Hazard));
	Mesh->SetWorldScale3D(FVector(0.06f, 0.24f, 0.16f));
	MakeShootable(Mesh);
	Led->SetStaticMesh(A->Cube);
	Led->SetMaterial(0, A->SurfMoving(ECESurf::RedDot));
	Led->SetRelativeLocation(FVector(55, 0, 30));
	Led->SetRelativeScale3D(FVector(0.4f, 0.12f, 0.12f));
}

void ACEBreachCharge::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Blink += DeltaTime * (bArmed ? 12.f : 2.f);
	Led->SetVisibility(FMath::Frac(Blink) < 0.5f);
	if (bArmed && !bWrecked)
	{
		FuseLeft -= DeltaTime;
		if (FuseLeft <= 0.f)
		{
			bArmed = false;
			if (ACEGameMode* G = GMOf(this)) G->ExplodeCharge(this);
		}
	}
}

// =============================================================================================
// Security camera
// =============================================================================================
ACESecurityCamera::ACESecurityCamera()
{
	View = CreateDefaultSubobject<UCameraComponent>(TEXT("View"));
	View->SetupAttachment(Mesh);
	View->SetFieldOfView(80.f);
	Lamp = CreateDefaultSubobject<UPointLightComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Mesh);
	Lamp->SetIntensityUnits(ELightUnits::Candelas);
	Lamp->SetIntensity(0.4f);
	Lamp->SetAttenuationRadius(60.f);
	Lamp->SetCastShadows(false);
	Lamp->SetLightColor(FLinearColor(1.f, 0.1f, 0.05f));
}

void ACESecurityCamera::BuildVisual()
{
	UCEAssets* A = UCEAssets::Get(this);
	Mesh->SetStaticMesh(A->Cube);
	Mesh->SetMaterial(0, A->SurfMoving(bFixed ? ECESurf::Paint : ECESurf::Charcoal));
	Mesh->SetWorldScale3D(bFixed ? FVector(0.26f, 0.12f, 0.12f) : FVector(0.12f, 0.08f, 0.08f));
	MakeShootable(Mesh);
	UStaticMeshComponent* Lens = AddPart(Mesh, FVector(52, 0, 0), FVector(1.5f, 3.5f, 3.5f), ECESurf::Polymer);
	Lens->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	View->SetRelativeLocation(FVector(60, 0, 0));
	Lamp->SetRelativeLocation(FVector(40, 0, 60));
	BaseYaw = GetActorRotation().Yaw;
	BasePitch = GetActorRotation().Pitch;
}

void ACESecurityCamera::Look(float DYaw, float DPitch)
{
	LookYaw = FMath::Clamp(LookYaw + DYaw, -60.f, 60.f);
	LookPitch = FMath::Clamp(LookPitch + DPitch, -35.f, 25.f);
	SetActorRotation(FRotator(BasePitch + LookPitch, BaseYaw + LookYaw, 0));
}

void ACESecurityCamera::Wreck()
{
	Super::Wreck();
	Lamp->SetVisibility(false);
}

// =============================================================================================
// Proximity alarm
// =============================================================================================
ACEAlarm::ACEAlarm()
{
	Led = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Led"));
	Led->SetupAttachment(Mesh);
	Led->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACEAlarm::BuildVisual()
{
	UCEAssets* A = UCEAssets::Get(this);
	Mesh->SetStaticMesh(A->Cube);
	Mesh->SetMaterial(0, A->SurfMoving(ECESurf::Olive));
	Mesh->SetWorldScale3D(FVector(0.09f, 0.09f, 0.04f));
	MakeShootable(Mesh);
	Led->SetStaticMesh(A->Cube);
	Led->SetMaterial(0, A->Emissive(FLinearColor(0.1f, 1.f, 0.2f), 20.f));
	Led->SetRelativeLocation(FVector(0, 0, 55));
	Led->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.3f));
}

void ACEAlarm::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bWrecked) return;
	Cooldown -= DeltaTime;
	CheckT -= DeltaTime;
	Led->SetVisibility(FMath::Frac(GetWorld()->GetTimeSeconds() * 0.8f) < 0.12f || Cooldown > 0.f);
	if (CheckT > 0.f || Cooldown > 0.f) return;
	CheckT = 0.2f;
	ACEGameMode* G = GMOf(this);
	if (!G) return;
	const float R = UCETuning::Get()->AlarmRadius;
	for (ACECharacter* C : G->Characters)
	{
		if (!C || !C->IsAlive() || C->Team == Team) continue;
		if (FVector::Dist(C->GetActorLocation(), GetActorLocation()) > R) continue;
		const bool bSneaking = C->IsCrouched() && C->GetVelocity().Size2D() < 150.f;
		if (bSneaking) continue;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(CEAlarm), false, this);
		if (GetWorld()->LineTraceTestByChannel(GetActorLocation() + FVector(0, 0, 10), C->ChestLocation(), ECC_Visibility, Q)) continue;
		if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Beep, GetActorLocation(), 1.f);
		G->AddIntel(Team, C, C->GetActorLocation(), true, true);
		Cooldown = 1.2f;
		break;
	}
}

// =============================================================================================
// Drone
// =============================================================================================
ACEDrone::ACEDrone()
{
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(13, 11, 6));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_WorldDynamic);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Body->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Block);
	RootComponent = Body;
	Mesh->SetupAttachment(Body);
	View = CreateDefaultSubobject<UCameraComponent>(TEXT("View"));
	View->SetupAttachment(Body);
	View->SetRelativeLocation(FVector(10, 0, 5));
	View->SetFieldOfView(95.f);
}

void ACEDrone::BuildVisual()
{
	UCEAssets* A = UCEAssets::Get(this);
	Mesh->SetStaticMesh(A->Cube);
	Mesh->SetMaterial(0, A->SurfMoving(ECESurf::Charcoal));
	Mesh->SetWorldScale3D(FVector(0.24f, 0.14f, 0.08f));
	for (int32 s = -1; s <= 1; s += 2)
	{
		UStaticMeshComponent* W = AddPart(Body, FVector(0, s * 12.f, -1), FVector(7, 2.5f, 7), ECESurf::Rubber);
		W->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	UStaticMeshComponent* Eye = AddPart(Body, FVector(13, 0, 3), FVector(1, 2.5f, 2), ECESurf::RedDot);
	Eye->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Yaw = GetActorRotation().Yaw;
}

void ACEDrone::Drive(float Forward, float Right, float DYaw, float DPitch, float Dt)
{
	if (bWrecked) return;
	Yaw += DYaw;
	Pitch = FMath::Clamp(Pitch + DPitch, -40.f, 40.f);
	const FRotator R(0, Yaw, 0);
	const FVector Want = (FRotationMatrix(R).GetUnitAxis(EAxis::X) * Forward + FRotationMatrix(R).GetUnitAxis(EAxis::Y) * Right).GetClampedToMaxSize(1.f) * UCETuning::Get()->DroneSpeed;
	Vel.X = FMath::FInterpTo(Vel.X, Want.X, Dt, 6.f);
	Vel.Y = FMath::FInterpTo(Vel.Y, Want.Y, Dt, 6.f);
	bMoving = Want.SizeSquared() > 1.f;
	SetActorRotation(FRotator(0, Yaw, 0));
	View->SetRelativeRotation(FRotator(Pitch, 0, 0));
}

void ACEDrone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bWrecked) return;
	Vel.Z -= 980.f * DeltaTime;
	FVector Delta = Vel * DeltaTime;
	FHitResult H;
	AddActorWorldOffset(Delta, true, &H);
	if (H.bBlockingHit)
	{
		if (H.ImpactNormal.Z > 0.6f) Vel.Z = 0.f;
		const FVector Remain = FVector::VectorPlaneProject(Delta * (1.f - H.Time), H.ImpactNormal);
		FHitResult H2;
		AddActorWorldOffset(Remain, true, &H2);
		if (H2.bBlockingHit && H2.ImpactNormal.Z > 0.6f) Vel.Z = 0.f;
	}
	if (!bMoving) { Vel.X *= FMath::Exp(-8.f * DeltaTime); Vel.Y *= FMath::Exp(-8.f * DeltaTime); }
	bMoving = false;
	HumT -= DeltaTime;
	if (Vel.Size2D() > 50.f && HumT <= 0.f)
	{
		HumT = 0.45f;
		if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::DroneHum, GetActorLocation(), 0.6f);
	}
}
