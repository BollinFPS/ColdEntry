#include "CECharacter.h"
#include "ColdEntry.h"
#include "CETuning.h"
#include "CEAssets.h"
#include "CESound.h"
#include "CEMap.h"
#include "CEEntities.h"
#include "CEGameMode.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "CollisionQueryParams.h"

static const UCETuning* TU() { return UCETuning::Get(); }

ACECharacter::ACECharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(30.f, 90.f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Ignore);
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -90.f), FRotator(0, -90.f, 0)); // template mannequin: feet at origin, faces +Y

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	BaseEyeHeight = 74.f;
	CrouchedEyeHeight = 45.f;

	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->GetNavAgentPropertiesRef().bCanCrouch = true;
	CMC->SetCrouchedHalfHeight(61.f);
	CMC->bCanWalkOffLedgesWhenCrouching = true;
	CMC->MaxStepHeight = 45.f;
	CMC->JumpZVelocity = 0.f;
	CMC->AirControl = 0.15f;
	CMC->GravityScale = 1.6f;
	CMC->bOrientRotationToMovement = false;
	CMC->GroundFriction = 10.f;
	CMC->bUseSeparateBrakingFriction = false;
	CMC->PerchRadiusThreshold = 10.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0, 0, 74.f));
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(90.f);

	ViewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ViewRoot"));
	ViewRoot->SetupAttachment(Camera);

	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(GetCapsuleComponent());
	BodyRoot->SetRelativeLocation(FVector(0, 0, -90.f));
	Pelvis = CreateDefaultSubobject<USceneComponent>(TEXT("Pelvis"));
	Pelvis->SetupAttachment(BodyRoot);
	Pelvis->SetRelativeLocation(FVector(0, 0, 92.f));
	Upper = CreateDefaultSubobject<USceneComponent>(TEXT("Upper"));
	Upper->SetupAttachment(Pelvis);
	Upper->SetRelativeLocation(FVector(0, 0, 8.f));
	HeadPivot = CreateDefaultSubobject<USceneComponent>(TEXT("HeadPivot"));
	HeadPivot->SetupAttachment(Upper);
	HeadPivot->SetRelativeLocation(FVector(0, 0, 56.f));
	LegL = CreateDefaultSubobject<USceneComponent>(TEXT("LegL"));
	LegL->SetupAttachment(BodyRoot);
	LegR = CreateDefaultSubobject<USceneComponent>(TEXT("LegR"));
	LegR->SetupAttachment(BodyRoot);

	auto MakeHitbox = [this](const TCHAR* Name, USceneComponent* Parent, const FVector& Loc, const FVector& Ext)
	{
		UBoxComponent* B = CreateDefaultSubobject<UBoxComponent>(Name);
		B->SetupAttachment(Parent);
		B->SetRelativeLocation(Loc);
		B->SetBoxExtent(Ext);
		B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		B->SetCollisionObjectType(ECC_Pawn);
		B->SetCollisionResponseToAllChannels(ECR_Ignore);
		B->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Block);
		B->SetGenerateOverlapEvents(false);
		B->SetCanEverAffectNavigation(false);
		return B;
	};
	HeadBox = MakeHitbox(TEXT("HeadBox"), HeadPivot, FVector(1, 0, 16), FVector(12, 11, 13));
	TorsoBox = MakeHitbox(TEXT("TorsoBox"), Upper, FVector(2, 0, 27), FVector(14, 20, 29));
	LegsBox = MakeHitbox(TEXT("LegsBox"), BodyRoot, FVector(0, 0, 46), FVector(12, 20, 46));
	ArmsBox = MakeHitbox(TEXT("ArmsBox"), Upper, FVector(24, 2, 34), FVector(16, 16, 8));

	MuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight"));
	MuzzleLight->SetupAttachment(Camera);
	MuzzleLight->SetRelativeLocation(FVector(70, 8, -10));
	MuzzleLight->SetIntensityUnits(ELightUnits::Candelas);
	MuzzleLight->SetIntensity(0.f);
	MuzzleLight->SetAttenuationRadius(650.f);
	MuzzleLight->SetCastShadows(false);
	MuzzleLight->SetLightColor(FLinearColor(1.f, 0.72f, 0.42f));
}

ACEGameMode* ACECharacter::GM() const { return GetWorld() ? GetWorld()->GetAuthGameMode<ACEGameMode>() : nullptr; }
float ACECharacter::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f; }
const FCEWeaponDef& ACECharacter::CurDef() const { return TU()->Weapon(Weapons[Slot].Id); }
bool ACECharacter::IsBlind() const { return Now() < BlindUntil; }
float ACECharacter::BlindAmount() const { return FMath::Clamp((BlindUntil - Now()) / FMath::Max(0.5f, BlindTotal * 0.6f), 0.f, 1.f); }

FVector ACECharacter::EyeLocation() const { return Camera->GetComponentLocation(); }
FRotator ACECharacter::AimRotation() const
{
	if (const AController* C = GetController()) return C->GetControlRotation();
	return GetActorRotation();
}
FVector ACECharacter::HeadLocation() const { return HeadBox->GetComponentLocation(); }
FVector ACECharacter::ChestLocation() const { return TorsoBox->GetComponentLocation() + FVector(0, 0, 6); }

// =============================================================================================
// Setup
// =============================================================================================
UStaticMeshComponent* ACECharacter::Part(USceneComponent* Parent, const FVector& Loc, const FVector& Half, ECESurf Surf, bool bFirstPerson, const FRotator& Rot)
{
	UCEAssets* A = UCEAssets::Get(this);
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent);
	C->SetStaticMesh(A->Cube);
	C->SetMaterial(0, A->SurfMoving(Surf));
	C->SetRelativeLocation(Loc);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(Half / 50.f);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	if (bFirstPerson) { C->SetOnlyOwnerSee(true); C->SetCastShadow(false); C->bCastHiddenShadow = false; }
	else { C->SetOwnerNoSee(true); C->bCastHiddenShadow = true; }
	C->RegisterComponent();
	AddInstanceComponent(C);
	return C;
}

void ACECharacter::InitLoadout(ECETeam InTeam, ECEWeaponId Primary, const FString& InName, bool bInIsPlayer)
{
	Team = InTeam; CEName = InName; bIsPlayer = bInIsPlayer;
	Health = TU()->MaxHealth;
	Weapons[0].Id = Primary; Weapons[0].Mag = TU()->Weapon(Primary).Mag; Weapons[0].Reserve = TU()->Weapon(Primary).Reserve;
	Weapons[1].Id = ECEWeaponId::P9; Weapons[1].Mag = TU()->P9.Mag; Weapons[1].Reserve = TU()->P9.Reserve;
	Slot = 0; Equipped = ECEGadget::None;
	FMemory::Memzero(GadgetCount, sizeof(GadgetCount));
	if (Team == ECETeam::Attack)
	{
		GadgetSlots[0] = ECEGadget::BreachCharge; GadgetSlots[1] = ECEGadget::Flash; GadgetSlots[2] = ECEGadget::Smoke;
		GadgetCount[(int32)ECEGadget::BreachCharge] = TU()->BreachCharges;
		GadgetCount[(int32)ECEGadget::Flash] = TU()->Flashbangs;
		GadgetCount[(int32)ECEGadget::Smoke] = TU()->Smokes;
		GadgetCount[(int32)ECEGadget::Drone] = 1;
	}
	else
	{
		GadgetSlots[0] = ECEGadget::Alarm; GadgetSlots[1] = ECEGadget::Camera; GadgetSlots[2] = ECEGadget::Barricade;
		GadgetCount[(int32)ECEGadget::Alarm] = TU()->Alarms;
		GadgetCount[(int32)ECEGadget::Camera] = TU()->Cameras;
		GadgetCount[(int32)ECEGadget::Barricade] = TU()->Barricades;
	}
	GadgetCount[(int32)ECEGadget::Medkit] = 1;

	if (!bBuilt)
	{
		bRealBody = BuildRealBody();
		if (!bRealBody) BuildBody();
		bRealVM = BuildRealViewmodels();
		if (!bRealVM) BuildViewmodels();
		bBuilt = true;
	}
	UCEAssets* A = UCEAssets::Get(this);
	const bool bAtk = Team == ECETeam::Attack;
	for (UStaticMeshComponent* P : UniformParts) P->SetMaterial(0, A->SurfMoving(bAtk ? ECESurf::Tan : ECESurf::Navy));
	for (UStaticMeshComponent* P : SleeveParts) P->SetMaterial(0, A->SurfMoving(bAtk ? ECESurf::Tan : ECESurf::Navy));
	for (UStaticMeshComponent* P : GearParts) P->SetMaterial(0, A->SurfMoving(bAtk ? ECESurf::Charcoal : ECESurf::Polymer));
	for (UStaticMeshComponent* P : BandParts) P->SetMaterial(0, A->SurfMoving(bAtk ? ECESurf::Hazard : ECESurf::Ceiling));
	ApplyRealTeamLook();

	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->MaxAcceleration = TU()->Acceleration;
	CMC->BrakingDecelerationWalking = TU()->BrakingDecel;
	CMC->MaxWalkSpeed = TU()->WalkSpeed;
	CMC->MaxWalkSpeedCrouched = TU()->CrouchSpeed;
	EyeZ = TU()->EyeHeightStand;
	EquipEnd = Now() + 0.3f;
}

