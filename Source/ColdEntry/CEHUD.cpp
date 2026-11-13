#include "CEHUD.h"
#include "ColdEntry.h"
#include "CETuning.h"
#include "CECharacter.h"
#include "CEGameMode.h"
#include "CEPlayerController.h"
#include "CEGadgets.h"
#include "CEEntities.h"
#include "CEMap.h"
#include "Engine/Canvas.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

static const FLinearColor White(0.92f, 0.92f, 0.9f, 1.f);
static const FLinearColor Dim(0.6f, 0.62f, 0.6f, 0.9f);
static const FLinearColor Squad(0.45f, 0.75f, 1.f, 1.f);
static const FLinearColor Enemy(1.f, 0.45f, 0.25f, 1.f);
static const FLinearColor Amber(1.f, 0.75f, 0.2f, 1.f);
static const FLinearColor Panel(0.f, 0.f, 0.f, 0.45f);

static FString Clock(float T) { const int32 S = FMath::Max(0, FMath::CeilToInt(T)); return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60); }
static const TCHAR* GadgetName(ECEGadget G)
{
	switch (G)
	{
	case ECEGadget::BreachCharge: return TEXT("Breach");
	case ECEGadget::Flash: return TEXT("Flash");
	case ECEGadget::Smoke: return TEXT("Smoke");
	case ECEGadget::Camera: return TEXT("Camera");
	case ECEGadget::Alarm: return TEXT("Alarm");
	case ECEGadget::Barricade: return TEXT("Barricade");
	default: return TEXT("-");
	}
}

void ACEHUD::Txt(const FString& Str, float X, float Y, const FLinearColor& C, float Scale, bool bCenter, bool bLarge, bool bShadow)
{
	UFont* F = bLarge ? GEngine->GetLargeFont() : GEngine->GetMediumFont();
	const float Sc = Scale * S;
	if (bCenter) { float TW, TH; GetTextSize(Str, TW, TH, F, Sc); X -= TW * 0.5f; }
	if (bShadow) DrawText(Str, FLinearColor(0, 0, 0, 0.75f * C.A), X + 1.5f, Y + 1.5f, F, Sc);
	DrawText(Str, C, X, Y, F, Sc);
}

void ACEHUD::Diamond(float X, float Y, float R, const FLinearColor& C)
{
	DrawLine(X, Y - R, X + R, Y, C, 2.f); DrawLine(X + R, Y, X, Y + R, C, 2.f);
	DrawLine(X, Y + R, X - R, Y, C, 2.f); DrawLine(X - R, Y, X, Y - R, C, 2.f);
}

void ACEHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas) return;
	W = Canvas->ClipX; H = Canvas->ClipY; S = H / 1080.f;
	ACEGameMode* G = GetWorld()->GetAuthGameMode<ACEGameMode>();
	ACEPlayerController* PC = Cast<ACEPlayerController>(GetOwningPlayerController());
	if (!G || !PC) return;

	if (G->Phase == ECEPhase::Menu) { DrawMenu(G); if (PC->bShowSettings) DrawSettings(); return; }

	ACECharacter* Me = PC->Char();
	if (G->bAutoPlay) Me = G->PlayerChar;
	const bool bAlive = Me && Me->IsAlive();
	ACECharacter* View = bAlive ? Me : PC->Spectated();
	const ECETeam Team = G->PlayerTeam();

	if (PC->IsInFeeds())
	{
		// feed overlay
		Box(0, 0, W, 60 * S, FLinearColor(0, 0, 0, 0.5f));
		Box(0, H - 50 * S, W, 50 * S, FLinearColor(0, 0, 0, 0.5f));
		AActor* F = PC->CurrentFeed();
		FString Label = TEXT("FEED");
		if (const ACEDrone* D = Cast<ACEDrone>(F)) Label = FString::Printf(TEXT("DRONE  -  %s     [WASD] drive   [LMB] mark   [<-/->] switch   [V] exit"), *G->CalloutAt(D->GetActorLocation()));
		else if (const ACESecurityCamera* Cam = Cast<ACESecurityCamera>(F)) Label = FString::Printf(TEXT("%s  -  %s     [mouse] look   [LMB] mark   [<-/->] switch   [V] exit"), *Cam->Label().ToUpper(), *Cam->Callout);
		Txt(Label, 24 * S, 18 * S, White, 1.f);
		Txt(FString::Printf(TEXT("REC  %s"), *Clock(G->PhaseLeft)), W - 200 * S, 18 * S, Enemy, 1.f);
		DrawMarkers(G, Me, Team);
		DrawTop(G);
		return;
	}

	DrawTop(G);
	if (View) DrawMarkers(G, View, Team);
	DrawKillFeed(G);

	if (bAlive)
	{
		DrawCrosshair(G, Me);
		DrawVitals(G, Me);
		// flashbang white-out
		const float Blind = Me->BlindAmount();
		if (Blind > 0.f) Box(0, 0, W, H, FLinearColor(1, 1, 1, FMath::Clamp(Blind * 1.1f, 0.f, 0.97f)));
		// subtle low-health edge
		if (Me->Health < 35.f)
		{
			const float A = 0.18f * (1.f - Me->Health / 35.f) + 0.04f;
			Box(0, 0, W, 40 * S, FLinearColor(0.5f, 0, 0, A)); Box(0, H - 40 * S, W, 40 * S, FLinearColor(0.5f, 0, 0, A));
		}
	}
	else if (G->Phase != ECEPhase::RoundEnd)
	{
		DrawDeathCard(G, PC);
	}

	if (G->Phase == ECEPhase::RoundEnd) DrawRoundEnd(G);
	if (G->Phase == ECEPhase::MatchEnd)
	{
		Box(0, 0, W, H, FLinearColor(0, 0, 0, 0.6f));
		const bool bWon = G->ScoreSquad > G->ScoreEnemy;
		Txt(bWon ? TEXT("VICTORY") : TEXT("DEFEAT"), W * 0.5f, H * 0.38f, bWon ? Squad : Enemy, 2.0f, true, true);
		Txt(FString::Printf(TEXT("%d  -  %d"), G->ScoreSquad, G->ScoreEnemy), W * 0.5f, H * 0.48f, White, 1.4f, true, true);
		Txt(TEXT("Press Enter to play again"), W * 0.5f, H * 0.58f, Dim, 1.1f, true);
	}
	if (G->Now() < G->BannerUntil) Txt(G->Banner, W * 0.5f, 118 * S, Amber, 1.15f, true);
	if (PC->bShowScores) DrawScores(G);
	if (PC->bShowSettings) DrawSettings();
}

void ACEHUD::DrawMenu(ACEGameMode* G)
{
	Box(0, 0, W, H, FLinearColor(0.01f, 0.012f, 0.015f, 0.62f));
	const float X = 140 * S;
	float Y = 120 * S;
	Txt(TEXT("COLD ENTRY"), X, Y, White, 2.6f, false, true); Y += 90 * S;
	Txt(TEXT("Kessler Annex  -  vertical slice  -  5v5 vs bots  -  first to 4"), X, Y, Dim, 1.1f); Y += 50 * S;
	const TCHAR* Brief[] = {
		TEXT("Attack: breach the facility and plant the signal interceptor in COMMS (ground floor, centre)."),
		TEXT("Defend: reinforce soft walls, barricade doorways, place alarms and cameras, then hold."),
		TEXT("One life per round. Headshots kill. Plate carriers soak 20% to the torso. Bullets go through"),
		TEXT("drywall, wood, glass and barricades (losing damage) but never concrete or reinforced steel."),
	};
	for (const TCHAR* L : Brief) { Txt(L, X, Y, White, 1.0f); Y += 30 * S; }
	Y += 24 * S;
	const TCHAR* Ctl[] = {
		TEXT("WASD move   Shift sprint   C crouch (toggle) / Ctrl hold   Q / E lean (toggle)"),
		TEXT("LMB fire   RMB aim   R reload   1 primary   2 pistol   Mouse wheel swap"),
		TEXT("3 / 4 / 5 gadgets  (LMB to use)   G detonate charges   H medkit (hold)"),
		TEXT("F interact / hold to reinforce, barricade, plant, disable   V drone / cameras   Tab scores"),
		TEXT("Prep:  B change attack spawn   N swap primary (R4 / SMG-9)      P settings      F9 nav debug"),
	};
	for (const TCHAR* L : Ctl) { Txt(L, X, Y, Dim, 1.0f); Y += 30 * S; }
	Y += 40 * S;
	const UCETuning* T = UCETuning::Get();
	Txt(FString::Printf(TEXT("Sensitivity %.1f  [ / ]      Bot difficulty %.1f  - / =      FOV %.0f  , / ."), T->MouseSensitivity, T->BotDifficulty, T->FieldOfView), X, Y, Amber, 1.0f); Y += 60 * S;
	Txt(TEXT("CLICK OR PRESS ENTER TO DEPLOY"), X, Y, White, 1.4f, false, true);
	Txt(TEXT("Graybox build: procedural placeholder materials and audio. Not the final visual target."), X, H - 70 * S, Dim, 0.85f);
}

