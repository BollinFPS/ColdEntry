#include "CEEntities.h"
#include "ColdEntry.h"
#include "CEAssets.h"
#include "CESound.h"
#include "CETuning.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

static void SetupMesh(UStaticMeshComponent* C, UObject* Ctx, ECESurf Surf)
{
	if (UCEAssets* A = UCEAssets::Get(Ctx))
	{
		C->SetStaticMesh(A->Cube);
		C->SetMaterial(0, A->Surf(Surf));
	}
}

static void BlockAllWithWeapon(UPrimitiveComponent* C)
{
	C->SetCollisionProfileName(TEXT("BlockAll"));
	C->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Block);
}

ACEEntity::ACEEntity()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	Tags.Add(TEXT("CEEntity"));
}

// =============================================================================================
// Door
// =============================================================================================
ACEDoor::ACEDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	Kind = ECEEntityKind::Door;
	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(RootComponent);
	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Hinge);
	Handle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Handle"));
	Handle->SetupAttachment(Hinge);
	Handle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACEDoor::Setup(const FVector& HingeLoc, float InClosedYaw, float Width, float Height)
{
	ClosedYaw = InClosedYaw; W = Width; H = Height;
	SetActorLocation(HingeLoc);
	Hinge->SetRelativeRotation(FRotator(0, ClosedYaw, 0));
	SetupMesh(Panel, this, ECESurf::Wood);
	SetupMesh(Handle, this, ECESurf::Gunmetal);
	Panel->SetRelativeLocation(FVector(W * 0.5f, 0, H * 0.5f));
	Panel->SetRelativeScale3D(FVector((W - 2.f) / 100.f, 0.045f, (H - 1.f) / 100.f));
	Handle->SetRelativeLocation(FVector(W - 12.f, 0, 100.f));
	Handle->SetRelativeScale3D(FVector(0.03f, 0.12f, 0.03f));
	BlockAllWithWeapon(Panel);
	const FVector Dir = FRotator(0, ClosedYaw, 0).Vector();
	OpeningCentre = HingeLoc + Dir * W * 0.5f + FVector(0, 0, H * 0.5f);
	OpeningExtent = FMath::Abs(Dir.X) > 0.5f ? FVector(W * 0.5f, 8.f, H * 0.5f) : FVector(8.f, W * 0.5f, H * 0.5f);
	ResetEntity();
}

void ACEDoor::ResetEntity()
{
	bDestroyed = false; bOpen = false; bMoving = false;
	CurYaw = TargetYaw = 0.f;
	Health = 140.f;
	Hinge->SetRelativeRotation(FRotator(0, ClosedYaw, 0));
	Panel->SetVisibility(true);
	Handle->SetVisibility(true);
	Panel->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Panel->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
}

void ACEDoor::Interact(const FVector& FromLoc)
{
	if (bDestroyed) return;
	SetOpen(!bOpen, FromLoc);
}

void ACEDoor::SetOpen(bool bNewOpen, const FVector& FromLoc)
{
	if (bDestroyed || bNewOpen == bOpen) return;
	bOpen = bNewOpen;
	if (bOpen)
	{
		const FVector N = FRotator(0, ClosedYaw + 90.f, 0).Vector();
		const float Side = FVector::DotProduct(FromLoc - GetActorLocation(), N);
		TargetYaw = Side > 0 ? -90.f : 90.f; // swing away from whoever opens it
	}
	else
	{
		TargetYaw = 0.f;
	}
	bMoving = true;
	Panel->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	if (UCESound* S = UCESound::Get(this)) S->Play(bOpen ? ECESnd::DoorOpen : ECESnd::DoorClose, OpeningCentre);
}

void ACEDoor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bMoving) return;
	CurYaw = FMath::FInterpConstantTo(CurYaw, TargetYaw, DeltaTime, 380.f);
	Hinge->SetRelativeRotation(FRotator(0, ClosedYaw + CurYaw, 0));
	if (FMath::IsNearlyEqual(CurYaw, TargetYaw, 0.1f))
	{
		bMoving = false;
		if (!bDestroyed) Panel->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}

