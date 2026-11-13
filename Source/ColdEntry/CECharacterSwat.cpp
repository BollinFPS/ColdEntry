// SWAT operator: a Mixamo-skeleton character with real clothing, animated with Mixamo's Basic Shooter Pack
// (Tools/ue_import_swat.py -> /Game/Characters/Swat). There is no animation blueprint for this skeleton:
// the clips are sampled and blended here, every frame, straight onto the poseable body.
//
//   whole body    aiming idle <-> walk and run loops in four directions, chosen by movement direction and
//                 speed. Every loop starts with the left foot lifting, so they share one phase and blend
//                 cleanly; the phase advances with ground speed so the feet keep up with the capsule.
//   upper body    one-shot overlays on top of whatever the legs are doing: reload (stretched over the real
//                 reload time), recoil per shot, hit reaction, grenade throw
//   on top        the same crouch (pelvis drop + leg IK), lean and first-person arm IK as the mannequin rig,
//                 plus aim pitch through the spine
//   death         a dying clip (the mesh has no physics asset, so no ragdoll)
//
// The clips are Mixamo downloads with travel baked into the hips; that travel is removed here.
#include "CECharacter.h"
#include "CERigShared.h"
#include "ColdEntry.h"
#include "CETuning.h"
#include "CEAssets.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ReferenceSkeleton.h"
#include "BoneIndices.h"

namespace CESwat
{
	static const TCHAR* MeshPath = TEXT("/Game/Characters/Swat/SK_Swat.SK_Swat");
	// Locomotion loops come first, in walk/run pairs per direction.
	enum EClip { CWalkF, CRunF, CWalkB, CRunB, CWalkL, CRunL, CWalkR, CRunR, NumLoco,
		CIdle = NumLoco, CFire, CReload, CHit, CToss, CDie, NumClips };
	static const TCHAR* ClipPaths[NumClips] = {
		TEXT("/Game/Characters/Swat/Pack/A_walking.A_walking"),
		TEXT("/Game/Characters/Swat/Pack/A_rifle_run.A_rifle_run"),
		TEXT("/Game/Characters/Swat/Pack/A_walking_backwards.A_walking_backwards"),
		TEXT("/Game/Characters/Swat/Pack/A_run_backwards.A_run_backwards"),
		TEXT("/Game/Characters/Swat/Pack/A_strafe_left.A_strafe_left"),
		TEXT("/Game/Characters/Swat/Pack/A_strafe_2.A_strafe_2"),              // run, to the character's left
		TEXT("/Game/Characters/Swat/Pack/A_strafe_right.A_strafe_right"),
		TEXT("/Game/Characters/Swat/Pack/A_strafe.A_strafe"),                  // run, to the character's right
		TEXT("/Game/Characters/Swat/Pack/A_rifle_aiming_idle.A_rifle_aiming_idle"),
		TEXT("/Game/Characters/Swat/Pack/A_firing_rifle.A_firing_rifle"),
		TEXT("/Game/Characters/Swat/Pack/A_reloading.A_reloading"),
		TEXT("/Game/Characters/Swat/Pack/A_hit_reaction.A_hit_reaction"),
		TEXT("/Game/Characters/Swat/Pack/A_toss_grenade.A_toss_grenade"),
		TEXT("/Game/Characters/Swat/Anims/A_Dying.A_Dying") };
	static_assert(NumClips <= 16, "ACECharacter::SwatClips is sized for 16");

	static const FName Hips(TEXT("Hips")), Spine(TEXT("Spine")), Spine1(TEXT("Spine1")), Spine2(TEXT("Spine2")), Neck(TEXT("Neck")), Head(TEXT("Head"));
	static const FName ArmL(TEXT("LeftArm")), ForeArmL(TEXT("LeftForeArm")), HandL(TEXT("LeftHand")), MidL(TEXT("LeftHandMiddle1"));
	static const FName ArmR(TEXT("RightArm")), ForeArmR(TEXT("RightForeArm")), HandR(TEXT("RightHand")), MidR(TEXT("RightHandMiddle1"));
	static const FName UpLegL(TEXT("LeftUpLeg")), ShinL(TEXT("LeftLeg")), FootL(TEXT("LeftFoot"));
	static const FName UpLegR(TEXT("RightUpLeg")), ShinR(TEXT("RightLeg")), FootR(TEXT("RightFoot"));

