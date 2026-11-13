#include "CEPlayerController.h"
#include "ColdEntry.h"
#include "CETuning.h"
#include "CECharacter.h"
#include "CEGameMode.h"
#include "CEGadgets.h"
#include "CEMap.h"
#include "CESound.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

ACEPlayerController::ACEPlayerController()
{
	bShowMouseCursor = false;
}

ACECharacter* ACEPlayerController::Char() const { return Cast<ACECharacter>(GetPawn()); }
ACEGameMode* ACEPlayerController::GM() const { return GetWorld() ? GetWorld()->GetAuthGameMode<ACEGameMode>() : nullptr; }

void ACEPlayerController::BeginPlay()
{
	Super::BeginPlay();
	FInputModeGameOnly Mode;
	SetInputMode(Mode);
	bShowMouseCursor = false;
	if (ACEGameMode* G = GM()) if (G->Overview) SetViewTarget(G->Overview);
}

void ACEPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	LeanToggle = 0;
	bCrouchToggle = false;
	bInFeeds = false;
	FeedActor.Reset();
	SpectateTarget.Reset();
	SetViewTarget(InPawn);
}

void ACEPlayerController::PlayerTick(float Dt)
{
	Super::PlayerTick(Dt);
	ACEGameMode* G = GM();
	if (!G) return;
	float MX = 0.f, MY = 0.f;
	GetInputMouseDelta(MX, MY);

	TickMenus();
	bShowScores = Down(EKeys::Tab);

	if (G->Phase == ECEPhase::Menu || G->Phase == ECEPhase::MatchEnd)
	{
		if (G->Overview && GetViewTarget() != G->Overview) SetViewTarget(G->Overview);
		if ((Pressed(EKeys::Enter) || Pressed(EKeys::LeftMouseButton)) && !bShowSettings) { FlushPressedKeys(); G->StartMatchFlow(); }
		return;
	}
	if (G->bAutoPlay)
	{
		if (G->PlayerChar && GetViewTarget() != G->PlayerChar) SetViewTarget(G->PlayerChar);
	}
	else
	{
		ACECharacter* C = Char();
		if (C && C->IsAlive())
		{
			if (bInFeeds) TickFeeds(Dt, MX, MY);
			else TickAlive(Dt, MX, MY);
		}
		else
		{
			if (bInFeeds) TickFeeds(Dt, MX, MY);
			else TickSpectate(Dt);
		}
	}

	// audio: listener-room reverb and flashbang deafening
	if (UCESound* S = UCESound::Get(this))
	{
		if (AActor* VT = GetViewTarget()) if (G->Level) S->SetListenerReverb(G->Level->ReverbAt(VT->GetActorLocation()));
		const ACECharacter* C = Char();
		S->SetMuffle(C && C->IsAlive() ? C->BlindAmount() : 0.f);
	}
}

void ACEPlayerController::TickMenus()
{
	ACEGameMode* G = GM();
	if (Pressed(EKeys::P)) bShowSettings = !bShowSettings;
	if (Pressed(EKeys::F9)) G->bDebugNav = !G->bDebugNav;
	if (!bShowSettings && G->Phase != ECEPhase::Menu) return;
	UCETuning* T = GetMutableDefault<UCETuning>();
	if (Pressed(EKeys::LeftBracket)) T->MouseSensitivity = FMath::Max(0.1f, T->MouseSensitivity - 0.1f);
	if (Pressed(EKeys::RightBracket)) T->MouseSensitivity = FMath::Min(5.f, T->MouseSensitivity + 0.1f);
	if (Pressed(EKeys::Hyphen)) T->BotDifficulty = FMath::Max(0.f, T->BotDifficulty - 0.1f);
	if (Pressed(EKeys::Equals)) T->BotDifficulty = FMath::Min(1.f, T->BotDifficulty + 0.1f);
	if (Pressed(EKeys::Comma)) T->FieldOfView = FMath::Max(70.f, T->FieldOfView - 2.f);
	if (Pressed(EKeys::Period)) T->FieldOfView = FMath::Min(110.f, T->FieldOfView + 2.f);
}