void ACECharacter::BuildBody()
{
	const FVector LegHalf(8, 8, 45);
	for (int32 s = -1; s <= 1; s += 2)
	{
		USceneComponent* Leg = s < 0 ? LegL.Get() : LegR.Get();
		Leg->SetRelativeLocation(FVector(0, s * 11.f, 92.f));
		UniformParts.Add(Part(Leg, FVector(0, 0, -45), LegHalf, ECESurf::Olive, false));
		Part(Leg, FVector(4, 0, -86), FVector(12, 8, 5), ECESurf::Rubber, false);
		GearParts.Add(Part(Leg, FVector(9, 0, -48), FVector(2.5f, 7, 6), ECESurf::Charcoal, false)); // knee pad
	}
	UniformParts.Add(Part(Pelvis, FVector(0, 0, 0), FVector(12, 18, 10), ECESurf::Olive, false));
	UniformParts.Add(Part(Upper, FVector(0, 0, 26), FVector(11, 18, 26), ECESurf::Tan, false));
	GearParts.Add(Part(Upper, FVector(2, 0, 30), FVector(14, 19, 18), ECESurf::Charcoal, false));          // plate carrier
	GearParts.Add(Part(Upper, FVector(15, 0, 21), FVector(3, 15, 6), ECESurf::Charcoal, false));           // pouches
	GearParts.Add(Part(Upper, FVector(-14, 0, 32), FVector(4, 13, 12), ECESurf::Charcoal, false));         // back panel
	UniformParts.Add(Part(Upper, FVector(6, 22, 38), FVector(6, 6, 14), ECESurf::Tan, false));
	UniformParts.Add(Part(Upper, FVector(6, -22, 38), FVector(6, 6, 14), ECESurf::Tan, false));
	UniformParts.Add(Part(Upper, FVector(20, 15, 28), FVector(14, 5, 5), ECESurf::Tan, false, FRotator(0, -20, 0)));
	UniformParts.Add(Part(Upper, FVector(28, -8, 33), FVector(16, 5, 5), ECESurf::Tan, false, FRotator(0, 22, 0)));
	BandParts.Add(Part(Upper, FVector(6, -22.5f, 45), FVector(6.6f, 6.6f, 2.2f), ECESurf::Hazard, false));
	Part(HeadPivot, FVector(0, 0, 2), FVector(6, 6, 5), ECESurf::Rubber, false);
	Part(HeadPivot, FVector(1, 0, 14), FVector(10, 9, 11), ECESurf::Rubber, false);                       // balaclava
	Part(HeadPivot, FVector(10.5f, 0, 16), FVector(1.5f, 7, 2.6f), ECESurf::Polymer, false);              // eyewear
	GearParts.Add(Part(HeadPivot, FVector(0, 0, 25), FVector(12.5f, 11.5f, 6), ECESurf::Charcoal, false)); // helmet
	Part(HeadPivot, FVector(0, 10.5f, 14), FVector(3, 1.8f, 4), ECESurf::Polymer, false);
	Part(HeadPivot, FVector(0, -10.5f, 14), FVector(3, 1.8f, 4), ECESurf::Polymer, false);

	TPGun = NewObject<USceneComponent>(this);
	TPGun->SetupAttachment(Upper);
	TPGun->SetRelativeLocation(FVector(36, 6, 36));
	TPGun->RegisterComponent();
	AddInstanceComponent(TPGun);
	Part(TPGun, FVector(0, 0, 0), FVector(20, 2, 4), ECESurf::Gunmetal, false);
	Part(TPGun, FVector(26, 0, 1), FVector(8, 1, 1), ECESurf::Gunmetal, false);
	Part(TPGun, FVector(4, 0, -8), FVector(2.5f, 1.5f, 5), ECESurf::Polymer, false);
	Part(TPGun, FVector(-24, 0, -1), FVector(7, 1.6f, 3), ECESurf::Polymer, false);
	Part(TPGun, FVector(2, 0, 5), FVector(4, 1.8f, 2), ECESurf::Gunmetal, false);
	TPFlash = Part(TPGun, FVector(38, 0, 1), FVector(4, 4, 4), ECESurf::Emissive, false);
	UCEAssets* A = UCEAssets::Get(this);
	TPFlash->SetMaterial(0, A->Emissive(FLinearColor(1.f, 0.6f, 0.25f), 60.f));
	TPFlash->SetVisibility(false);
	TPFlash->SetCastShadow(false);
}

