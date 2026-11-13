#include "CEMap.h"
#include "ColdEntry.h"
#include "CEAssets.h"
#include "CEEntities.h"
#include "CETuning.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"

// Plan conventions (metres): X east, Y north, Z up. Ground floor top = 0, upper floor top = 3.5,
// upper ceiling = 6.7, roof top = 7.0. Footprint 40 x 28 m.
static constexpr float F2 = 3.5f;
static constexpr float C1 = 3.2f;
static constexpr float C2 = 6.7f;
static constexpr float DoorH = 2.15f;

ACELevelBuilder::ACELevelBuilder()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

void ACELevelBuilder::Build()
{
	BuildGeometry();
	BuildProps();
	BuildLighting();
	BuildGameplayData();
	BuildNav();
	UE_LOG(LogColdEntry, Log, TEXT("Kessler Annex built: %d entities, %d nav nodes, %d edges, %d lights"),
		Entities.Num(), Nodes.Num(), Edges.Num(), Lights.Num());
}

// ---------------------------------------------------------------------------------------------
// Primitive helpers
// ---------------------------------------------------------------------------------------------
UInstancedStaticMeshComponent* ACELevelBuilder::GetISM(ECESurf S, ECECol C)
{
	const int32 Key = (int32)S * 8 + (int32)C;
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = ISMs.Find(Key)) return Found->Get();
	UCEAssets* A = UCEAssets::Get(this);
	UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
	I->SetupAttachment(RootComponent);
	I->SetMobility(EComponentMobility::Static);
	I->SetStaticMesh(A->Cube);
	I->SetMaterial(0, A->Surf(S));
	switch (C)
	{
	case ECECol::Solid:
	case ECECol::HiddenSolid:
		I->SetCollisionProfileName(TEXT("BlockAll"));
		I->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Block);
		break;
	case ECECol::PawnOnly:
	case ECECol::HiddenPawn:
		I->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		I->SetCollisionObjectType(ECC_WorldStatic);
		I->SetCollisionResponseToAllChannels(ECR_Ignore);
		I->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		break;
	default:
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		break;
	}
	if (C == ECECol::HiddenPawn || C == ECECol::HiddenSolid) { I->SetVisibility(false); I->SetHiddenInGame(true); I->SetCastShadow(false); }
	if (S == ECESurf::Emissive) I->SetCastShadow(false);
	I->RegisterComponent();
	AddInstanceComponent(I);
	ISMs.Add(Key, I);
	CompSurf.Add(I, S);
	return I;
}

void ACELevelBuilder::Box(float X0, float Y0, float Z0, float X1, float Y1, float Z1, ECESurf S, ECECol C)
{
	const FVector Min = M(FMath::Min(X0, X1), FMath::Min(Y0, Y1), FMath::Min(Z0, Z1));
	const FVector Max = M(FMath::Max(X0, X1), FMath::Max(Y0, Y1), FMath::Max(Z0, Z1));
	const FVector Size = Max - Min;
	if (Size.X < 0.5f || Size.Y < 0.5f || Size.Z < 0.5f) return;
	GetISM(S, C)->AddInstance(FTransform(FRotator::ZeroRotator, (Min + Max) * 0.5f, Size / 100.f), true);
}

// Wall at constant X running along Y.
void ACELevelBuilder::WallX(float X, float Y0, float Y1, float Z0, float Z1, float T, ECESurf S, TArray<FCEOpening> Ops)
{
	Ops.Sort([](const FCEOpening& L, const FCEOpening& R) { return L.A < R.A; });
	float Cur = Y0;
	const float H = T * 0.5f;
	for (const FCEOpening& O : Ops)
	{
		Box(X - H, Cur, Z0, X + H, O.A, Z1, S);
		if (O.Bot > Z0 + 0.01f) Box(X - H, O.A, Z0, X + H, O.B, O.Bot, S);
		if (O.Top < Z1 - 0.01f) Box(X - H, O.A, O.Top, X + H, O.B, Z1, S);
		Opening(true, X, T, O);
		Cur = O.B;
	}
	Box(X - H, Cur, Z0, X + H, Y1, Z1, S);
}

// Wall at constant Y running along X.
void ACELevelBuilder::WallY(float Y, float X0, float X1, float Z0, float Z1, float T, ECESurf S, TArray<FCEOpening> Ops)
{
	Ops.Sort([](const FCEOpening& L, const FCEOpening& R) { return L.A < R.A; });
	float Cur = X0;
	const float H = T * 0.5f;
	for (const FCEOpening& O : Ops)
	{
		Box(Cur, Y - H, Z0, O.A, Y + H, Z1, S);
		if (O.Bot > Z0 + 0.01f) Box(O.A, Y - H, Z0, O.B, Y + H, O.Bot, S);
		if (O.Top < Z1 - 0.01f) Box(O.A, Y - H, O.Top, O.B, Y + H, Z1, S);
		Opening(false, Y, T, O);
		Cur = O.B;
	}
	Box(Cur, Y - H, Z0, X1, Y + H, Z1, S);
}

void ACELevelBuilder::Opening(bool bAlongY, float Fixed, float T, const FCEOpening& O)
{
	UWorld* W = GetWorld();
	const float H = T * 0.5f + 0.025f;
	const float Mid = (O.A + O.B) * 0.5f;
	const float HalfW = (O.B - O.A) * 0.5f;
	const float HalfH = (O.Top - O.Bot) * 0.5f;
	const FVector Centre = bAlongY ? M(Fixed, Mid, O.Bot + HalfH) : M(Mid, Fixed, O.Bot + HalfH);
	const FVector Half = bAlongY ? FVector(T * 50.f, HalfW * 100.f, HalfH * 100.f) : FVector(HalfW * 100.f, T * 50.f, HalfH * 100.f);

	// Trim (jambs + head) around doors, frames and windows: readable entry points.
	auto Trim = [&](ECESurf S)
	{
		if (bAlongY)
		{
			Box(Fixed - H, O.A - 0.06f, O.Bot, Fixed + H, O.A, O.Top, S);
			Box(Fixed - H, O.B, O.Bot, Fixed + H, O.B + 0.06f, O.Top, S);
			Box(Fixed - H, O.A - 0.06f, O.Top, Fixed + H, O.B + 0.06f, O.Top + 0.07f, S);
		}
		else
		{
			Box(O.A - 0.06f, Fixed - H, O.Bot, O.A, Fixed + H, O.Top, S);
			Box(O.B, Fixed - H, O.Bot, O.B + 0.06f, Fixed + H, O.Top, S);
			Box(O.A - 0.06f, Fixed - H, O.Top, O.B + 0.06f, Fixed + H, O.Top + 0.07f, S);
		}
	};

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	switch (O.Kind)
	{
	case ECEOpen::Door:
	{
		Trim(ECESurf::Paint);
		ACEDoor* D = W->SpawnActor<ACEDoor>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		D->Setup(bAlongY ? M(Fixed, O.A, O.Bot) : M(O.A, Fixed, O.Bot), bAlongY ? 90.f : 0.f, (O.B - O.A) * 100.f, (O.Top - O.Bot) * 100.f);
		Entities.Add(D);
		Doors.Add(D);
		break;
	}
	case ECEOpen::Breach:
	{
		ACEBreakable* B = W->SpawnActor<ACEBreakable>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		B->Setup(ECEEntityKind::BreachWall, Centre, Half);
		Entities.Add(B);
		BreachWalls.Add(B);
		break;
	}
	case ECEOpen::Glass:
	{
		Trim(ECESurf::Metal);
		ACEBreakable* B = W->SpawnActor<ACEBreakable>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		B->Setup(ECEEntityKind::Glass, Centre, Half);
		Entities.Add(B);
		break;
	}
	case ECEOpen::Barricade:
	{
		Trim(ECESurf::Paint);
		ACEBreakable* B = W->SpawnActor<ACEBreakable>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		B->Setup(ECEEntityKind::Barricade, Centre, Half);
		Entities.Add(B);
		Barricades.Add(B);
		break;
	}
	default:
		break;
	}
}

void ACELevelBuilder::Rail(float X0, float Y0, float X1, float Y1, float Z)
{
	const bool bX = FMath::Abs(X1 - X0) > FMath::Abs(Y1 - Y0);
	const float t = 0.025f;
	if (bX)
	{
		Box(X0, Y0 - t, Z + 1.0f, X1, Y0 + t, Z + 1.05f, ECESurf::Hazard, ECECol::None);
		Box(X0, Y0 - t, Z + 0.5f, X1, Y0 + t, Z + 0.53f, ECESurf::Metal, ECECol::None);
		for (float X = X0; X <= X1 + 0.01f; X += 1.5f) Box(X - t, Y0 - t, Z, X + t, Y0 + t, Z + 1.0f, ECESurf::Metal, ECECol::None);
		Box(X0, Y0 - 0.06f, Z, X1, Y0 + 0.06f, Z + 1.05f, ECESurf::Metal, ECECol::HiddenPawn);
	}
	else
	{
		Box(X0 - t, Y0, Z + 1.0f, X0 + t, Y1, Z + 1.05f, ECESurf::Hazard, ECECol::None);
		Box(X0 - t, Y0, Z + 0.5f, X0 + t, Y1, Z + 0.53f, ECESurf::Metal, ECECol::None);
		for (float Y = Y0; Y <= Y1 + 0.01f; Y += 1.5f) Box(X0 - t, Y - t, Z, X0 + t, Y + t, Z + 1.0f, ECESurf::Metal, ECECol::None);
		Box(X0 - 0.06f, Y0, Z, X0 + 0.06f, Y1, Z + 1.05f, ECESurf::Metal, ECECol::HiddenPawn);
	}
}

