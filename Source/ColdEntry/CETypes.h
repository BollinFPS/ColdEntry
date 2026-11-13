#pragma once

#include "CoreMinimal.h"
#include "CETypes.generated.h"

UENUM(BlueprintType)
enum class ECETeam : uint8 { Attack, Defend, None };

// Visual / physical surface classes used by the procedural level and props.
UENUM(BlueprintType)
enum class ECESurf : uint8
{
	Concrete, Block, Floor, Drywall, Wood, Metal, Steel, Glass, Grate, Crate,
	Asphalt, Ceiling, Hazard, Emissive, Tan, Navy, Olive, Charcoal, Gunmetal, Polymer,
	Skin, RedDot, Rubber, Paint, Container, ContainerB, Count UMETA(Hidden)
};

// Bullet penetration classes. Solid stops everything.
UENUM(BlueprintType)
enum class ECEPen : uint8 { None, Glass, Drywall, Wood, Barricade, Sheet, Crate, Solid };

UENUM(BlueprintType)
enum class ECEStep : uint8 { Concrete, Metal, Wood, Tile, Gravel };

UENUM(BlueprintType)
enum class ECEHitZone : uint8 { Head, Torso, Limb };

UENUM(BlueprintType)
enum class ECEWeaponId : uint8 { R4, SMG9, P9, None };

UENUM(BlueprintType)
enum class ECEGadget : uint8 { None, BreachCharge, Flash, Smoke, Camera, Alarm, Barricade, Drone, Medkit };

UENUM(BlueprintType)
enum class ECEPhase : uint8 { Menu, Prep, Action, Planted, RoundEnd, MatchEnd };

UENUM(BlueprintType)
enum class ECEAction : uint8 { None, Reinforce, Barricade, Plant, Defuse, PlaceCharge, Heal };

UENUM(BlueprintType)
enum class ECEReverb : uint8 { Outdoor, SmallRoom, Office, Corridor, LargeHall, Stairwell };

USTRUCT(BlueprintType)
struct FCEWeaponDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Weapon") FString Name;
	UPROPERTY(EditAnywhere, Category = "Weapon") FString Kind;
	UPROPERTY(EditAnywhere, Category = "Weapon") bool bAuto = true;
	UPROPERTY(EditAnywhere, Category = "Weapon") float RPM = 700.f;
	UPROPERTY(EditAnywhere, Category = "Damage") float Damage = 35.f;
	UPROPERTY(EditAnywhere, Category = "Damage") float FalloffStart = 20.f;   // metres
	UPROPERTY(EditAnywhere, Category = "Damage") float FalloffEnd = 40.f;     // metres
	UPROPERTY(EditAnywhere, Category = "Damage") float MinDamage = 28.f;
	UPROPERTY(EditAnywhere, Category = "Damage") float PenPower = 30.f;       // "cm of drywall"
	UPROPERTY(EditAnywhere, Category = "Ammo") int32 Mag = 30;
	UPROPERTY(EditAnywhere, Category = "Ammo") int32 Reserve = 120;
	UPROPERTY(EditAnywhere, Category = "Handling") float Reload = 2.3f;
	UPROPERTY(EditAnywhere, Category = "Handling") float ReloadEmpty = 2.8f;
	UPROPERTY(EditAnywhere, Category = "Handling") float Equip = 0.4f;
	UPROPERTY(EditAnywhere, Category = "Handling") float AdsTime = 0.2f;
	UPROPERTY(EditAnywhere, Category = "Accuracy") float HipSpread = 2.6f;   // degrees (cone half angle)
	UPROPERTY(EditAnywhere, Category = "Accuracy") float AdsSpread = 0.08f;
	UPROPERTY(EditAnywhere, Category = "Accuracy") float MoveSpread = 1.5f;
	UPROPERTY(EditAnywhere, Category = "Recoil") float RecoilScale = 1.f;
	UPROPERTY(EditAnywhere, Category = "Recoil") float RecoilVariance = 0.08f; // degrees random wobble
	UPROPERTY(EditAnywhere, Category = "Recoil") float RecoilRecover = 0.45f;  // fraction of vertical recoil auto-recovered after a burst
	// Per-shot recoil: X = pitch up (deg), Y = yaw right (deg). Deterministic & learnable.
	UPROPERTY(EditAnywhere, Category = "Recoil") TArray<FVector2D> Pattern;
	UPROPERTY(EditAnywhere, Category = "Feel") float MuzzleLight = 1.f;
};

// Everything needed to explain a hit to the player afterwards.
USTRUCT(BlueprintType)
struct FCEHitRecord
{
	GENERATED_BODY()
	UPROPERTY() FString Attacker;
	UPROPERTY() FString Victim;
	UPROPERTY() FString Weapon;
	UPROPERTY() ECEHitZone Zone = ECEHitZone::Torso;
	UPROPERTY() float Damage = 0.f;
	UPROPERTY() FString Through;     // "drywall", "wood", "" ...
	UPROPERTY() float Distance = 0.f; // metres
	UPROPERTY() float Time = 0.f;
	UPROPERTY() bool bKill = false;
	UPROPERTY() bool bExplosion = false;
};

USTRUCT()
struct FCEKillFeedEntry
{
	GENERATED_BODY()
	UPROPERTY() FString Killer;
	UPROPERTY() FString Victim;
	UPROPERTY() FString Weapon;
	UPROPERTY() bool bHeadshot = false;
	UPROPERTY() bool bThrough = false;
	UPROPERTY() ECETeam KillerTeam = ECETeam::None;
	UPROPERTY() float Time = 0.f;
};

// Team-shared knowledge about one enemy (from sight, sound, cameras, drones, alarms).
USTRUCT()
struct FCEIntel
{
	GENERATED_BODY()
	UPROPERTY() FVector Pos = FVector::ZeroVector;
	UPROPERTY() float Time = -100.f;
	UPROPERTY() bool bVisual = false;   // seen (precise) vs heard (fuzzy)
	UPROPERTY() bool bPinged = false;   // marked for HUD
	UPROPERTY() float PingTime = -100.f;
};
