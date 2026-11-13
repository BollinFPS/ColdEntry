#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CETypes.h"
#include "CESound.generated.h"

class USoundAttenuation;
class UReverbEffect;
class UAudioComponent;

UENUM()
enum class ECESnd : uint8
{
	ShotR4, ShotSMG, ShotP9,
	StepConcrete, StepMetal, StepWood, StepTile, StepGravel,
	MagOut, MagIn, Bolt, DryFire, Equip,
	DoorOpen, DoorClose, Breach, Flash, GlassBreak, WoodBreak,
	Beep, PlantBeep, Defused, HitMarker, HitHead, Hurt,
	ImpactHard, ImpactMetal, ImpactSoft, Throw, Deploy, Reinforce,
	RoundStart, RoundEnd, Ring, DroneHum, Count UMETA(Hidden)
};

UENUM()
enum class ECERange : uint8 { Loud, Medium, Quiet };

/**
 * Procedural audio. Every sound is synthesised in code at startup (PLACEHOLDER — replace with
 * recorded assets for the final build). Spatialised with engine occlusion (lowpass + volume per
 * blocking surface), distance lowpass, and a listener-room reverb preset per zone.
 */
UCLASS()
class COLDENTRY_API UCESound : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static UCESound* Get(const UObject* WorldContext);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void Play(ECESnd Snd, const FVector& Loc, float Volume = 1.f);
	void Play2D(ECESnd Snd, float Volume = 1.f);
	void SetListenerReverb(ECEReverb R);
	void SetMuffle(float Amount) { Muffle = Amount; } // flashbang deafening 0..1

private:
	void Generate();
	UAudioComponent* Spawn(ECESnd Snd, const FVector* Loc, float Volume);
	ECERange RangeOf(ECESnd Snd) const;

	TArray<TArray<TArray<int16>>> Bank; // [snd][variant][samples]
	UPROPERTY() TArray<TObjectPtr<USoundAttenuation>> Atten;
	UPROPERTY() TArray<TObjectPtr<UReverbEffect>> Reverbs;
	struct FLive { TWeakObjectPtr<UAudioComponent> Comp; float StopAt; };
	TArray<FLive> Live;
	float Now = 0.f;
	float Muffle = 0.f;
	int32 CurrentReverb = -1;
	int32 SampleRate = 44100;
	bool bGenerated = false;
};