void ACEPlayerController::TickAlive(float Dt, float MX, float MY)
{
	ACECharacter* C = Char();
	ACEGameMode* G = GM();
	const UCETuning* T = UCETuning::Get();
	if (GetViewTarget() != C) SetViewTarget(C);
	FCEInput& In = C->Input;

	In.Forward = (Down(EKeys::W) ? 1.f : 0.f) - (Down(EKeys::S) ? 1.f : 0.f);
	In.Right = (Down(EKeys::D) ? 1.f : 0.f) - (Down(EKeys::A) ? 1.f : 0.f);
	In.bSprint = Down(EKeys::LeftShift);
	if (Pressed(EKeys::C)) bCrouchToggle = !bCrouchToggle;
	if (In.bSprint && In.Forward > 0.f) bCrouchToggle = false;
	In.bCrouch = bCrouchToggle || Down(EKeys::LeftControl);
	if (Pressed(EKeys::Q)) LeanToggle = LeanToggle == -1 ? 0 : -1;
	if (Pressed(EKeys::E)) LeanToggle = LeanToggle == 1 ? 0 : 1;
	if (In.bSprint && In.Forward > 0.f) LeanToggle = 0;
	In.Lean = LeanToggle;
	In.bAds = Down(EKeys::RightMouseButton);
	In.bFire = Down(EKeys::LeftMouseButton);
	In.bFirePressed = Pressed(EKeys::LeftMouseButton);
	In.bReload = Pressed(EKeys::R);
	if (Pressed(EKeys::One)) In.Switch = 0;
	if (Pressed(EKeys::Two)) In.Switch = 1;
	if (Pressed(EKeys::Three)) In.Switch = 2;
	if (Pressed(EKeys::Four)) In.Switch = 3;
	if (Pressed(EKeys::Five)) In.Switch = 4;
	if (Pressed(EKeys::MouseScrollUp) || Pressed(EKeys::MouseScrollDown)) In.Switch = C->Slot == 0 ? 1 : 0;
	In.bInteract = Down(EKeys::F);
	In.bInteractPressed = Pressed(EKeys::F);
	In.bDetonate = Pressed(EKeys::G);
	In.bHeal = Down(EKeys::H);
	if (Pressed(EKeys::B)) G->CycleSpawn();
	if (Pressed(EKeys::N)) G->CyclePrimary();
	if (Pressed(EKeys::V)) EnterFeeds(true);

	// raw mouse look, no acceleration or smoothing. ADS scales by the zoom ratio so tracking feels consistent.
	const float AdsScale = FMath::Lerp(1.f, T->AdsSensitivityMult * T->AdsFovMult, C->GetAdsAlpha());
	// GetInputMouseDelta returns counts x 0.07 (engine axis config); 1.5 => ~0.105 deg per count at sens 1.0
	const float K = 1.5f * T->MouseSensitivity * AdsScale;
	FRotator R = GetControlRotation();
	R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch) + MY * K, -88.f, 88.f);
	R.Yaw += MX * K;
	SetControlRotation(R);
	C->LookDelta = FVector2D(MX, MY) * K;
}

void ACEPlayerController::EnterFeeds(bool bPreferDrone)
{
	ACEGameMode* G = GM();
	ACECharacter* C = Char();
	const ECETeam Team = C ? C->Team : G->PlayerTeam();
	if (C && C->IsAlive() && Team == ECETeam::Attack && bPreferDrone)
	{
		ACEDrone* D = G->DroneOf(C);
		if (!D && C->GadgetCount[(int32)ECEGadget::Drone] > 0) D = G->SpawnDrone(C);
		if (D) { bInFeeds = true; FeedActor = D; SetViewTarget(D); return; }
	}
	TArray<AActor*> Feeds = G->FeedsFor(Team);
	if (Feeds.Num() == 0) { G->Announce(TEXT("No feeds available"), 1.2f); return; }
	bInFeeds = true;
	FeedIdx = FMath::Clamp(FeedIdx, 0, Feeds.Num() - 1);
	FeedActor = Feeds[FeedIdx];
	SetViewTarget(Feeds[FeedIdx]);
}