void ACELevelBuilder::Stairs(float X0, float Y0, float Y1, float Dir, int32 Steps, ECESurf S)
{
	for (int32 i = 0; i < Steps; ++i)
	{
		const float A = X0 + Dir * i * 0.3f, B = X0 + Dir * (i + 1) * 0.3f;
		Box(A, Y0, 0.f, B, Y1, (i + 1) * 0.25f, S);
		Box(A, Y0, (i + 1) * 0.25f - 0.02f, A + Dir * 0.04f, Y1, (i + 1) * 0.25f + 0.005f, ECESurf::Hazard, ECECol::None); // nosing
	}
}

void ACELevelBuilder::Light(float X, float Y, float Z, float Candela, float RadiusM, float TempK, float LengthM, bool bFixture, bool bShadows)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(this);
	L->SetupAttachment(RootComponent);
	L->SetMobility(EComponentMobility::Movable);
	L->SetRelativeLocation(M(X, Y, Z));
	L->IntensityUnits = ELightUnits::Candelas;
	L->Intensity = Candela * UCETuning::Get()->InteriorLightScale;
	L->AttenuationRadius = RadiusM * 100.f;
	L->bUseTemperature = true;
	L->Temperature = TempK;
	L->SourceLength = LengthM * 100.f;
	L->SourceRadius = 3.f;
	L->CastShadows = bShadows;
	L->RegisterComponent();
	AddInstanceComponent(L);
	Lights.Add(L);
	if (bFixture)
	{
		const float HL = FMath::Max(0.15f, LengthM * 0.5f);
		Box(X - HL, Y - 0.09f, Z + 0.06f, X + HL, Y + 0.09f, Z + 0.1f, ECESurf::Emissive, ECECol::None);
		Box(X - HL - 0.03f, Y - 0.12f, Z + 0.1f, X + HL + 0.03f, Y + 0.12f, Z + 0.16f, ECESurf::Paint, ECECol::None);
	}
}

// ---------------------------------------------------------------------------------------------
// Scanned props (imported by Tools/ue_import_photoreal.py). Everything here degrades to the graybox
// when an asset has not been imported. Convention: the scans face +Y, "Front" is the world yaw the
// front should face (0 = +X east, 90 = +Y north, 180 = west, 270 = south).
// ---------------------------------------------------------------------------------------------
UStaticMesh* ACELevelBuilder::PropMesh(const TCHAR* Key) const
{
	const FString Path = FString::Printf(TEXT("/Game/Photoreal/Props/SM_%s.SM_%s"), Key, Key);
	return LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UStaticMeshComponent* ACELevelBuilder::AddPropComp(UStaticMesh* Mesh, const FVector& Loc, float Yaw, const FVector& Scale, bool bCollide, ECESurf Surf)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Static);
	C->SetStaticMesh(Mesh);
	C->SetRelativeScale3D(Scale);
	C->SetRelativeRotation(FRotator(0, Yaw, 0));
	C->SetRelativeLocation(Loc);
	C->SetCanEverAffectNavigation(false);
	if (bCollide)
	{
		C->SetCollisionProfileName(TEXT("BlockAll"));
		C->SetCollisionResponseToChannel(CE_TRACE_WEAPON, ECR_Block);
		CompSurf.Add(C, Surf);
	}
	else C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	AddInstanceComponent(C);
	return C;
}

// Free-standing prop resting on Z. Yaw is the raw mesh rotation.
void ACELevelBuilder::Prop(const TCHAR* Key, float X, float Y, float Z, float Yaw, ECESurf Surf, float Scale, bool bCollide)
{
	UStaticMesh* Mesh = PropMesh(Key);
	if (!Mesh) return;
	const FBox B = Mesh->GetBoundingBox();
	AddPropComp(Mesh, M(X, Y, Z) - FVector(0, 0, B.Min.Z * Scale), Yaw, FVector(Scale), bCollide, Surf);
}

// Set dressing with no collision, centred on (X, Y) and turned to face Front.
void ACELevelBuilder::PropFacing(const TCHAR* Key, float X, float Y, float Z, float Front, float Scale)
{
	UStaticMesh* Mesh = PropMesh(Key);
	if (!Mesh) return;
	const FBox B = Mesh->GetBoundingBox();
	const float Yaw = Front - 90.f;
	const FVector Anchor(B.GetCenter().X, B.GetCenter().Y, B.Min.Z);
	AddPropComp(Mesh, M(X, Y, Z) - FRotator(0, Yaw, 0).RotateVector(Anchor * Scale), Yaw, FVector(Scale), false, ECESurf::Metal);
}

// Wall-mounted dressing: (X, Y) is a point on the wall face, Z the underside of the object.
void ACELevelBuilder::WallProp(const TCHAR* Key, float X, float Y, float Z, float Front, float Scale)
{
	UStaticMesh* Mesh = PropMesh(Key);
	if (!Mesh) return;
	const FBox B = Mesh->GetBoundingBox();
	const float Yaw = Front - 90.f;
	const FVector Anchor(B.GetCenter().X, B.Min.Y, B.Min.Z);   // back face, bottom centre
	AddPropComp(Mesh, M(X, Y, Z) - FRotator(0, Yaw, 0).RotateVector(Anchor * Scale), Yaw, FVector(Scale), false, ECESurf::Metal);
}

// Replaces a graybox block with scanned meshes. The block stays as hidden collision, so cover, bullet
// penetration and bot navigation are exactly what they were. The mesh is repeated along the long side and
// each axis is then stretched a little (never far from uniform) so what you see fills what stops you.
// Returns the top of the dressing in metres.
float ACELevelBuilder::Dress(const TCHAR* Key, float X0, float Y0, float Z0, float X1, float Y1, float Z1, ECESurf S, float Front, float MaxScale)
{
	UStaticMesh* Mesh = PropMesh(Key);
	if (!Mesh) { Box(X0, Y0, Z0, X1, Y1, Z1, S); return Z1; }
	Box(X0, Y0, Z0, X1, Y1, Z1, S, ECECol::HiddenSolid);
	const FBox B = Mesh->GetBoundingBox();
	const FVector MS = B.GetSize().ComponentMax(FVector(1.f));
	const float Yaw = Front - 90.f;
	const bool bSwap = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Yaw))) > 0.5f;   // footprint turned a quarter
	const float BX = (X1 - X0) * 100.f, BY = (Y1 - Y0) * 100.f, BZ = (Z1 - Z0) * 100.f;
	const bool bAlongX = BX >= BY;
	const float Long = bAlongX ? BX : BY, Short = bAlongX ? BY : BX;
	const float MAlongX = bSwap ? MS.Y : MS.X, MAlongY = bSwap ? MS.X : MS.Y;
	const float MLong = bAlongX ? MAlongX : MAlongY, MShort = bAlongX ? MAlongY : MAlongX;
	const float Base = FMath::Clamp(FMath::Min(BZ / MS.Z, Short / MShort), 0.7f, MaxScale);
	const int32 N = FMath::Max(1, FMath::RoundToInt(Long / (MLong * Base)));
	const float SL = FMath::Clamp(Long / (N * MLong), Base * 0.77f, Base * 1.3f);
	const float SS = FMath::Clamp(Short / MShort, Base * 0.77f, Base * 1.15f);
	const float SZ = FMath::Clamp(BZ / MS.Z, Base * 0.77f, Base * 1.3f);
	const float SWX = bAlongX ? SL : SS, SWY = bAlongX ? SS : SL;                 // per world axis
	const FVector Scale(bSwap ? SWY : SWX, bSwap ? SWX : SWY, SZ);               // per mesh axis
	const FVector Anchor(B.GetCenter().X, B.GetCenter().Y, B.Min.Z);
	const FVector Off = FRotator(0, Yaw, 0).RotateVector(Anchor * Scale);
	for (int32 i = 0; i < N; ++i)
	{
		const float T = (i + 0.5f) / N;
		const FVector Slot = bAlongX ? M(FMath::Lerp(X0, X1, T), (Y0 + Y1) * 0.5f, Z0) : M((X0 + X1) * 0.5f, FMath::Lerp(Y0, Y1, T), Z0);
		AddPropComp(Mesh, Slot - Off, Yaw, Scale, false, S);
	}
	UE_LOG(LogColdEntry, Log, TEXT("Dress %s x%d scale L%.2f S%.2f Z%.2f in %.0fx%.0fx%.0f"), Key, N, SL, SS, SZ, BX, BY, BZ);
	return Z0 + MS.Z * SZ / 100.f;
}

static const TCHAR* GPropKeys[] = {
	TEXT("barrel"), TEXT("barrel_red"), TEXT("barrel_blue"), TEXT("crate"), TEXT("box"), TEXT("dresser"), TEXT("desk"),
	TEXT("chair"), TEXT("monobloc"), TEXT("stool"), TEXT("sofa"), TEXT("shelves"), TEXT("shelves_wide"), TEXT("cabinet"),
	TEXT("toolchest"), TEXT("tv"), TEXT("barrier"), TEXT("barrier_b"), TEXT("trashcan"), TEXT("extinguisher"),
	TEXT("powerbox"), TEXT("utilbox"), TEXT("utilbox_green"), TEXT("aircon"), TEXT("tyre"), TEXT("wetfloor"), TEXT("walllamp") };