float ACEDoor::TakeDamage(float Damage, FDamageEvent const& Event, AController* Inst, AActor* Causer)
{
	if (bDestroyed) return 0.f;
	Health -= Damage;
	if (Health <= 0.f)
	{
		Shatter();
		if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::WoodBreak, OpeningCentre);
	}
	return Damage;
}

void ACEDoor::Breach(AActor* By)
{
	if (!bDestroyed) Shatter();
}

void ACEDoor::Shatter()
{
	bDestroyed = true; bOpen = true; bMoving = false;
	Panel->SetVisibility(false);
	Handle->SetVisibility(false);
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (ACEFx* Fx = ACEFx::Get(GetWorld())) Fx->Debris(OpeningCentre, OpeningExtent, ECESurf::Wood, 12, FVector::ZeroVector);
}

// =============================================================================================
// Breakable: breach wall / glass / barricade
// =============================================================================================
ACEBreakable::ACEBreakable()
{
	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(RootComponent);
	Blocker->SetCollisionProfileName(TEXT("NoCollision"));
	Plate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
	Plate->SetupAttachment(RootComponent);
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACEBreakable::Setup(ECEEntityKind InKind, const FVector& Centre, const FVector& Half)
{
	Kind = InKind;
	SetActorLocation(Centre);
	OpeningCentre = Centre;
	OpeningExtent = Half;
	const bool bThinX = Half.X < Half.Y;
	auto Thin = [bThinX](const FVector& E, float T) { return bThinX ? FVector(T, E.Y, E.Z) : FVector(E.X, T, E.Z); };

	auto AddPart = [this](const FVector& RelLoc, const FVector& HalfSize, ECESurf Surf) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->RegisterComponent();
		AddInstanceComponent(C);
		SetupMesh(C, this, Surf);
		C->SetRelativeLocation(RelLoc);
		C->SetRelativeScale3D(HalfSize / 50.f);
		BlockAllWithWeapon(C);
		Parts.Add(C);
		return C;
	};

	if (Kind == ECEEntityKind::BreachWall)
	{
		AddPart(FVector::ZeroVector, Thin(Half, 7.f), ECESurf::Drywall);
		// Reinforcement visual: steel plate + two vertical braces, hidden until reinforced
		SetupMesh(Plate, this, ECESurf::Steel);
		Plate->SetRelativeScale3D(Thin(Half, 9.f) / 50.f);
	}
	else if (Kind == ECEEntityKind::Glass)
	{
		UStaticMeshComponent* G = AddPart(FVector::ZeroVector, Thin(Half, 1.2f), ECESurf::Glass);
		G->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore); // see and hear through glass
		G->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		G->SetCastShadow(false);
	}
	else // Barricade: four planks with gaps (partial sightlines), solid blocker for movement
	{
		const int32 Planks = 4;
		const float PlankH = Half.Z * 2.f * 0.15f;
		for (int32 i = 0; i < Planks; ++i)
		{
			const float Z = -Half.Z + PlankH * 0.5f + 12.f + i * (Half.Z * 2.f - PlankH - 24.f) / (Planks - 1);
			FVector HS = Thin(Half, 2.5f); HS.Z = PlankH * 0.5f;
			if (bThinX) HS.Y += 8.f; else HS.X += 8.f;
			UStaticMeshComponent* P = AddPart(FVector(0, 0, Z), HS, ECESurf::Wood);
			P->SetRelativeRotation(FRotator(0, 0, 0));
			P->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
		Blocker->SetBoxExtent(Thin(Half, 4.f));
	}
	ResetEntity();
}

void ACEBreakable::ResetEntity()
{
	bIntact = true;
	bReinforced = false;
	bDeployed = Kind != ECEEntityKind::Barricade;
	Health = Kind == ECEEntityKind::Glass ? 1.f : (Kind == ECEEntityKind::Barricade ? UCETuning::Get()->BarricadeHealth : 1e9f);
	Apply();
}

