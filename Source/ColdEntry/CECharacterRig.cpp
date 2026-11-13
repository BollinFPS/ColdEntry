// Real character models: UE 5.8 template mannequin (Manny/Quinn), its rifle/pistol animation blueprints,
// and the template rifle and pistol meshes. Everything here falls back to the box-built operators in
// CECharacter.cpp when the assets are missing (run Import_Characters.bat to copy them in).
//
// Layout per character:
//   GetMesh()  driver: runs ABP_TP_Rifle / ABP_TP_Pistol, never drawn (its pose is read every frame).
//   TPBody     poseable copy seen by everyone else. Adds a crouch (pelvis drop + two-bone leg IK) and
//              lean (spine roll) so the visible body matches the gameplay hitboxes, which are unchanged.
//   FPArms     poseable copy seen only by the owner: head/legs hidden, locked to the camera, both arms
//              solved with two-bone IK onto the first-person weapon using the grip relationship the
//              third-person animation uses.
#include "CECharacter.h"
#include "CERigShared.h"
#include "ColdEntry.h"
#include "CETuning.h"
#include "CEAssets.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimInstance.h"

void UCERigTicker::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (ACECharacter* C = Cast<ACECharacter>(GetOwner())) C->TickRigLate(DeltaTime);
}

// =============================================================================================
// Build
// =============================================================================================
bool ACECharacter::BuildRealBody()
{
	const UCETuning* T = RT();
	if (!T->bRealCharacters) return false;
	if (T->bSwatCharacters && BuildSwatBody()) return true;
	const bool bAlt = (GetTypeHash(CEName) % 3u) == 1u;
	USkeletalMesh* BodyMesh = LoadQuiet<USkeletalMesh>(bAlt ? CERig::Quinn : CERig::Manny);
	if (!BodyMesh) BodyMesh = LoadQuiet<USkeletalMesh>(CERig::Manny);
	AnimRifle = LoadClass<UAnimInstance>(nullptr, CERig::AbpRifle, nullptr, LOAD_NoWarn | LOAD_Quiet);
	AnimPistol = LoadClass<UAnimInstance>(nullptr, CERig::AbpPistol, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UStaticMesh* RifleM = LoadQuiet<UStaticMesh>(CERig::Rifle);
	UStaticMesh* PistolM = LoadQuiet<UStaticMesh>(CERig::Pistol);
	if (!BodyMesh || !AnimRifle || !RifleM || !PistolM)
	{
		UE_LOG(LogColdEntry, Warning, TEXT("Template character assets missing (mesh %d anim %d rifle %d pistol %d) - using box operators. Run Import_Characters.bat."),
			BodyMesh != nullptr, AnimRifle != nullptr, RifleM != nullptr, PistolM != nullptr);
		return false;
	}
	if (!AnimPistol) AnimPistol = AnimRifle;

	USkeletalMeshComponent* Drv = GetMesh();
	Drv->SetSkeletalMeshAsset(BodyMesh);
	Drv->SetVisibility(false);
	Drv->SetCastShadow(false);
	Drv->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Drv->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Drv->SetAnimInstanceClass(AnimRifle);
	AnimMode = 0;

	auto MakePoseable = [&](const TCHAR* Name, USceneComponent* Parent, bool bFirstPerson)
	{
		UPoseableMeshComponent* P = NewObject<UPoseableMeshComponent>(this, Name);
		P->SetupAttachment(Parent);
		P->SetSkinnedAssetAndUpdate(BodyMesh, true);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetCanEverAffectNavigation(false);
		if (bFirstPerson) { P->SetOnlyOwnerSee(true); P->SetCastShadow(false); P->bCastHiddenShadow = false; }
		else { P->SetOwnerNoSee(true); P->bCastHiddenShadow = true; }
		P->RegisterComponent();
		AddInstanceComponent(P);
		return P;
	};
	TPBody = MakePoseable(TEXT("TPBody"), Drv, false);
	FPArms = MakePoseable(TEXT("FPArms"), Camera, true);
	FPArms->SetBoundsScale(3.f);
	FPArms->HideBoneByName(TEXT("neck_01"), PBO_None);
	FPArms->HideBoneByName(TEXT("thigh_l"), PBO_None);
	FPArms->HideBoneByName(TEXT("thigh_r"), PBO_None);
	FPArms->SetVisibility(false);

	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* W = NewObject<UStaticMeshComponent>(this);
		W->SetStaticMesh(i == 0 ? RifleM : PistolM);
		if (UMaterialInterface* WM = UCEAssets::Get(this)->WeaponMaterial(i == 1)) W->SetMaterial(0, WM);
		W->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		W->SetCanEverAffectNavigation(false);
		W->SetOwnerNoSee(true);
		W->bCastHiddenShadow = true;
		W->SetupAttachment(TPBody, CERig::Grip);
		W->RegisterComponent();
		AddInstanceComponent(W);
		TPWeapon[i] = W;
	}
	UCEAssets* A = UCEAssets::Get(this);
	TPFlash = Part(TPWeapon[0], CERig::RifleMuzzle + FVector(0, 3, 0), FVector(4, 4, 4), ECESurf::Emissive, false);
	TPFlash->SetMaterial(0, A->Emissive(FLinearColor(1.f, 0.6f, 0.25f), 60.f));
	TPFlash->SetVisibility(false);
	TPFlash->SetCastShadow(false);

	// Kit over the mannequin: helmet, plate carrier, belt, back panel. Placed in the reference pose and
	// attached to the bones, so it follows the animation.
	GearParts.Add(GearOnBone(TEXT("head"), FVector(1.5f, 0, 12.f), FVector(12.5f, 11.5f, 10.f), ECESurf::Charcoal, true));
	GearParts.Add(GearOnBone(TEXT("spine_04"), FVector(3.f, 0, -2.f), FVector(13.5f, 17.f, 14.f), ECESurf::Charcoal, false));
	GearParts.Add(GearOnBone(TEXT("spine_04"), FVector(16.f, 0, -12.f), FVector(3.f, 14.f, 6.f), ECESurf::Charcoal, false));
	GearParts.Add(GearOnBone(TEXT("pelvis"), FVector(1.f, 0, 6.f), FVector(13.f, 17.5f, 3.5f), ECESurf::Charcoal, false));
	GearParts.Add(GearOnBone(TEXT("spine_04"), FVector(-15.f, 0, 1.f), FVector(4.f, 12.f, 12.f), ECESurf::Charcoal, false));

	// The rig is finished after animation has run this frame.
	UCERigTicker* Ticker = NewObject<UCERigTicker>(this, TEXT("RigTicker"));
	Ticker->RegisterComponent();
	AddInstanceComponent(Ticker);
	return true;
}