void ACECharacter::BuildViewmodels()
{
	UCEAssets* A = UCEAssets::Get(this);
	UMaterialInterface* FlashMat = A->Emissive(FLinearColor(1.f, 0.62f, 0.28f), 80.f);
	auto Root = [this]()
	{
		USceneComponent* R = NewObject<USceneComponent>(this);
		R->SetupAttachment(ViewRoot);
		R->RegisterComponent();
		AddInstanceComponent(R);
		return R;
	};
	auto Optic = [&](USceneComponent* R, float X)
	{
		Part(R, FVector(X, 0, 1.75f), FVector(3.2f, 1.7f, 0.3f), ECESurf::Gunmetal, true);
		Part(R, FVector(X, -1.75f, 0), FVector(3.2f, 0.3f, 1.75f), ECESurf::Gunmetal, true);
		Part(R, FVector(X, 1.75f, 0), FVector(3.2f, 0.3f, 1.75f), ECESurf::Gunmetal, true);
		Part(R, FVector(X, 0, -2.4f), FVector(3.2f, 1.3f, 0.9f), ECESurf::Gunmetal, true);
		Part(R, FVector(X + 3.4f, 0, 0), FVector(0.07f, 0.07f, 0.07f), ECESurf::RedDot, true);
	};
	auto Arms = [&](USceneComponent* R, const FVector& LeftHand, const FVector& RightHand)
	{
		Part(R, LeftHand, FVector(4, 2.6f, 2.6f), ECESurf::Rubber, true);
		Part(R, RightHand, FVector(3.6f, 2.6f, 3.2f), ECESurf::Rubber, true);
		SleeveParts.Add(Part(R, LeftHand + FVector(-11, -6, -6), FVector(12, 3.6f, 3.6f), ECESurf::Tan, true, FRotator(-18, 28, 0)));
		SleeveParts.Add(Part(R, RightHand + FVector(-10, 6, -5), FVector(10, 3.6f, 3.6f), ECESurf::Tan, true, FRotator(-20, -32, 0)));
	};

	// ---- R4 carbine ----
	{
		USceneComponent* R = VMWeapon[0] = Root();
		Part(R, FVector(8, 0, -6.4f), FVector(13, 1.6f, 3.0f), ECESurf::Gunmetal, true);   // receiver
		Part(R, FVector(29, 0, -6.2f), FVector(9, 2.0f, 2.3f), ECESurf::Polymer, true);    // handguard
		Part(R, FVector(43, 0, -6.0f), FVector(6, 0.7f, 0.7f), ECESurf::Gunmetal, true);   // barrel
		Part(R, FVector(50, 0, -6.0f), FVector(2.2f, 1.0f, 1.0f), ECESurf::Gunmetal, true);// muzzle device
		Part(R, FVector(18, 0, -3.1f), FVector(19, 1.2f, 0.4f), ECESurf::Gunmetal, true);  // top rail
		VMMag[0] = Part(R, FVector(11, 0, -13), FVector(2.2f, 1.4f, 5.5f), ECESurf::Polymer, true, FRotator(-8, 0, 0));
		Part(R, FVector(0, 0, -12), FVector(1.8f, 1.4f, 4.5f), ECESurf::Polymer, true, FRotator(-15, 0, 0)); // grip
		Part(R, FVector(-14, 0, -7.2f), FVector(8, 1.5f, 3.0f), ECESurf::Polymer, true);   // stock
		Optic(R, 7.f);
		VMFlash[0] = Part(R, FVector(56, 0, -6.0f), FVector(5, 3, 3), ECESurf::Emissive, true);
		Arms(R, FVector(28, -2.5f, -9.5f), FVector(1, 1.5f, -11.5f));
	}
	// ---- SMG-9 ----
	{
		USceneComponent* R = VMWeapon[1] = Root();
		Part(R, FVector(6, 0, -6.2f), FVector(10, 1.5f, 3.0f), ECESurf::Gunmetal, true);
		Part(R, FVector(20, 0, -6.0f), FVector(5, 1.7f, 1.9f), ECESurf::Polymer, true);
		Part(R, FVector(27, 0, -6.0f), FVector(2.5f, 0.6f, 0.6f), ECESurf::Gunmetal, true);
		Part(R, FVector(10, 0, -3.1f), FVector(12, 1.1f, 0.4f), ECESurf::Gunmetal, true);
		VMMag[1] = Part(R, FVector(8, 0, -14.5f), FVector(1.6f, 1.2f, 6.2f), ECESurf::Polymer, true);
		Part(R, FVector(-1, 0, -11.5f), FVector(1.6f, 1.3f, 4.0f), ECESurf::Polymer, true, FRotator(-12, 0, 0));
		Part(R, FVector(-10, 0, -6.0f), FVector(5, 1.0f, 2.0f), ECESurf::Gunmetal, true);
		Optic(R, 6.f);
		VMFlash[1] = Part(R, FVector(32, 0, -6.0f), FVector(4, 2.5f, 2.5f), ECESurf::Emissive, true);
		Arms(R, FVector(19, -2.5f, -9), FVector(-0.5f, 1.5f, -11));
	}
	// ---- P9 pistol (iron sights at z ~ -0.9) ----
	{
		USceneComponent* R = VMWeapon[2] = Root();
		Part(R, FVector(6, 0, -3.3f), FVector(9, 1.4f, 1.6f), ECESurf::Gunmetal, true);    // slide
		Part(R, FVector(5, 0, -5.4f), FVector(8, 1.3f, 1.0f), ECESurf::Polymer, true);     // frame
		Part(R, FVector(-1.5f, 0, -10), FVector(1.9f, 1.4f, 4.6f), ECESurf::Polymer, true, FRotator(-18, 0, 0));
		Part(R, FVector(14.4f, 0, -1.35f), FVector(0.3f, 0.16f, 0.45f), ECESurf::Gunmetal, true); // front post
		Part(R, FVector(-2.2f, 0.42f, -1.35f), FVector(0.3f, 0.22f, 0.45f), ECESurf::Gunmetal, true);
		Part(R, FVector(-2.2f, -0.42f, -1.35f), FVector(0.3f, 0.22f, 0.45f), ECESurf::Gunmetal, true);
		VMMag[2] = Part(R, FVector(-1.5f, 0, -13.5f), FVector(1.5f, 1.1f, 1.2f), ECESurf::Polymer, true);
		VMFlash[2] = Part(R, FVector(18, 0, -3.3f), FVector(3, 2, 2), ECESurf::Emissive, true);
		Part(R, FVector(-1, -2.4f, -10.5f), FVector(3.6f, 2.4f, 3.4f), ECESurf::Rubber, true);
		Part(R, FVector(-1, 2.4f, -10.5f), FVector(3.6f, 2.4f, 3.4f), ECESurf::Rubber, true);
		SleeveParts.Add(Part(R, FVector(-14, -6, -16), FVector(12, 3.6f, 3.6f), ECESurf::Tan, true, FRotator(-22, 25, 0)));
		SleeveParts.Add(Part(R, FVector(-14, 6, -16), FVector(12, 3.6f, 3.6f), ECESurf::Tan, true, FRotator(-22, -25, 0)));
	}
	// ---- gadget in hand ----
	{
		USceneComponent* R = VMGadget = Root();
		Part(R, FVector(0, 0, 0), FVector(3.2f, 3.2f, 5.2f), ECESurf::Olive, true);
		Part(R, FVector(0, 0, 6), FVector(1.2f, 1.2f, 1.2f), ECESurf::Gunmetal, true);
		Part(R, FVector(-2, 1.5f, -2), FVector(4, 3, 3.4f), ECESurf::Rubber, true);
		SleeveParts.Add(Part(R, FVector(-14, 6, -7), FVector(11, 3.6f, 3.6f), ECESurf::Tan, true, FRotator(-20, -30, 0)));
	}
	for (int32 i = 0; i < 3; ++i)
	{
		MagRest[i] = VMMag[i]->GetRelativeLocation();
		VMFlash[i]->SetMaterial(0, FlashMat);
		VMFlash[i]->SetVisibility(false);
	}
}

// =============================================================================================
// Tick
// =============================================================================================
void ACECharacter::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bBuilt) return;
	if (bDead)
	{
		DeathT = FMath::Min(1.f, DeathT + Dt / 0.45f);
		const float E = DeathT * DeathT;
		if (!bRealBody)
		{
			BodyRoot->SetRelativeRotation(FRotator(-84.f * E, 0, 0));
			BodyRoot->SetRelativeLocation(FVector(-10.f * E, 0, -90.f + 10.f * E));
		}
		ViewRoot->SetVisibility(false, true);
		MuzzleLight->SetIntensity(0.f);
		return;
	}
	TickMovement(Dt);
	TickAction(Dt);
	TickWeapon(Dt);
	TickCamera(Dt);
	TickBody(Dt);
	TickViewmodel(Dt);
	TickFootsteps(Dt);
	Input.bFirePressed = false;
	Input.bReload = false;
	Input.Switch = -1;
	Input.bInteractPressed = false;
	Input.bDetonate = false;
}

void ACECharacter::TickMovement(float Dt)
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	const UCETuning* T = TU();
	ACEGameMode* G = GM();
	const bool bFrozen = G && G->IsMovementFrozen(this);

	const bool bWantSprint = Input.bSprint && Input.Forward > 0.3f && !Input.bAds && !Input.bFire && Action == ECEAction::None && !bFrozen;
	if (bWantSprint && bIsCrouched) UnCrouch();
	if (bSprinting && !bWantSprint) SprintEndTime = Now();
	bSprinting = bWantSprint;
	if (bSprinting && bReloading) { /* allowed: reload continues while sprinting */ }

	if (Input.bCrouch && !bIsCrouched && !bSprinting) Crouch();
	else if (!Input.bCrouch && bIsCrouched) UnCrouch();

	const float ActionMult = Action != ECEAction::None ? 0.35f : 1.f;
	CMC->MaxWalkSpeed = (bSprinting ? T->SprintSpeed : T->WalkSpeed * FMath::Lerp(1.f, T->AdsSpeedMult, AdsAlpha)) * ActionMult;
	CMC->MaxWalkSpeedCrouched = T->CrouchSpeed * FMath::Lerp(1.f, 0.85f, AdsAlpha) * ActionMult;

	if (!bFrozen)
	{
		const FRotator Yaw(0, AimRotation().Yaw, 0);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), FMath::Clamp(Input.Forward, -1.f, 1.f));
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), FMath::Clamp(Input.Right, -1.f, 1.f));
	}
	const float LeanTarget = bSprinting ? 0.f : (float)FMath::Clamp(Input.Lean, -1, 1);
	LeanAlpha = FMath::FInterpTo(LeanAlpha, LeanTarget, Dt, T->LeanRate);
}