	enum EKind : uint8 { KStatic, KLower, KUpper };   // Static = fingers and face: always the aim pose

	static FTransform Mix(const FTransform& A, const FTransform& B, float Alpha)
	{
		return FTransform(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha),
			FMath::Lerp(A.GetTranslation(), B.GetTranslation(), Alpha), FMath::Lerp(A.GetScale3D(), B.GetScale3D(), Alpha));
	}

	static FTransform Sample(const UAnimSequence* Seq, double Time, int32 SkelBone)
	{
		FTransform Out = FTransform::Identity;
		Seq->GetBoneTransform(Out, FSkeletonPoseBoneIndex(SkelBone), FAnimExtractContext(Time), false);
		return Out;
	}

	static void StripTravel(FTransform& RootBone)
	{
		FVector P = RootBone.GetTranslation();
		P.X = 0.f; P.Y = 0.f;
		RootBone.SetTranslation(P);
	}
}

// =============================================================================================
// Build
// =============================================================================================
bool ACECharacter::BuildSwatBody()
{
	using namespace CESwat;
	USkeletalMesh* BodyMesh = LoadQuiet<USkeletalMesh>(MeshPath);
	UStaticMesh* RifleM = LoadQuiet<UStaticMesh>(CERig::Rifle);
	UStaticMesh* PistolM = LoadQuiet<UStaticMesh>(CERig::Pistol);
	bool bClips = true;
	for (int32 i = 0; i < NumClips; ++i)
	{
		SwatClips[i] = LoadQuiet<UAnimSequence>(ClipPaths[i]);
		bClips &= SwatClips[i] != nullptr;
	}
	if (!BodyMesh || !bClips || !RifleM || !PistolM || !BodyMesh->GetSkeleton())
	{
		UE_LOG(LogColdEntry, Warning, TEXT("SWAT assets missing (mesh %d clips %d) - falling back to the template mannequin. Run Import_Swat.bat."),
			BodyMesh != nullptr, bClips ? 1 : 0);
		return false;
	}

	USkeletalMeshComponent* Drv = GetMesh();   // carries the mesh for attachment/materials only; never drawn or animated
	Drv->SetSkeletalMeshAsset(BodyMesh);
	Drv->SetVisibility(false);
	Drv->SetCastShadow(false);
	Drv->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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
	FPArms->HideBoneByName(Neck, PBO_None);
	FPArms->HideBoneByName(UpLegL, PBO_None);
	FPArms->HideBoneByName(UpLegR, PBO_None);
	FPArms->SetVisibility(false);

	// ---- bone tables and the standing aim pose ----
	const FReferenceSkeleton& MeshRef = BodyMesh->GetRefSkeleton();
	const FReferenceSkeleton& SkelRef = BodyMesh->GetSkeleton()->GetReferenceSkeleton();
	const int32 NumB = MeshRef.GetNum();
	if (TPBody->BoneSpaceTransforms.Num() != NumB) return false;
	const int32 SpineIdx = MeshRef.FindBoneIndex(Spine);
	SwatSkelBone.SetNum(NumB); SwatKind.SetNum(NumB); SwatAim.SetNum(NumB); SwatAcc.SetNum(NumB); SwatAccW.SetNum(NumB);
	for (int32 b = 0; b < NumB; ++b)
	{
		const FString Name = MeshRef.GetBoneName(b).ToString();
		SwatSkelBone[b] = SkelRef.FindBoneIndex(MeshRef.GetBoneName(b));
		const bool bFinger = Name.Contains(TEXT("Thumb")) || Name.Contains(TEXT("Index")) || Name.Contains(TEXT("Middle")) || Name.Contains(TEXT("Ring")) || Name.Contains(TEXT("Pinky"));
		const bool bFace = Name.Contains(TEXT("Eye")) || Name.Contains(TEXT("Jaw")) || Name.Contains(TEXT("_End"));
		const bool bUpper = b == SpineIdx || (SpineIdx != INDEX_NONE && MeshRef.BoneIsChildOf(b, SpineIdx));
		SwatKind[b] = (bFinger || bFace) ? (uint8)KStatic : (bUpper ? (uint8)KUpper : (uint8)KLower);
		SwatAim[b] = SwatSkelBone[b] != INDEX_NONE ? Sample(SwatClips[CIdle], 0.0, SwatSkelBone[b]) : MeshRef.GetRefBonePose()[b];
		if (b == 0) StripTravel(SwatAim[b]);
	}

	// ---- how long each loop is and how much ground it covers (hips travel), so playback can follow ground speed ----
	for (int32 i = 0; i < NumLoco; ++i)
	{
		SwatLen[i] = FMath::Max(0.1f, SwatClips[i]->GetPlayLength());
		const FVector A = Sample(SwatClips[i], 0.0, SwatSkelBone[0]).GetTranslation();
		const FVector B = Sample(SwatClips[i], SwatLen[i] - 0.001, SwatSkelBone[0]).GetTranslation();
		SwatTravel[i] = FMath::Max(20.f, FVector::Dist2D(A, B));
	}

	// ---- where the weapon sits in the right hand: pointed from the firing-hand palm to the support-hand palm ----
	TPBody->BoneSpaceTransforms = SwatAim;
	TPBody->RefreshBoneTransforms();
	const FTransform HandCS = TPBody->GetBoneTransformByName(HandR, EBoneSpaces::ComponentSpace);
	const FVector RPalm = FMath::Lerp(HandCS.GetLocation(), TPBody->GetBoneTransformByName(MidR, EBoneSpaces::ComponentSpace).GetLocation(), 0.65f);
	const FVector LPalm = FMath::Lerp(TPBody->GetBoneTransformByName(HandL, EBoneSpaces::ComponentSpace).GetLocation(),
		TPBody->GetBoneTransformByName(MidL, EBoneSpaces::ComponentSpace).GetLocation(), 0.65f);
	const FVector Bore = (LPalm - RPalm).GetSafeNormal(UE_SMALL_NUMBER, FVector(0, 1, 0));
	const FTransform WeaponCS(FRotationMatrix::MakeFromYZ(Bore, FVector::UpVector).ToQuat(), RPalm + FVector(0, 0, RT()->SwatGripRise));
	SwatGripRel = WeaponCS.GetRelativeTransform(HandCS);

	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* W = NewObject<UStaticMeshComponent>(this);
		W->SetStaticMesh(i == 0 ? RifleM : PistolM);
		if (UMaterialInterface* WM = UCEAssets::Get(this)->WeaponMaterial(i == 1)) W->SetMaterial(0, WM);
		W->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		W->SetCanEverAffectNavigation(false);
		W->SetOwnerNoSee(true);
		W->bCastHiddenShadow = true;
		W->SetupAttachment(TPBody, HandR);
		W->SetRelativeTransform(SwatGripRel);
		W->RegisterComponent();
		AddInstanceComponent(W);
		TPWeapon[i] = W;
	}
	UCEAssets* A = UCEAssets::Get(this);
	TPFlash = Part(TPWeapon[0], CERig::RifleMuzzle + FVector(0, 3, 0), FVector(4, 4, 4), ECESurf::Emissive, false);
	TPFlash->SetMaterial(0, A->Emissive(FLinearColor(1.f, 0.6f, 0.25f), 60.f));
	TPFlash->SetVisibility(false);
	TPFlash->SetCastShadow(false);
	AnimMode = 0;

	UCERigTicker* Ticker = NewObject<UCERigTicker>(this, TEXT("RigTicker"));
	Ticker->RegisterComponent();
	AddInstanceComponent(Ticker);
	bSwat = true;
	return true;
}

