#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CETypes.h"
#include "CEGadgets.generated.h"

class UStaticMeshComponent;
class UCameraComponent;
class USphereComponent;
class UBoxComponent;
class UProjectileMovementComponent;
class UPointLightComponent;
class ACECharacter;
class ACEEntity;

/** Shared base: owned by a team, shootable (1 hit by default), removed at round reset. */
UCLASS(Abstract)
class COLDENTRY_API ACEGadget : public AActor
{
	GENERATED_BODY()
public:
	ACEGadget();
	virtual void BeginPlay() override;
	virtual void BuildVisual() {}
	virtual float TakeDamage(float Damage, struct FDamageEvent const& Event, AController* Inst, AActor* Causer) override;
	virtual void Wreck();
	bool IsWrecked() const { return bWrecked; }
	virtual FString Label() const { return TEXT("Gadget"); }
	ECETeam Team = ECETeam::None;
	TWeakObjectPtr<ACECharacter> OwnerChar;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
protected:
	UStaticMeshComponent* AddPart(USceneComponent* Parent, const FVector& Loc, const FVector& Half, ECESurf Surf);
	void MakeShootable(UPrimitiveComponent* C);
	bool bWrecked = false;
	float Health = 1.f;
};

/** Flashbang or smoke grenade. */
UCLASS()
class COLDENTRY_API ACEGrenade : public ACEGadget
{
	GENERATED_BODY()
public:
	ACEGrenade();
	virtual void BuildVisual() override;
	void Launch(ECEGadget InType, const FVector& Velocity);
	virtual void Tick(float DeltaTime) override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Move;
	ECEGadget Type = ECEGadget::Flash;
private:
	float Fuse = 1.4f;
	bool bDone = false;
};

/** Smoke volume: blocks sight (rendered) and bot vision (sphere test). */
UCLASS()
class COLDENTRY_API ACESmoke : public AActor
{
	GENERATED_BODY()
public:
	ACESmoke();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	bool Blocks(const FVector& A, const FVector& B) const;
	float Radius = 360.f;
	float Life = 13.f;
private:
	float Age = 0.f;
	TArray<TObjectPtr<UStaticMeshComponent>> Puffs;
	TArray<FVector> Offsets;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Mat;
};

UCLASS()
class COLDENTRY_API ACEBreachCharge : public ACEGadget
{
	GENERATED_BODY()
public:
	ACEBreachCharge();
	virtual void BuildVisual() override;
	virtual void Tick(float DeltaTime) override;
	virtual FString Label() const override { return TEXT("Breach charge"); }
	void Arm() { if (!bArmed) { bArmed = true; FuseLeft = 0.6f; } }
	TWeakObjectPtr<ACEEntity> Target;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Led;
private:
	bool bArmed = false;
	float FuseLeft = 0.6f, Blink = 0.f;
};

/** Wall camera: pre-installed security cams and the defenders' deployable one. */
UCLASS()
class COLDENTRY_API ACESecurityCamera : public ACEGadget
{
	GENERATED_BODY()
public:
	ACESecurityCamera();
	virtual void BuildVisual() override;
	virtual FString Label() const override { return bFixed ? TEXT("Security camera") : TEXT("Deployable camera"); }
	virtual void Wreck() override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> View;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Lamp;
	bool bFixed = false;
	FString Callout;
	float BaseYaw = 0.f, BasePitch = 0.f, LookYaw = 0.f, LookPitch = 0.f;
	void Look(float DYaw, float DPitch);
};

/** Proximity alarm: chirps and pings attackers that move nearby (crouch-walking slowly avoids it). */
UCLASS()
class COLDENTRY_API ACEAlarm : public ACEGadget
{
	GENERATED_BODY()
public:
	ACEAlarm();
	virtual void BuildVisual() override;
	virtual void Tick(float DeltaTime) override;
	virtual FString Label() const override { return TEXT("Proximity alarm"); }
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Led;
private:
	float CheckT = 0.f, Cooldown = 0.f;
};

/** Attackers' recon drone: small, quiet, fragile, ground-bound. */
UCLASS()
class COLDENTRY_API ACEDrone : public ACEGadget
{
	GENERATED_BODY()
public:
	ACEDrone();
	virtual void BuildVisual() override;
	virtual FString Label() const override { return TEXT("Drone"); }
	virtual void Tick(float DeltaTime) override;
	void Drive(float Forward, float Right, float DYaw, float DPitch, float Dt);
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> View;
	float Yaw = 0.f, Pitch = 0.f;
private:
	FVector Vel = FVector::ZeroVector;
	float HumT = 0.f;
	bool bMoving = false;
};