// Uniform colours on the mannequin: the torso material takes the team tint, head/legs/hands the dark under-layer.
// The template material is glossy painted metal; pushed to a matte, non-metallic finish it reads as fabric.
void ACECharacter::ApplyRealTeamLook()
{
	if (!bRealBody) return;
	if (bSwat) { ApplySwatTeamLook(); return; }
	const UCETuning* T = RT();
	USkeletalMeshComponent* Drv = GetMesh();
	const int32 Num = Drv->GetNumMaterials();
	if (BodyMIDs.Num() != Num)
	{
		BodyMIDs.Reset();
		for (int32 i = 0; i < Num; ++i)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Drv->GetMaterial(i), this);
			BodyMIDs.Add(MID);
			for (UMeshComponent* M : { (UMeshComponent*)Drv, (UMeshComponent*)TPBody.Get(), (UMeshComponent*)FPArms.Get() })
				if (M && MID) M->SetMaterial(i, MID);
		}
	}
	const FLinearColor TeamTint = Team == ECETeam::Attack ? T->AttackTint : T->DefendTint;
	for (int32 i = 0; i < Num; ++i)
	{
		UMaterialInstanceDynamic* MID = BodyMIDs[i];
		if (!MID) continue;
		MID->SetVectorParameterValue(TEXT("Paint Tint"), i == 1 ? TeamTint : T->UnderTint);
		MID->SetVectorParameterValue(TEXT("LogoTint"), FLinearColor::Black);
		MID->SetScalarParameterValue(TEXT("MetalPaintMetallic"), 0.f);
		MID->SetScalarParameterValue(TEXT("MetalPaintRoughness"), T->UniformRoughness);
		MID->SetScalarParameterValue(TEXT("EmissivePower"), 0.f);
	}
}