// Both teams wear the same kit; the uniform is tinted per team so they can be told apart at a glance.
void ACECharacter::ApplySwatTeamLook()
{
	const ELoadFlags Quiet = (ELoadFlags)(LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Prop = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Characters/Swat/M_CE_Swat.M_CE_Swat"), nullptr, Quiet);
	if (!Prop) return;   // the imported materials stay as they are
	const UCETuning* T = RT();
	USkeletalMeshComponent* Drv = GetMesh();
	const TArray<FName> Slots = Drv->GetMaterialSlotNames();
	if (BodyMIDs.Num() != Slots.Num())
	{
		BodyMIDs.Reset();
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const bool bHead = Slots[i].ToString().Contains(TEXT("head"));
			UTexture* BC = LoadObject<UTexture>(nullptr, bHead ? TEXT("/Game/Characters/Swat/Soldier_head_diffuse.Soldier_head_diffuse") : TEXT("/Game/Characters/Swat/Soldier_Body_diffuse.Soldier_Body_diffuse"), nullptr, Quiet);
			UTexture* N = LoadObject<UTexture>(nullptr, bHead ? TEXT("/Game/Characters/Swat/Soldier_head_normal.Soldier_head_normal") : TEXT("/Game/Characters/Swat/Soldier_Body_normal.Soldier_Body_normal"), nullptr, Quiet);
			UMaterialInstanceDynamic* MID = BC ? UMaterialInstanceDynamic::Create(Prop, this) : nullptr;
			if (MID)
			{
				MID->SetTextureParameterValue(TEXT("BaseColorTex"), BC);
				if (N) MID->SetTextureParameterValue(TEXT("NormalTex"), N);
				if (UTexture* Sp = LoadObject<UTexture>(nullptr, bHead ? TEXT("/Game/Characters/Swat/Soldier_head_specular.Soldier_head_specular") : TEXT("/Game/Characters/Swat/Soldier_Body_specular.Soldier_Body_specular"), nullptr, Quiet))
					MID->SetTextureParameterValue(TEXT("SpecTex"), Sp);
				MID->SetScalarParameterValue(TEXT("Roughness"), T->UniformRoughness);
				for (UMeshComponent* M : { (UMeshComponent*)Drv, (UMeshComponent*)TPBody.Get(), (UMeshComponent*)FPArms.Get() })
					if (M) M->SetMaterial(i, MID);
			}
			BodyMIDs.Add(MID);
		}
	}
	const FLinearColor Tint = Team == ECETeam::Attack ? T->SwatAttackTint : T->SwatDefendTint;
	for (int32 i = 0; i < BodyMIDs.Num(); ++i)
	{
		if (!BodyMIDs[i]) continue;
		const bool bHead = Slots[i].ToString().Contains(TEXT("head"));
		BodyMIDs[i]->SetVectorParameterValue(TEXT("Tint"), bHead ? FMath::Lerp(FLinearColor::White, Tint, 0.35f) : Tint);
	}
}