void ACEHUD::DrawTop(ACEGameMode* G)
{
	const float CX = W * 0.5f;
	Box(CX - 170 * S, 14 * S, 340 * S, 70 * S, Panel);
	const bool bPlanted = G->Phase == ECEPhase::Planted;
	const FString Phase = G->Phase == ECEPhase::Prep ? TEXT("PREPARATION") : (bPlanted ? TEXT("INTERCEPTOR ACTIVE") : (G->Phase == ECEPhase::Action ? TEXT("ACTION") : TEXT("")));
	Txt(Clock(FMath::Max(0.f, G->PhaseLeft)), CX, 20 * S, bPlanted ? Enemy : White, 1.5f, true, true);
	Txt(Phase, CX, 62 * S, Dim, 0.8f, true);
	Txt(FString::Printf(TEXT("%d"), G->ScoreSquad), CX - 140 * S, 26 * S, Squad, 1.4f, true, true);
	Txt(FString::Printf(TEXT("%d"), G->ScoreEnemy), CX + 140 * S, 26 * S, Enemy, 1.4f, true, true);
	// alive pips
	const ECETeam Team = G->PlayerTeam();
	const ECETeam Other = Team == ECETeam::Attack ? ECETeam::Defend : ECETeam::Attack;
	const int32 A = G->AliveCount(Team), B = G->AliveCount(Other);
	for (int32 i = 0; i < 5; ++i)
	{
		Box(CX - 330 * S + i * 28 * S, 34 * S, 20 * S, 26 * S, i < A ? Squad : FLinearColor(0.2f, 0.2f, 0.22f, 0.7f));
		Box(CX + 198 * S + i * 28 * S, 34 * S, 20 * S, 26 * S, i < B ? Enemy : FLinearColor(0.2f, 0.2f, 0.22f, 0.7f));
	}
	Txt(Team == ECETeam::Attack ? TEXT("ATTACK") : TEXT("DEFEND"), CX - 260 * S, 64 * S, Squad, 0.7f, true);
	Txt(Other == ECETeam::Attack ? TEXT("ATTACK") : TEXT("DEFEND"), CX + 268 * S, 64 * S, Enemy, 0.7f, true);
}