UStaticMeshComponent* ACECharacter::GearOnBone(FName Bone, const FVector& Off, const FVector& Half, ECESurf Surf, bool bSphere)
{
	UCEAssets* A = UCEAssets::Get(this);
	const FTransform ActorT = GetActorTransform();
	const FVector BoneLocal = ActorT.InverseTransformPosition(BoneW(TPBody, Bone).GetLocation());
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(bSphere ? A->Sphere.Get() : A->Cube.Get());
	C->SetMaterial(0, A->SurfMoving(Surf));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetOwnerNoSee(true);
	C->bCastHiddenShadow = true;
	C->RegisterComponent();
	AddInstanceComponent(C);
	C->SetWorldTransform(FTransform(ActorT.GetRotation(), ActorT.TransformPosition(BoneLocal + Off), Half / 50.f));
	C->AttachToComponent(TPBody, FAttachmentTransformRules::KeepWorldTransform, Bone);
	RigGear.Add(C);
	RigGearBones.Add(Bone);
	return C;
}

bool ACECharacter::BuildRealViewmodels()
{
	if (!bRealBody) return false;
	UStaticMesh* RifleM = LoadQuiet<UStaticMesh>(CERig::Rifle);
	UStaticMesh* PistolM = LoadQuiet<UStaticMesh>(CERig::Pistol);
	if (!RifleM || !PistolM) return false;
	const UCETuning* T = RT();
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
	for (int32 i = 0; i < 3; ++i)
	{
		USceneComponent* R = VMWeapon[i] = Root();
		const bool bPistol = i == 2;
		const float S = i == 1 ? T->SmgScale : 1.f;
		const FVector Off = (bPistol ? T->FPPistolOffset : T->FPRifleOffset) * S;
		UStaticMeshComponent* W = NewObject<UStaticMeshComponent>(this);
		W->SetStaticMesh(bPistol ? PistolM : RifleM);
		if (UMaterialInterface* WM = A->WeaponMaterial(bPistol)) W->SetMaterial(0, WM);
		W->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		W->SetCanEverAffectNavigation(false);
		W->SetOnlyOwnerSee(true);
		W->SetCastShadow(false);
		W->SetupAttachment(R);
		W->SetRelativeLocationAndRotation(Off, CERig::MeshYaw);
		W->SetRelativeScale3D(FVector(S));
		W->RegisterComponent();
		AddInstanceComponent(W);
		FPWeapon[i] = W;
		if (!bPistol) Optic(R, 7.f * S);
		const FVector Muzzle = Off + CERig::MeshYaw.RotateVector(bPistol ? CERig::PistolMuzzle : CERig::RifleMuzzle) * S;
		VMFlash[i] = Part(R, Muzzle + FVector(bPistol ? 2.f : 4.f, 0, 0), bPistol ? FVector(3, 2, 2) : FVector(5, 3, 3), ECESurf::Emissive, true);
		VMFlash[i]->SetMaterial(0, FlashMat);
		VMFlash[i]->SetVisibility(false);
		VMMag[i] = nullptr;
		MagRest[i] = FVector::ZeroVector;
	}
	// gadget in hand: the canister only; the hands come from the arms rig
	{
		USceneComponent* R = VMGadget = Root();
		Part(R, FVector(0, 0, 0), FVector(3.2f, 3.2f, 5.2f), ECESurf::Olive, true);
		Part(R, FVector(0, 0, 6), FVector(1.2f, 1.2f, 1.2f), ECESurf::Gunmetal, true);
	}
	return true;
}