TArray<FVector> ACELevelBuilder::SpawnPropLineup(const FVector& StartCm, const FVector& StepCm)
{
	TArray<FVector> Out;
	for (const TCHAR* Key : GPropKeys)
	{
		const FVector P = StartCm + StepCm * Out.Num();
		Out.Add(P);
		UStaticMesh* Mesh = PropMesh(Key);
		if (!Mesh) { UE_LOG(LogColdEntry, Warning, TEXT("Lineup: %s is not imported"), Key); continue; }
		const FBox B = Mesh->GetBoundingBox();
		AddPropComp(Mesh, P - FVector(B.GetCenter().X, B.GetCenter().Y, B.Min.Z), 0.f, FVector::OneVector, false, ECESurf::Metal);
		UE_LOG(LogColdEntry, Log, TEXT("Lineup %d: %s size %.0f x %.0f x %.0f cm"), Out.Num() - 1, Key, B.GetSize().X, B.GetSize().Y, B.GetSize().Z);
	}
	return Out;
}

void ACELevelBuilder::BuildProps()
{
	const ECESurf Mt = ECESurf::Metal, Cr = ECESurf::Crate;
	// loading bay
	Prop(TEXT("barrel"), 1.4f, 10.6f, 0, 0, Mt); Prop(TEXT("barrel"), 2.3f, 10.4f, 0, 30, Mt); Prop(TEXT("barrel_red"), 1.8f, 11.4f, 0, 70, Mt);
	Prop(TEXT("crate"), 9.7f, 21.9f, 0, 15, Cr); Prop(TEXT("crate"), 6.6f, 2.5f, 0, -10, Cr);
	Prop(TEXT("box"), 2.0f, 0.8f, 0, 0, Cr); Prop(TEXT("box"), 2.6f, 0.9f, 0, 30, Cr);
	PropFacing(TEXT("extinguisher"), 13.5f, 11.2f, 0, 180);
	WallProp(TEXT("powerbox"), 13.875f, 9.0f, 1.1f, 180);
	PropFacing(TEXT("tyre"), 12.9f, 0.26f, 0, 90); PropFacing(TEXT("tyre"), 12.2f, 0.28f, 0, 82); PropFacing(TEXT("barrel_blue"), 13.2f, 1.9f, 0, 90);
	// service corridor
	PropFacing(TEXT("extinguisher"), 14.45f, 10.4f, 0, 0);
	PropFacing(TEXT("wetfloor"), 15.7f, 18.6f, 0, 250);
	PropFacing(TEXT("extinguisher"), 14.45f, 12.0f, F2, 0);
	// workshop
	Prop(TEXT("barrel"), 38.9f, 8.9f, 0, 0, Mt); Prop(TEXT("barrel_red"), 39.3f, 8.1f, 0, 40, Mt);
	Prop(TEXT("box"), 34.1f, 1.4f, 0, 0, Cr); Prop(TEXT("box"), 34.8f, 1.3f, 0, 40, Cr);
	Prop(TEXT("dresser"), 31.2f, 1.6f, 0, 0, ECESurf::Wood);
	PropFacing(TEXT("stool"), 21.0f, 1.9f, 0, 60);
	WallProp(TEXT("powerbox"), 25.0f, 0.15f, 1.1f, 90);
	PropFacing(TEXT("extinguisher"), 17.5f, 9.4f, 0, 45);
	// offices & upstairs
	Prop(TEXT("box"), 23.2f, 19.0f, 0, 0, Cr); Prop(TEXT("box"), 22.6f, 19.1f, 0, 25, Cr);
	PropFacing(TEXT("chair"), 18.8f, 25.9f, 0, 270); PropFacing(TEXT("chair"), 21.8f, 25.95f, 0, 255);
	PropFacing(TEXT("chair"), 25.8f, 21.9f, 0, 280); PropFacing(TEXT("chair"), 29.2f, 25.5f, 0, 265);
	Prop(TEXT("box"), 30.3f, 1.0f, F2, 0, Cr); Prop(TEXT("box"), 29.7f, 1.1f, F2, 30, Cr);
	PropFacing(TEXT("chair"), 20.4f, 4.1f, F2, 270); PropFacing(TEXT("chair"), 26.4f, 4.1f, F2, 285); PropFacing(TEXT("chair"), 20.6f, 8.1f, F2, 260);
	PropFacing(TEXT("chair"), 20.0f, 23.7f, F2, 270);
	PropFacing(TEXT("monobloc"), 25.4f, 20.7f, F2, 0); PropFacing(TEXT("monobloc"), 28.6f, 20.9f, F2, 180); PropFacing(TEXT("monobloc"), 27.1f, 22.2f, F2, 265);
	// exterior
	Prop(TEXT("barrel"), -1.2f, 12.5f, 0, 0, Mt); Prop(TEXT("barrel_blue"), -1.3f, 13.4f, 0, 20, Mt); Prop(TEXT("barrel"), -1.2f, 15.0f, 0, 60, Mt);
	Prop(TEXT("crate"), 12.f, -5.5f, 0, 10, Cr);
	PropFacing(TEXT("trashcan"), 8.6f, -3.4f, 0, 270);
	PropFacing(TEXT("tyre"), 12.0f, -0.26f, 0, 270); PropFacing(TEXT("tyre"), 12.75f, -0.28f, 0, 262);
	WallProp(TEXT("aircon"), 10.0f, -0.15f, 3.0f, 270); WallProp(TEXT("aircon"), 40.15f, 21.0f, 3.2f, 0);
	WallProp(TEXT("walllamp"), -0.15f, 7.0f, 2.95f, 180); WallProp(TEXT("walllamp"), -0.15f, 21.0f, 2.95f, 180);
	WallProp(TEXT("walllamp"), 28.5f, -0.15f, 2.35f, 270); WallProp(TEXT("walllamp"), 40.15f, 12.5f, 2.35f, 0);
	WallProp(TEXT("utilbox"), 16.5f, 28.15f, 0, 90);
}

void ACELevelBuilder::Zone(const TCHAR* Name, float X0, float Y0, float Z0, float X1, float Y1, float Z1, ECEReverb R, bool bIndoor)
{
	FCEZone Z;
	Z.Name = Name;
	Z.Box = FBox(M(X0, Y0, Z0), M(X1, Y1, Z1));
	Z.Reverb = R;
	Z.bIndoor = bIndoor;
	Zones.Add(Z);
}

