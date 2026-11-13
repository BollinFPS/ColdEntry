#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CETypes.h"
#include "CECharacter.generated.h"

class UCameraComponent;
class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UPoseableMeshComponent;
class UAnimInstance;
class ACEGameMode;
class ACEBreachCharge;

/** Input intent. Written by the player controller or a bot controller every frame. */
USTRUCT()
struct FCEInput
{
	GENERATED_BODY()
	float Forward = 0.f, Right = 0.f;
	bool bSprint = false;
	bool bCrouch = false;
	int32 Lean = 0;              // -1 left, 0 none, +1 right
	bool bAds = false;
	bool bFire = false;          // held
	bool bFirePressed = false;   // edge
	bool bReload = false;        // edge
	int32 Switch = -1;           // 0 primary, 1 secondary, 2..4 gadget slots (edge)
	bool bInteract = false;      // held
	bool bInteractPressed = false;
	bool bDetonate = false;      // edge
	bool bHeal = false;          // held
};

USTRUCT()
struct FCEWeaponState
{
	GENERATED_BODY()
	ECEWeaponId Id = ECEWeaponId::None;
	int32 Mag = 0;
	int32 Reserve = 0;
};

/** Runs the character's rig update after animation has been evaluated (TG_PostUpdateWork). */
UCLASS()
class COLDENTRY_API UCERigTicker : public UActorComponent
{
	GENERATED_BODY()
public:
	UCERigTicker() { PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickGroup = TG_PostUpdateWork; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};

UCLASS()
class COLDENTRY_API ACECharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ACECharacter();
	virtual void Tick(float DeltaTime) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	void InitLoadout(ECETeam InTeam, ECEWeaponId Primary, const FString& InName, bool bInIsPlayer);

	// ---------- identity & state ----------
	ECETeam Team = ECETeam::Attack;
	FString CEName;
	bool bIsPlayer = false;
	float Health = 100.f;
	bool bDead = false;
	FCEInput Input;
	FVector2D LookDelta = FVector2D::ZeroVector; // for weapon sway (player only)

	bool IsAlive() const { return !bDead; }
	bool IsSprinting() const { return bSprinting; }
	bool IsCrouched() const { return bIsCrouched; }
	bool IsBlind() const;
	float BlindAmount() const;
	float GetAdsAlpha() const { return AdsAlpha; }
	float GetLeanAlpha() const { return LeanAlpha; }
	FVector EyeLocation() const;
	FRotator AimRotation() const;
	FVector AimDir() const { return AimRotation().Vector(); }
	FVector HeadLocation() const;
	FVector ChestLocation() const;
	float GetSpreadDeg() const;

	// ---------- weapons ----------
	FCEWeaponState Weapons[2];
	int32 Slot = 0;
	ECEGadget Equipped = ECEGadget::None;     // None = holding a gun
	ECEGadget GadgetSlots[3] = { ECEGadget::None, ECEGadget::None, ECEGadget::None };
	int32 GadgetCount[(int32)ECEGadget::Medkit + 1] = {};
	FCEWeaponState& CurWeapon() { return Weapons[Slot]; }
	const FCEWeaponState& CurWeapon() const { return Weapons[Slot]; }
	const struct FCEWeaponDef& CurDef() const;
	bool IsReloading() const { return bReloading; }
	float ReloadProgress() const;
	bool CanFire() const;

	// ---------- actions (hold-to-complete) ----------
	ECEAction Action = ECEAction::None;
	float ActionTime = 0.f, ActionDuration = 1.f;
	TWeakObjectPtr<AActor> ActionTarget;
	FVector ActionLoc = FVector::ZeroVector;
	void BeginAction(ECEAction A, AActor* Target, const FVector& Loc, float Duration);
	void CancelAction();
	// What the player would do with F right now (for HUD prompts and bots).
	ECEAction QueryInteract(AActor*& OutTarget, FVector& OutLoc, FString& OutPrompt, bool& bOutInstant) const;

	// ---------- damage ----------
	void ApplyHit(FCEHitRecord Rec, ACECharacter* Attacker, const FVector& FromDir);
	void ApplyExplosion(float Damage, ACECharacter* Attacker, const FVector& Origin, const FString& Source);
	void Blind(float Seconds);
	TArray<FCEHitRecord> HitsTaken;   // this life
	TArray<FCEHitRecord> HitsDealt;
	float LastDamageTime = -100.f;
	FVector LastDamageFrom = FVector::ZeroVector;
	float LastHitMarkerTime = -100.f;
	bool bLastHitKill = false, bLastHitHead = false;
	TWeakObjectPtr<ACECharacter> Killer;