void ACEHUD::DrawVitals(ACEGameMode* G, ACECharacter* C)
{
	const UCETuning* T = UCETuning::Get();
	// --- health (bottom left) ---
	const float X = 40 * S, Y = H - 120 * S;
	Box(X - 10 * S, Y - 10 * S, 300 * S, 100 * S, Panel);
	Txt(FString::Printf(TEXT("%d"), FMath::CeilToInt(C->Health)), X, Y, C->Health < 35.f ? Enemy : White, 1.9f, false, true);
	Box(X + 90 * S, Y + 22 * S, 180 * S, 8 * S, FLinearColor(0.15f, 0.15f, 0.15f, 0.8f));
	Box(X + 90 * S, Y + 22 * S, 180 * S * C->Health / T->MaxHealth, 8 * S, C->Health < 35.f ? Enemy : White);
	Txt(TEXT("PLATE  torso -20%"), X + 90 * S, Y - 4 * S, Dim, 0.7f);
	Txt(G->CalloutAt(C->GetActorLocation()).ToUpper(), X, Y + 52 * S, Amber, 0.9f);
	if (C->GadgetCount[(int32)ECEGadget::Medkit] > 0) Txt(TEXT("[H] MEDKIT"), X + 170 * S, Y + 52 * S, Dim, 0.75f);

	// --- weapon & gadgets (bottom right) ---
	const float RX = W - 420 * S, RY = H - 150 * S;
	Box(RX - 10 * S, RY - 10 * S, 400 * S, 140 * S, Panel);
	const FCEWeaponDef& D = C->CurDef();
	const FCEWeaponState& Ws = C->CurWeapon();
	if (C->Equipped == ECEGadget::None)
	{
		Txt(D.Name.ToUpper(), RX, RY, Dim, 0.85f);
		Txt(FString::Printf(TEXT("%d"), Ws.Mag), RX, RY + 22 * S, Ws.Mag <= D.Mag / 4 ? Enemy : White, 1.9f, false, true);
		Txt(FString::Printf(TEXT("/ %d"), Ws.Reserve), RX + 80 * S, RY + 40 * S, Dim, 1.0f);
		Txt(D.bAuto ? TEXT("AUTO") : TEXT("SEMI"), RX + 200 * S, RY + 40 * S, Dim, 0.75f);
		if (C->IsReloading())
		{
			Box(RX, RY + 78 * S, 360 * S, 4 * S, FLinearColor(0.2f, 0.2f, 0.2f, 0.8f));
			Box(RX, RY + 78 * S, 360 * S * C->ReloadProgress(), 4 * S, White);
		}
	}
	else
	{
		Txt(FString(GadgetName(C->Equipped)).ToUpper(), RX, RY + 22 * S, Amber, 1.6f, false, true);
		Txt(C->Equipped == ECEGadget::BreachCharge ? TEXT("Hold LMB on door / soft wall / barricade") : TEXT("LMB to use"), RX, RY, Dim, 0.8f);
	}
	FString Row;
	for (int32 i = 0; i < 3; ++i)
	{
		const ECEGadget Gd = C->GadgetSlots[i];
		Row += FString::Printf(TEXT("[%d] %s %d   "), i + 3, GadgetName(Gd), C->GadgetCount[(int32)Gd]);
	}
	Txt(Row, RX, RY + 92 * S, White, 0.8f);
	if (C->Team == ECETeam::Attack)
	{
		int32 Charges = 0;
		for (ACEGadget* Gd : G->Gadgets) if (ACEBreachCharge* Ch = Cast<ACEBreachCharge>(Gd)) if (Ch->OwnerChar.Get() == C && !Ch->IsWrecked()) ++Charges;
		Txt(FString::Printf(TEXT("[V] Drone %s   %s"), C->GadgetCount[(int32)ECEGadget::Drone] > 0 || G->DroneOf(C) ? TEXT("ready") : TEXT("lost"),
			Charges ? *FString::Printf(TEXT("[G] DETONATE (%d)"), Charges) : TEXT("")), RX, RY + 114 * S, Charges ? Amber : Dim, 0.8f);
	}
	else
	{
		Txt(FString::Printf(TEXT("[V] Cameras   Reinforcements left: %d"), G->ReinforcePool), RX, RY + 114 * S, Dim, 0.8f);
	}

	// --- prep instructions ---
	if (G->Phase == ECEPhase::Prep)
	{
		const FString Tip = C->Team == ECETeam::Attack
			? FString::Printf(TEXT("PREP  -  [B] spawn: %s    [N] primary: %s    [V] drone the building"), *G->Level->AttackSpawnNames[G->AttackSpawnChoice], *D.Name)
			: FString::Printf(TEXT("PREP  -  [F] reinforce soft walls (%d left) / barricade doorways    [3] alarm  [4] camera    [N] primary: %s"), G->ReinforcePool, C->Weapons[0].Id == ECEWeaponId::R4 ? TEXT("Halden R4") : TEXT("Strix SMG-9"));
		Txt(Tip, W * 0.5f, 150 * S, White, 0.9f, true);
	}
}