void ACEPlayerController::ExitFeeds()
{
	bInFeeds = false;
	FeedActor.Reset();
	if (ACECharacter* C = Char()) if (C->IsAlive()) SetViewTarget(C);
}

void ACEPlayerController::CycleFeed(int32 Dir)
{
	ACEGameMode* G = GM();
	ACECharacter* C = Char();
	TArray<AActor*> Feeds = G->FeedsFor(C ? C->Team : G->PlayerTeam());
	if (Feeds.Num() == 0) { ExitFeeds(); return; }
	FeedIdx = (FeedIdx + Dir + Feeds.Num()) % Feeds.Num();
	FeedActor = Feeds[FeedIdx];
	SetViewTarget(Feeds[FeedIdx]);
}

void ACEPlayerController::TickFeeds(float Dt, float MX, float MY)
{
	ACEGameMode* G = GM();
	ACECharacter* C = Char();
	if (C && C->IsAlive()) C->Input = FCEInput();
	AActor* F = FeedActor.Get();
	ACEGadget* Gd = Cast<ACEGadget>(F);
	if (!F || (Gd && Gd->IsWrecked()))
	{
		G->Announce(TEXT("Feed lost"), 1.f);
		ExitFeeds();
		return;
	}
	if (Pressed(EKeys::V) || Pressed(EKeys::Escape)) { ExitFeeds(); return; }
	if (Pressed(EKeys::Right) || Pressed(EKeys::MouseScrollUp)) CycleFeed(1);
	if (Pressed(EKeys::Left) || Pressed(EKeys::MouseScrollDown)) CycleFeed(-1);
	const float K = 1.5f * UCETuning::Get()->MouseSensitivity * 0.6f;
	FVector ViewLoc; FRotator ViewRot;
	if (ACEDrone* D = Cast<ACEDrone>(F))
	{
		const bool bMine = C && D->OwnerChar.Get() == C && C->IsAlive();
		const float Fw = bMine ? (Down(EKeys::W) ? 1.f : 0.f) - (Down(EKeys::S) ? 1.f : 0.f) : 0.f;
		const float Rt = bMine ? (Down(EKeys::D) ? 1.f : 0.f) - (Down(EKeys::A) ? 1.f : 0.f) : 0.f;
		D->Drive(Fw, Rt, MX * K, MY * K, Dt);
		ViewLoc = D->View->GetComponentLocation(); ViewRot = D->View->GetComponentRotation();
	}
	else if (ACESecurityCamera* Cam = Cast<ACESecurityCamera>(F))
	{
		Cam->Look(MX * K * 0.6f, MY * K * 0.6f);
		ViewLoc = Cam->View->GetComponentLocation(); ViewRot = Cam->View->GetComponentRotation();
	}
	if (Pressed(EKeys::LeftMouseButton) && Gd) G->PingFromView(Gd->Team, ViewLoc, ViewRot.Vector());
}

void ACEPlayerController::TickSpectate(float Dt)
{
	ACEGameMode* G = GM();
	const ECETeam Team = G->PlayerTeam();
	TArray<ACECharacter*> Mates;
	for (ACECharacter* C : G->Characters) if (C && C->IsAlive() && C->Team == Team && C != Char()) Mates.Add(C);
	if (Pressed(EKeys::V)) { EnterFeeds(false); return; }
	if (Mates.Num() == 0)
	{
		SpectateTarget.Reset();
		if (G->Overview && GetViewTarget() != G->Overview) SetViewTarget(G->Overview);
		return;
	}
	if (Pressed(EKeys::LeftMouseButton)) SpectateIdx++;
	if (Pressed(EKeys::RightMouseButton)) SpectateIdx--;
	if (!SpectateTarget.IsValid() || !SpectateTarget->IsAlive() || Pressed(EKeys::LeftMouseButton) || Pressed(EKeys::RightMouseButton))
	{
		SpectateIdx = (SpectateIdx % Mates.Num() + Mates.Num()) % Mates.Num();
		SpectateTarget = Mates[SpectateIdx];
	}
	if (GetViewTarget() != SpectateTarget.Get()) SetViewTarget(SpectateTarget.Get());
}