void ACEBreakable::Apply()
{
	const bool bOn = bIntact && bDeployed;
	for (UStaticMeshComponent* P : Parts)
	{
		if (!P) continue;
		P->SetVisibility(bOn);
		P->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (Kind == ECEEntityKind::BreachWall)
			if (UCEAssets* A = UCEAssets::Get(this)) P->SetMaterial(0, A->Surf(bReinforced ? ECESurf::Steel : ECESurf::Drywall));
	}
	Plate->SetVisibility(bOn && bReinforced);
	if (Kind == ECEEntityKind::Barricade)
	{
		Blocker->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
		Blocker->SetCollisionResponseToChannel(ECC_Pawn, bOn ? ECR_Block : ECR_Ignore);
		Blocker->SetCollisionObjectType(ECC_WorldDynamic);
	}
}

ECEPen ACEBreakable::GetPen() const
{
	switch (Kind)
	{
	case ECEEntityKind::Glass: return ECEPen::Glass;
	case ECEEntityKind::Barricade: return ECEPen::Barricade;
	default: return bReinforced ? ECEPen::Solid : ECEPen::Drywall;
	}
}

FString ACEBreakable::DisplayName() const
{
	switch (Kind)
	{
	case ECEEntityKind::Glass: return TEXT("Window");
	case ECEEntityKind::Barricade: return TEXT("Barricade");
	default: return bReinforced ? TEXT("Reinforced wall") : TEXT("Soft wall");
	}
}

float ACEBreakable::TakeDamage(float Damage, FDamageEvent const& Event, AController* Inst, AActor* Causer)
{
	if (!IsIntact() || Kind == ECEEntityKind::BreachWall) return 0.f;
	Health -= Damage;
	if (Health <= 0.f) Break();
	return Damage;
}

void ACEBreakable::Break(bool bSilent)
{
	if (!IsIntact()) return;
	bIntact = false;
	Apply();
	UCESound* S = UCESound::Get(this);
	ACEFx* Fx = ACEFx::Get(GetWorld());
	if (Kind == ECEEntityKind::Glass)
	{
		if (S && !bSilent) S->Play(ECESnd::GlassBreak, OpeningCentre);
		if (Fx) Fx->Burst(OpeningCentre, 26, 260.f, 2.5f, false, 0.8f);
	}
	else if (Kind == ECEEntityKind::Barricade)
	{
		if (S && !bSilent) S->Play(ECESnd::WoodBreak, OpeningCentre);
		if (Fx) Fx->Debris(OpeningCentre, OpeningExtent, ECESurf::Wood, 8, FVector::ZeroVector);
	}
	else
	{
		if (Fx)
		{
			Fx->Debris(OpeningCentre, OpeningExtent, ECESurf::Drywall, 22, FVector::ZeroVector);
			Fx->Burst(OpeningCentre, 40, 350.f, 30.f, false, 2.5f);
		}
	}
}

void ACEBreakable::Breach(AActor* By)
{
	if (CanBeBreached()) Break(true);
}

void ACEBreakable::Reinforce()
{
	if (Kind != ECEEntityKind::BreachWall || !bIntact || bReinforced) return;
	bReinforced = true;
	Apply();
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Reinforce, OpeningCentre);
}

void ACEBreakable::Deploy()
{
	if (Kind != ECEEntityKind::Barricade || bDeployed) return;
	bDeployed = true; bIntact = true;
	Health = UCETuning::Get()->BarricadeHealth;
	Apply();
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Deploy, OpeningCentre);
}

// =============================================================================================
// Device
// =============================================================================================
ACEDevice::ACEDevice()
{
	PrimaryActorTick.bCanEverTick = true;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Led = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Led"));
	Led->SetupAttachment(Body);
	Led->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Body);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(6.f);
	Light->SetLightColor(FLinearColor(1.f, 0.1f, 0.05f));
	Light->SetAttenuationRadius(250.f);
	Light->SetCastShadows(false);
	Tags.Add(TEXT("CERound"));
}

