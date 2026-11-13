#pragma once

// Shared by the character rigs (CECharacterRig.cpp: template mannequin, CECharacterSwat.cpp: SWAT operator).
#include "CoreMinimal.h"
#include "CETuning.h"
#include "Components/PoseableMeshComponent.h"

namespace CERig
{
	static const TCHAR* Manny = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	static const TCHAR* Quinn = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");
	static const TCHAR* AbpRifle = TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Rifle.ABP_TP_Rifle_C");
	static const TCHAR* AbpPistol = TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Pistol.ABP_TP_Pistol_C");
	static const TCHAR* Rifle = TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle");
	static const TCHAR* Pistol = TEXT("/Game/Weapons/Pistol/Meshes/SM_Pistol.SM_Pistol");
	// Measured from the template meshes (Saved/Inspect): +Y is the barrel axis.
	static const FVector RifleMuzzle(0.f, 51.5f, 9.9f);
	static const FVector PistolMuzzle(0.f, 19.6f, 9.2f);
	static const FVector RifleMagWell(0.f, 15.f, -2.f);
	static const FVector PistolMagWell(0.f, 1.f, -4.f);
	static const FName Grip(TEXT("HandGrip_R"));
	static const FRotator MeshYaw(0.f, -90.f, 0.f); // character assets face +Y; our actors face +X
}

static inline const UCETuning* RT() { return UCETuning::Get(); }

template <class T>
static T* LoadQuiet(const TCHAR* Path) { return LoadObject<T>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet); }

static inline FTransform BoneW(UPoseableMeshComponent* M, FName B) { return M->GetBoneTransformByName(B, EBoneSpaces::WorldSpace); }

/** Two-bone IK in world space on a poseable mesh. Pole is a direction the middle joint should bend towards. */
static inline void TwoBoneIK(UPoseableMeshComponent* M, FName A, FName B, FName C, const FVector& Target, const FVector& Pole, const FQuat* EndRot)
{
	const FVector PA = BoneW(M, A).GetLocation();
	const FVector PB = BoneW(M, B).GetLocation();
	const FVector PC = BoneW(M, C).GetLocation();
	const float L1 = (PB - PA).Size(), L2 = (PC - PB).Size();
	if (L1 < 1.f || L2 < 1.f) return;
	const FVector ToT = Target - PA;
	const float Raw = ToT.Size();
	if (Raw < 1.f) return;
	const FVector Dir = ToT / Raw;
	const float D = FMath::Clamp(Raw, FMath::Abs(L1 - L2) + 0.5f, L1 + L2 - 0.05f);
	const float CosA = FMath::Clamp((L1 * L1 + D * D - L2 * L2) / (2.f * L1 * D), -1.f, 1.f);
	FVector PoleDir = Pole - Dir * FVector::DotProduct(Pole, Dir);
	if (!PoleDir.Normalize())
	{
		PoleDir = (PB - PA) - Dir * FVector::DotProduct(PB - PA, Dir);
		if (!PoleDir.Normalize()) return;
	}
	const FVector NewB = PA + (Dir * CosA + PoleDir * FMath::Sqrt(FMath::Max(0.f, 1.f - CosA * CosA))) * L1;
	const FVector NewC = PA + Dir * D;
	const FQuat KeepEnd = EndRot ? *EndRot : BoneW(M, C).GetRotation();

	const FQuat RA = BoneW(M, A).GetRotation();
	const FQuat DA = FQuat::FindBetweenNormals((PB - PA).GetSafeNormal(), (NewB - PA).GetSafeNormal());
	M->SetBoneRotationByName(A, (DA * RA).Rotator(), EBoneSpaces::WorldSpace);

	const FTransform TB = BoneW(M, B);
	const FVector PC2 = BoneW(M, C).GetLocation();
	const FQuat DB = FQuat::FindBetweenNormals((PC2 - TB.GetLocation()).GetSafeNormal(), (NewC - TB.GetLocation()).GetSafeNormal());
	M->SetBoneRotationByName(B, (DB * TB.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	M->SetBoneRotationByName(C, KeepEnd.Rotator(), EBoneSpaces::WorldSpace);
}
