#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "CETypes.h"
#include "CEBotController.generated.h"

class ACECharacter;
class ACEGameMode;
class ACEEntity;
struct FCENavEdge;

/**
 * Bot brain. Drives an ACECharacter through the same FCEInput the player uses, so bots obey the
 * same movement, recoil, reload and gadget rules. Perception: vision cone + line of sight + smoke +
 * reaction time; hearing via GameMode noise reports; shared team intel from cameras/alarms/drones.
 */
UCLASS()
class COLDENTRY_API ACEBotController : public AController
{
	GENERATED_BODY()
public:
	ACEBotController();
	virtual void Tick(float DeltaTime) override;

	int32 SquadIndex = 0;
	bool bPlayerSquad = false;
	bool bAutoPilot = false;

	ACECharacter* Char() const;
	void OnRoundStart();
	void OnActionStart();
	void OnPlanted();
	void HearNoise(ACECharacter* Src, const FVector& Loc, bool bShot);
	void OnDamaged(ACECharacter* Attacker);

	FString DebugState;

private:
	enum class ETask : uint8 { None, Reinforce, Barricade, Alarm, Hold, Entry, Objective, Plant, GuardPlant, Defuse, Investigate };
	struct FTask { ETask Type = ETask::None; FVector Pos = FVector::ZeroVector; FVector LookAt = FVector::ZeroVector; TWeakObjectPtr<ACEEntity> Target; bool bCrouch = false; };

	ACEGameMode* GM() const;
	void Perceive(float Dt);
	void Combat(float Dt);
	void Objective(float Dt);
	bool MoveTo(const FVector& Goal, float Dt, bool bCareful, float Arrive = 90.f);
	bool HandleBlockers(float Dt);
	void AimAt(const FVector& P, float Dt, float Speed = -1.f);
	float AimError(const FVector& P) const;
	void Replan(const FVector& Goal);
	float EdgeCost(const FCENavEdge& E) const;
	void NextTask();
	void BuildPrepTasks();

	// perception
	TMap<ACECharacter*, float> Spot;
	TWeakObjectPtr<ACECharacter> Target;
	float TargetSince = 0.f;
	FVector AimOffset = FVector::ZeroVector;
	bool bAimHead = false;
	float PerceiveT = 0.f;
	FVector LastHeard = FVector::ZeroVector; float LastHeardTime = -100.f;
	FVector LastSeen = FVector::ZeroVector; float LastSeenTime = -100.f;

	// shooting
	int32 BurstLeft = 0;
	float NextBurst = 0.f, NextSemi = 0.f;
	float StrafeDir = 1.f, StrafeT = 0.f;

	// movement
	TArray<int32> Path;
	int32 PathIdx = 0;
	FVector PathGoal = FVector(1e9);
	float ReplanT = 0.f;
	FVector LastProgressPos = FVector::ZeroVector;
	float StuckT = 0.f, UnstickT = 0.f;
	TSet<int32> BlockedEdges;
	FVector2D MoveDir = FVector2D::ZeroVector;

	// tasks
	TArray<FTask> Tasks;
	FTask Cur;
	float TaskT = 0.f;
	int32 EntryIdx = 0;
	float RoamT = 0.f;
	float FlashCooldown = 0.f;
	bool bChargePlaced = false;
	TWeakObjectPtr<ACEEntity> ChargeTarget;
	float LookScan = 0.f;
	float Difficulty = 0.6f;
};