void ACEDevice::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Body->GetStaticMesh() == nullptr)
	{
		if (UCEAssets* A = UCEAssets::Get(this))
		{
			Body->SetStaticMesh(A->Cube);
			Body->SetMaterial(0, A->Surf(ECESurf::Charcoal));
			Body->SetWorldScale3D(FVector(0.42f, 0.3f, 0.24f));
			Led->SetStaticMesh(A->Cube);
			Led->SetMaterial(0, A->Surf(ECESurf::RedDot));
			Led->SetRelativeLocation(FVector(0, 0, 52.f));
			Led->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.12f));
			Light->SetRelativeLocation(FVector(0, 0, 80.f));
		}
	}
	if (!bActive) { Light->SetVisibility(false); return; }
	TimeLeft -= DeltaTime;
	BeepTimer -= DeltaTime;
	if (BeepTimer <= 0.f)
	{
		BeepTimer = FMath::Lerp(0.18f, 1.0f, FMath::Clamp(TimeLeft / UCETuning::Get()->DeviceTime, 0.f, 1.f));
		if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::PlantBeep, GetActorLocation());
		Light->SetIntensity(14.f);
	}
	Light->SetIntensity(FMath::FInterpTo(Light->Intensity, 1.f, DeltaTime, 10.f));
}

// =============================================================================================
// FX
// =============================================================================================
TWeakObjectPtr<ACEFx> ACEFx::Instance;

ACEFx::ACEFx()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	auto MakeISM = [this](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* I = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		I->SetupAttachment(RootComponent);
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		I->SetCastShadow(false);
		I->SetMobility(EComponentMobility::Movable);
		return I;
	};
	DustISM = MakeISM(TEXT("Dust"));
	SparkISM = MakeISM(TEXT("Sparks"));
	TracerISM = MakeISM(TEXT("Tracers"));
}

ACEFx* ACEFx::Get(UWorld* World)
{
	if (!World) return nullptr;
	if (Instance.IsValid() && Instance->GetWorld() == World) return Instance.Get();
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACEFx* Fx = World->SpawnActor<ACEFx>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	if (Fx)
	{
		if (UCEAssets* A = UCEAssets::Get(World))
		{
			Fx->DustISM->SetStaticMesh(A->Cube);
			Fx->DustISM->SetMaterial(0, A->HasTranslucency() ? (UMaterialInterface*)A->MakeTranslucent(FLinearColor(0.42f, 0.4f, 0.37f), 0.35f) : A->Surf(ECESurf::Concrete));
			Fx->SparkISM->SetStaticMesh(A->Cube);
			Fx->SparkISM->SetMaterial(0, A->Emissive(FLinearColor(1.f, 0.55f, 0.15f), 30.f));
			Fx->TracerISM->SetStaticMesh(A->Cube);
			Fx->TracerISM->SetMaterial(0, A->Emissive(FLinearColor(1.f, 0.85f, 0.6f), 6.f));
		}
	}
	Instance = Fx;
	return Fx;
}

void ACEFx::Burst(const FVector& Loc, int32 Count, float Speed, float Size, bool bSpark, float Life)
{
	for (int32 i = 0; i < Count; ++i)
	{
		FP P;
		P.Pos = Loc;
		P.Vel = FMath::VRand() * FMath::FRandRange(0.3f, 1.f) * Speed;
		P.Dir = FVector::ForwardVector;
		P.MaxLife = P.Life = Life * FMath::FRandRange(0.6f, 1.2f);
		P.Size = Size * FMath::FRandRange(0.6f, 1.3f);
		P.Type = bSpark ? 1 : 0;
		Ps.Add(P);
	}
}

void ACEFx::Impact(const FVector& Loc, const FVector& Normal, ECEPen Pen, bool bMetal)
{
	if (bMetal)
	{
		for (int32 i = 0; i < 7; ++i)
		{
			FP P; P.Pos = Loc + Normal * 2.f;
			P.Vel = (Normal + FMath::VRand() * 0.8f).GetSafeNormal() * FMath::FRandRange(300.f, 900.f);
			P.MaxLife = P.Life = FMath::FRandRange(0.12f, 0.35f); P.Size = 0.7f; P.Type = 1; P.Dir = FVector::ForwardVector;
			Ps.Add(P);
		}
	}
	const int32 N = Pen == ECEPen::Glass ? 4 : 6;
	for (int32 i = 0; i < N; ++i)
	{
		FP P; P.Pos = Loc + Normal * 3.f;
		P.Vel = (Normal + FMath::VRand() * 0.6f).GetSafeNormal() * FMath::FRandRange(60.f, 220.f);
		P.MaxLife = P.Life = FMath::FRandRange(0.5f, 1.1f); P.Size = FMath::FRandRange(2.f, 5.f); P.Type = 0; P.Dir = FVector::ForwardVector;
		Ps.Add(P);
	}
	if (Pen != ECEPen::Glass) Decal(Loc, Normal, 5.f, false);
}