// =============================================================================================
// Per frame
// =============================================================================================
void ACECharacter::AddSwatClip(int32 Clip, float Weight, float Time)
{
	using namespace CESwat;
	if (Weight < 0.01f) return;
	const UAnimSequence* Seq = SwatClips[Clip];
	for (int32 b = 0; b < SwatAcc.Num(); ++b)
	{
		if (SwatKind[b] == KStatic || SwatSkelBone[b] == INDEX_NONE) continue;
		FTransform X = Sample(Seq, Time, SwatSkelBone[b]);
		if (b == 0) StripTravel(X);
		SwatAcc[b] = SwatAccW[b] > 0.f ? Mix(SwatAcc[b], X, Weight / (SwatAccW[b] + Weight)) : X;
		SwatAccW[b] += Weight;
	}
}

// One-shot clip laid over the upper body (spine upwards) of the pose already on the body.
void ACECharacter::OverlaySwatClip(int32 Clip, float Time, float Weight)
{
	using namespace CESwat;
	if (Weight < 0.01f) return;
	const UAnimSequence* Seq = SwatClips[Clip];
	const double T = FMath::Clamp(Time, 0.f, Seq->GetPlayLength() - 0.002f);
	const float W = FMath::Min(Weight, 1.f);
	for (int32 b = 0; b < SwatAim.Num(); ++b)
		if (SwatKind[b] == KUpper && SwatSkelBone[b] != INDEX_NONE)
			TPBody->BoneSpaceTransforms[b] = Mix(TPBody->BoneSpaceTransforms[b], Sample(Seq, T, SwatSkelBone[b]), W);
}