bool ACECharacter::CanFire() const
{
	const float Tm = Now();
	return !bDead && !bReloading && Equipped == ECEGadget::None && Action == ECEAction::None && !bSprinting
		&& Tm >= EquipEnd && Tm >= SprintEndTime + TU()->SprintToFireDelay && Tm >= NextFireTime && LowerAlpha < 0.5f;
}

float ACECharacter::ReloadProgress() const
{
	return bReloading ? FMath::Clamp((Now() - ReloadStart) / FMath::Max(0.01f, ReloadEnd - ReloadStart), 0.f, 1.f) : 0.f;
}

float ACECharacter::GetSpreadDeg() const
{
	const FCEWeaponDef& D = CurDef();
	const float Speed = GetVelocity().Size2D();
	float S = FMath::Lerp(D.HipSpread, D.AdsSpread, AdsAlpha);
	S += D.MoveSpread * FMath::Clamp(Speed / TU()->WalkSpeed, 0.f, 1.5f) * (1.f - 0.75f * AdsAlpha);
	if (bIsCrouched) S *= 0.8f;
	if (!GetCharacterMovement()->IsMovingOnGround()) S += 3.f;
	return S;
}

void ACECharacter::SwitchTo(int32 NewSlot)
{
	if (NewSlot <= 1)
	{
		if (NewSlot == Slot && Equipped == ECEGadget::None) return;
		Slot = NewSlot;
		Equipped = ECEGadget::None;
		EquipEnd = Now() + CurDef().Equip;
	}
	else
	{
		const ECEGadget G = GadgetSlots[NewSlot - 2];
		if (G == ECEGadget::None || GadgetCount[(int32)G] <= 0) return;
		Equipped = G;
		EquipEnd = Now() + 0.3f;
	}
	bReloading = false;
	ShotsInBurst = 0;
	CancelAction();
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Equip, GetActorLocation(), bIsPlayer ? 0.6f : 0.8f);
}

void ACECharacter::StartReload()
{
	FCEWeaponState& W = CurWeapon();
	const FCEWeaponDef& D = CurDef();
	if (bReloading || W.Reserve <= 0 || W.Mag >= D.Mag || Equipped != ECEGadget::None) return;
	bReloading = true;
	ReloadStart = Now();
	ReloadEnd = ReloadStart + (W.Mag == 0 ? D.ReloadEmpty : D.Reload);
	ReloadStage = 0;
	ShotsInBurst = 0;
}

void ACECharacter::FinishReload()
{
	FCEWeaponState& W = CurWeapon();
	const int32 Need = CurDef().Mag - W.Mag;
	const int32 Take = FMath::Min(Need, W.Reserve);
	W.Mag += Take; W.Reserve -= Take;
	bReloading = false;
}

void ACECharacter::TickWeapon(float Dt)
{
	const FCEWeaponDef& D = CurDef();
	const float Tm = Now();
	UCESound* Snd = UCESound::Get(this);
	const float SelfVol = bIsPlayer ? 0.7f : 1.f;

	if (Input.Switch >= 0) SwitchTo(Input.Switch);
	if (Input.bReload) StartReload();
	if (Input.bDetonate) if (ACEGameMode* G = GM()) G->DetonateCharges(this);

	// reload progress with audible stages
	if (bReloading)
	{
		const float P = ReloadProgress();
		const bool bEmpty = CurWeapon().Mag == 0;
		if (ReloadStage == 0 && P > 0.12f) { ReloadStage = 1; if (Snd) Snd->Play(ECESnd::MagOut, GetActorLocation(), SelfVol); }
		if (ReloadStage == 1 && P > 0.55f) { ReloadStage = 2; if (Snd) Snd->Play(ECESnd::MagIn, GetActorLocation(), SelfVol); }
		if (ReloadStage == 2 && bEmpty && P > 0.82f) { ReloadStage = 3; if (Snd) Snd->Play(ECESnd::Bolt, GetActorLocation(), SelfVol); }
		if (Tm >= ReloadEnd) FinishReload();
	}

	// ADS
	const bool bCanAds = Input.bAds && !bSprinting && !bReloading && Equipped == ECEGadget::None && Action == ECEAction::None;
	AdsAlpha = FMath::FInterpConstantTo(AdsAlpha, bCanAds ? 1.f : 0.f, Dt, 1.f / FMath::Max(0.05f, D.AdsTime));

	// lowered weapon near walls
	{
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(CELower), false, this);
		const bool bNear = GetWorld()->LineTraceSingleByChannel(H, EyeLocation(), EyeLocation() + AimDir() * (45.f + 25.f * (1.f - AdsAlpha)), ECC_Visibility, Q);
		const float Target = (bNear || Action != ECEAction::None) ? 1.f : 0.f;
		LowerAlpha = FMath::FInterpTo(LowerAlpha, Target, Dt, 12.f);
	}

	// gadgets / firing
	if (Equipped != ECEGadget::None)
	{
		if (Input.bFirePressed && Tm >= EquipEnd) UseGadget();
	}
	else
	{
		const bool bTrigger = D.bAuto ? Input.bFire : Input.bFirePressed;
		if (bTrigger)
		{
			if (CurWeapon().Mag <= 0)
			{
				if (Input.bFirePressed)
				{
					if (Snd) Snd->Play(ECESnd::DryFire, GetActorLocation(), SelfVol);
					StartReload();
				}
			}
			else if (CanFire())
			{
				FireShot();
			}
		}
		// end of burst -> partial auto recovery of vertical recoil
		const float Gap = 60.f / D.RPM + (D.bAuto ? 0.05f : 0.22f);
		if (ShotsInBurst > 0 && (!Input.bFire || !D.bAuto) && Tm - LastShotTime > Gap)
		{
			RecoverRemaining = RecoilDebt * D.RecoilRecover;
			RecoilDebt = 0.f;
			ShotsInBurst = 0;
		}
	}
	if (RecoverRemaining > 0.001f && ShotsInBurst == 0)
	{
		if (AController* C = GetController())
		{
			const float Step = RecoverRemaining * (1.f - FMath::Exp(-9.f * Dt));
			FRotator R = C->GetControlRotation();
			R.Pitch = FRotator::NormalizeAxis(R.Pitch) - Step;
			C->SetControlRotation(R);
			RecoverRemaining -= Step;
		}
	}

	// muzzle flash & kick decay
	MuzzleT = FMath::Max(0.f, MuzzleT - Dt);
	const bool bFlash = MuzzleT > 0.f;
	MuzzleLight->SetIntensity(bFlash ? 60.f * D.MuzzleLight * TU()->InteriorLightScale : 0.f);
	for (int32 i = 0; i < 3; ++i) if (VMFlash[i]) VMFlash[i]->SetVisibility(bFlash && i == (Slot == 1 ? 2 : (Weapons[0].Id == ECEWeaponId::SMG9 ? 1 : 0)));
	if (TPFlash) TPFlash->SetVisibility(bFlash);
	VMKick = FMath::FInterpTo(VMKick, 0.f, Dt, 16.f);
}

void ACECharacter::FireShot()
{
	const FCEWeaponDef& D = CurDef();
	FCEWeaponState& W = CurWeapon();
	const float Tm = Now();
	W.Mag--;
	NextFireTime = Tm + 60.f / D.RPM;
	LastShotTime = Tm;
	RecoverRemaining = 0.f;
	const int32 ShotIdx = ShotsInBurst++;

	const float SpreadRad = FMath::DegreesToRadians(GetSpreadDeg());
	const FVector Dir = FMath::VRandCone(AimDir(), SpreadRad);
	TraceBullet(EyeLocation(), Dir);

	// learnable recoil: fixed pattern + small variance
	if (D.Pattern.Num() > 0)
	{
		const FVector2D P = D.Pattern[FMath::Min(ShotIdx, D.Pattern.Num() - 1)] * D.RecoilScale;
		const float Pitch = P.X + FMath::FRandRange(-D.RecoilVariance, D.RecoilVariance) * 0.5f;
		const float Yaw = P.Y + FMath::FRandRange(-D.RecoilVariance, D.RecoilVariance);
		if (AController* C = GetController())
		{
			FRotator R = C->GetControlRotation();
			R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch) + Pitch, -88.f, 88.f);
			R.Yaw += Yaw;
			C->SetControlRotation(R);
		}
		RecoilDebt += Pitch;
	}
	VMKick = FMath::Min(VMKick + 1.f, 2.5f);
	MuzzleT = 0.045f;

	const ECESnd S = W.Id == ECEWeaponId::R4 ? ECESnd::ShotR4 : (W.Id == ECEWeaponId::SMG9 ? ECESnd::ShotSMG : ECESnd::ShotP9);
	if (UCESound* Snd = UCESound::Get(this)) Snd->Play(S, EyeLocation() + AimDir() * 40.f, 1.f);
	if (ACEGameMode* G = GM()) G->ReportNoise(this, GetActorLocation(), TU()->HearShot, true);
}

