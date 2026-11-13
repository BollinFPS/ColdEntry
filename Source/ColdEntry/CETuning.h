#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CETypes.h"
#include "CETuning.generated.h"

/**
 * All gameplay tuning in one place. Editable in Project Settings > Game > Cold Entry Tuning,
 * saved to Config/DefaultGame.ini.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Cold Entry Tuning"))
class COLDENTRY_API UCETuning : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UCETuning();
	static const UCETuning* Get() { return GetDefault<UCETuning>(); }
	const FCEWeaponDef& Weapon(ECEWeaponId Id) const;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ---------------- Match ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Match") int32 RoundsToWin = 4;
	UPROPERTY(Config, EditAnywhere, Category = "Match") float PrepTime = 30.f;
	UPROPERTY(Config, EditAnywhere, Category = "Match") float ActionTime = 150.f;
	UPROPERTY(Config, EditAnywhere, Category = "Match") float DeviceTime = 40.f;
	UPROPERTY(Config, EditAnywhere, Category = "Match") float PlantTime = 5.f;
	UPROPERTY(Config, EditAnywhere, Category = "Match") float DefuseTime = 6.f;
	UPROPERTY(Config, EditAnywhere, Category = "Match") float RoundEndTime = 8.f;
	UPROPERTY(Config, EditAnywhere, Category = "Match") bool bPlayerStartsAttacking = true;

	// ---------------- Movement (cm, s) ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float WalkSpeed = 330.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float SprintSpeed = 540.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float CrouchSpeed = 175.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float AdsSpeedMult = 0.62f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float Acceleration = 3800.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float BrakingDecel = 3000.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float EyeHeightStand = 74.f;   // above capsule centre (90 half height)
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float EyeHeightCrouch = 45.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float CrouchCameraRate = 18.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float LeanOffset = 36.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float LeanRoll = 9.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float LeanRate = 16.f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float SprintToFireDelay = 0.13f;
	UPROPERTY(Config, EditAnywhere, Category = "Movement") float HeadBob = 0.9f;          // cm, restrained

	// ---------------- Look ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Look") float MouseSensitivity = 1.0f;     // multiplies 0.07 deg/count * 2.5
	UPROPERTY(Config, EditAnywhere, Category = "Look") float AdsSensitivityMult = 0.8f;
	UPROPERTY(Config, EditAnywhere, Category = "Look") float FieldOfView = 90.f;         // horizontal
	UPROPERTY(Config, EditAnywhere, Category = "Look") float AdsFovMult = 0.85f;

	// ---------------- Damage ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Damage") float MaxHealth = 100.f;
	UPROPERTY(Config, EditAnywhere, Category = "Damage") bool bHeadshotsLethal = true;
	UPROPERTY(Config, EditAnywhere, Category = "Damage") float TorsoMult = 0.8f;  // plate carrier
	UPROPERTY(Config, EditAnywhere, Category = "Damage") float LimbMult = 0.75f;
	UPROPERTY(Config, EditAnywhere, Category = "Damage") float MedkitHeal = 40.f;
	UPROPERTY(Config, EditAnywhere, Category = "Damage") float MedkitTime = 4.f;
	UPROPERTY(Config, EditAnywhere, Category = "Damage") int32 MaxPenetrations = 2;
	UPROPERTY(Config, EditAnywhere, Category = "Damage") bool bFriendlyFire = false;

	// ---------------- Weapons ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Weapons") FCEWeaponDef R4;
	UPROPERTY(Config, EditAnywhere, Category = "Weapons") FCEWeaponDef SMG9;
	UPROPERTY(Config, EditAnywhere, Category = "Weapons") FCEWeaponDef P9;

	// ---------------- Gadgets ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") int32 BreachCharges = 2;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float BreachPlaceTime = 1.2f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float BreachRadius = 320.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float BreachDamage = 110.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") int32 Flashbangs = 2;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float FlashFuse = 1.4f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float FlashRadius = 1300.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float FlashMaxBlind = 4.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") int32 Smokes = 1;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float SmokeRadius = 360.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float SmokeDuration = 13.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float ThrowSpeed = 1400.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float DroneSpeed = 360.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float PingDuration = 5.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") int32 Cameras = 1;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") int32 Alarms = 2;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float AlarmRadius = 340.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") int32 Barricades = 2;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float BarricadeTime = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float BarricadeHealth = 90.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") int32 ReinforcePool = 6;
	UPROPERTY(Config, EditAnywhere, Category = "Gadgets") float ReinforceTime = 2.2f;

	// ---------------- Penetration (density per cm, damage lost) ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Penetration") float GlassDensity = 0.2f;
	UPROPERTY(Config, EditAnywhere, Category = "Penetration") float DrywallDensity = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Penetration") float WoodDensity = 1.6f;
	UPROPERTY(Config, EditAnywhere, Category = "Penetration") float SheetDensity = 4.0f;
	UPROPERTY(Config, EditAnywhere, Category = "Penetration") float CrateDensity = 0.6f;
	UPROPERTY(Config, EditAnywhere, Category = "Penetration") float PenDamageLoss = 0.32f;

	// ---------------- Debug / testing ----------------
	// Bots also drive the player's pawn and matches loop forever (for soak testing).
	UPROPERTY(Config, EditAnywhere, Category = "Debug") bool bAutoPlay = false;
	UPROPERTY(Config, EditAnywhere, Category = "Debug") float AutoPlayTimeDilation = 1.f;

	// ---------------- Visuals (placeholder lighting calibration) ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float SunLux = 3.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float SkyLightIntensity = 3.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float HdriIntensity = 1.f;   // when the scanned HDR sky is imported
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float InteriorLightScale = 2.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float ExposureMinEV = 1.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float ExposureMaxEV = 7.f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float InteriorExposureMinEV = -1.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float InteriorExposureMaxEV = 4.f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float InteriorExposureBias = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float ExposureBias = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float FogDensity = 0.011f;      // clear courtyard, layered woodland haze
	UPROPERTY(Config, EditAnywhere, Category = "Visuals") float WhiteBalance = 6200.f;
	UPROPERTY(Config, EditAnywhere, Category = "World") bool bForestEnvironment = true;
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "0.25", ClampMax = "1.5")) float ForestDensity = 1.f;
	UPROPERTY(Config, EditAnywhere, Category = "World") int32 ForestSeed = 42017;

	// ---------------- Character models (UE template mannequin, rifle, pistol) ----------------
	// Off = the old box-built operators and viewmodels.
	UPROPERTY(Config, EditAnywhere, Category = "Models") bool bRealCharacters = true;
	// SWAT operator (imported by Import_Swat.bat) instead of the template mannequin, when its assets exist.
	UPROPERTY(Config, EditAnywhere, Category = "Models") bool bSwatCharacters = true;
	UPROPERTY(Config, EditAnywhere, Category = "Models") FLinearColor SwatAttackTint = FLinearColor(1.6f, 1.0f, 0.45f);   // multiplies the uniform texture
	UPROPERTY(Config, EditAnywhere, Category = "Models") FLinearColor SwatDefendTint = FLinearColor(0.75f, 0.85f, 1.15f);
	UPROPERTY(Config, EditAnywhere, Category = "Models") float SwatGripRise = 0.f;   // cm the weapon sits above the firing-hand palm
	UPROPERTY(Config, EditAnywhere, Category = "Models") FLinearColor AttackTint = FLinearColor(0.20f, 0.17f, 0.10f);
	UPROPERTY(Config, EditAnywhere, Category = "Models") FLinearColor DefendTint = FLinearColor(0.035f, 0.05f, 0.085f);
	UPROPERTY(Config, EditAnywhere, Category = "Models") FLinearColor UnderTint = FLinearColor(0.03f, 0.03f, 0.032f); // legs/gloves
	UPROPERTY(Config, EditAnywhere, Category = "Models") float UniformRoughness = 0.85f;  // matte fabric rather than the template's glossy paint
	// First-person weapon mesh placement inside the viewmodel pivot (sight line = pivot X axis).
	UPROPERTY(Config, EditAnywhere, Category = "Models") FVector FPRifleOffset = FVector(0.f, 0.f, -18.1f);
	UPROPERTY(Config, EditAnywhere, Category = "Models") FVector FPPistolOffset = FVector(-2.f, 0.f, -13.4f);
	UPROPERTY(Config, EditAnywhere, Category = "Models") float SmgScale = 0.82f;
	UPROPERTY(Config, EditAnywhere, Category = "Models") float FPArmsBack = 7.f;      // cm the arms rig sits behind the eye
	UPROPERTY(Config, EditAnywhere, Category = "Models") float FPArmsDown = 4.f;
	UPROPERTY(Config, EditAnywhere, Category = "Models") bool bRagdoll = true;

	// ---------------- Bots ----------------
	UPROPERTY(Config, EditAnywhere, Category = "Bots", meta = (ClampMin = 0, ClampMax = 1)) float BotDifficulty = 0.6f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float BotReactionMin = 0.22f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float BotReactionMax = 0.55f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float BotTurnSpeed = 540.f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float BotAimErrorStart = 110.f; // cm at 10 m
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float BotAimSettle = 0.55f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float BotHeadBias = 0.18f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float BotFov = 115.f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float HearWalk = 1300.f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float HearSprint = 2400.f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float HearCrouch = 400.f;
	UPROPERTY(Config, EditAnywhere, Category = "Bots") float HearShot = 7000.f;
};
