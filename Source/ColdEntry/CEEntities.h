#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CETypes.h"
#include "CEEntities.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UPointLightComponent;

UENUM()
enum class ECEEntityKind : uint8 { Door, BreachWall, Glass, Barricade };

/** Base for every map entity that can block a route and be changed during a round. */
UCLASS(Abstract)
class COLDENTRY_API ACEEntity : public AActor
{
	GENERATED_BODY()
public:
	ACEEntity();
	virtual void ResetEntity() {}
	virtual ECEPen GetPen() const { return ECEPen::Solid; }
	virtual bool BlocksMovement() const { return false; }    // currently blocks walking through
	virtual bool CanBeBreached() const { return false; }     // a breach charge would remove it
	virtual void Breach(AActor* By) {}
	virtual FString DisplayName() const { return TEXT("Entity"); }
	ECEEntityKind Kind = ECEEntityKind::Door;
	FVector OpeningCentre = FVector::ZeroVector; // world, cm
	FVector OpeningExtent = FVector::ZeroVector; // half size
	FString Callout;
};

UCLASS()
class COLDENTRY_API ACEDoor : public ACEEntity
{
	GENERATED_BODY()
public:
	ACEDoor();
	// Hinge at HingeLoc; panel extends along ClosedYaw direction (0 = +X, 90 = +Y).
	void Setup(const FVector& HingeLoc, float ClosedYaw, float Width, float Height);
	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& Event, AController* Inst, AActor* Causer) override;
	virtual void ResetEntity() override;
	virtual ECEPen GetPen() const override { return ECEPen::Wood; }
	virtual bool BlocksMovement() const override { return !bDestroyed && !bOpen; }
	virtual bool CanBeBreached() const override { return !bDestroyed; }
	virtual void Breach(AActor* By) override;
	virtual FString DisplayName() const override { return TEXT("Door"); }

	void Interact(const FVector& FromLoc);
	void SetOpen(bool bNewOpen, const FVector& FromLoc);
	bool IsOpen() const { return bOpen; }
	bool IsDestroyed() const { return bDestroyed; }

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hinge;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Panel;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Handle;
private:
	void Shatter();
	float ClosedYaw = 0.f, CurYaw = 0.f, TargetYaw = 0.f, W = 100.f, H = 210.f;
	bool bOpen = false, bDestroyed = false, bMoving = false;
	float Health = 140.f;
};

UCLASS()
class COLDENTRY_API ACEBreakable : public ACEEntity
{
	GENERATED_BODY()
public:
	ACEBreakable();
	void Setup(ECEEntityKind InKind, const FVector& Centre, const FVector& HalfExtent);
	virtual float TakeDamage(float Damage, struct FDamageEvent const& Event, AController* Inst, AActor* Causer) override;
	virtual void ResetEntity() override;
	virtual ECEPen GetPen() const override;
	virtual bool BlocksMovement() const override { return bIntact && bDeployed; }
	virtual bool CanBeBreached() const override { return bIntact && bDeployed && !bReinforced; }
	virtual void Breach(AActor* By) override;
	virtual FString DisplayName() const override;

	bool IsIntact() const { return bIntact && bDeployed; }
	bool IsReinforced() const { return bReinforced; }
	bool IsDeployed() const { return bDeployed; }
	void Reinforce();
	void Deploy();          // barricade
	void Break(bool bSilent = false);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Blocker;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Plate;
private:
	void Apply();
	bool bIntact = true, bReinforced = false, bDeployed = true;
	float Health = 1.f;
};

/** The attackers' signal interceptor. Planted in the Comms room, defenders must disable it. */
UCLASS()
class COLDENTRY_API ACEDevice : public AActor
{
	GENERATED_BODY()
public:
	ACEDevice();
	virtual void Tick(float DeltaTime) override;
	float TimeLeft = 40.f;
	bool bActive = true;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Led;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
private:
	float BeepTimer = 0.f;
};

/** Particle-ish effects (dust, sparks, tracers, muzzle flashes) drawn with instanced cubes. */
UCLASS()
class COLDENTRY_API ACEFx : public AActor
{
	GENERATED_BODY()
public:
	ACEFx();
	static ACEFx* Get(UWorld* World);
	virtual void Tick(float DeltaTime) override;
	void Impact(const FVector& Loc, const FVector& Normal, ECEPen Pen, bool bMetal);
	void Tracer(const FVector& From, const FVector& To);
	void Burst(const FVector& Loc, int32 Count, float Speed, float Size, bool bSpark, float Life = 0.6f);
	void Decal(const FVector& Loc, const FVector& Normal, float Size, bool bScorch);
	void Debris(const FVector& Centre, const FVector& Extent, ECESurf Surf, int32 Count, const FVector& Push);

	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> DustISM;
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> SparkISM;
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> TracerISM;
private:
	struct FP { FVector Pos, Vel, Dir; float Life, MaxLife, Size; uint8 Type; };
	TArray<FP> Ps;
	int32 DecalCount = 0;
	static TWeakObjectPtr<ACEFx> Instance;
};