void ACECharacter::TraceBullet(const FVector& Start, const FVector& Dir)
{
	const FCEWeaponDef& D = CurDef();
	const UCETuning* T = TU();
	ACEGameMode* G = GM();
	ACELevelBuilder* Lvl = G ? G->Level : nullptr;
	UWorld* World = GetWorld();
	float Power = D.PenPower;
	float DmgMult = 1.f;
	FString Through;
	int32 Pens = 0;
	FVector S = Start;
	float Travelled = 0.f;
	const float MaxRange = 12000.f;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CEBullet), false, this);
	Q.bReturnPhysicalMaterial = false;

	for (int32 Iter = 0; Iter < 6; ++Iter)
	{
		FHitResult Hit;
		const FVector End = S + Dir * (MaxRange - Travelled);
		if (!World->LineTraceSingleByChannel(Hit, S, End, CE_TRACE_WEAPON, Q))
		{
			if (ACEFx* Fx = ACEFx::Get(World)) Fx->Tracer(Start + Dir * 60.f, End);
			return;
		}
		Travelled += Hit.Distance;
		if (Iter == 0) if (ACEFx* Fx = ACEFx::Get(World)) Fx->Tracer(Start + Dir * 60.f, Hit.ImpactPoint);
		AActor* HA = Hit.GetActor();
		UPrimitiveComponent* HC = Hit.GetComponent();

		if (ACECharacter* Victim = Cast<ACECharacter>(HA))
		{
			if (!Victim->IsAlive() || (Victim->Team == Team && !T->bFriendlyFire)) { S = Hit.ImpactPoint + Dir * 2.f; Q.AddIgnoredActor(Victim); continue; }
			const float DistM = (Hit.ImpactPoint - Start).Size() / 100.f;
			float Dmg = D.Damage;
			if (DistM > D.FalloffStart) Dmg = FMath::Lerp(D.Damage, D.MinDamage, FMath::Clamp((DistM - D.FalloffStart) / FMath::Max(1.f, D.FalloffEnd - D.FalloffStart), 0.f, 1.f));
			FCEHitRecord R;
			R.Attacker = CEName; R.Victim = Victim->CEName; R.Weapon = D.Name;
			R.Zone = HC == Victim->HeadBox ? ECEHitZone::Head : (HC == Victim->TorsoBox ? ECEHitZone::Torso : ECEHitZone::Limb);
			R.Damage = Dmg * DmgMult; R.Through = Through; R.Distance = DistM; R.Time = Now();
			Victim->ApplyHit(R, this, Dir);
			if (ACEFx* Fx = ACEFx::Get(World)) Fx->Burst(Hit.ImpactPoint, 4, 120.f, 1.8f, false, 0.4f);
			return;
		}

		// world surface or entity
		ECEPen Pen = ECEPen::Solid;
		bool bMetal = false;
		if (ACEEntity* Ent = Cast<ACEEntity>(HA)) Pen = Ent->GetPen();
		else if (Lvl && HA == Lvl) { Pen = Lvl->PenOf(HC); bMetal = Lvl->StepOf(HC) == ECEStep::Metal; }
		else Pen = ECEPen::Solid;
		bMetal |= Pen == ECEPen::Sheet;
		if (HA && HA != Lvl) HA->TakeDamage(D.Damage * DmgMult, FDamageEvent(), GetController(), this);
		if (ACEFx* Fx = ACEFx::Get(World)) Fx->Impact(Hit.ImpactPoint, Hit.ImpactNormal, Pen, bMetal);
		if (UCESound* Snd = UCESound::Get(this))
			Snd->Play(bMetal ? ECESnd::ImpactMetal : (Pen == ECEPen::Solid ? ECESnd::ImpactHard : ECESnd::ImpactSoft), Hit.ImpactPoint, 0.55f);

		if (Pen == ECEPen::Solid || Pen == ECEPen::None || Pens >= T->MaxPenetrations) return;

		// thickness along the ray from the hit box (all world geometry is boxes)
		FBox Bx(ForceInit);
		if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(HC))
		{
			FTransform IT;
			if (Hit.Item >= 0 && ISM->GetInstanceTransform(Hit.Item, IT, true))
				Bx = FBox(IT.GetLocation() - IT.GetScale3D() * 50.f, IT.GetLocation() + IT.GetScale3D() * 50.f);
		}
		if (!Bx.IsValid && HC) Bx = HC->Bounds.GetBox();
		float Thick = 20.f;
		if (Bx.IsValid)
		{
			float TExit = 200.f;
			for (int32 Ax = 0; Ax < 3; ++Ax)
			{
				if (FMath::Abs(Dir[Ax]) < 1e-4f) continue;
				const float Bound = Dir[Ax] > 0 ? Bx.Max[Ax] : Bx.Min[Ax];
				TExit = FMath::Min(TExit, (Bound - Hit.ImpactPoint[Ax]) / Dir[Ax]);
			}
			Thick = FMath::Clamp(TExit, 0.5f, 200.f);
		}
		float Density = 1.f; FString Label;
		switch (Pen)
		{
		case ECEPen::Glass: Density = T->GlassDensity; Label = TEXT("glass"); break;
		case ECEPen::Drywall: Density = T->DrywallDensity; Label = TEXT("drywall"); break;
		case ECEPen::Wood: Density = T->WoodDensity; Label = TEXT("wood"); break;
		case ECEPen::Barricade: Density = T->WoodDensity * 0.8f; Label = TEXT("barricade"); break;
		case ECEPen::Sheet: Density = T->SheetDensity; Label = TEXT("sheet metal"); break;
		case ECEPen::Crate: Density = T->CrateDensity; Label = TEXT("crate"); break;
		default: break;
		}
		const float Cost = Density * Thick;
		if (Cost > Power) return;
		Power -= Cost;
		if (Pen != ECEPen::Glass) DmgMult *= (1.f - T->PenDamageLoss);
		if (Pen != ECEPen::Glass || Through.IsEmpty()) Through = Through.IsEmpty() ? Label : Through + TEXT(" + ") + Label;
		++Pens;
		const FVector Exit = Hit.ImpactPoint + Dir * (Thick + 0.5f);
		if (Pen != ECEPen::Glass) if (ACEFx* Fx = ACEFx::Get(World)) Fx->Decal(Exit - Dir * 0.6f, -Dir, 4.f, false);
		Travelled += Thick;
		S = Exit + Dir * 0.5f;
	}
}