	// ---------- components ----------
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> ViewRoot;     // first-person weapon pivot (owner only)
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> BodyRoot;     // third-person body (others only)
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Pelvis;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Upper;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> HeadPivot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LegL;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LegR;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> HeadBox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> TorsoBox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> LegsBox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> ArmsBox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> MuzzleLight;

	// ---------- real character model (UE template mannequin + animations) ----------
	// GetMesh() runs the animation blueprint but is not drawn; TPBody (seen by others) and FPArms (seen by the
	// owner) copy its pose every frame and add crouch/lean and first-person hand IK on top.
	bool HasRealBody() const { return bRealBody; }
	void TickRigLate(float Dt);
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> TPBody;
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> FPArms;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TPWeapon[2];   // primary, pistol (in hand, third person)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FPWeapon[3];   // R4, SMG-9, P9 (first person)

private:
	void TickMovement(float Dt);
	void TickWeapon(float Dt);
	void TickAction(float Dt);
	void TickCamera(float Dt);
	void TickBody(float Dt);
	void TickViewmodel(float Dt);
	void TickFootsteps(float Dt);
	void FireShot();
	void TraceBullet(const FVector& Start, const FVector& Dir);
	void StartReload();
	void FinishReload();
	void SwitchTo(int32 NewSlot);
	void UseGadget();
	void CompleteAction();
	void Die(ACECharacter* InKiller, const FCEHitRecord& Rec);
	void BuildBody();
	void BuildViewmodels();
	bool BuildRealBody();
	bool BuildRealViewmodels();
	void TickFPArms(float Dt, int32 VMIdx, float RelA, float RP);
	void StartRagdoll(const FVector& Dir);
	UStaticMeshComponent* GearOnBone(FName Bone, const FVector& ActorSpaceOffset, const FVector& Half, ECESurf Surf, bool bSphere);
	UStaticMeshComponent* Part(USceneComponent* Parent, const FVector& Loc, const FVector& Half, ECESurf Surf, bool bFirstPerson, const FRotator& Rot = FRotator::ZeroRotator);
	ACEGameMode* GM() const;
	float Now() const;

	bool bSprinting = false;
	float SprintEndTime = -10.f;
	float AdsAlpha = 0.f, LeanAlpha = 0.f, EyeZ = 74.f, PelvisZ = 92.f;
	float NextFireTime = 0.f, LastShotTime = -10.f;
	int32 ShotsInBurst = 0;
	float RecoilDebt = 0.f, RecoverRemaining = 0.f;
	bool bReloading = false;
	float ReloadStart = 0.f, ReloadEnd = 0.f;
	int32 ReloadStage = 0;
	float EquipEnd = 0.f;
	float BlindUntil = -10.f, BlindTotal = 1.f;
	float StepAccum = 0.f, BobPhase = 0.f, WalkPhase = 0.f;
	float VMKick = 0.f;
	FVector2D VMSway = FVector2D::ZeroVector;
	float MuzzleT = 0.f;
	float DeathT = 0.f;
	float LowerAlpha = 0.f;   // weapon lowered (near wall / action)
	bool bBuilt = false;

	// viewmodel roots per weapon + gadget
	UPROPERTY() TObjectPtr<USceneComponent> VMWeapon[3];
	UPROPERTY() TObjectPtr<USceneComponent> VMGadget;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> VMMag[3];
	UPROPERTY() TObjectPtr<UStaticMeshComponent> VMFlash[3];
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TPFlash;
	UPROPERTY() TObjectPtr<USceneComponent> TPGun;
	FVector MagRest[3];
	TArray<TObjectPtr<UStaticMeshComponent>> SleeveParts;
	TArray<TObjectPtr<UStaticMeshComponent>> UniformParts;
	TArray<TObjectPtr<UStaticMeshComponent>> GearParts;
	TArray<TObjectPtr<UStaticMeshComponent>> BandParts;

	bool bRealBody = false, bRealVM = false;
	int32 AnimMode = -1;
	UPROPERTY() TSubclassOf<UAnimInstance> AnimRifle;
	UPROPERTY() TSubclassOf<UAnimInstance> AnimPistol;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RigGear;
	TArray<FName> RigGearBones;
	FVector FPArmShift = FVector::ZeroVector;
	bool bFPView = false;
	int32 RigVMIdx = 0;
	float RigRelA = 0.f, RigRP = 0.f;
};