void ACEHUD::DrawCrosshair(ACEGameMode* G, ACECharacter* C)
{
	const float CX = W * 0.5f, CY = H * 0.5f;
	const float Now = G->Now();
	// interaction / action progress
	AActor* Tgt; FVector Loc; FString Prompt; bool bInstant;
	C->QueryInteract(Tgt, Loc, Prompt, bInstant);
	if (C->Equipped == ECEGadget::BreachCharge && Prompt.IsEmpty()) Prompt = TEXT("Aim at a door, soft wall or barricade");
	if (C->Action != ECEAction::None)
	{
		const TCHAR* Names[] = { TEXT(""), TEXT("REINFORCING"), TEXT("BARRICADING"), TEXT("PLANTING"), TEXT("DISABLING"), TEXT("PLACING CHARGE"), TEXT("HEALING") };
		const float P = FMath::Clamp(C->ActionTime / C->ActionDuration, 0.f, 1.f);
		Txt(Names[(int32)C->Action], CX, CY + 60 * S, White, 0.9f, true);
		Box(CX - 120 * S, CY + 90 * S, 240 * S, 6 * S, FLinearColor(0.1f, 0.1f, 0.1f, 0.8f));
		Box(CX - 120 * S, CY + 90 * S, 240 * S * P, 6 * S, Amber);
	}
	else if (!Prompt.IsEmpty()) Txt(Prompt, CX, CY + 60 * S, White, 0.9f, true);

	// crosshair: spread-driven, hidden when aiming (optic dot / irons take over)
	if (C->GetAdsAlpha() < 0.6f && C->Equipped == ECEGadget::None)
	{
		const float Fov = UCETuning::Get()->FieldOfView;
		const float Gap = FMath::Tan(FMath::DegreesToRadians(C->GetSpreadDeg())) / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)) * W * 0.5f + 4 * S;
		const FLinearColor Col(1, 1, 1, 0.85f * (1.f - C->GetAdsAlpha()));
		const float L = 9 * S;
		DrawLine(CX - Gap - L, CY, CX - Gap, CY, Col, 2.f); DrawLine(CX + Gap, CY, CX + Gap + L, CY, Col, 2.f);
		DrawLine(CX, CY - Gap - L, CX, CY - Gap, Col, 2.f); DrawLine(CX, CY + Gap, CX, CY + Gap + L, Col, 2.f);
		Box(CX - 1, CY - 1, 2, 2, Col);
	}
	// hit marker
	const float HM = Now - C->LastHitMarkerTime;
	if (HM < 0.18f)
	{
		const FLinearColor Col = C->bLastHitKill ? Enemy : (C->bLastHitHead ? Amber : White);
		const float A = 7 * S, B = 15 * S;
		DrawLine(CX - B, CY - B, CX - A, CY - A, Col, 2.f); DrawLine(CX + A, CY - A, CX + B, CY - B, Col, 2.f);
		DrawLine(CX - B, CY + B, CX - A, CY + A, Col, 2.f); DrawLine(CX + A, CY + A, CX + B, CY + B, Col, 2.f);
	}
	// damage direction
	const float DA = Now - C->LastDamageTime;
	if (DA < 1.2f)
	{
		const FVector To = C->LastDamageFrom - C->GetActorLocation();
		const float Ang = FMath::DegreesToRadians(FRotator::NormalizeAxis(To.Rotation().Yaw - C->AimRotation().Yaw));
		const float R = 120 * S;
		const FVector2D P(CX + FMath::Sin(Ang) * R, CY - FMath::Cos(Ang) * R);
		const FVector2D T(FMath::Cos(Ang), FMath::Sin(Ang));
		DrawLine(P.X - T.X * 22 * S, P.Y - T.Y * 22 * S, P.X + T.X * 22 * S, P.Y + T.Y * 22 * S, FLinearColor(1, 0.15f, 0.1f, 1.f - DA / 1.2f), 5.f);
	}
}

void ACEHUD::DrawMarkers(ACEGameMode* G, ACECharacter* View, ECETeam Team)
{
	APlayerController* PC = GetOwningPlayerController();
	auto Proj = [PC](const FVector& P, FVector2D& Out) { return PC->ProjectWorldLocationToScreen(P, Out, false); };
	FVector2D SP;
	const FVector ViewLoc = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	// teammates
	for (ACECharacter* C : G->Characters)
	{
		if (!C || !C->IsAlive() || C->Team != Team || C == View) continue;
		if (Proj(C->GetActorLocation() + FVector(0, 0, 115), SP))
		{
			Diamond(SP.X, SP.Y, 5 * S, Squad);
			if (FVector::Dist(ViewLoc, C->GetActorLocation()) < 1500.f) Txt(C->CEName, SP.X, SP.Y - 30 * S, Squad, 0.7f, true);
		}
	}
	// pinged enemies
	const float Now = G->Now();
	for (auto& KV : G->Intel[(int32)Team])
	{
		if (!KV.Key || !KV.Key->IsAlive() || !KV.Value.bPinged || Now - KV.Value.PingTime > UCETuning::Get()->PingDuration) continue;
		if (Proj(KV.Value.Pos + FVector(0, 0, 120), SP))
		{
			Diamond(SP.X, SP.Y, 9 * S, Enemy);
			Txt(FString::Printf(TEXT("%.0fm"), FVector::Dist(ViewLoc, KV.Value.Pos) / 100.f), SP.X, SP.Y + 12 * S, Enemy, 0.7f, true);
		}
	}
	// objective
	if (G->Device && G->Device->bActive)
	{
		if (Proj(G->Device->GetActorLocation() + FVector(0, 0, 60), SP))
		{
			Diamond(SP.X, SP.Y, 11 * S, Enemy);
			Txt(FString::Printf(TEXT("INTERCEPTOR %s"), *Clock(G->Device->TimeLeft)), SP.X, SP.Y - 34 * S, Enemy, 0.8f, true);
		}
	}
	else if (G->Level && !G->Level->InObjective(ViewLoc) && G->Phase != ECEPhase::RoundEnd)
	{
		if (Proj(G->Level->ObjectiveCentre + FVector(0, 0, 160), SP))
		{
			Diamond(SP.X, SP.Y, 8 * S, Amber);
			Txt(FString::Printf(TEXT("COMMS %.0fm"), FVector::Dist(ViewLoc, G->Level->ObjectiveCentre) / 100.f), SP.X, SP.Y - 30 * S, Amber, 0.7f, true);
		}
	}
}