void ACECharacter::UseGadget()
{
	ACEGameMode* G = GM();
	if (!G) return;
	const UCETuning* T = TU();
	const FVector Eye = EyeLocation();
	const FVector Dir = AimDir();
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CEGadget), false, this);
	auto Consume = [this](ECEGadget Gd)
	{
		GadgetCount[(int32)Gd]--;
		if (GadgetCount[(int32)Gd] <= 0) { Equipped = ECEGadget::None; EquipEnd = Now() + CurDef().Equip; }
	};
	switch (Equipped)
	{
	case ECEGadget::Flash:
	case ECEGadget::Smoke:
		G->SpawnGrenade(this, Equipped, Eye + Dir * 35.f, (Dir + FVector(0, 0, 0.12f)).GetSafeNormal() * T->ThrowSpeed);
		LastThrowTime = Now();
		Consume(Equipped);
		Equipped = ECEGadget::None;
		EquipEnd = Now() + 0.45f;
		break;
	case ECEGadget::BreachCharge:
	{
		AActor* Target = nullptr; FVector Loc = FVector::ZeroVector;
		// find a breachable entity under the crosshair
		for (ACEEntity* E : G->Level->Entities)
		{
			if (!E || !E->CanBeBreached()) continue;
			const FBox Bx(E->OpeningCentre - E->OpeningExtent - FVector(6), E->OpeningCentre + E->OpeningExtent + FVector(6));
			FVector HitLoc, HitNorm; float HitTime;
			if (FMath::LineExtentBoxIntersection(Bx, Eye, Eye + Dir * 220.f, FVector::ZeroVector, HitLoc, HitNorm, HitTime))
			{
				Target = E; Loc = HitLoc; break;
			}
		}
		if (Target) BeginAction(ECEAction::PlaceCharge, Target, Loc, T->BreachPlaceTime);
		break;
	}
	case ECEGadget::Camera:
		if (GetWorld()->LineTraceSingleByChannel(H, Eye, Eye + Dir * 300.f, ECC_Visibility, Q) && FMath::Abs(H.ImpactNormal.Z) < 0.6f)
		{
			G->SpawnCamera(this, H.ImpactPoint + H.ImpactNormal * 6.f, FRotator(-10.f, H.ImpactNormal.Rotation().Yaw, 0));
			Consume(ECEGadget::Camera);
		}
		break;
	case ECEGadget::Alarm:
		if (GetWorld()->LineTraceSingleByChannel(H, Eye, Eye + Dir * 300.f, ECC_Visibility, Q))
		{
			G->SpawnAlarm(this, H.ImpactPoint + H.ImpactNormal * 3.f, H.ImpactNormal);
			Consume(ECEGadget::Alarm);
		}
		break;
	case ECEGadget::Barricade:
	{
		AActor* Target = nullptr; FVector Loc; FString Prompt; bool bInstant = false;
		if (QueryInteract(Target, Loc, Prompt, bInstant) == ECEAction::Barricade) BeginAction(ECEAction::Barricade, Target, Loc, T->BarricadeTime);
		break;
	}
	default: break;
	}
}

// =============================================================================================
// Actions
// =============================================================================================
ECEAction ACECharacter::QueryInteract(AActor*& OutTarget, FVector& OutLoc, FString& OutPrompt, bool& bOutInstant) const
{
	OutTarget = nullptr; bOutInstant = false; OutPrompt.Reset();
	ACEGameMode* G = GM();
	if (!G || !G->Level || bDead) return ECEAction::None;
	const UCETuning* T = TU();
	const FVector Eye = EyeLocation();
	const FVector Dir = AimDir();
	const ECEPhase Ph = G->Phase;

	// objective first
	if (Team == ECETeam::Defend && G->Device && G->Device->bActive && FVector::Dist(G->Device->GetActorLocation(), GetActorLocation()) < 220.f)
	{
		OutTarget = G->Device; OutLoc = G->Device->GetActorLocation(); OutPrompt = TEXT("Hold F  Disable interceptor");
		return ECEAction::Defuse;
	}
	if (Team == ECETeam::Attack && Ph == ECEPhase::Action && !G->Device && G->Level->InObjective(GetActorLocation()))
	{
		OutLoc = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) + GetActorForwardVector() * 40.f;
		OutPrompt = TEXT("Hold F  Plant interceptor");
		return ECEAction::Plant;
	}

	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CEUse), false, this);
	float Block = 230.f;
	if (GetWorld()->LineTraceSingleByChannel(H, Eye, Eye + Dir * 230.f, ECC_Visibility, Q)) Block = H.Distance + 25.f;

	ACEEntity* Best = nullptr; float BestT = 1e9f; FVector BestLoc = FVector::ZeroVector;
	for (ACEEntity* E : G->Level->Entities)
	{
		if (!E) continue;
		const FBox Bx(E->OpeningCentre - E->OpeningExtent - FVector(4), E->OpeningCentre + E->OpeningExtent + FVector(4));
		FVector HitLoc, HitNorm; float HitTime;
		if (!FMath::LineExtentBoxIntersection(Bx, Eye, Eye + Dir * 230.f, FVector::ZeroVector, HitLoc, HitNorm, HitTime)) continue;
		const float D = HitTime * 230.f;
		if (D > Block || D > BestT) continue;
		Best = E; BestT = D; BestLoc = HitLoc;
	}
	if (!Best) return ECEAction::None;
	OutTarget = Best; OutLoc = BestLoc;
	if (ACEDoor* Door = Cast<ACEDoor>(Best))
	{
		if (Door->IsDestroyed()) return ECEAction::None;
		bOutInstant = true;
		OutPrompt = Door->IsOpen() ? TEXT("F  Close door") : TEXT("F  Open door");
		return ECEAction::None;
	}
	if (ACEBreakable* B = Cast<ACEBreakable>(Best))
	{
		if (Team != ECETeam::Defend || (Ph != ECEPhase::Prep && Ph != ECEPhase::Action)) return ECEAction::None;
		if (B->Kind == ECEEntityKind::BreachWall && B->IsIntact() && !B->IsReinforced())
		{
			if (G->ReinforcePool <= 0) { OutPrompt = TEXT("No reinforcements left"); return ECEAction::None; }
			OutPrompt = FString::Printf(TEXT("Hold F  Reinforce wall  (%d left)"), G->ReinforcePool);
			return ECEAction::Reinforce;
		}
		if (B->Kind == ECEEntityKind::Barricade && !B->IsDeployed())
		{
			if (GadgetCount[(int32)ECEGadget::Barricade] <= 0) { OutPrompt = TEXT("No barricades left"); return ECEAction::None; }
			OutPrompt = FString::Printf(TEXT("Hold F  Barricade  (%d)"), GadgetCount[(int32)ECEGadget::Barricade]);
			return ECEAction::Barricade;
		}
	}
	return ECEAction::None;
}

void ACECharacter::BeginAction(ECEAction A, AActor* Target, const FVector& Loc, float Duration)
{
	if (bDead || A == ECEAction::None) return;
	Action = A; ActionTarget = Target; ActionLoc = Loc; ActionTime = 0.f; ActionDuration = FMath::Max(0.1f, Duration);
	bReloading = false;
	if (A == ECEAction::PlaceCharge || A == ECEAction::Plant || A == ECEAction::Barricade)
		if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Deploy, GetActorLocation(), 0.7f);
}

void ACECharacter::CancelAction()
{
	Action = ECEAction::None;
	ActionTarget.Reset();
	ActionTime = 0.f;
}

void ACECharacter::TickAction(float Dt)
{
	// begin actions from input
	if (Input.bInteractPressed)
	{
		AActor* Tgt; FVector Loc; FString Prompt; bool bInstant;
		QueryInteract(Tgt, Loc, Prompt, bInstant);
		if (bInstant) if (ACEDoor* D = Cast<ACEDoor>(Tgt)) D->Interact(EyeLocation());
	}
	if (Action == ECEAction::None)
	{
		if (Input.bInteract)
		{
			AActor* Tgt; FVector Loc; FString Prompt; bool bInstant;
			const ECEAction A = QueryInteract(Tgt, Loc, Prompt, bInstant);
			const UCETuning* T = TU();
			if (A == ECEAction::Reinforce) BeginAction(A, Tgt, Loc, T->ReinforceTime);
			else if (A == ECEAction::Barricade) BeginAction(A, Tgt, Loc, T->BarricadeTime);
			else if (A == ECEAction::Plant) BeginAction(A, nullptr, Loc, T->PlantTime);
			else if (A == ECEAction::Defuse) BeginAction(A, Tgt, Loc, T->DefuseTime);
		}
		else if (Input.bHeal && GadgetCount[(int32)ECEGadget::Medkit] > 0 && Health < TU()->MaxHealth)
		{
			BeginAction(ECEAction::Heal, nullptr, GetActorLocation(), TU()->MedkitTime);
		}
		return;
	}
	// continue / validate
	bool bHeld;
	switch (Action)
	{
	case ECEAction::Heal: bHeld = Input.bHeal; break;
	case ECEAction::PlaceCharge: bHeld = Input.bFire || Input.bInteract; break;
	case ECEAction::Barricade: bHeld = Input.bInteract || Input.bFire; break;
	default: bHeld = Input.bInteract; break;
	}
	const bool bTargetGone = (Action == ECEAction::Reinforce || Action == ECEAction::Barricade || Action == ECEAction::PlaceCharge || Action == ECEAction::Defuse) && !ActionTarget.IsValid();
	if (!bHeld || bTargetGone || GetVelocity().Size2D() > 160.f) { CancelAction(); return; }
	ActionTime += Dt;
	if (ActionTime >= ActionDuration) CompleteAction();
}