void ACECharacter::TickSwatLate(float Dt)
{
	using namespace CESwat;
	if (!bRealBody || !bBuilt) return;
	const UCETuning* T = RT();
	const int32 NumB = SwatAim.Num();
	if (bDead)
	{
		const UAnimSequence* Seq = SwatClips[CDie];
		SwatDeathT = FMath::Min(SwatDeathT + Dt, Seq->GetPlayLength() - 0.02f);
		for (int32 b = 0; b < NumB; ++b)
			TPBody->BoneSpaceTransforms[b] = (SwatKind[b] == KStatic || SwatSkelBone[b] == INDEX_NONE) ? SwatAim[b] : Sample(Seq, SwatDeathT, SwatSkelBone[b]);
		TPBody->RefreshBoneTransforms();
		TPBody->UpdateChildTransforms();
		return;
	}

	// ---- weapon in hand ----
	const int32 Want = (Equipped == ECEGadget::None && Slot == 0) ? 0 : 1;
	if (Want != AnimMode)
	{
		AnimMode = Want;
		if (TPFlash)
		{
			TPFlash->AttachToComponent(TPWeapon[Want], FAttachmentTransformRules::KeepRelativeTransform);
			TPFlash->SetRelativeLocation((Want ? CERig::PistolMuzzle : CERig::RifleMuzzle) + FVector(0, 3, 0));
		}
	}
	const float Time = Now();
	const float SinceThrow = Time - LastThrowTime;
	const bool bThrowing = SinceThrow >= 0.f && SinceThrow < 0.75f;
	const bool bGun = Equipped == ECEGadget::None && !bThrowing;   // the throwing hand is the rifle hand
	TPWeapon[0]->SetVisibility(bGun && Slot == 0);
	TPWeapon[1]->SetVisibility(bGun && Slot == 1);
	TPWeapon[0]->SetRelativeScale3D(FVector(Weapons[0].Id == ECEWeaponId::SMG9 ? T->SmgScale : 1.f));

	// ---- what the legs should be doing ----
	const FVector Vel = GetVelocity();
	const float Speed = Vel.Size2D();
	const FVector Local = GetActorRotation().UnrotateVector(Vel);   // X forward, Y right
	SwatMove = FMath::FInterpTo(SwatMove, FMath::Clamp(Speed / 70.f, 0.f, 1.f), Dt, 10.f);
	SwatSprint = FMath::FInterpTo(SwatSprint, bSprinting ? 1.f : 0.f, Dt, 8.f);
	if (Speed > 20.f) SwatDir = FMath::Vector2DInterpTo(SwatDir, FVector2D(Local.X, Local.Y) / Speed, Dt, 10.f);
	float WF = FMath::Max(SwatDir.X, 0.f), WB = FMath::Max(-SwatDir.X, 0.f), WR = FMath::Max(SwatDir.Y, 0.f), WL = FMath::Max(-SwatDir.Y, 0.f);
	const float Sum = WF + WB + WR + WL;
	if (Sum < 0.01f) { WF = 1.f; WB = WR = WL = 0.f; } else { WF /= Sum; WB /= Sum; WR /= Sum; WL /= Sum; }
	// walk loops cover about 1 m/s and run loops about 2.8 m/s: crossfade between them over that range
	const float RunA = FMath::Clamp((Speed - 110.f) / 170.f, 0.f, 1.f);
	const float W[NumLoco] = { WF * (1.f - RunA), WF * RunA, WB * (1.f - RunA), WB * RunA, WL * (1.f - RunA), WL * RunA, WR * (1.f - RunA), WR * RunA };
	float Travel = 0.f, Len = 0.f;
	for (int32 i = 0; i < NumLoco; ++i) { Travel += W[i] * SwatTravel[i]; Len += W[i] * SwatLen[i]; }
	Len = FMath::Max(Len, 0.1f);
	// cycles per second: follow ground speed, but never play slower than half or faster than double speed
	const float Rate = FMath::Clamp(Speed / FMath::Max(Travel, 1.f), 0.5f / Len, 2.0f / Len);
	if (Speed > 5.f) SwatPhase = FMath::Frac(SwatPhase + Dt * Rate);
	SwatIdleT = FMath::Fmod(SwatIdleT + Dt, FMath::Max(0.2f, SwatClips[CIdle]->GetPlayLength() - 0.01f));

	for (int32 b = 0; b < NumB; ++b) SwatAccW[b] = 0.f;
	for (int32 i = 0; i < NumLoco; ++i) AddSwatClip(i, SwatMove * W[i], SwatPhase * (SwatLen[i] - 0.001f));
	AddSwatClip(CIdle, 1.f - SwatMove, SwatIdleT);
	const float AimHold = 0.45f * (1.f - SwatSprint);   // steady the rifle on target unless sprinting
	for (int32 b = 0; b < NumB; ++b)
	{
		FTransform X = SwatAim[b];
		if (SwatKind[b] != KStatic && SwatAccW[b] > 0.f)
		{
			X = SwatAcc[b];
			if (SwatKind[b] == KUpper) X = Mix(X, SwatAim[b], AimHold * SwatMove);
		}
		TPBody->BoneSpaceTransforms[b] = X;
	}

	// ---- upper-body one-shots ----
	if (bReloading)
	{
		const float RP = FMath::Clamp(ReloadProgress(), 0.f, 1.f);
		OverlaySwatClip(CReload, RP * SwatClips[CReload]->GetPlayLength(), FMath::Min(RP, 1.f - RP) * 10.f);
	}
	const float FireLen = SwatClips[CFire]->GetPlayLength(), SinceShot = Time - LastShotTime;
	if (SinceShot >= 0.f && SinceShot < FireLen && bGun) OverlaySwatClip(CFire, SinceShot, (FireLen - SinceShot) / 0.08f);
	const float HitLen = SwatClips[CHit]->GetPlayLength(), SinceHit = Time - LastDamageTime;
	if (SinceHit >= 0.f && SinceHit < HitLen) OverlaySwatClip(CHit, SinceHit, FMath::Min(SinceHit / 0.04f, (HitLen - SinceHit) / 0.12f));
	// the grenade leaves the hand the moment it is used, so only the forward swing and recovery of the throw are shown
	if (bThrowing) OverlaySwatClip(CToss, 1.8f + SinceThrow * 1.2f, FMath::Min(SinceThrow / 0.06f, (0.75f - SinceThrow) / 0.2f));
	TPBody->MarkRefreshTransformDirty();

	// ---- crouch, lean and aim pitch, so the visible body matches the gameplay hitboxes ----
	const FVector Fwd = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const float Drop = FMath::Clamp(92.f - PelvisZ, 0.f, 40.f);
	if (Drop > 0.5f)
	{
		const FTransform FL = BoneW(TPBody, FootL), FR = BoneW(TPBody, FootR);
		const FVector Pel = BoneW(TPBody, Hips).GetLocation();
		TPBody->SetBoneLocationByName(Hips, Pel - FVector(0, 0, Drop), EBoneSpaces::WorldSpace);
		const FQuat QL = FL.GetRotation(), QR = FR.GetRotation();
		TwoBoneIK(TPBody, UpLegL, ShinL, FootL, FL.GetLocation(), Fwd - Right * 0.15f, &QL);
		TwoBoneIK(TPBody, UpLegR, ShinR, FootR, FR.GetLocation(), Fwd + Right * 0.15f, &QR);
		const FTransform S = BoneW(TPBody, Spine);
		TPBody->SetBoneRotationByName(Spine, (FQuat(Right, FMath::DegreesToRadians(14.f * Drop / 30.f)) * S.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	}
	if (FMath::Abs(LeanAlpha) > 0.01f)
	{
		const FTransform S = BoneW(TPBody, Spine);
		const FVector HeadAt = BoneW(TPBody, Head).GetLocation() + FVector(0, 0, 10.f);
		auto Flat = [&](const FVector& V) { return (V - Fwd * FVector::DotProduct(V, Fwd)).GetSafeNormal(); };
		const FVector From = Flat(HeadAt - S.GetLocation());
		const FVector To = Flat(HeadBox->GetComponentLocation() - S.GetLocation());
		if (!From.IsNearlyZero() && !To.IsNearlyZero())
			TPBody->SetBoneRotationByName(Spine, (FQuat::FindBetweenNormals(From, To) * S.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	}
	const float Pitch = FMath::Clamp(FRotator::NormalizeAxis(AimRotation().Pitch), -70.f, 70.f);
	if (FMath::Abs(Pitch) > 1.f)
	{
		const FQuat Step(Right, FMath::DegreesToRadians(-Pitch / 3.f));   // looking up is a negative turn about the right axis
		for (const FName& Bone : { Spine, Spine1, Spine2 })
			TPBody->SetBoneRotationByName(Bone, (Step * BoneW(TPBody, Bone).GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	}
	TPBody->RefreshBoneTransforms();
	TPBody->UpdateChildTransforms();

	// ---- first-person arms ----
	FPArms->SetVisibility(bFPView);
	if (bFPView && bRealVM) TickSwatFPArms(Dt);
}

// The owner's view: the same body in the standing aim pose, head just behind the eye, both arms solved onto
// the first-person weapon with the grip the third-person pose uses.
void ACECharacter::TickSwatFPArms(float Dt)
{
	using namespace CESwat;
	const UCETuning* T = RT();
	FPArms->BoneSpaceTransforms = SwatAim;
	FPArms->MarkRefreshTransformDirty();
	const FVector HeadCS = FPArms->GetBoneTransformByName(Head, EBoneSpaces::ComponentSpace).GetLocation();
	const FVector Eye(-T->FPArmsBack, 0.f, -T->FPArmsDown);
	FPArms->SetRelativeLocationAndRotation(Eye + FPArmShift - CERig::MeshYaw.RotateVector(HeadCS), CERig::MeshYaw);

	const FTransform HandRW = BoneW(FPArms, HandR);
	const FTransform GripW = SwatGripRel * HandRW;
	const FTransform RelR = HandRW.GetRelativeTransform(GripW);
	const FTransform RelL = BoneW(FPArms, HandL).GetRelativeTransform(GripW);

	const bool bGadget = Equipped != ECEGadget::None;
	const int32 VMIdx = RigVMIdx;
	FTransform HeldW;
	if (bGadget)
	{
		HeldW = VMGadget->GetComponentTransform();
		HeldW.SetRotation(HeldW.GetRotation() * CERig::MeshYaw.Quaternion());
		HeldW.AddToTranslation(-Camera->GetUpVector() * 9.f - Camera->GetForwardVector() * 4.f);
	}
	else HeldW = FPWeapon[VMIdx]->GetComponentTransform();
	HeldW.SetScale3D(FVector::OneVector);
	FTransform TR = RelR * HeldW;
	FTransform TL = RelL * HeldW;
	if (RigRelA > 0.f && !bGadget)
	{
		// reload: support hand goes to the magazine well and down
		const FVector Well = FPWeapon[VMIdx]->GetComponentTransform().TransformPosition(VMIdx == 2 ? CERig::PistolMagWell : CERig::RifleMagWell);
		const float Down = RigRP < 0.45f ? 14.f : 4.f;
		TL.SetLocation(FMath::Lerp(TL.GetLocation(), Well - Camera->GetUpVector() * Down * RigRelA, RigRelA));
	}
	if (bGadget) TL.SetLocation(TR.GetLocation() - Camera->GetRightVector() * 12.f - Camera->GetUpVector() * 14.f);

	// Keep both hands within reach: slide the rig towards the gun if an arm would over-stretch.
	FVector Need = FVector::ZeroVector;
	for (int32 s = 0; s < 2; ++s)
	{
		const FVector PU = BoneW(FPArms, s ? ArmL : ArmR).GetLocation(), PL = BoneW(FPArms, s ? ForeArmL : ForeArmR).GetLocation(), PH = BoneW(FPArms, s ? HandL : HandR).GetLocation();
		const float Reach = ((PL - PU).Size() + (PH - PL).Size()) * 0.97f;
		const FVector To = (s ? TL : TR).GetLocation() - PU;
		if (To.Size() > Reach) Need += To.GetSafeNormal() * (To.Size() - Reach);
	}
	FPArmShift += Camera->GetComponentTransform().InverseTransformVectorNoScale(Need) * FMath::Min(1.f, Dt * 12.f);
	FPArmShift = FMath::VInterpTo(FPArmShift, FVector::ZeroVector, Dt, 0.8f);
	FPArmShift = FPArmShift.GetClampedToMaxSize(30.f);

	const FVector CF = Camera->GetForwardVector(), CR = Camera->GetRightVector(), CU = Camera->GetUpVector();
	const FQuat QR = TR.GetRotation(), QL = TL.GetRotation();
	TwoBoneIK(FPArms, ArmR, ForeArmR, HandR, TR.GetLocation(), -CU + CR * 0.8f - CF * 0.2f, &QR);
	TwoBoneIK(FPArms, ArmL, ForeArmL, HandL, TL.GetLocation(), -CU - CR * 0.8f - CF * 0.2f, &QL);
	FPArms->RefreshBoneTransforms();
}

void ACECharacter::StartSwatDeath()
{
	SwatDeathT = 0.f;
	if (TPFlash) TPFlash->SetVisibility(false);
	if (FPArms) FPArms->SetVisibility(false);
}

void ACECharacter::DebugAnim(int32 Kind)
{
	const float Time = Now();
	if (Kind == 0) { bReloading = true; ReloadStart = Time - 1.0f; ReloadEnd = Time + 1.2f; }
	else if (Kind == 1) LastDamageTime = Time - 0.15f;
	else if (Kind == 2) LastThrowTime = Time - 0.3f;
}