// ---------------------------------------------------------------------------------------------
// The map
// ---------------------------------------------------------------------------------------------
void ACELevelBuilder::BuildGeometry()
{
	using O = FCEOpening;
	const ECESurf Ext = ECESurf::Concrete, Blk = ECESurf::Block, Dry = ECESurf::Drywall;

	// ---- ground, slabs, roof ----
	Box(-420, -420, -0.4f, 460, 450, -0.02f, ECESurf::Asphalt);   // runs out to the haze
	Box(-8, -2, -0.3f, 0, 30, 0.0f, ECESurf::Concrete);           // west loading apron
	Box(0, 0, -0.3f, 40, 28, 0.0f, ECESurf::Floor);                 // ground floor slab
	Box(14, 0, C1, 32.5f, 28, F2, ECESurf::Floor);                  // upper slab (stair hole at 32.5-37.2 x 23.9-26.1)
	Box(37.2f, 0, C1, 40, 28, F2, ECESurf::Floor);
	Box(32.5f, 0, C1, 37.2f, 23.9f, F2, ECESurf::Floor);
	Box(32.5f, 26.1f, C1, 37.2f, 28, F2, ECESurf::Floor);
	Box(0, 0, C2, 40, 28, 7.0f, ECESurf::Concrete);                 // roof
	Box(-0.2f, -0.2f, 7.0f, 40.2f, 28.2f, 7.4f, ECESurf::Concrete); // parapet cap

	// drop ceilings (visual only)
	auto Ceil = [this](float X0, float Y0, float X1, float Y1, float Z) { Box(X0, Y0, Z - 0.05f, X1, Y1, Z, ECESurf::Ceiling, ECECol::None); };
	Ceil(14.15f, 0.15f, 16.9f, 27.85f, 3.15f); Ceil(17.1f, 0.15f, 39.85f, 9.9f, 3.15f); Ceil(17.1f, 10.1f, 21.9f, 17.9f, 3.0f);
	Ceil(22.1f, 10.1f, 30.9f, 17.9f, 3.0f); Ceil(31.1f, 10.1f, 39.85f, 17.9f, 3.15f); Ceil(17.1f, 18.1f, 23.9f, 27.85f, 2.95f); Ceil(24.1f, 18.1f, 30.9f, 27.85f, 2.95f);
	Ceil(14.15f, 0.15f, 16.9f, 27.85f, 6.6f); Ceil(17.1f, 0.15f, 30.9f, 13.9f, 6.6f); Ceil(17.1f, 14.1f, 23.9f, 27.85f, 6.45f); Ceil(24.1f, 14.1f, 30.9f, 27.85f, 6.45f);

	// ---- exterior walls ----
	WallY(0, -0.15f, 40.15f, 0, 7.0f, 0.3f, Ext, {
		O{3, 4.1f, 0, DoorH, ECEOpen::Door},            // bay south personnel door
		O{19, 23, 2.0f, 2.9f, ECEOpen::Glass},          // workshop clerestory
		O{24.5f, 27.5f, 4.4f, 5.6f, ECEOpen::Glass},    // records window
		O{28, 29.1f, 0, DoorH, ECEOpen::Door} });       // workshop south door
	WallY(28, -0.15f, 40.15f, 0, 7.0f, 0.3f, Ext, {
		O{15, 16.1f, 0, DoorH, ECEOpen::Door},          // corridor north door
		O{19, 22, 1.0f, 2.2f, ECEOpen::Glass},          // office A window (sightline only)
		O{26, 28, 0.35f, 2.2f, ECEOpen::Glass},         // office B low window (entry once broken)
		O{29, 30.1f, F2, F2 + DoorH, ECEOpen::Door} }); // fire escape door
	WallX(0, 0.15f, 27.85f, 0, 7.0f, 0.3f, Ext, {
		O{5, 9, 0, 2.4f, ECEOpen::Open},                // loading door 1 (shutter half open)
		O{19, 23, 0, 2.4f, ECEOpen::Open} });           // loading door 2
	WallX(40, 0.15f, 27.85f, 0, 7.0f, 0.3f, Ext, {
		O{3, 5, 4.4f, 5.6f, ECEOpen::Glass},            // plant room window
		O{12, 13.1f, 0, DoorH, ECEOpen::Door} });       // east door
	// shutters (partly rolled down)
	Box(0.16f, 5, 2.4f, 0.3f, 9, 3.3f, ECESurf::Metal, ECECol::None);
	Box(0.16f, 19, 2.4f, 0.3f, 23, 3.3f, ECESurf::Metal, ECECol::None);
	Box(0.18f, 5, 2.36f, 0.32f, 9, 2.42f, ECESurf::Hazard, ECECol::None);
	Box(0.18f, 19, 2.36f, 0.32f, 23, 2.42f, ECESurf::Hazard, ECECol::None);

	// ---- ground floor interior ----
	WallX(14, 0.15f, 27.85f, 0, F2, 0.25f, Blk, {
		O{3, 4.1f, 0, DoorH, ECEOpen::Door},
		O{13, 15, 0, 2.6f, ECEOpen::Barricade},
		O{19.5f, 21.5f, 0, 2.5f, ECEOpen::Breach} });
	WallX(14, 0.15f, 27.85f, F2, C2, 0.25f, Blk, {
		O{6, 7.1f, F2, F2 + DoorH, ECEOpen::Door},
		O{20, 21.5f, F2, F2 + 2.3f, ECEOpen::Barricade} });
	WallX(17, 0.15f, 27.85f, 0, C1, 0.2f, Blk, {
		O{4, 5.5f, 0, 2.4f, ECEOpen::Barricade},
		O{13, 15, 0, 2.6f, ECEOpen::Open},
		O{21, 22.1f, 0, DoorH, ECEOpen::Door} });
	WallY(10, 17.1f, 39.85f, 0, C1, 0.2f, Blk, {
		O{18.5f, 20, 0, 2.4f, ECEOpen::Barricade},
		O{24, 26, 0, 2.5f, ECEOpen::Breach},
		O{27.5f, 29.5f, 1.0f, 2.1f, ECEOpen::Glass},
		O{35, 37, 0, 2.6f, ECEOpen::Open} });
	WallX(22, 10.1f, 17.9f, 0, C1, 0.2f, Blk, {
		O{13.4f, 14.5f, 0, DoorH, ECEOpen::Door},
		O{16, 17.5f, 0, 2.5f, ECEOpen::Breach} });
	WallX(31, 10.1f, 17.9f, 0, C1, 0.2f, Blk, { O{15, 16.1f, 0, DoorH, ECEOpen::Door} });
	WallX(31, 18.1f, 27.85f, 0, C1, 0.25f, Ext, {});
	WallY(18, 17.1f, 22, 0, C1, 0.15f, Dry, {});
	WallY(18, 22, 31, 0, C1, 0.2f, Blk, { O{26, 28, 0, 2.5f, ECEOpen::Breach} });
	WallY(18, 31.1f, 39.85f, 0, C1, 0.2f, Blk, { O{32, 34, 0, 2.6f, ECEOpen::Open} });
	WallX(24, 18.1f, 27.85f, 0, C1, 0.15f, Dry, { O{22, 23.1f, 0, DoorH, ECEOpen::Door} });

	// ---- upper floor interior ----
	WallX(17, 0.15f, 27.85f, F2, C2, 0.15f, Dry, {
		O{4, 5.1f, F2, F2 + DoorH, ECEOpen::Door},
		O{20, 21.1f, F2, F2 + DoorH, ECEOpen::Door} });
	WallY(14, 17.1f, 30.9f, F2, C2, 0.15f, Dry, {
		O{20, 21.5f, F2, F2 + 2.4f, ECEOpen::Open},
		O{26, 27.1f, F2, F2 + DoorH, ECEOpen::Door} });
	WallX(24, 14.1f, 27.85f, F2, C2, 0.15f, Dry, { O{18, 19.1f, F2, F2 + DoorH, ECEOpen::Door} });
	WallX(31, 0.15f, 17.9f, F2, C2, 0.25f, Blk, {
		O{2, 4, F2, F2 + 2.5f, ECEOpen::Breach},
		O{7, 8.1f, F2, F2 + DoorH, ECEOpen::Door} });
	WallX(31, 18.1f, 27.85f, F2, C2, 0.25f, Ext, { O{20, 21.1f, F2, F2 + DoorH, ECEOpen::Door} });
	WallY(18, 31.1f, 39.85f, F2, C2, 0.2f, Blk, { O{36, 38, F2, F2 + 2.4f, ECEOpen::Open} });

	// ---- loading bay: stairs, catwalk, props ----
	Stairs(4.f, 26.2f, 27.6f, 1.f, 14, ECESurf::Grate);
	Box(8.2f, 25.6f, 3.4f, 13.88f, 27.85f, F2, ECESurf::Grate);   // catwalk north
	Box(11.2f, 0.15f, 3.4f, 13.88f, 25.6f, F2, ECESurf::Grate);   // catwalk east
	for (float Y : {2.f, 7.f, 12.f, 17.f, 22.f}) Box(11.2f, Y, 0, 11.38f, Y + 0.18f, 3.4f, ECESurf::Metal);
	Box(8.4f, 25.62f, 0, 8.58f, 25.8f, 3.4f, ECESurf::Metal);
	Rail(11.2f, 0.15f, 11.2f, 25.6f, F2);
	Rail(8.2f, 25.6f, 11.2f, 25.6f, F2);
	Box(3, 12, 0, 5.4f, 18, 2.6f, ECESurf::Container);                // container
	Box(2.98f, 12, 2.55f, 5.42f, 18, 2.62f, ECESurf::Paint, ECECol::None);
	Box(8, 4, 0, 9.2f, 5.2f, 1.2f, ECESurf::Crate); Box(8, 5.3f, 0, 9.2f, 6.5f, 1.2f, ECESurf::Crate);
	Box(8.1f, 4.2f, 1.2f, 9.1f, 5.1f, 2.2f, ECESurf::Crate);
	Box(6, 20, 0, 7.2f, 21.2f, 1.0f, ECESurf::Crate);
	Box(9.3f, 15, 0, 10.4f, 17.5f, 1.3f, ECESurf::Crate);
	Box(5.6f, 7.6f, 0, 7.0f, 9.2f, 1.5f, ECESurf::Hazard);             // forklift body
	Box(5.3f, 7.8f, 0, 5.5f, 9.0f, 2.4f, ECESurf::Gunmetal);           // mast
	Box(1.0f, 1.0f, 0, 1.6f, 9.6f, 0.012f, ECESurf::Hazard, ECECol::None);   // floor markings
	Box(1.0f, 18.4f, 0, 1.6f, 23.6f, 0.012f, ECESurf::Hazard, ECECol::None);
	for (float Y : {1.f, 4.5f}) Dress(TEXT("shelves"), 0.3f, Y, 0, 1.0f, Y + 2.5f, 2.6f, ECESurf::Metal, 0); // pallet racking
	Box(0.3f, 1.0f, 1.2f, 1.0f, 7.0f, 1.25f, ECESurf::Hazard);

	// ---- service corridor ----
	Dress(TEXT("utilbox_green"), 16.35f, 6, 0, 16.9f, 9, 1.9f, ECESurf::Metal, 180);   // switchgear cabinets
	Box(14.2f, 0.2f, 2.85f, 14.42f, 27.8f, 3.05f, ECESurf::Gunmetal, ECECol::None); // pipe run
	Box(14.15f, 0.2f, 6.35f, 14.4f, 27.8f, 6.5f, ECESurf::Gunmetal, ECECol::None);
	Dress(TEXT("utilbox"), 16.4f, 16, F2, 16.9f, 19, F2 + 1.9f, ECESurf::Metal, 180);

	// ---- workshop ----
	{
		const float Top = Dress(TEXT("desk"), 19, 0.4f, 0, 23, 1.2f, 0.95f, ECESurf::Metal, 90);   // workbench
		Prop(TEXT("toolchest"), 19.7f, 0.75f, Top, 5, ECESurf::Metal, 1.f, false);
	}
	Box(24.6f, 4, 0, 27.2f, 5.2f, 1.4f, ECESurf::Paint);               // lathe
	Box(30, 6.3f, 0, 32, 8.3f, 1.8f, ECESurf::Paint);                  // press
	Dress(TEXT("shelves"), 33, 0.2f, 0, 38, 0.8f, 2.2f, ECESurf::Metal, 90);          // shelving
	Dress(TEXT("toolchest"), 35.4f, 3.8f, 0, 36.4f, 4.6f, 1.0f, ECESurf::Metal, 90, 1.5f);   // roller cabinet
	Box(28.4f, 4.6f, 0, 28.9f, 5.1f, C1, ECESurf::Concrete);           // column
	Box(20, 6.8f, 0, 21.2f, 8.0f, 1.1f, ECESurf::Crate);

	// ---- security desk ----
	{
		const float Top = Dress(TEXT("desk"), 18.6f, 12, 0, 19.4f, 16, 1.05f, ECESurf::Wood, 180);
		if (PropMesh(TEXT("tv"))) { PropFacing(TEXT("tv"), 19.0f, 12.85f, Top, 180); PropFacing(TEXT("tv"), 19.0f, 14.95f, Top, 180); }
		else
		{
			Box(18.75f, 12.5f, 1.05f, 19.15f, 13.2f, 1.45f, ECESurf::Polymer, ECECol::None);
			Box(18.75f, 14.6f, 1.05f, 19.15f, 15.3f, 1.45f, ECESurf::Polymer, ECECol::None);
		}
	}

	// ---- comms (objective) ----
	Dress(TEXT("utilbox"), 23.4f, 11, 0, 24.2f, 13, 2.0f, ECESurf::Metal, 0); Dress(TEXT("utilbox"), 23.4f, 15.2f, 0, 24.2f, 17.0f, 2.0f, ECESurf::Metal, 0);
	Dress(TEXT("utilbox_green"), 29.2f, 11.2f, 0, 30.0f, 13.4f, 2.0f, ECESurf::Metal, 180);
	{
		const float Top = Dress(TEXT("desk"), 26, 13.5f, 0, 28, 14.6f, 0.9f, ECESurf::Metal, 90);   // console
		if (PropMesh(TEXT("tv"))) { PropFacing(TEXT("tv"), 26.55f, 13.95f, Top, 90); PropFacing(TEXT("tv"), 27.45f, 13.95f, Top, 90); }
		else Box(26.2f, 13.7f, 0.9f, 27.8f, 13.9f, 1.4f, ECESurf::Polymer, ECECol::None);
	}
	Box(22.2f, 10.2f, 2.6f, 30.8f, 10.5f, 2.7f, ECESurf::Gunmetal, ECECol::None); // cable tray
	Box(25.0f, 13.0f, 0, 29.0f, 15.1f, 0.012f, ECESurf::Hazard, ECECol::None);   // plant zone floor marking

	// ---- east hall ----
	Box(37, 14.5f, 0, 38.2f, 15.7f, 1.2f, ECESurf::Crate);
	Dress(TEXT("utilbox_green"), 39.2f, 15, 0, 39.85f, 17, 2.0f, ECESurf::Metal, 180);

	// ---- offices ----
	Dress(TEXT("desk"), 18, 24, 0, 19.6f, 25.2f, 0.76f, ECESurf::Wood, 90); Dress(TEXT("desk"), 21, 24, 0, 22.6f, 25.2f, 0.76f, ECESurf::Wood, 90);
	Dress(TEXT("cabinet"), 23.3f, 26, 0, 23.85f, 27.8f, 1.3f, ECESurf::Paint, 180);
	Dress(TEXT("desk"), 25, 20, 0, 26.6f, 21.2f, 0.76f, ECESurf::Wood, 90); Dress(TEXT("desk"), 28.5f, 23.4f, 0, 30, 24.8f, 0.76f, ECESurf::Wood, 90);
	Dress(TEXT("shelves"), 30.4f, 19, 0, 30.85f, 22, 2.0f, ECESurf::Wood, 180);

	// ---- east stairwell ----
	Stairs(33.f, 24.f, 26.f, 1.f, 14, ECESurf::Concrete);
	Rail(32.5f, 23.9f, 37.2f, 23.9f, F2);
	Rail(32.5f, 26.1f, 37.2f, 26.1f, F2);
	Rail(32.5f, 23.9f, 32.5f, 26.1f, F2);

	// ---- upper floor props ----
	Dress(TEXT("desk"), 19, 2.5f, F2, 22, 3.4f, F2 + 0.76f, ECESurf::Wood, 90); Dress(TEXT("desk"), 19, 6.5f, F2, 22, 7.4f, F2 + 0.76f, ECESurf::Wood, 90);
	Dress(TEXT("desk"), 25, 2.5f, F2, 28, 3.4f, F2 + 0.76f, ECESurf::Wood, 90);
	Dress(TEXT("shelves"), 27.5f, 9, F2, 30.5f, 9.6f, F2 + 2.0f, ECESurf::Metal, 270);
	Dress(TEXT("desk"), 19, 22, F2, 21, 23, F2 + 0.76f, ECESurf::Wood, 90);
	Dress(TEXT("sofa"), 22.5f, 25, F2, 23.8f, 27, F2 + 0.7f, ECESurf::Olive, 180);
	Dress(TEXT("desk"), 26, 20, F2, 28, 21.5f, F2 + 0.75f, ECESurf::Wood, 90);
	Dress(TEXT("dresser"), 24.2f, 25, F2, 24.8f, 27.8f, F2 + 0.9f, ECESurf::Paint, 0);
	Dress(TEXT("utilbox"), 30.2f, 15, F2, 30.85f, 15.8f, F2 + 1.8f, ECESurf::Paint, 180);
	Box(33, 2, F2, 36, 6, F2 + 1.8f, ECESurf::Metal);                  // HVAC
	Box(36.5f, 10, F2, 39.5f, 13, F2 + 1.5f, ECESurf::Metal);
	Box(31.2f, 0.4f, 6.2f, 39.8f, 0.7f, 6.45f, ECESurf::Gunmetal, ECECol::None);

	// ---- fire escape ----
	Stairs(38.f, 28.3f, 29.7f, -1.f, 14, ECESurf::Grate);
	Box(28.5f, 28.15f, 3.4f, 34.1f, 30.0f, F2, ECESurf::Grate);
	Rail(28.5f, 30.0f, 34.1f, 30.0f, F2);
	Rail(28.5f, 28.15f, 28.5f, 30.0f, F2);
	Box(28.6f, 29.8f, 0, 28.75f, 29.95f, 3.4f, ECESurf::Metal);
	Box(33.9f, 29.8f, 0, 34.05f, 29.95f, 3.4f, ECESurf::Metal);

	// ---- exterior cover & dressing ----
	Box(-9, -3, 0, -6.5f, 3, 3.0f, ECESurf::Paint);                     // box truck
	Box(-9, 3, 0, -6.6f, 4.8f, 2.4f, ECESurf::Paint);
	Box(-15, 8, 0, -12.6f, 14, 2.6f, ECESurf::Container);              // containers
	Box(-11, 20, 0, -5, 22.4f, 2.6f, ECESurf::ContainerB);
	Box(18, 36, 0, 24, 38.4f, 2.6f, ECESurf::Container);
	Box(30, 36, 0, 32.4f, 42, 2.6f, ECESurf::ContainerB);
	Dress(TEXT("barrier"), 20, -8, 0, 24, -7.4f, 0.85f, ECESurf::Concrete, 90);        // road barriers
	Dress(TEXT("barrier_b"), 31, -10, 0, 35, -9.4f, 0.85f, ECESurf::Concrete, 90);
	Box(10, -4, 0, 12, -2.8f, 1.4f, ECESurf::Metal);                   // dumpster
	Box(44, 9, 0, 46.5f, 11.5f, 2.0f, ECESurf::Paint);                 // generator
	Dress(TEXT("barrier"), 44, 16, 0, 48, 16.6f, 0.85f, ECESurf::Concrete, 90);
	Box(5, 31, 0, 7.4f, 37, 2.6f, ECESurf::Container);
	// perimeter wall (visual) + invisible boundary
	Box(-30.2f, -28.2f, 0, -29.9f, 56.2f, 2.8f, ECESurf::Concrete, ECECol::None);
	Box(70, -28.2f, 0, 70.3f, 56.2f, 2.8f, ECESurf::Concrete, ECECol::None);
	Box(-30.2f, -28.2f, 0, 70.3f, -27.9f, 2.8f, ECESurf::Concrete, ECECol::None);
	Box(-30.2f, 56, 0, 70.3f, 56.3f, 2.8f, ECESurf::Concrete, ECECol::None);
	Box(-30.5f, -28.5f, 0, -29.9f, 56.5f, 8, ECESurf::Concrete, ECECol::HiddenPawn);
	Box(70, -28.5f, 0, 70.6f, 56.5f, 8, ECESurf::Concrete, ECECol::HiddenPawn);
	Box(-30.5f, -28.5f, 0, 70.6f, -27.9f, 8, ECESurf::Concrete, ECECol::HiddenPawn);
	Box(-30.5f, 56, 0, 70.6f, 56.5f, 8, ECESurf::Concrete, ECECol::HiddenPawn);
	// floodlight poles
	for (const FVector2D& P : { FVector2D(-12, -12), FVector2D(52, 30), FVector2D(14, 44) })
	{
		Box(P.X - 0.1f, P.Y - 0.1f, 0, P.X + 0.1f, P.Y + 0.1f, 8, ECESurf::Gunmetal);
	}
}