void ACECharacter::CompleteAction()
{
	ACEGameMode* G = GM();
	const ECEAction A = Action;
	AActor* Tgt = ActionTarget.Get();
	CancelAction();
	if (!G) return;
	switch (A)
	{
	case ECEAction::Reinforce:
		if (ACEBreakable* B = Cast<ACEBreakable>(Tgt)) if (G->ReinforcePool > 0 && !B->IsReinforced()) { B->Reinforce(); G->ReinforcePool--; }
		break;
	case ECEAction::Barricade:
		if (ACEBreakable* B = Cast<ACEBreakable>(Tgt)) if (GadgetCount[(int32)ECEGadget::Barricade] > 0 && !B->IsDeployed()) { B->Deploy(); GadgetCount[(int32)ECEGadget::Barricade]--; }
		break;
	case ECEAction::Plant: G->PlantDevice(this, ActionLoc); break;
	case ECEAction::Defuse: G->DefuseDevice(this); break;
	case ECEAction::PlaceCharge:
		if (ACEEntity* E = Cast<ACEEntity>(Tgt))
		{
			if (E->CanBeBreached() && GadgetCount[(int32)ECEGadget::BreachCharge] > 0)
			{
				G->SpawnCharge(this, E, ActionLoc);
				GadgetCount[(int32)ECEGadget::BreachCharge]--;
				if (GadgetCount[(int32)ECEGadget::BreachCharge] <= 0) { Equipped = ECEGadget::None; EquipEnd = Now() + CurDef().Equip; }
			}
		}
		break;
	case ECEAction::Heal:
		Health = FMath::Min(TU()->MaxHealth, Health + TU()->MedkitHeal);
		GadgetCount[(int32)ECEGadget::Medkit]--;
		break;
	default: break;
	}
}

// =============================================================================================
// Damage
// =============================================================================================
void ACECharacter::ApplyHit(FCEHitRecord Rec, ACECharacter* Attacker, const FVector& FromDir)
{
	if (bDead) return;
	const UCETuning* T = TU();
	float Dmg = Rec.Damage;
	switch (Rec.Zone)
	{
	case ECEHitZone::Head: if (T->bHeadshotsLethal) Dmg = FMath::Max(Dmg, Health); break;
	case ECEHitZone::Torso: Dmg *= T->TorsoMult; break;
	default: Dmg *= T->LimbMult; break;
	}
	Dmg = FMath::Min(Dmg, Health);
	Rec.Damage = FMath::RoundToFloat(Dmg);
	Health -= Dmg;
	Rec.bKill = Health <= 0.5f;
	HitsTaken.Add(Rec);
	LastDamageTime = Now();
	LastDamageFrom = Attacker ? Attacker->GetActorLocation() : GetActorLocation() - FromDir * 500.f;
	if (Attacker)
	{
		Attacker->HitsDealt.Add(Rec);
		Attacker->LastHitMarkerTime = Now();
		Attacker->bLastHitKill = Rec.bKill;
		Attacker->bLastHitHead = Rec.Zone == ECEHitZone::Head;
		if (Attacker->bIsPlayer)
			if (UCESound* S = UCESound::Get(this)) S->Play2D(Rec.Zone == ECEHitZone::Head ? ECESnd::HitHead : ECESnd::HitMarker, 0.55f);
	}
	if (bIsPlayer) if (UCESound* S = UCESound::Get(this)) S->Play2D(ECESnd::Hurt, 0.7f);
	if (Action == ECEAction::Heal) CancelAction();
	if (ACEGameMode* G = GM()) G->OnHit(this, Attacker, Rec);
	if (Health <= 0.5f) { Health = 0.f; Die(Attacker, Rec); }
}

void ACECharacter::ApplyExplosion(float Damage, ACECharacter* Attacker, const FVector& Origin, const FString& Source)
{
	FCEHitRecord R;
	R.Attacker = Attacker ? Attacker->CEName : TEXT("?");
	R.Victim = CEName; R.Weapon = Source; R.Zone = ECEHitZone::Torso; R.Damage = Damage / TU()->TorsoMult;
	R.Distance = FVector::Dist(Origin, GetActorLocation()) / 100.f; R.Time = Now(); R.bExplosion = true;
	ApplyHit(R, Attacker, (GetActorLocation() - Origin).GetSafeNormal());
}

void ACECharacter::Blind(float Seconds)
{
	if (bDead || Seconds <= 0.f) return;
	BlindTotal = Seconds;
	BlindUntil = FMath::Max(BlindUntil, Now() + Seconds);
	if (bIsPlayer)
	{
		if (UCESound* S = UCESound::Get(this)) { S->Play2D(ECESnd::Ring, FMath::Clamp(Seconds / 3.f, 0.3f, 1.f)); }
	}
}

void ACECharacter::Die(ACECharacter* InKiller, const FCEHitRecord& Rec)
{
	bDead = true;
	Killer = InKiller;
	CancelAction();
	bReloading = false;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	for (UBoxComponent* B : { HeadBox.Get(), TorsoBox.Get(), LegsBox.Get(), ArmsBox.Get() }) B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (bRealBody)
	{
		FVector Dir = -GetActorForwardVector();
		if (InKiller && InKiller != this) Dir = (GetActorLocation() - InKiller->GetActorLocation()).GetSafeNormal2D();
		StartRagdoll(Dir);
	}
	if (ACEGameMode* G = GM()) G->OnKilled(this, InKiller, Rec);
}

// =============================================================================================
// Presentation
// =============================================================================================
void ACECharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	EyeZ += HalfHeightAdjust; // keep the camera continuous, then ease down
}

void ACECharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	EyeZ -= HalfHeightAdjust;
}

void ACECharacter::TickCamera(float Dt)
{
	const UCETuning* T = TU();
	const float TargetEye = bIsCrouched ? T->EyeHeightCrouch : T->EyeHeightStand;
	EyeZ = FMath::FInterpTo(EyeZ, TargetEye, Dt, T->CrouchCameraRate);

	// lean offset, clamped by walls
	float LeanY = LeanAlpha * T->LeanOffset;
	if (FMath::Abs(LeanY) > 1.f)
	{
		const FVector Base = GetActorLocation() + FVector(0, 0, EyeZ);
		const FVector Want = Base + GetActorRightVector() * LeanY;
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(CELean), false, this);
		if (GetWorld()->SweepSingleByChannel(H, Base, Want, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(12.f), Q))
			LeanY *= H.Time;
	}
	const float Speed = GetVelocity().Size2D();
	const bool bGround = GetCharacterMovement()->IsMovingOnGround();
	if (bGround) BobPhase += Dt * Speed / 55.f;
	const float BobAmp = (bSprinting ? T->HeadBob * 1.8f : T->HeadBob) * FMath::Clamp(Speed / T->WalkSpeed, 0.f, 1.5f) * (1.f - AdsAlpha);
	const float BobZ = FMath::Sin(BobPhase * 2.f) * BobAmp;
	Camera->SetRelativeLocation(FVector(0, LeanY, EyeZ + BobZ));
	const FRotator Aim = AimRotation();
	Camera->SetWorldRotation(FRotator(Aim.Pitch, Aim.Yaw, LeanAlpha * T->LeanRoll));
	if (bIsPlayer) Camera->SetFieldOfView(T->FieldOfView * FMath::Lerp(1.f, T->AdsFovMult, AdsAlpha));
	MuzzleLight->SetRelativeLocation(FVector(70, 8, -10));
}