void ACEHUD::DrawKillFeed(ACEGameMode* G)
{
	float Y = 24 * S;
	const float Now = G->Now();
	const ECETeam Team = G->PlayerTeam();
	for (const FCEKillFeedEntry& K : G->KillFeed)
	{
		if (Now - K.Time > 9.f) continue;
		const FLinearColor KC = K.KillerTeam == Team ? Squad : Enemy;
		const FString Line = FString::Printf(TEXT("%s  [%s%s%s]  %s"), *K.Killer, *K.Weapon, K.bHeadshot ? TEXT(" HS") : TEXT(""), K.bThrough ? TEXT(" WALL") : TEXT(""), *K.Victim);
		float TW, TH; GetTextSize(Line, TW, TH, GEngine->GetMediumFont(), 0.8f * S);
		Box(W - TW - 44 * S, Y - 4 * S, TW + 20 * S, TH + 8 * S, Panel);
		Txt(Line, W - TW - 34 * S, Y, KC, 0.8f);
		Y += TH + 12 * S;
	}
}

FString ACEHUD::Hit(const FCEHitRecord& R, bool bTaken) const
{
	const TCHAR* Z = R.Zone == ECEHitZone::Head ? TEXT("HEAD") : (R.Zone == ECEHitZone::Torso ? TEXT("torso") : TEXT("limb"));
	FString Out = FString::Printf(TEXT("%s %s  %.0f dmg  %s  %.0fm"), bTaken ? *R.Attacker : *R.Victim, R.bExplosion ? TEXT("blast") : Z, R.Damage, *R.Weapon, R.Distance);
	if (!R.Through.IsEmpty()) Out += FString::Printf(TEXT("  through %s"), *R.Through);
	if (R.bKill) Out += TEXT("  KILL");
	return Out;
}

void ACEHUD::DrawDeathCard(ACEGameMode* G, ACEPlayerController* PC)
{
	ACECharacter* Me = PC->Char();
	if (!Me) return;
	const float X = 40 * S;
	float Y = H * 0.32f;
	Box(X - 14 * S, Y - 14 * S, 640 * S, 330 * S, Panel);
	Txt(TEXT("YOU WERE KILLED"), X, Y, Enemy, 1.2f, false, true); Y += 44 * S;
	if (Me->Killer.IsValid())
	{
		ACECharacter* K = Me->Killer.Get();
		Txt(FString::Printf(TEXT("by %s  -  %s  -  %.0f HP left  -  %s"), *K->CEName, *K->CurDef().Name, K->Health, *G->CalloutAt(K->GetActorLocation())), X, Y, White, 0.9f);
		Y += 34 * S;
	}
	Txt(TEXT("Damage taken"), X, Y, Dim, 0.8f); Y += 24 * S;
	for (int32 i = FMath::Max(0, Me->HitsTaken.Num() - 5); i < Me->HitsTaken.Num(); ++i) { Txt(Hit(Me->HitsTaken[i], true), X + 10 * S, Y, White, 0.75f); Y += 22 * S; }
	Y += 8 * S;
	Txt(TEXT("Damage dealt"), X, Y, Dim, 0.8f); Y += 24 * S;
	if (Me->HitsDealt.Num() == 0) { Txt(TEXT("none"), X + 10 * S, Y, Dim, 0.75f); Y += 22 * S; }
	for (int32 i = FMath::Max(0, Me->HitsDealt.Num() - 4); i < Me->HitsDealt.Num(); ++i) { Txt(Hit(Me->HitsDealt[i], false), X + 10 * S, Y, Squad, 0.75f); Y += 22 * S; }
	// spectator bar
	const FString Spec = PC->Spectated() ? FString::Printf(TEXT("SPECTATING %s  (%s)     [LMB / RMB] switch     [V] team cameras"), *PC->Spectated()->CEName, *G->CalloutAt(PC->Spectated()->GetActorLocation()))
		: FString(TEXT("No teammates alive     [V] team cameras"));
	Box(0, H - 46 * S, W, 46 * S, Panel);
	Txt(Spec, W * 0.5f, H - 38 * S, White, 0.9f, true);
}