void ACELevelBuilder::BuildLighting()
{
	// ---- sky / sun / exposure ----
	UTextureCube* Hdri = LoadObject<UTextureCube>(nullptr, TEXT("/Game/Photoreal/HDRI/HDRI_Sky.HDRI_Sky"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* SkyMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Photoreal/Materials/M_CE_Sky.M_CE_Sky"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	const bool bDome = Hdri && SkyMat;
	if (bDome)
	{
		// the scanned sky is also what you see: a dome showing the same HDRI that lights the scene
		UStaticMeshComponent* Dome = NewObject<UStaticMeshComponent>(this);
		Dome->SetupAttachment(RootComponent);
		Dome->SetMobility(EComponentMobility::Static);
		Dome->SetStaticMesh(UCEAssets::Get(this)->Sphere);
		Dome->SetMaterial(0, SkyMat);
		Dome->SetRelativeScale3D(FVector(30000.f));   // 15 km radius
		Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Dome->SetCastShadow(false);
		Dome->bAffectDistanceFieldLighting = false;
		Dome->bAffectDynamicIndirectLighting = false;
		Dome->RegisterComponent(); AddInstanceComponent(Dome);
	}
	else
	{
		USkyAtmosphereComponent* Sky = NewObject<USkyAtmosphereComponent>(this);
		Sky->SetupAttachment(RootComponent); Sky->RegisterComponent(); AddInstanceComponent(Sky);
	}
	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(RootComponent);
	Fog->FogDensity = UCETuning::Get()->FogDensity;
	Fog->FogHeightFalloff = 0.12f;
	Fog->FogInscatteringLuminance = FLinearColor(0.34f, 0.37f, 0.4f);
	Fog->StartDistance = 1200.f;
	Fog->RegisterComponent(); AddInstanceComponent(Fog);

	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->Intensity = UCETuning::Get()->SunLux;   // overcast late afternoon
	Sun->bUseTemperature = true; Sun->Temperature = 5200.f;
	Sun->SetWorldRotation(FRotator(-24.f, 132.f, 0.f));
	Sun->RegisterComponent(); AddInstanceComponent(Sun);
	Sun->SetAtmosphereSunLight(!bDome);

	USkyLightComponent* SkyL = NewObject<USkyLightComponent>(this);
	SkyL->SetupAttachment(RootComponent);
	SkyL->SetMobility(EComponentMobility::Movable);
	SkyL->bRealTimeCapture = true;
	SkyL->Intensity = UCETuning::Get()->SkyLightIntensity;
	if (Hdri)
	{
		// scanned overcast sky (Poly Haven, CC0) drives ambient light and reflections
		SkyL->bRealTimeCapture = false;
		SkyL->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
		SkyL->Cubemap = Hdri;
		SkyL->Intensity = UCETuning::Get()->SkyLightIntensity * UCETuning::Get()->HdriIntensity;
		UE_LOG(LogColdEntry, Log, TEXT("Sky light: scanned HDRI"));
	}
	SkyL->RegisterComponent(); AddInstanceComponent(SkyL);
	SkyL->RecaptureSky();

	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(this);
	PP->SetupAttachment(RootComponent);
	PP->bUnbound = true;
	FPostProcessSettings& S = PP->Settings;
	S.bOverride_AutoExposureMethod = true;      S.AutoExposureMethod = AEM_Histogram;
	S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = UCETuning::Get()->ExposureMinEV;
	S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = UCETuning::Get()->ExposureMaxEV;
	S.bOverride_AutoExposureSpeedUp = true;     S.AutoExposureSpeedUp = 3.5f;
	S.bOverride_AutoExposureSpeedDown = true;   S.AutoExposureSpeedDown = 2.0f;
	S.bOverride_AutoExposureBias = true;        S.AutoExposureBias = UCETuning::Get()->ExposureBias;
	S.bOverride_BloomIntensity = true;          S.BloomIntensity = 0.3f;
	S.bOverride_VignetteIntensity = true;       S.VignetteIntensity = 0.32f;
	S.bOverride_MotionBlurAmount = true;        S.MotionBlurAmount = 0.f;
	S.bOverride_LensFlareIntensity = true;      S.LensFlareIntensity = 0.f;
	S.bOverride_SceneFringeIntensity = true;    S.SceneFringeIntensity = 0.f;
	S.bOverride_FilmGrainIntensity = true;      S.FilmGrainIntensity = 0.06f;
	S.bOverride_ColorSaturation = true;         S.ColorSaturation = FVector4(0.86f, 0.88f, 0.9f, 1.f);
	S.bOverride_ColorContrast = true;           S.ColorContrast = FVector4(1.04f, 1.04f, 1.04f, 1.f);
	S.bOverride_WhiteTemp = true;               S.WhiteTemp = UCETuning::Get()->WhiteBalance;
	PP->RegisterComponent(); AddInstanceComponent(PP);

	// ---- interior practical lights (values are in candela; exposure adapts between in/out) ----
	const float Fl = 5200.f;   // cool fluorescent
	const float Wm = 4200.f;   // warm office
	// loading bay high-bays
	for (float X : {3.5f, 9.5f}) for (float Y : {4.f, 14.f, 24.f}) Light(X, Y, 6.25f, 14.f, 16.f, 4200.f, 0.6f, true);
	// GF corridor
	Light(15.5f, 4, 3.0f, 3.f, 7.f, Fl, 1.2f, true); Light(15.5f, 14, 3.0f, 3.f, 7.f, Fl, 1.2f, true);
	Light(15.5f, 24, 3.0f, 3.f, 7.f, Fl, 1.2f, true);
	FlickerLight = Lights.Last();
	// workshop
	for (float X : {20.f, 26.f, 32.f, 37.5f}) Light(X, 5, 3.0f, 4.f, 9.f, Fl, 1.2f, true);
	// security desk, comms, east hall
	Light(19.5f, 12, 2.85f, 3.f, 7.f, Fl, 1.2f, true); Light(19.5f, 16.5f, 2.85f, 2.5f, 7.f, Fl, 1.2f, true);
	Light(24.5f, 12, 2.85f, 3.5f, 8.f, Fl, 1.2f, true); Light(28.5f, 12, 2.85f, 3.5f, 8.f, Fl, 1.2f, true);
	Light(24.5f, 16.2f, 2.85f, 3.5f, 8.f, Fl, 1.2f, true); Light(28.5f, 16.2f, 2.85f, 3.5f, 8.f, Fl, 1.2f, true);
	Light(33.5f, 14, 3.0f, 3.f, 8.f, Fl, 1.2f, true); Light(38, 14, 3.0f, 3.f, 8.f, Fl, 1.2f, true);
	// offices
	Light(19, 21, 2.8f, 2.5f, 7.f, Wm, 1.2f, true); Light(22, 25.5f, 2.8f, 2.5f, 7.f, Wm, 1.2f, true);
	Light(26, 22, 2.8f, 2.5f, 7.f, Wm, 1.2f, true); Light(29, 25.5f, 2.8f, 2.5f, 7.f, Wm, 1.2f, true);
	// stairwell
	Light(35, 20.5f, 3.0f, 2.f, 8.f, Fl, 0.8f, true); Light(38.5f, 24, 6.4f, 2.5f, 9.f, Fl, 0.8f, true);
	// upper floor
	Light(15.5f, 5, 6.45f, 2.5f, 7.f, Fl, 1.2f, true); Light(15.5f, 15, 6.45f, 2.5f, 7.f, Fl, 1.2f, true); Light(15.5f, 25, 6.45f, 2.5f, 7.f, Fl, 1.2f, true);
	for (float X : {20.f, 27.f}) for (float Y : {4.f, 10.f}) Light(X, Y, 6.45f, 3.f, 8.f, Fl, 1.2f, true);
	Light(20.5f, 21, 6.3f, 2.5f, 7.f, Wm, 1.2f, true);
	Light(27.5f, 18, 6.3f, 2.5f, 7.f, Wm, 1.2f, true); Light(27.5f, 24.5f, 6.3f, 2.5f, 7.f, Wm, 1.2f, true);
	Light(34, 9, 6.4f, 2.5f, 9.f, Fl, 1.2f, true); Light(36, 15, 6.4f, 2.f, 8.f, Fl, 1.2f, true);
	// exterior floodlights (sodium) & door lamps
	Light(-12, -12, 7.7f, 25.f, 30.f, 2200.f, 0.f, false);
	Light(52, 30, 7.7f, 25.f, 30.f, 2200.f, 0.f, false);
	Light(14, 44, 7.7f, 25.f, 30.f, 2200.f, 0.f, false);
	Light(-0.6f, 7, 3.2f, 4.f, 9.f, 2600.f, 0.f, false);
	Light(-0.6f, 21, 3.2f, 4.f, 9.f, 2600.f, 0.f, false);
	Light(28.5f, -0.6f, 2.6f, 3.f, 8.f, 2600.f, 0.f, false);
	Light(40.6f, 12.5f, 2.6f, 3.f, 8.f, 2600.f, 0.f, false);
}

void ACELevelBuilder::BuildGameplayData()
{
	// ---- callout zones (first match wins; building zones before yards) ----
	Zone(TEXT("Loading Bay"), 0, 0, -1, 14, 28, C2, ECEReverb::LargeHall);
	Zone(TEXT("Service Corridor"), 14, 0, -1, 17, 28, 3.3f, ECEReverb::Corridor);
	Zone(TEXT("Workshop"), 17, 0, -1, 40, 10, 3.3f, ECEReverb::Office);
	Zone(TEXT("Security Desk"), 17, 10, -1, 22, 18, 3.3f, ECEReverb::SmallRoom);
	Zone(TEXT("Comms"), 22, 10, -1, 31, 18, 3.3f, ECEReverb::SmallRoom);
	Zone(TEXT("East Hall"), 31, 10, -1, 40, 18, 3.3f, ECEReverb::Corridor);
	Zone(TEXT("Office A"), 17, 18, -1, 24, 28, 3.3f, ECEReverb::Office);
	Zone(TEXT("Office B"), 24, 18, -1, 31, 28, 3.3f, ECEReverb::Office);
	Zone(TEXT("East Stairs"), 31, 18, -1, 40, 28, C2, ECEReverb::Stairwell);
	Zone(TEXT("Upper Corridor"), 14, 0, 3.3f, 17, 28, C2, ECEReverb::Corridor);
	Zone(TEXT("Records"), 17, 0, 3.3f, 31, 14, C2, ECEReverb::Office);
	Zone(TEXT("Manager"), 17, 14, 3.3f, 24, 28, C2, ECEReverb::SmallRoom);
	Zone(TEXT("Break Room"), 24, 14, 3.3f, 31, 28, C2, ECEReverb::SmallRoom);
	Zone(TEXT("Plant Room"), 31, 0, 3.3f, 40, 18, C2, ECEReverb::Corridor);
	Zone(TEXT("Fire Escape"), 28, 28, -1, 40, 31, 5, ECEReverb::Outdoor, false);
	Zone(TEXT("West Yard"), -31, -29, -1, 0, 57, 20, ECEReverb::Outdoor, false);
	Zone(TEXT("South Yard"), 0, -29, -1, 71, 0, 20, ECEReverb::Outdoor, false);
	Zone(TEXT("North Yard"), 0, 28, -1, 71, 57, 20, ECEReverb::Outdoor, false);
	Zone(TEXT("East Yard"), 40, 0, -1, 71, 28, 20, ECEReverb::Outdoor, false);

	ObjectiveBox = FBox(M(22.1f, 10.1f, -0.5f), M(30.9f, 17.9f, 3.0f));
	ObjectiveCentre = M(27.f, 12.3f, 0.f);
	BuildingBox = FBox(M(0, 0, -1), M(40, 28, 7.5f));
	PlantSpots = { M(25, 12, 0), M(28.5f, 16.5f, 0), M(27, 12.3f, 0) };

	auto Ring = [this](float X, float Y) { TArray<FVector> R; for (int32 i = 0; i < 5; ++i) R.Add(M(X + (i % 3 - 1) * 1.6f, Y + (i / 3) * 1.6f, 0.1f)); return R; };
	AttackSpawns = { Ring(-20, 13), Ring(24, -18), Ring(20, 45) };
	AttackSpawnNames = { TEXT("West Lot"), TEXT("South Gate"), TEXT("North Road") };
	DefendSpawns = { M(25, 11.5f, 0.1f), M(28.5f, 11.5f, 0.1f), M(25, 16.2f, 0.1f), M(29.5f, 16.5f, 0.1f), M(20.5f, 13, 0.1f) };

	EntryPoints = { M(-2.5f, 7, 0), M(-2.5f, 21, 0), M(3.55f, -2, 0), M(28.55f, -2, 0), M(42, 12.55f, 0), M(15.55f, 30.5f, 0), M(27, 30.6f, 0), M(39.4f, 29, 0) };
	EntryNames = { TEXT("Loading Door 1"), TEXT("Loading Door 2"), TEXT("Bay South Door"), TEXT("Workshop Door"), TEXT("East Door"), TEXT("North Door"), TEXT("Office Window"), TEXT("Fire Escape") };

	auto H = [this](float X, float Y, float Z, float LX, float LY, float LZ, bool bC) { FCEHold Ho; Ho.Pos = M(X, Y, Z); Ho.LookAt = M(LX, LY, LZ); Ho.bCrouch = bC; return Ho; };
	DefendHolds = {
		H(30.2f, 11.0f, 0, 22.5f, 14, 1.2f, true),   // comms SE corner -> west door
		H(23.0f, 11.0f, 0, 30.5f, 15.5f, 1.2f, false),// comms SW -> east door
		H(24.8f, 17.2f, 0, 27, 10, 1.0f, true),        // comms N -> south breach / window
		H(21.0f, 11.0f, 0, 17, 14, 1.2f, true),        // security desk -> corridor
		H(39.0f, 17.0f, 0, 36, 10, 1.2f, false),       // east hall -> workshop opening
		H(30.0f, 19.0f, 0, 27, 27.5f, 1.0f, true),     // office B -> window
		H(24.0f, 7.0f, F2, 16, 4.5f, F2 + 1.2f, false),// records -> upper corridor
		H(26.0f, 26.0f, F2, 29.5f, 28, F2 + 1.2f, true),// break room -> fire door
		H(33.0f, 2.5f, 0, 28.5f, 0, 1.2f, true),       // workshop -> south door
		H(15.5f, 25.0f, 0, 15.5f, 28, 1.2f, false),    // north corridor
	};
	PlantHolds = {
		H(24.0f, 12.0f, 0, 22, 14, 1.2f, true),
		H(29.0f, 16.5f, 0, 31, 15.5f, 1.2f, true),
		H(25.2f, 16.8f, 0, 27, 18, 1.2f, false),
		H(28.6f, 11.0f, 0, 25, 10, 1.2f, true),
		H(26.8f, 15.5f, 0, 22, 17, 1.2f, true),
	};
	AlarmSpots = { M(26.8f, 26.9f, 0.02f), M(38.6f, 13.8f, 0.02f), M(15.5f, 26.2f, 0.02f), M(28.4f, 1.6f, 0.02f), M(1.6f, 14.f, 0.02f) };
	CameraSpots = {
		{ M(14.4f, 27.2f, 2.7f), FRotator(-12, -100, 0) },
		{ M(39.5f, 0.5f, 2.7f), FRotator(-14, 160, 0) },
	};
	FixedCameras = {
		{ M(0.5f, 0.5f, 5.8f), FRotator(-24, 55, 0) },        // loading bay
		{ M(17.3f, 10.3f, 2.9f), FRotator(-20, 60, 0) },      // security desk
		{ M(20, -0.5f, 4.5f), FRotator(-22, -90, 0) },        // exterior south
		{ M(40.5f, 27.5f, 4.5f), FRotator(-20, 20, 0) },      // exterior NE
	};
}

// ---------------------------------------------------------------------------------------------
// Navigation graph: hand-placed nodes, auto-linked with clearance + ground checks.
// ---------------------------------------------------------------------------------------------
int32 ACELevelBuilder::Node(float X, float Y, float Z)
{
	FCENavNode N;
	N.Pos = M(X, Y, Z);
	N.Zone = ZoneAt(N.Pos + FVector(0, 0, 50));
	return Nodes.Add(N);
}

void ACELevelBuilder::Link(int32 A, int32 B) { ForcedLinks.Add(TPair<int32, int32>(A, B)); }

void ACELevelBuilder::BuildNav()
{
	// --- outdoors ---
	for (const TArray<FVector>& S : AttackSpawns) { const FVector& P = S[1]; Node(P.X / 100, P.Y / 100, 0); }
	Node(-20, 25, 0); Node(-20, 0, 0); Node(-16, 16.5f, 0);
	Node(-6, -6, 0); Node(-4, 7, 0); Node(-2.5f, 7, 0); Node(-4, 16, 0); Node(-4, 21, 0); Node(-2.5f, 21, 0); Node(-6, 33, 0);
	Node(3.55f, -2, 0); Node(14, -6, 0); Node(24, -9, 0); Node(28.55f, -2, 0); Node(38, -6, 0); Node(46, -6, 0);
	Node(46, 12.55f, 0); Node(42, 12.55f, 0); Node(46, 22, 0); Node(44, 32, 0); Node(39.4f, 29, 0); Node(39.4f, 31, 0);
	Node(27, 30.6f, 0); Node(27, 33, 0); Node(15.55f, 30.5f, 0); Node(15.55f, 33, 0); Node(5, 40, 0); Node(26, 45, 0); Node(10, 33, 0);
	// --- loading bay ---
	Node(1.6f, 7, 0); Node(1.6f, 21, 0); Node(3.55f, 1.6f, 0); Node(7, 2.8f, 0); Node(7, 11, 0); Node(7, 14, 0); Node(7, 23, 0);
	Node(10.3f, 10, 0); Node(12.5f, 3.55f, 0); Node(12.5f, 14, 0); Node(12.5f, 20.5f, 0); Node(10.5f, 24, 0);
	const int32 BayStairBottom = Node(3.1f, 26.9f, 0);
	const int32 BayStairTop = Node(9.0f, 26.7f, F2);
	Node(12.6f, 26.7f, F2); Node(12.6f, 20.75f, F2); Node(12.6f, 14, F2); Node(12.6f, 6.55f, F2); Node(12.6f, 1.5f, F2);
	// --- GF corridor ---
	Node(15.5f, 1.5f, 0); Node(15.5f, 3.55f, 0); Node(15.5f, 4.75f, 0); Node(15.5f, 14, 0); Node(15.5f, 20.5f, 0); Node(15.5f, 21.55f, 0); Node(15.55f, 26.6f, 0);
	// --- workshop ---
	Node(18.5f, 4.75f, 0); Node(19.25f, 8.6f, 0); Node(23.5f, 7.0f, 0); Node(25, 8.6f, 0); Node(28.55f, 1.6f, 0); Node(28.6f, 8.6f, 0);
	Node(33.5f, 4.5f, 0); Node(36, 8.6f, 0); Node(38.5f, 3.5f, 0); Node(26, 2.8f, 0);
	// --- security desk ---
	Node(19.25f, 11.4f, 0); Node(18.0f, 14, 0); Node(20.8f, 13.95f, 0); Node(20.8f, 16.75f, 0); Node(18.0f, 17.0f, 0);
	// --- comms ---
	Node(23.0f, 13.95f, 0); Node(23.0f, 16.75f, 0); Node(25.0f, 11.3f, 0); Node(27.0f, 12.4f, 0); Node(27.0f, 16.6f, 0);
	Node(27.0f, 17.1f, 0); Node(30.0f, 15.55f, 0); Node(25.2f, 14.0f, 0);
	// --- east hall ---
	Node(32.0f, 15.55f, 0); Node(36.0f, 11.5f, 0); Node(38.6f, 12.55f, 0); Node(33.0f, 16.8f, 0); Node(36.0f, 16.8f, 0);
	// --- offices ---
	Node(18.5f, 21.55f, 0); Node(20.5f, 20.0f, 0); Node(20.3f, 26.6f, 0); Node(23.0f, 22.55f, 0); Node(18.5f, 26.8f, 0);
	Node(25.0f, 22.55f, 0); Node(27.0f, 19.2f, 0); Node(27.0f, 26.5f, 0); Node(29.0f, 21.8f, 0);
	// --- east stairwell ---
	Node(33.0f, 19.2f, 0); Node(33.0f, 21.5f, 0);
	const int32 EastStairBottom = Node(32.2f, 25.0f, 0);
	const int32 EastStairTop = Node(37.8f, 25.0f, F2);
	Node(38.6f, 21.0f, F2); Node(37.0f, 19.2f, F2); Node(32.1f, 20.55f, F2); Node(35.0f, 21.5f, F2); Node(38.6f, 27.0f, F2);
	// --- upper corridor ---
	Node(15.5f, 6.55f, F2); Node(15.5f, 4.55f, F2); Node(15.5f, 14, F2); Node(15.5f, 20.6f, F2); Node(15.5f, 26, F2); Node(15.5f, 1.5f, F2);
	// --- records ---
	Node(18.5f, 4.55f, F2); Node(23.5f, 5.0f, F2); Node(24.0f, 8.2f, F2); Node(29.0f, 7.55f, F2); Node(29.5f, 3.0f, F2);
	Node(20.75f, 12.8f, F2); Node(26.55f, 12.8f, F2); Node(18.5f, 10.5f, F2);
	// --- manager / break ---
	Node(18.5f, 20.6f, F2); Node(20.75f, 15.3f, F2); Node(22.8f, 18.55f, F2); Node(19.0f, 26.5f, F2);
	Node(25.2f, 18.55f, F2); Node(26.55f, 15.3f, F2); Node(29.8f, 20.55f, F2); Node(29.55f, 26.6f, F2); Node(26.0f, 24.5f, F2);
	// --- plant room ---
	Node(32.3f, 7.55f, F2); Node(32.3f, 3.0f, F2); Node(36.0f, 8.5f, F2); Node(37.0f, 16.6f, F2); Node(38.5f, 1.5f, F2);
	// --- fire escape ---
	const int32 FireBottom = Node(39.4f, 29.0f, 0);
	const int32 FireTop = Node(33.4f, 29.05f, F2);
	Node(29.55f, 29.05f, F2);

	Link(BayStairBottom, BayStairTop);
	Link(EastStairBottom, EastStairTop);
	Link(FireBottom, FireTop);

	// ---------- auto-link ----------
	UWorld* W = GetWorld();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CENav), false);
	TArray<AActor*> Ignore;
	for (ACEEntity* E : Entities) Ignore.Add(E);
	Q.AddIgnoredActors(Ignore);

	auto Clear = [&](const FVector& A, const FVector& B) -> bool
	{
		for (float H : {60.f, 150.f})
			if (W->LineTraceTestByChannel(A + FVector(0, 0, H), B + FVector(0, 0, H), ECC_Pawn, Q)) return false;
		// ground continuity
		const float Len = FVector::Dist2D(A, B);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len / 75.f));
		for (int32 s = 1; s < Steps; ++s)
		{
			const FVector P = FMath::Lerp(A, B, (float)s / Steps);
			FHitResult Hit;
			if (!W->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 50), P - FVector(0, 0, 80), ECC_Pawn, Q)) return false;
			if (FMath::Abs(Hit.ImpactPoint.Z - P.Z) > 42.f) return false;
		}
		return true;
	};
	auto AddEdge = [&](int32 A, int32 B)
	{
		FCENavEdge E; E.A = A; E.B = B; E.Len = FVector::Dist(Nodes[A].Pos, Nodes[B].Pos);
		const FVector S = Nodes[A].Pos + FVector(0, 0, 100), T = Nodes[B].Pos + FVector(0, 0, 100);
		for (ACEEntity* Ent : Entities)
		{
			const FBox Bx(Ent->OpeningCentre - Ent->OpeningExtent - FVector(12), Ent->OpeningCentre + Ent->OpeningExtent + FVector(12));
			if (FMath::LineBoxIntersection(Bx, S, T, T - S)) E.Blockers.Add(Ent);
		}
		const int32 Idx = Edges.Add(E);
		Nodes[A].Edges.Add(Idx);
		Nodes[B].Edges.Add(Idx);
	};
	for (int32 a = 0; a < Nodes.Num(); ++a)
	{
		for (int32 b = a + 1; b < Nodes.Num(); ++b)
		{
			const FVector& A = Nodes[a].Pos; const FVector& B = Nodes[b].Pos;
			if (FMath::Abs(A.Z - B.Z) > 30.f) continue;
			if (FVector::Dist2D(A, B) > (A.Z < 50.f && !BuildingBox.IsInsideOrOn(A) ? 1700.f : 1150.f)) continue;
			if (Clear(A, B)) AddEdge(a, b);
		}
	}
	for (const TPair<int32, int32>& L : ForcedLinks) AddEdge(L.Key, L.Value);

	int32 Isolated = 0;
	for (const FCENavNode& N : Nodes) if (N.Edges.Num() == 0) { ++Isolated; UE_LOG(LogColdEntry, Warning, TEXT("Nav node isolated at %s"), *N.Pos.ToString()); }
	if (Isolated) UE_LOG(LogColdEntry, Warning, TEXT("%d isolated nav nodes"), Isolated);
}