void ACECharacter::TickBody(float Dt)
{
	const float HalfH = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	BodyRoot->SetRelativeLocation(FVector(0, 0, -HalfH));
	BodyRoot->SetRelativeRotation(FRotator::ZeroRotator);
	const float TargetPelvis = bIsCrouched ? 62.f : 92.f;
	PelvisZ = FMath::FInterpTo(PelvisZ, TargetPelvis, Dt, 14.f);
	Pelvis->SetRelativeLocation(FVector(0, 0, PelvisZ));
	const float LegScale = PelvisZ / 92.f;
	LegL->SetRelativeLocation(FVector(0, -11, PelvisZ));
	LegR->SetRelativeLocation(FVector(0, 11, PelvisZ));
	const float Speed = GetVelocity().Size2D();
	WalkPhase += Dt * Speed / 45.f;
	const float Swing = FMath::Clamp(Speed / 300.f, 0.f, 1.2f) * 26.f * FMath::Sin(WalkPhase);
	const FVector LocalVel = GetActorRotation().UnrotateVector(GetVelocity());
	const float Side = FMath::Abs(LocalVel.Y) > FMath::Abs(LocalVel.X) ? 0.5f : 1.f;
	LegL->SetRelativeRotation(FRotator(Swing * Side, 0, 0));
	LegR->SetRelativeRotation(FRotator(-Swing * Side, 0, 0));
	LegL->SetRelativeScale3D(FVector(1, 1, LegScale));
	LegR->SetRelativeScale3D(FVector(1, 1, LegScale));
	LegsBox->SetRelativeLocation(FVector(0, 0, PelvisZ * 0.5f));
	LegsBox->SetBoxExtent(FVector(12, 20, PelvisZ * 0.5f));
	// lean rolls the upper body, aim pitch tilts it a little
	const float AimPitch = FRotator::NormalizeAxis(AimRotation().Pitch);
	Upper->SetRelativeRotation(FRotator(AimPitch * 0.35f, 0, LeanAlpha * 16.f));
	Upper->SetRelativeLocation(FVector(0, LeanAlpha * 10.f, 8.f));
	HeadPivot->SetRelativeRotation(FRotator(AimPitch * 0.5f, 0, 0));
	if (TPGun) TPGun->SetRelativeRotation(FRotator(AimPitch * 0.4f, 0, 0));
}

void ACECharacter::TickViewmodel(float Dt)
{
	bFPView = bIsPlayer || (GetWorld() && GetWorld()->GetFirstPlayerController() && GetWorld()->GetFirstPlayerController()->GetViewTarget() == this);
	if (!bFPView)
	{
		ViewRoot->SetVisibility(false, true);
		return;
	}
	ViewRoot->SetVisibility(true, false);
	const int32 VMIdx = Slot == 1 ? 2 : (Weapons[0].Id == ECEWeaponId::SMG9 ? 1 : 0);
	static const FVector Hip[3] = { FVector(30, 13, -16), FVector(28, 12, -15), FVector(36, 11, -14) };
	static const FVector Ads[3] = { FVector(14, 0, 0), FVector(14, 0, 0), FVector(26, 0, 0.9f) };

	VMSway = FMath::Vector2DInterpTo(VMSway, LookDelta * 0.6f, Dt, 10.f);
	const float Speed = GetVelocity().Size2D();
	const float Move = FMath::Clamp(Speed / TU()->WalkSpeed, 0.f, 1.6f) * (1.f - AdsAlpha * 0.85f);
	const float SprintA = bSprinting ? 1.f : 0.f;
	static float SprintBlend = 0.f; // shared visual blend is fine for the single local view
	SprintBlend = FMath::FInterpTo(SprintBlend, SprintA, Dt, 10.f);

	FVector Pos = FMath::Lerp(Hip[VMIdx], Ads[VMIdx], AdsAlpha);
	FRotator Rot = FRotator::ZeroRotator;
	Pos += FVector(-2, -5, -5) * SprintBlend;
	Rot += FRotator(-16.f, 28.f, -14.f) * SprintBlend;
	Pos += FVector(-8, 0, -9) * LowerAlpha;
	Rot.Pitch -= 32.f * LowerAlpha;
	const float RP = ReloadProgress();
	const float RelA = bReloading ? FMath::Sin(FMath::Clamp(RP, 0.f, 1.f) * PI) : 0.f;
	Rot.Roll += 24.f * RelA; Rot.Pitch += 6.f * RelA; Pos.Z -= 3.f * RelA;
	Pos.X -= VMKick * 2.0f * (1.f - 0.5f * AdsAlpha);
	Rot.Pitch += VMKick * 2.4f * (1.f - 0.6f * AdsAlpha);
	Rot.Yaw -= VMSway.X * (1.f - 0.7f * AdsAlpha);
	Rot.Pitch += VMSway.Y * (1.f - 0.7f * AdsAlpha);
	Pos.Z += FMath::Sin(BobPhase * 2.f) * 0.45f * Move;
	Pos.Y += FMath::Cos(BobPhase) * 0.6f * Move;

	for (int32 i = 0; i < 3; ++i)
	{
		const bool bOn = Equipped == ECEGadget::None && i == VMIdx;
		VMWeapon[i]->SetVisibility(bOn, true);
		if (!bOn) continue;
		VMWeapon[i]->SetRelativeLocationAndRotation(Pos, Rot);
		// magazine swap
		FVector MagLoc = MagRest[i];
		bool bMagVis = true;
		if (bReloading)
		{
			if (RP < 0.3f) MagLoc.Z -= RP / 0.3f * 22.f;
			else if (RP < 0.45f) bMagVis = false;
			else if (RP < 0.65f) MagLoc.Z -= (1.f - (RP - 0.45f) / 0.2f) * 22.f;
		}
		if (VMMag[i])
		{
			VMMag[i]->SetRelativeLocation(MagLoc);
			VMMag[i]->SetVisibility(bMagVis);
		}
	}
	if (VMFlash[VMIdx]) VMFlash[VMIdx]->SetVisibility(MuzzleT > 0.f && Equipped == ECEGadget::None);
	VMGadget->SetVisibility(Equipped != ECEGadget::None, true);
	if (Equipped != ECEGadget::None)
		VMGadget->SetRelativeLocationAndRotation((bRealVM ? FVector(46, 13, -21) : FVector(40, 15, -19)) + FVector(-6, 0, -8) * LowerAlpha, FRotator(10, -10, 0));
	RigVMIdx = VMIdx; RigRelA = RelA; RigRP = RP;
}

void ACECharacter::TickFootsteps(float Dt)
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();
	if (!CMC->IsMovingOnGround() || Speed < 60.f) { StepAccum = FMath::Min(StepAccum, 40.f); return; }
	StepAccum += Speed * Dt;
	const float Stride = bSprinting ? 150.f : (bIsCrouched ? 80.f : 110.f);
	if (StepAccum < Stride) return;
	StepAccum = 0.f;
	const UCETuning* T = TU();
	ACEGameMode* G = GM();
	const FVector Feet = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	ECEStep Surf = ECEStep::Concrete;
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CEStep), false, this);
	if (GetWorld()->LineTraceSingleByChannel(H, GetActorLocation(), Feet - FVector(0, 0, 30), ECC_Visibility, Q) && G && G->Level)
	{
		if (H.GetActor() == G->Level) Surf = G->Level->StepOf(H.GetComponent());
		else if (Cast<ACEEntity>(H.GetActor())) Surf = ECEStep::Wood;
	}
	static const ECESnd Map[] = { ECESnd::StepConcrete, ECESnd::StepMetal, ECESnd::StepWood, ECESnd::StepTile, ECESnd::StepGravel };
	const float Vol = (bSprinting ? 1.f : (bIsCrouched ? 0.22f : 0.55f)) * (bIsPlayer ? 0.55f : 1.f);
	if (UCESound* S = UCESound::Get(this)) S->Play(Map[(int32)Surf], Feet, Vol);
	if (G) G->ReportNoise(this, Feet, bSprinting ? T->HearSprint : (bIsCrouched ? T->HearCrouch : T->HearWalk), false);
}