// =============================================================================================
// Per frame (after animation)
// =============================================================================================
void ACECharacter::TickRigLate(float Dt)
{
	if (bSwat) { TickSwatLate(Dt); return; }
	if (!bRealBody || bDead || !bBuilt) return;
	USkeletalMeshComponent* Drv = GetMesh();
	const UCETuning* T = RT();

	// animation set + weapon in hand
	const int32 Want = (Equipped == ECEGadget::None && Slot == 0) ? 0 : 1;
	if (Want != AnimMode)
	{
		Drv->SetAnimInstanceClass(Want ? AnimPistol : AnimRifle);
		AnimMode = Want;
		if (TPFlash)
		{
			TPFlash->AttachToComponent(TPWeapon[Want], FAttachmentTransformRules::KeepRelativeTransform);
			TPFlash->SetRelativeLocation((Want ? CERig::PistolMuzzle : CERig::RifleMuzzle) + FVector(0, 3, 0));
		}
	}
	const bool bGun = Equipped == ECEGadget::None;
	TPWeapon[0]->SetVisibility(bGun && Slot == 0);
	TPWeapon[1]->SetVisibility(bGun && Slot == 1);
	TPWeapon[0]->SetRelativeScale3D(FVector(Weapons[0].Id == ECEWeaponId::SMG9 ? T->SmgScale : 1.f));

	// ---- third-person body: copy the animated pose, then crouch and lean to match the hitboxes ----
	TPBody->CopyPoseFromSkeletalComponent(Drv);
	const FVector Fwd = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const float Drop = FMath::Clamp(92.f - PelvisZ, 0.f, 40.f);
	if (Drop > 0.5f)
	{
		const FTransform FL = BoneW(TPBody, TEXT("foot_l")), FR = BoneW(TPBody, TEXT("foot_r"));
		const FVector Pel = BoneW(TPBody, TEXT("pelvis")).GetLocation();
		TPBody->SetBoneLocationByName(TEXT("pelvis"), Pel - FVector(0, 0, Drop), EBoneSpaces::WorldSpace);
		const FQuat QL = FL.GetRotation(), QR = FR.GetRotation();
		TwoBoneIK(TPBody, TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), FL.GetLocation(), Fwd - Right * 0.15f, &QL);
		TwoBoneIK(TPBody, TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), FR.GetLocation(), Fwd + Right * 0.15f, &QR);
		const FTransform S1 = BoneW(TPBody, TEXT("spine_01"));
		const FQuat Bend(Right, FMath::DegreesToRadians(14.f * Drop / 30.f));
		TPBody->SetBoneRotationByName(TEXT("spine_01"), (Bend * S1.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	}
	if (FMath::Abs(LeanAlpha) > 0.01f)
	{
		// roll the spine so the head lines up sideways with the head hitbox
		const FTransform S1 = BoneW(TPBody, TEXT("spine_01"));
		const FVector Head = BoneW(TPBody, TEXT("head")).GetLocation() + FVector(0, 0, 10.f);
		auto Flat = [&](const FVector& V) { return (V - Fwd * FVector::DotProduct(V, Fwd)).GetSafeNormal(); };
		const FVector From = Flat(Head - S1.GetLocation());
		const FVector To = Flat(HeadBox->GetComponentLocation() - S1.GetLocation());
		if (!From.IsNearlyZero() && !To.IsNearlyZero())
			TPBody->SetBoneRotationByName(TEXT("spine_01"), (FQuat::FindBetweenNormals(From, To) * S1.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	}
	TPBody->RefreshBoneTransforms();
	TPBody->UpdateChildTransforms();

	// ---- first-person arms ----
	FPArms->SetVisibility(bFPView);
	if (bFPView && bRealVM) TickFPArms(Dt, RigVMIdx, RigRelA, RigRP);
}

void ACECharacter::TickFPArms(float Dt, int32 VMIdx, float RelA, float RP)
{
	USkeletalMeshComponent* Drv = GetMesh();
	const UCETuning* T = RT();
	FPArms->CopyPoseFromSkeletalComponent(Drv);

	// Lock the rig to the camera with the (hidden) head just behind the eye.
	const FVector HeadCS = FPArms->GetBoneTransformByName(TEXT("head"), EBoneSpaces::ComponentSpace).GetLocation();
	const FVector Eye(-T->FPArmsBack, 0.f, -T->FPArmsDown);
	FPArms->SetRelativeLocationAndRotation(Eye + FPArmShift - CERig::MeshYaw.RotateVector(HeadCS), CERig::MeshYaw);

	// Where the hands sit on the gun in the third-person animation (gun = grip socket).
	const FTransform GripW = Drv->GetSocketTransform(CERig::Grip, RTS_World);
	const FTransform RelR = Drv->GetSocketTransform(TEXT("hand_r"), RTS_World).GetRelativeTransform(GripW);
	const FTransform RelL = Drv->GetSocketTransform(TEXT("hand_l"), RTS_World).GetRelativeTransform(GripW);

	const bool bGadget = Equipped != ECEGadget::None;
	FTransform HeldW;
	if (bGadget)
	{
		HeldW = VMGadget->GetComponentTransform();
		HeldW.SetRotation(HeldW.GetRotation() * CERig::MeshYaw.Quaternion());
		HeldW.AddToTranslation(-Camera->GetUpVector() * 9.f - Camera->GetForwardVector() * 4.f);
	}
	else
	{
		HeldW = FPWeapon[VMIdx]->GetComponentTransform();
	}
	HeldW.SetScale3D(FVector::OneVector);
	FTransform TR = RelR * HeldW;
	FTransform TL = RelL * HeldW;
	if (RelA > 0.f && !bGadget)
	{
		// reload: support hand goes to the magazine well and down
		const FVector Well = FPWeapon[VMIdx]->GetComponentTransform().TransformPosition(VMIdx == 2 ? CERig::PistolMagWell : CERig::RifleMagWell);
		const float Down = RP < 0.45f ? 14.f : 4.f;
		TL.SetLocation(FMath::Lerp(TL.GetLocation(), Well - Camera->GetUpVector() * Down * RelA, RelA));
	}
	if (bGadget)
	{
		// support hand stays low and out of the way
		TL.SetLocation(TR.GetLocation() - Camera->GetRightVector() * 12.f - Camera->GetUpVector() * 14.f);
	}

	// Keep both hands within reach: slide the rig towards the gun if an arm would over-stretch.
	FVector Need = FVector::ZeroVector;
	for (int32 s = 0; s < 2; ++s)
	{
		const TCHAR* U = s ? TEXT("upperarm_l") : TEXT("upperarm_r");
		const TCHAR* L = s ? TEXT("lowerarm_l") : TEXT("lowerarm_r");
		const TCHAR* H = s ? TEXT("hand_l") : TEXT("hand_r");
		const FVector PU = BoneW(FPArms, U).GetLocation(), PL = BoneW(FPArms, L).GetLocation(), PH = BoneW(FPArms, H).GetLocation();
		const float Reach = ((PL - PU).Size() + (PH - PL).Size()) * 0.97f;
		const FVector To = (s ? TL : TR).GetLocation() - PU;
		if (To.Size() > Reach) Need += To.GetSafeNormal() * (To.Size() - Reach);
	}
	const FVector NeedRel = Camera->GetComponentTransform().InverseTransformVectorNoScale(Need);
	FPArmShift += NeedRel * FMath::Min(1.f, Dt * 12.f);
	FPArmShift = FMath::VInterpTo(FPArmShift, FVector::ZeroVector, Dt, 0.8f);
	FPArmShift = FPArmShift.GetClampedToMaxSize(30.f);

	const FVector CF = Camera->GetForwardVector(), CR = Camera->GetRightVector(), CU = Camera->GetUpVector();
	const FQuat QR = TR.GetRotation(), QL = TL.GetRotation();
	TwoBoneIK(FPArms, TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), TR.GetLocation(), -CU + CR * 0.8f - CF * 0.2f, &QR);
	TwoBoneIK(FPArms, TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TL.GetLocation(), -CU - CR * 0.8f - CF * 0.2f, &QL);
	FPArms->RefreshBoneTransforms();
}

// =============================================================================================
// Death
// =============================================================================================
void ACECharacter::StartRagdoll(const FVector& Dir)
{
	if (bSwat) { StartSwatDeath(); return; }
	USkeletalMeshComponent* Drv = GetMesh();
	for (int32 i = 0; i < RigGear.Num(); ++i)
		if (RigGear[i]) RigGear[i]->AttachToComponent(Drv, FAttachmentTransformRules::KeepRelativeTransform, RigGearBones[i]);
	for (int32 i = 0; i < 2; ++i)
		if (TPWeapon[i]) TPWeapon[i]->AttachToComponent(Drv, FAttachmentTransformRules::KeepRelativeTransform, CERig::Grip);
	if (TPFlash) TPFlash->SetVisibility(false);
	TPBody->SetVisibility(false);
	FPArms->SetVisibility(false);

	Drv->SetVisibility(true);
	Drv->SetOwnerNoSee(true);
	Drv->SetCastShadow(true);
	Drv->bCastHiddenShadow = true;
	if (!RT()->bRagdoll || !Drv->GetPhysicsAsset()) return;
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	Drv->SetCollisionObjectType(ECC_PhysicsBody);
	Drv->SetCollisionResponseToAllChannels(ECR_Ignore);
	Drv->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Drv->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Drv->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	Drv->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Drv->SetAllBodiesSimulatePhysics(true);
	Drv->SetSimulatePhysics(true);
	Drv->bBlendPhysics = true;
	Drv->WakeAllRigidBodies();
	Drv->AddImpulseToAllBodiesBelow(Dir.GetSafeNormal() * 260.f + FVector(0, 0, 40.f), TEXT("pelvis"), true);
}