int32 ACELevelBuilder::NearestNode(const FVector& P, bool bRequireSight) const
{
	int32 Best = -1;
	float BestD = TNumericLimits<float>::Max();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CENear), false);
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		const FVector& N = Nodes[i].Pos;
		const float DZ = FMath::Abs(N.Z - (P.Z - 90.f));
		float D = FVector::Dist2D(N, P) + DZ * 3.f;
		if (D >= BestD) continue;
		if (bRequireSight && GetWorld()->LineTraceTestByChannel(P, N + FVector(0, 0, 90), ECC_Visibility, Q)) continue;
		BestD = D; Best = i;
	}
	if (Best < 0 && bRequireSight) return NearestNode(P, false);
	return Best;
}

const FCENavEdge* ACELevelBuilder::EdgeBetween(int32 A, int32 B) const
{
	if (!Nodes.IsValidIndex(A)) return nullptr;
	for (int32 E : Nodes[A].Edges)
	{
		const FCENavEdge& Ed = Edges[E];
		if ((Ed.A == A && Ed.B == B) || (Ed.A == B && Ed.B == A)) return &Ed;
	}
	return nullptr;
}

bool ACELevelBuilder::FindPath(int32 From, int32 To, TFunctionRef<float(const FCENavEdge&)> EdgeCost, TArray<int32>& Out) const
{
	Out.Reset();
	if (!Nodes.IsValidIndex(From) || !Nodes.IsValidIndex(To)) return false;
	const int32 N = Nodes.Num();
	TArray<float> G; G.Init(TNumericLimits<float>::Max(), N);
	TArray<int32> Prev; Prev.Init(-1, N);
	TArray<bool> Closed; Closed.Init(false, N);
	TArray<int32> Open;
	G[From] = 0.f;
	Open.Add(From);
	while (Open.Num())
	{
		int32 BestI = 0; float BestF = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Open.Num(); ++i)
		{
			const float F = G[Open[i]] + FVector::Dist(Nodes[Open[i]].Pos, Nodes[To].Pos);
			if (F < BestF) { BestF = F; BestI = i; }
		}
		const int32 Cur = Open[BestI];
		Open.RemoveAtSwap(BestI);
		if (Cur == To) break;
		Closed[Cur] = true;
		for (int32 EI : Nodes[Cur].Edges)
		{
			const FCENavEdge& E = Edges[EI];
			const int32 Nb = E.A == Cur ? E.B : E.A;
			if (Closed[Nb]) continue;
			const float C = EdgeCost(E);
			if (C < 0.f) continue;
			const float NG = G[Cur] + C;
			if (NG < G[Nb])
			{
				G[Nb] = NG; Prev[Nb] = Cur;
				Open.AddUnique(Nb);
			}
		}
	}
	if (Prev[To] < 0 && From != To) return false;
	for (int32 C = To; C != -1; C = Prev[C]) Out.Insert(C, 0);
	return true;
}

