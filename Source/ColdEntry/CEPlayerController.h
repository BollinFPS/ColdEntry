#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CEPlayerController.generated.h"

class ACECharacter;
class ACEGameMode;

UCLASS()
class COLDENTRY_API ACEPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ACEPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;

	ACECharacter* Char() const;
	ACECharacter* Spectated() const { return SpectateTarget.Get(); }
	AActor* CurrentFeed() const { return FeedActor.Get(); }
	bool IsInFeeds() const { return bInFeeds; }
	bool bShowSettings = false;
	bool bShowScores = false;
	int32 LeanToggle = 0;
	bool bCrouchToggle = false;

private:
	ACEGameMode* GM() const;
	void TickAlive(float Dt, float MX, float MY);
	void TickFeeds(float Dt, float MX, float MY);
	void TickSpectate(float Dt);
	void TickMenus();
	void EnterFeeds(bool bPreferDrone);
	void ExitFeeds();
	void CycleFeed(int32 Dir);
	bool Pressed(const FKey& K) const { return WasInputKeyJustPressed(K); }
	bool Down(const FKey& K) const { return IsInputKeyDown(K); }

	bool bInFeeds = false;
	int32 FeedIdx = 0;
	TWeakObjectPtr<AActor> FeedActor;
	TWeakObjectPtr<ACECharacter> SpectateTarget;
	int32 SpectateIdx = 0;
	TWeakObjectPtr<AActor> LastViewTarget;
};
