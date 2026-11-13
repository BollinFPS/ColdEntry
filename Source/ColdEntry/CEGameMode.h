#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CETypes.h"
#include "CEGameMode.generated.h"

class ACELevelBuilder;
class ACECharacter;
class ACEBotController;
class ACEDevice;
class ACEGadget;
class ACEGrenade;
class ACESmoke;
class ACEBreachCharge;
class ACESecurityCamera;
class ACEAlarm;
class ACEDrone;
class ACEEntity;
class ACameraActor;

USTRUCT()
struct FCERoundSummary
{
	GENERATED_BODY()
	ECETeam Winner = ECETeam::None;
	FString Reason;
	bool bPlayerWon = false;
	FString Mvp;
	int32 PlayerKills = 0;
	TArray<FCEHitRecord> PlayerHitsTaken;
	TArray<FCEHitRecord> PlayerHitsDealt;
	bool bPlayerDied = false;
	FString KilledBy;
};

/**
 * Round flow: Menu -> [Prep -> Action -> (Planted) -> RoundEnd] x N -> MatchEnd.
 * 5v5 (player + 4 bots vs 5 bots), one life per round, alternating attack/defence.
 * NETWORKING: none. This is a single-player build with bots; all state lives here on the local
 * authority. See README "Multiplayer plan" for the replication roadmap.
 */
UCLASS()
class COLDENTRY_API ACEGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ACEGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override {}

	// ---------- state ----------
	ECEPhase Phase = ECEPhase::Menu;
	float PhaseLeft = 0.f;
	int32 Round = 0;
	int32 ScoreSquad = 0;   // player's squad
	int32 ScoreEnemy = 0;
	bool bSquadAttacking = true;
	int32 ReinforcePool = 0;
	int32 AttackSpawnChoice = 0;
	ECEWeaponId PlayerPrimary = ECEWeaponId::R4;
	FCERoundSummary LastSummary;
	TArray<FCEKillFeedEntry> KillFeed;
	FString Banner; float BannerUntil = 0.f;
	bool bAutoPlay = false;
	bool bDebugNav = false;
	bool bShotTour = false;                 // -ceshots: fly a fixed set of cameras, save Saved/Shots/*.png, quit

	UPROPERTY() TObjectPtr<ACELevelBuilder> Level;
	UPROPERTY() TArray<TObjectPtr<ACECharacter>> Characters;
	UPROPERTY() TObjectPtr<ACECharacter> PlayerChar;
	UPROPERTY() TArray<TObjectPtr<ACEBotController>> Bots;
	UPROPERTY() TObjectPtr<ACEDevice> Device;
	UPROPERTY() TArray<TObjectPtr<ACEGadget>> Gadgets;
	UPROPERTY() TArray<TObjectPtr<ACESmoke>> Smokes;
	UPROPERTY() TObjectPtr<ACameraActor> Overview;
	UPROPERTY() TObjectPtr<AActor> ViewOverride;   // shot tour: what the local view should look through

	TMap<ACECharacter*, FCEIntel> Intel[2];     // [ECETeam] -> what that team knows about enemies
	TMap<ACECharacter*, FVector> SpawnAnchor;

	ECETeam PlayerTeam() const { return bSquadAttacking ? ECETeam::Attack : ECETeam::Defend; }
	float Now() const;

	// ---------- flow ----------
	void StartMatchFlow();
	void StartRound();
	void BeginActionPhase();
	void EndRound(ECETeam Winner, const FString& Reason);
	void CycleSpawn();
	void CyclePrimary();

	// ---------- queries ----------
	bool IsMovementFrozen(const ACECharacter* C) const;
	int32 AliveCount(ECETeam T) const;
	bool SmokeBlocks(const FVector& A, const FVector& B) const;
	bool HasLineOfSight(const FVector& From, const FVector& To, const AActor* IgnoreA = nullptr, const AActor* IgnoreB = nullptr) const;
	TArray<AActor*> FeedsFor(ECETeam T) const;
	ACEDrone* DroneOf(const ACECharacter* C) const;
	FString CalloutAt(const FVector& P) const;

	// ---------- events ----------
	void ReportNoise(ACECharacter* Src, const FVector& Loc, float Radius, bool bShot);
	void OnHit(ACECharacter* Victim, ACECharacter* Attacker, const FCEHitRecord& Rec);
	void OnKilled(ACECharacter* Victim, ACECharacter* Killer, const FCEHitRecord& Rec);
	void AddIntel(ECETeam Knower, ACECharacter* Enemy, const FVector& Pos, bool bVisual, bool bPing);
	void PingFromView(ECETeam Team, const FVector& From, const FVector& Dir);
	void PlantDevice(ACECharacter* By, const FVector& Loc);
	void DefuseDevice(ACECharacter* By);
	void SpawnGrenade(ACECharacter* By, ECEGadget Type, const FVector& Loc, const FVector& Vel);
	void GrenadeDetonate(ACEGrenade* G);
	void SpawnCharge(ACECharacter* By, ACEEntity* Target, const FVector& Loc);
	void DetonateCharges(ACECharacter* By);
	void ExplodeCharge(ACEBreachCharge* C);
	ACESecurityCamera* SpawnCamera(ACECharacter* By, const FVector& Loc, const FRotator& Rot);
	ACEAlarm* SpawnAlarm(ACECharacter* By, const FVector& Loc, const FVector& Normal);
	ACEDrone* SpawnDrone(ACECharacter* By);
	void Announce(const FString& Text, float Seconds = 2.5f);

private:
	void CleanupRound();
	void SpawnTeams();
	void SpawnFixedCameras();
	void UpdateCameraIntel(float Dt);
	void UpdateRules(float Dt);
	void DrawNavDebug();
	void TickShotTour();
	bool SetupShot(int32 Idx);
	UPROPERTY() TObjectPtr<ACameraActor> ShotCam;
	int32 ShotIdx = -1;
	bool bShotTaken = false;
	double TourStart = 0.0, ShotStart = 0.0, QuietSince = 0.0;
	FCERoundSummary BuildSummary(ECETeam Winner, const FString& Reason) const;
	float CamIntelT = 0.f;
	TMap<ACECharacter*, float> OutsideTime;
	TMap<ACECharacter*, float> CamSeenTime;
};