void ACEHUD::DrawRoundEnd(ACEGameMode* G)
{
	const FCERoundSummary& R = G->LastSummary;
	Box(0, H * 0.28f, W, H * 0.36f, FLinearColor(0, 0, 0, 0.55f));
	Txt(R.bPlayerWon ? TEXT("ROUND WON") : TEXT("ROUND LOST"), W * 0.5f, H * 0.3f, R.bPlayerWon ? Squad : Enemy, 1.8f, true, true);
	Txt(R.Reason, W * 0.5f, H * 0.3f + 70 * S, White, 1.1f, true);
	Txt(FString::Printf(TEXT("%d  -  %d          MVP  %s"), G->ScoreSquad, G->ScoreEnemy, *R.Mvp), W * 0.5f, H * 0.3f + 110 * S, Dim, 1.0f, true);
	float Y = H * 0.3f + 160 * S;
	if (R.bPlayerDied && !R.KilledBy.IsEmpty() && R.PlayerHitsTaken.Num())
	{
		Txt(FString::Printf(TEXT("You died: %s"), *Hit(R.PlayerHitsTaken.Last(), true)), W * 0.5f, Y, Enemy, 0.85f, true); Y += 28 * S;
	}
	Txt(FString::Printf(TEXT("Your kills: %d     Hits landed: %d"), R.PlayerKills, R.PlayerHitsDealt.Num()), W * 0.5f, Y, White, 0.85f, true);
}

void ACEHUD::DrawScores(ACEGameMode* G)
{
	const float X = W * 0.5f - 360 * S;
	float Y = H * 0.22f;
	Box(X - 20 * S, Y - 20 * S, 760 * S, 420 * S, FLinearColor(0, 0, 0, 0.7f));
	Txt(FString::Printf(TEXT("ROUND %d     SQUAD %d - %d ENEMY"), G->Round, G->ScoreSquad, G->ScoreEnemy), X, Y, White, 1.0f, false, true); Y += 50 * S;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const ECETeam T = Pass == 0 ? G->PlayerTeam() : (G->PlayerTeam() == ECETeam::Attack ? ECETeam::Defend : ECETeam::Attack);
		for (ACECharacter* C : G->Characters)
		{
			if (!C || C->Team != T) continue;
			int32 K = 0; for (const FCEHitRecord& R : C->HitsDealt) if (R.bKill) ++K;
			Txt(FString::Printf(TEXT("%-10s  %-7s  %-12s  kills %d  %s"), *C->CEName, T == ECETeam::Attack ? TEXT("ATK") : TEXT("DEF"), *C->CurDef().Name, K, C->IsAlive() ? TEXT("") : TEXT("DEAD")),
				X, Y, Pass == 0 ? Squad : Enemy, 0.85f);
			Y += 30 * S;
		}
		Y += 20 * S;
	}
}

void ACEHUD::DrawSettings()
{
	const UCETuning* T = UCETuning::Get();
	const float X = W * 0.5f - 300 * S, Y = H * 0.4f;
	Box(X - 20 * S, Y - 20 * S, 640 * S, 170 * S, FLinearColor(0, 0, 0, 0.8f));
	Txt(TEXT("SETTINGS (P to close)"), X, Y, White, 1.0f, false, true);
	Txt(FString::Printf(TEXT("Mouse sensitivity  %.1f     [ / ]"), T->MouseSensitivity), X, Y + 50 * S, White, 0.9f);
	Txt(FString::Printf(TEXT("Bot difficulty     %.1f     - / ="), T->BotDifficulty), X, Y + 80 * S, White, 0.9f);
	Txt(FString::Printf(TEXT("Field of view      %.0f     , / ."), T->FieldOfView), X, Y + 110 * S, White, 0.9f);
}