void ACEFx::Tracer(const FVector& From, const FVector& To)
{
	const FVector D = To - From;
	const float Len = D.Size();
	if (Len < 400.f) return;
	FP P; P.Pos = From; P.Dir = D / Len; P.Vel = P.Dir * 40000.f;
	P.MaxLife = P.Life = Len / 40000.f; P.Size = 1.f; P.Type = 2;
	Ps.Add(P);
}

void ACEFx::Decal(const FVector& Loc, const FVector& Normal, float Size, bool bScorch)
{
	UCEAssets* A = UCEAssets::Get(this);
	UMaterialInterface* M = A ? A->DecalMaterial(bScorch) : nullptr;
	if (!M) return;
	UDecalComponent* D = UGameplayStatics::SpawnDecalAtLocation(GetWorld(), M, FVector(Size * 0.6f, Size, Size), Loc, (-Normal).Rotation(), bScorch ? 60.f : 40.f);
	if (D) D->SetFadeScreenSize(0.f);
}

void ACEFx::Debris(const FVector& Centre, const FVector& Extent, ECESurf Surf, int32 Count, const FVector& Push)
{
	UCEAssets* A = UCEAssets::Get(this);
	if (!A) return;
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Loc = Centre + FVector(FMath::FRandRange(-Extent.X, Extent.X), FMath::FRandRange(-Extent.Y, Extent.Y), FMath::FRandRange(-Extent.Z, Extent.Z) * 0.8f);
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* D = GetWorld()->SpawnActor<AStaticMeshActor>(Loc, FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f), P);
		if (!D) continue;
		D->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* C = D->GetStaticMeshComponent();
		C->SetStaticMesh(A->Cube);
		C->SetMaterial(0, A->Surf(Surf));
		C->SetWorldScale3D(FVector(FMath::FRandRange(0.05f, 0.22f), FMath::FRandRange(0.04f, 0.16f), FMath::FRandRange(0.02f, 0.12f)));
		C->SetCollisionProfileName(TEXT("PhysicsActor"));
		C->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		C->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Ignore);
		C->SetSimulatePhysics(true);
		C->AddImpulse(Push + FMath::VRand() * FMath::FRandRange(80.f, 350.f), NAME_None, true);
		D->SetLifeSpan(FMath::FRandRange(6.f, 10.f));
		D->Tags.Add(TEXT("CERound"));
	}
}

void ACEFx::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	DustISM->ClearInstances();
	SparkISM->ClearInstances();
	TracerISM->ClearInstances();
	for (int32 i = Ps.Num() - 1; i >= 0; --i)
	{
		FP& P = Ps[i];
		P.Life -= DeltaTime;
		if (P.Life <= 0.f) { Ps.RemoveAtSwap(i); continue; }
		const float A = P.Life / P.MaxLife;
		if (P.Type == 0) // dust: drag, slight rise, grows
		{
			P.Vel *= FMath::Exp(-4.f * DeltaTime);
			P.Vel.Z += 15.f * DeltaTime;
			P.Pos += P.Vel * DeltaTime;
			const float S = P.Size * (1.f + (1.f - A) * 2.f) / 100.f;
			DustISM->AddInstance(FTransform(FRotator(P.Life * 90.f, P.Life * 50.f, 0), P.Pos, FVector(S)), true);
		}
		else if (P.Type == 1) // sparks / glass shards: gravity
		{
			P.Vel.Z -= 980.f * DeltaTime;
			P.Pos += P.Vel * DeltaTime;
			const float S = P.Size / 100.f;
			SparkISM->AddInstance(FTransform(P.Vel.Rotation(), P.Pos, FVector(S * 3.f, S, S)), true);
		}
		else // tracer streak 2.5 m long
		{
			P.Pos += P.Vel * DeltaTime;
			TracerISM->AddInstance(FTransform(P.Dir.Rotation(), P.Pos - P.Dir * 125.f, FVector(2.5f, 0.012f, 0.012f)), true);
		}
	}
}