// ---------------------------------------------------------------------------------------------
int32 ACELevelBuilder::ZoneAt(const FVector& P) const
{
	for (int32 i = 0; i < Zones.Num(); ++i) if (Zones[i].Box.IsInsideOrOn(P)) return i;
	return -1;
}

FString ACELevelBuilder::CalloutAt(const FVector& P) const
{
	const int32 Z = ZoneAt(P);
	return Zones.IsValidIndex(Z) ? Zones[Z].Name : FString(TEXT("Perimeter"));
}

ECEReverb ACELevelBuilder::ReverbAt(const FVector& P) const
{
	const int32 Z = ZoneAt(P);
	return Zones.IsValidIndex(Z) ? Zones[Z].Reverb : ECEReverb::Outdoor;
}

ECEPen ACELevelBuilder::PenOf(const UPrimitiveComponent* C) const
{
	if (const ECESurf* S = CompSurf.Find(C))
	{
		if (UCEAssets* A = UCEAssets::Get(this)) return A->Info(*S).Pen;
	}
	return ECEPen::Solid;
}

ECEStep ACELevelBuilder::StepOf(const UPrimitiveComponent* C) const
{
	if (const ECESurf* S = CompSurf.Find(C))
	{
		if (UCEAssets* A = UCEAssets::Get(this)) return A->Info(*S).Step;
	}
	return ECEStep::Concrete;
}

void ACELevelBuilder::ResetEntities()
{
	for (ACEEntity* E : Entities) if (E) E->ResetEntity();
}

void ACELevelBuilder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// one tired fluorescent tube in the corridor (atmosphere, used sparingly)
	if (FlickerLight)
	{
		FlickerT += DeltaTime;
		const float N = FMath::PerlinNoise1D(FlickerT * 3.f);
		const bool bOff = N > 0.42f && FMath::Fmod(FlickerT * 17.f, 1.f) > 0.4f;
		FlickerLight->SetIntensity((bOff ? 0.3f : 3.f) * UCETuning::Get()->InteriorLightScale);
	}
}
