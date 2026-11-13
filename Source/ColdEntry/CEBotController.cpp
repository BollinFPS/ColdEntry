#include "CEBotController.h"
#include "ColdEntry.h"
#include "CETuning.h"
#include "CECharacter.h"
#include "CEGameMode.h"
#include "CEMap.h"
#include "CEEntities.h"
#include "CEGadgets.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

ACEBotController::ACEBotController()
{
	PrimaryActorTick.bCanEverTick = true;
}

ACECharacter* ACEBotController::Char() const { return Cast<ACECharacter>(GetPawn()); }
ACEGameMode* ACEBotController::GM() const { return GetWorld() ? GetWorld()->GetAuthGameMode<ACEGameMode>() : nullptr; }

// =============================================================================================
// Round hooks
// =============================================================================================
void ACEBotController::OnRoundStart()
{
	Spot.Reset(); Target.Reset(); Path.Reset(); BlockedEdges.Reset(); Tasks.Reset();
	Cur = FTask(); PathGoal = FVector(1e9); LastHeardTime = LastSeenTime = -100.f;
	bChargePlaced = false; ChargeTarget.Reset(); BurstLeft = 0; FlashCooldown = 0.f;
	Difficulty = FMath::Clamp(UCETuning::Get()->BotDifficulty + FMath::FRandRange(-0.08f, 0.08f), 0.f, 1.f);
	if (ACECharacter* C = Char()) SetControlRotation(C->GetActorRotation());
	BuildPrepTasks();
	NextTask();
}

void ACEBotController::BuildPrepTasks()
{
	ACEGameMode* G = GM();
	ACECharacter* C = Char();
	if (!G || !C || !G->Level) return;
	ACELevelBuilder* L = G->Level;
	const int32 k = SquadIndex;
	if (C->Team == ECETeam::Defend)
	{
		// setup: reinforce the soft wall closest to the objective that is "ours", barricade a frame, place an alarm
		TArray<ACEBreakable*> Walls; for (ACEBreakable* B : L->BreachWalls) Walls.Add(B);
		Walls.Sort([L](const ACEBreakable& A, const ACEBreakable& B) { return FVector::DistSquared(A.OpeningCentre, L->ObjectiveCentre) < FVector::DistSquared(B.OpeningCentre, L->ObjectiveCentre); });
		TArray<ACEBreakable*> Frames; for (ACEBreakable* B : L->Barricades) Frames.Add(B);
		Frames.Sort([L](const ACEBreakable& A, const ACEBreakable& B) { return FVector::DistSquared(A.OpeningCentre, L->ObjectiveCentre) < FVector::DistSquared(B.OpeningCentre, L->ObjectiveCentre); });
		auto Stand = [](const ACEEntity* E, const FVector& Toward) { FVector D = Toward - E->OpeningCentre; D.Z = 0; FVector N = FMath::Abs(E->OpeningExtent.X) < FMath::Abs(E->OpeningExtent.Y) ? FVector(FMath::Sign(D.X), 0, 0) : FVector(0, FMath::Sign(D.Y), 0); FVector P = E->OpeningCentre + N * 90.f; P.Z = E->OpeningCentre.Z - E->OpeningExtent.Z; return P; };
		if (Walls.IsValidIndex(k)) { FTask T; T.Type = ETask::Reinforce; T.Target = Walls[k]; T.Pos = Stand(Walls[k], L->ObjectiveCentre); Tasks.Add(T); }
		if (k == 0 && Walls.IsValidIndex(4)) { FTask T; T.Type = ETask::Reinforce; T.Target = Walls[4]; T.Pos = Stand(Walls[4], L->ObjectiveCentre); Tasks.Add(T); }
		if (Frames.IsValidIndex(k)) { FTask T; T.Type = ETask::Barricade; T.Target = Frames[k]; T.Pos = Stand(Frames[k], L->ObjectiveCentre); Tasks.Add(T); }
		if (L->AlarmSpots.IsValidIndex(k)) { FTask T; T.Type = ETask::Alarm; T.Pos = L->AlarmSpots[k]; Tasks.Add(T); }
		// hold position: three anchors in/around Comms, others roam
		const int32 HoldIdx = k < 3 ? (k + G->Round) % 3 : 3 + FMath::RandRange(0, L->DefendHolds.Num() - 4);
		const FCEHold& H = L->DefendHolds[FMath::Clamp(HoldIdx, 0, L->DefendHolds.Num() - 1)];
		FTask T; T.Type = ETask::Hold; T.Pos = H.Pos; T.LookAt = H.LookAt; T.bCrouch = H.bCrouch; Tasks.Add(T);
	}
	else
	{
		// choose an entry near our spawn; squadmates spread over the closest few
		const FVector Me = C->GetActorLocation();
		TArray<int32> Order; for (int32 i = 0; i < L->EntryPoints.Num(); ++i) Order.Add(i);
		Order.Sort([&](int32 A, int32 B) { return FVector::DistSquared(L->EntryPoints[A], Me) < FVector::DistSquared(L->EntryPoints[B], Me); });
		EntryIdx = Order[(k + FMath::RandRange(0, 1)) % FMath::Min(3, Order.Num())];
		FTask E; E.Type = ETask::Entry; E.Pos = L->EntryPoints[EntryIdx]; Tasks.Add(E);
		FTask O; O.Type = ETask::Objective; O.Pos = L->PlantSpots[k % L->PlantSpots.Num()]; Tasks.Add(O);
	}
}

void ACEBotController::OnActionStart()
{
	ACECharacter* C = Char();
	if (!C) return;
	if (C->Team == ECETeam::Defend)
	{
		// drop unfinished setup, keep the hold
		Tasks.RemoveAll([](const FTask& T) { return T.Type != ETask::Hold; });
		if (Cur.Type != ETask::Hold) NextTask();
	}
	else
	{
		NextTask();
	}
	RoamT = FMath::FRandRange(15.f, 35.f);
}

void ACEBotController::OnPlanted()
{
	ACEGameMode* G = GM();
	ACECharacter* C = Char();
	if (!G || !C || !C->IsAlive() || !G->Device) return;
	Tasks.Reset();
	FTask T;
	if (C->Team == ECETeam::Defend) { T.Type = ETask::Defuse; T.Pos = G->Device->GetActorLocation(); }
	else
	{
		const FCEHold& H = G->Level->PlantHolds[SquadIndex % G->Level->PlantHolds.Num()];
		T.Type = ETask::GuardPlant; T.Pos = H.Pos; T.LookAt = H.LookAt; T.bCrouch = H.bCrouch;
	}
	Cur = T; Path.Reset(); PathGoal = FVector(1e9);
}

void ACEBotController::NextTask()
{
	if (Tasks.Num()) { Cur = Tasks[0]; Tasks.RemoveAt(0); }
	else if (Cur.Type != ETask::Hold && Cur.Type != ETask::GuardPlant) Cur = FTask();
	TaskT = 0.f;
	Path.Reset();
	PathGoal = FVector(1e9);
}

void ACEBotController::HearNoise(ACECharacter* Src, const FVector& Loc, bool bShot)
{
	const float Fuzz = bShot ? 150.f : 260.f;
	LastHeard = Loc + FVector(FMath::FRandRange(-Fuzz, Fuzz), FMath::FRandRange(-Fuzz, Fuzz), 0);
	LastHeardTime = GetWorld()->GetTimeSeconds();
	if (ACEGameMode* G = GM()) if (ACECharacter* C = Char()) G->AddIntel(C->Team, Src, LastHeard, false, false);
}

void ACEBotController::OnDamaged(ACECharacter* Attacker)
{
	if (!Attacker) return;
	LastSeen = Attacker->GetActorLocation();
	LastSeenTime = GetWorld()->GetTimeSeconds();
	Spot.FindOrAdd(Attacker) += 0.25f; // getting shot speeds up recognition
}

// =============================================================================================
// Tick
// =============================================================================================
void ACEBotController::Tick(float Dt)
{
	Super::Tick(Dt);
	ACECharacter* C = Char();
	ACEGameMode* G = GM();
	if (!C || !G || !C->IsAlive() || !G->Level) return;
	FCEInput& In = C->Input;
	const bool bKeepCrouch = In.bCrouch;
	In = FCEInput();
	In.bCrouch = bKeepCrouch;
	if (G->Phase == ECEPhase::Menu || G->Phase == ECEPhase::RoundEnd || G->Phase == ECEPhase::MatchEnd) return;

	FlashCooldown -= Dt;
	PerceiveT -= Dt;
	if (PerceiveT <= 0.f) { Perceive(0.1f); PerceiveT = 0.1f; }

	if (Target.IsValid() && Target->IsAlive() && G->Phase != ECEPhase::Prep) Combat(Dt);
	else Objective(Dt);

	// reload when nothing is happening
	const FCEWeaponState& W = C->CurWeapon();
	const float Tm = GetWorld()->GetTimeSeconds();
	if (!Target.IsValid() && !C->IsReloading() && W.Mag < C->CurDef().Mag * 0.45f && W.Reserve > 0 && Tm - LastSeenTime > 1.5f) In.bReload = true;
	if (W.Mag == 0 && W.Reserve <= 0 && C->Slot == 0) In.Switch = 1;
	// heal when safe
	if (!Target.IsValid() && C->Health < 55.f && C->GadgetCount[(int32)ECEGadget::Medkit] > 0 && Tm - LastSeenTime > 3.f && C->Action == ECEAction::None)
		In.bHeal = true;
	if (C->Action == ECEAction::Heal) { In.bHeal = true; In.Forward = In.Right = 0.f; }
	DebugState = FString::Printf(TEXT("task %d tgt %s path %d/%d"), (int32)Cur.Type, Target.IsValid() ? *Target->CEName : TEXT("-"), PathIdx, Path.Num());
}

void ACEBotController::Perceive(float Dt)
{
	ACECharacter* C = Char();
	ACEGameMode* G = GM();
	const UCETuning* T = UCETuning::Get();
	const float Tm = GetWorld()->GetTimeSeconds();
	const FVector Eye = C->EyeLocation();
	const FVector Fwd = C->AimDir();
	const float CosFov = FMath::Cos(FMath::DegreesToRadians(T->BotFov * 0.5f));
	const float Reaction = FMath::Lerp(T->BotReactionMax, T->BotReactionMin, Difficulty);

	ACECharacter* Best = nullptr; float BestD = 1e9f;
	for (ACECharacter* E : G->Characters)
	{
		if (!E || !E->IsAlive() || E->Team == C->Team) continue;
		float& S = Spot.FindOrAdd(E);
		bool bVisible = false;
		const FVector P = E->ChestLocation();
		const FVector To = P - Eye;
		const float D = To.Size();
		if (!C->IsBlind() && D < 6000.f)
		{
			const float Dot = FVector::DotProduct(To / D, Fwd);
			if (Dot > CosFov || D < 250.f)
			{
				if (G->HasLineOfSight(Eye, P, C, E) || G->HasLineOfSight(Eye, E->HeadLocation(), C, E))
				{
					bVisible = true;
					float Rate = Dot > 0.9f ? 1.f : 0.55f;
					if (E->IsCrouched() && D > 1200.f) Rate *= 0.75f;
					if (E->GetVelocity().Size2D() > 300.f) Rate *= 1.25f;
					S += Dt * Rate;
				}
			}
		}
		if (!bVisible) S = FMath::Max(0.f, S - Dt * 0.5f);
		if (bVisible && S >= Reaction)
		{
			G->AddIntel(C->Team, E, E->GetActorLocation(), true, false);
			LastSeen = E->GetActorLocation(); LastSeenTime = Tm;
			if (D < BestD) { BestD = D; Best = E; }
		}
	}
	if (Best && Best != Target.Get())
	{
		Target = Best;
		TargetSince = Tm;
		bAimHead = FMath::FRand() < T->BotHeadBias * (0.5f + Difficulty);
		const float D = FVector::Dist(Best->GetActorLocation(), C->GetActorLocation());
		const float Err = T->BotAimErrorStart * (1.4f - Difficulty) * FMath::Clamp(D / 1000.f, 0.4f, 2.5f);
		AimOffset = FMath::VRand() * Err;
		BurstLeft = 0;
		NextBurst = Tm + 0.05f;
	}
	else if (!Best && Target.IsValid())
	{
		// lose the target after a moment without sight
		if (Tm - LastSeenTime > 1.2f) Target.Reset();
	}
}

void ACEBotController::AimAt(const FVector& P, float Dt, float Speed)
{
	ACECharacter* C = Char();
	const FRotator Want = (P - C->EyeLocation()).Rotation();
	FRotator CurRot = GetControlRotation();
	CurRot.Pitch = FRotator::NormalizeAxis(CurRot.Pitch);
	FRotator Delta = (Want - CurRot).GetNormalized();
	const float Max = (Speed > 0.f ? Speed : UCETuning::Get()->BotTurnSpeed) * Dt;
	// ease in: fast far, gentle near
	Delta.Yaw = FMath::Clamp(Delta.Yaw * FMath::Min(1.f, Dt * 14.f), -Max, Max);
	Delta.Pitch = FMath::Clamp(Delta.Pitch * FMath::Min(1.f, Dt * 14.f), -Max, Max);
	SetControlRotation(FRotator(FMath::Clamp(CurRot.Pitch + Delta.Pitch, -80.f, 80.f), CurRot.Yaw + Delta.Yaw, 0));
}

float ACEBotController::AimError(const FVector& P) const
{
	const ACECharacter* C = Char();
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(C->AimDir(), (P - C->EyeLocation()).GetSafeNormal()), -1.f, 1.f)));
}

void ACEBotController::Combat(float Dt)
{
	ACECharacter* C = Char();
	ACECharacter* E = Target.Get();
	const UCETuning* T = UCETuning::Get();
	FCEInput& In = C->Input;
	const float Tm = GetWorld()->GetTimeSeconds();
	const bool bSeeNow = Tm - LastSeenTime < 0.25f;

	// settle aim error over time on target
	const float Settle = FMath::Clamp((Tm - TargetSince) / T->BotAimSettle, 0.f, 1.f);
	const FVector Err = AimOffset * FMath::Lerp(1.f, 0.12f, Settle);
	const FVector AimPoint = (bSeeNow ? (bAimHead ? E->HeadLocation() : E->ChestLocation()) : LastSeen + FVector(0, 0, 60)) + Err;
	AimAt(AimPoint, Dt);
	const float D = FVector::Dist(E->GetActorLocation(), C->GetActorLocation());
	In.bAds = D > 700.f && !C->IsBlind();

	if (C->Equipped != ECEGadget::None) In.Switch = 0;
	if (C->Action != ECEAction::None && C->Action != ECEAction::Plant && C->Action != ECEAction::Defuse) C->CancelAction();

	// fire in bursts once roughly on target
	const float Tol = FMath::Max(1.2f, FMath::RadiansToDegrees(FMath::Atan(45.f / FMath::Max(100.f, D))));
	if (bSeeNow && AimError(AimPoint) < Tol * 1.6f && Tm >= NextBurst)
	{
		if (BurstLeft <= 0) BurstLeft = FMath::RandRange(4, 9) + (D < 800.f ? 4 : 0);
		if (C->CurDef().bAuto) { In.bFire = true; In.bFirePressed = true; }
		else if (Tm >= NextSemi) { In.bFirePressed = true; In.bFire = true; NextSemi = Tm + FMath::FRandRange(0.16f, 0.26f); }
		if (C->GetSpreadDeg() >= 0.f) BurstLeft--;
		if (BurstLeft <= 0) NextBurst = Tm + FMath::FRandRange(0.12f, 0.3f);
	}
	else if (C->IsBlind() && FMath::FRand() < 0.04f) { In.bFire = true; }

	if (C->CurWeapon().Mag == 0) In.bReload = true;

	// small strafes, some crouching for anchors
	StrafeT -= Dt;
	if (StrafeT <= 0.f) { StrafeT = FMath::FRandRange(0.4f, 1.1f); StrafeDir = FMath::RandBool() ? 1.f : -1.f; if (FMath::FRand() < 0.25f) StrafeDir = 0.f; }
	const bool bAnchor = Cur.Type == ETask::Hold && Cur.bCrouch;
	In.bCrouch = bAnchor || (In.bCrouch && D > 600.f);
	In.Right = bAnchor ? 0.f : StrafeDir * 0.8f;
	if (!bSeeNow)
	{
		// push toward last known position carefully
		MoveTo(LastSeen, Dt, true, 150.f);
		AimAt(LastSeen + FVector(0, 0, 70), Dt);
	}
	if (C->IsBlind()) { In.Forward = -0.6f; In.bCrouch = true; }
}

// =============================================================================================
// Objective play
// =============================================================================================
void ACEBotController::Objective(float Dt)
{
	ACECharacter* C = Char();
	ACEGameMode* G = GM();
	ACELevelBuilder* L = G->Level;
	FCEInput& In = C->Input;
	const float Tm = GetWorld()->GetTimeSeconds();
	TaskT += Dt;
	if (C->Equipped != ECEGadget::None && Cur.Type != ETask::Alarm) In.Switch = 0;

	// react to fresh intel (investigate), except anchors who hold their angle
	const FCEIntel* Fresh = nullptr; ACECharacter* FreshE = nullptr;
	for (auto& KV : G->Intel[(int32)C->Team]) if (KV.Key && KV.Key->IsAlive() && Tm - KV.Value.Time < 4.f) { if (!Fresh || KV.Value.Time > Fresh->Time) { Fresh = &KV.Value; FreshE = KV.Key; } }
	const bool bHoldTask = Cur.Type == ETask::Hold || Cur.Type == ETask::GuardPlant;

	if (G->Phase == ECEPhase::Prep)
	{
		if (C->Team == ECETeam::Attack) { AimAt(L->ObjectiveCentre + FVector(0, 0, 150), Dt, 60.f); return; }
	}

	switch (Cur.Type)
	{
	case ETask::Reinforce:
	case ETask::Barricade:
	{
		ACEEntity* E = Cur.Target.Get();
		ACEBreakable* B = Cast<ACEBreakable>(E);
		const bool bDone = !B || (Cur.Type == ETask::Reinforce ? (B->IsReinforced() || !B->IsIntact() || G->ReinforcePool <= 0) : (B->IsDeployed() || C->GadgetCount[(int32)ECEGadget::Barricade] <= 0));
		if (bDone || TaskT > 25.f) { NextTask(); break; }
		if (MoveTo(Cur.Pos, Dt, false, 60.f))
		{
			AimAt(E->OpeningCentre, Dt);
			In.bInteract = true;
			if (C->Action == ECEAction::None && AimError(E->OpeningCentre) < 25.f)
				C->BeginAction(Cur.Type == ETask::Reinforce ? ECEAction::Reinforce : ECEAction::Barricade, E, E->OpeningCentre,
					Cur.Type == ETask::Reinforce ? UCETuning::Get()->ReinforceTime : UCETuning::Get()->BarricadeTime);
		}
		break;
	}
	case ETask::Alarm:
		if (C->GadgetCount[(int32)ECEGadget::Alarm] <= 0 || TaskT > 20.f) { NextTask(); break; }
		if (MoveTo(Cur.Pos, Dt, false, 120.f))
		{
			G->SpawnAlarm(C, Cur.Pos + FVector(0, 0, 3), FVector::UpVector);
			C->GadgetCount[(int32)ECEGadget::Alarm]--;
			NextTask();
		}
		break;
	case ETask::Hold:
	case ETask::GuardPlant:
	{
		if (Fresh && C->Team == ECETeam::Defend && !Cur.bCrouch && FVector::Dist(Fresh->Pos, C->GetActorLocation()) < 1500.f && G->Phase != ECEPhase::Prep)
		{
			// roamer: hunt the fresh contact
			MoveTo(Fresh->Pos, Dt, true, 200.f);
			AimAt(Fresh->Pos + FVector(0, 0, 100), Dt);
			break;
		}
		if (MoveTo(Cur.Pos, Dt, G->Phase != ECEPhase::Prep, 70.f))
		{
			In.bCrouch = Cur.bCrouch;
			LookScan += Dt;
			const FVector Look = (Fresh && FVector::Dist(Fresh->Pos, C->GetActorLocation()) < 2000.f) ? Fresh->Pos + FVector(0, 0, 100)
				: Cur.LookAt + FRotationMatrix((Cur.LookAt - Cur.Pos).Rotation()).GetUnitAxis(EAxis::Y) * FMath::Sin(LookScan * 0.5f) * 150.f;
			AimAt(Look, Dt, 120.f);
			// roamers relocate now and then
			RoamT -= Dt;
			if (C->Team == ECETeam::Defend && !Cur.bCrouch && RoamT <= 0.f && G->Phase == ECEPhase::Action)
			{
				RoamT = FMath::FRandRange(18.f, 35.f);
				const FCEHold& H = L->DefendHolds[FMath::RandRange(3, L->DefendHolds.Num() - 1)];
				Cur.Pos = H.Pos; Cur.LookAt = H.LookAt; Cur.bCrouch = false;
				Path.Reset(); PathGoal = FVector(1e9);
			}
		}
		break;
	}
	case ETask::Entry:
		if (MoveTo(Cur.Pos, Dt, false, 150.f) || TaskT > 60.f) NextTask();
		break;
	case ETask::Objective:
	{
		if (Fresh && FVector::Dist(Fresh->Pos, C->GetActorLocation()) < 1200.f && Fresh->bVisual)
		{
			MoveTo(Fresh->Pos, Dt, true, 200.f);
			AimAt(Fresh->Pos + FVector(0, 0, 100), Dt);
			break;
		}
		if (L->InObjective(C->GetActorLocation()) && G->Phase == ECEPhase::Action && !G->Device)
		{
			// plant if nobody is in sight; otherwise hold the room
			In.bInteract = true;
			In.bCrouch = true;
			AimAt(L->ObjectiveCentre + FVector(0, 0, 40), Dt, 90.f);
			break;
		}
		// flash before entering the objective room
		if (FlashCooldown <= 0.f && C->GadgetCount[(int32)ECEGadget::Flash] > 0 && Path.IsValidIndex(PathIdx))
		{
			const FVector Next = L->Nodes[Path[PathIdx]].Pos;
			if (L->InObjective(Next + FVector(0, 0, 50)) && FVector::Dist(Next, C->GetActorLocation()) < 450.f && !L->InObjective(C->GetActorLocation()))
			{
				FlashCooldown = 6.f;
				C->Equipped = ECEGadget::Flash;
				AimAt(L->ObjectiveCentre + FVector(0, 0, 120), 1.f, 9999.f);
				G->SpawnGrenade(C, ECEGadget::Flash, C->EyeLocation() + C->AimDir() * 35.f, (C->AimDir() + FVector(0, 0, 0.1f)).GetSafeNormal() * UCETuning::Get()->ThrowSpeed);
				C->GadgetCount[(int32)ECEGadget::Flash]--;
				C->Equipped = ECEGadget::None;
				break;
			}
		}
		MoveTo(Cur.Pos, Dt, L->InBuilding(C->GetActorLocation()), 80.f);
		break;
	}
	case ETask::Defuse:
		if (!G->Device || !G->Device->bActive) break;
		Cur.Pos = G->Device->GetActorLocation();
		if (MoveTo(Cur.Pos, Dt, false, 150.f))
		{
			In.bInteract = true;
			In.bCrouch = true;
			AimAt(Cur.Pos, Dt, 120.f);
		}
		break;
	default:
		if (C->Team == ECETeam::Attack && G->Phase == ECEPhase::Action)
		{
			FTask O; O.Type = ETask::Objective; O.Pos = L->PlantSpots[SquadIndex % L->PlantSpots.Num()]; Cur = O;
		}
		break;
	}
	(void)bHoldTask;
	(void)FreshE;
}

// =============================================================================================
// Movement along the nav graph
// =============================================================================================
float ACEBotController::EdgeCost(const FCENavEdge& E) const
{
	const ACECharacter* C = Char();
	ACEGameMode* G = GM();
	const int32 Idx = (int32)(&E - G->Level->Edges.GetData());
	if (BlockedEdges.Contains(Idx)) return -1.f;
	float Cost = E.Len;
	for (const TWeakObjectPtr<ACEEntity>& W : E.Blockers)
	{
		const ACEEntity* B = W.Get();
		if (!B) continue;
		if (const ACEDoor* D = Cast<ACEDoor>(B)) { if (!D->IsDestroyed() && !D->IsOpen()) Cost += 120.f; continue; }
		const ACEBreakable* Br = Cast<ACEBreakable>(B);
		if (!Br || !Br->IsIntact()) continue;
		switch (Br->Kind)
		{
		case ECEEntityKind::Glass: if (C->Team == ECETeam::Defend) return -1.f; Cost += 300.f; break;
		case ECEEntityKind::Barricade: if (C->Team == ECETeam::Defend) return -1.f; Cost += 450.f; break;
		case ECEEntityKind::BreachWall:
			if (C->Team == ECETeam::Defend || Br->IsReinforced() || C->GadgetCount[(int32)ECEGadget::BreachCharge] <= 0) return -1.f;
			Cost += 700.f; break;
		default: break;
		}
	}
	return Cost;
}

void ACEBotController::Replan(const FVector& Goal)
{
	ACECharacter* C = Char();
	ACELevelBuilder* L = GM()->Level;
	PathGoal = Goal;
	const int32 From = L->NearestNode(C->GetActorLocation(), true);
	const int32 To = L->NearestNode(Goal + FVector(0, 0, 90), false);
	if (!L->FindPath(From, To, [this](const FCENavEdge& E) { return EdgeCost(E); }, Path)) Path.Reset();
	PathIdx = 0;
	// skip the first node if we're already past it
	if (Path.Num() > 1 && FVector::Dist2D(L->Nodes[Path[1]].Pos, C->GetActorLocation()) < FVector::Dist2D(L->Nodes[Path[0]].Pos, L->Nodes[Path[1]].Pos)) PathIdx = 1;
	ReplanT = 4.f;
}

bool ACEBotController::HandleBlockers(float Dt)
{
	ACECharacter* C = Char();
	ACEGameMode* G = GM();
	ACELevelBuilder* L = G->Level;
	if (!Path.IsValidIndex(PathIdx) || PathIdx == 0) return false;
	const FCENavEdge* E = L->EdgeBetween(Path[PathIdx - 1], Path[PathIdx]);
	if (!E) return false;
	FCEInput& In = C->Input;
	for (const TWeakObjectPtr<ACEEntity>& W : E->Blockers)
	{
		ACEEntity* B = W.Get();
		if (!B) continue;
		const float D = FVector::Dist(B->OpeningCentre, C->EyeLocation());
		if (ACEDoor* Door = Cast<ACEDoor>(B))
		{
			if (!Door->IsDestroyed() && !Door->IsOpen() && D < 190.f) Door->SetOpen(true, C->GetActorLocation());
			continue;
		}
		ACEBreakable* Br = Cast<ACEBreakable>(B);
		if (!Br || !Br->IsIntact()) continue;
		if (Br->Kind == ECEEntityKind::BreachWall)
		{
			if (Br->IsReinforced() || C->GadgetCount[(int32)ECEGadget::BreachCharge] <= 0)
			{
				BlockedEdges.Add((int32)(E - L->Edges.GetData()));
				Path.Reset(); PathGoal = FVector(1e9);
				return true;
			}
			if (!bChargePlaced)
			{
				if (D < 200.f)
				{
					AimAt(Br->OpeningCentre, Dt);
					In.bInteract = true;
					if (C->Action == ECEAction::None)
					{
						const FVector Loc = Br->OpeningCentre + (C->GetActorLocation() - Br->OpeningCentre).GetSafeNormal2D() * 9.f - FVector(0, 0, 30);
						C->BeginAction(ECEAction::PlaceCharge, Br, Loc, UCETuning::Get()->BreachPlaceTime);
					}
					if (C->Action == ECEAction::PlaceCharge && C->ActionTime + Dt >= C->ActionDuration) { bChargePlaced = true; ChargeTarget = Br; }
					return true;
				}
				return false;
			}
			// charge placed: back off, then detonate
			if (D < 330.f)
			{
				const FVector Away = (C->GetActorLocation() - Br->OpeningCentre).GetSafeNormal2D();
				const FVector2D Local = FVector2D(FRotationMatrix(FRotator(0, GetControlRotation().Yaw, 0)).InverseTransformVector(Away));
				In.Forward = Local.X; In.Right = Local.Y;
				AimAt(Br->OpeningCentre, Dt);
				return true;
			}
			In.bDetonate = true;
			bChargePlaced = false;
			return true;
		}
		// glass / barricade: shoot it out
		if (D < 500.f)
		{
			AimAt(Br->OpeningCentre, Dt);
			if (AimError(Br->OpeningCentre) < 6.f) { In.bFire = true; In.bFirePressed = true; }
			return true;
		}
	}
	return false;
}

bool ACEBotController::MoveTo(const FVector& Goal, float Dt, bool bCareful, float Arrive)
{
	ACECharacter* C = Char();
	ACELevelBuilder* L = GM()->Level;
	FCEInput& In = C->Input;
	const FVector Feet = C->GetActorLocation() - FVector(0, 0, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (FVector::Dist2D(Feet, Goal) < Arrive && FMath::Abs(Feet.Z - Goal.Z) < 150.f) { Path.Reset(); return true; }

	ReplanT -= Dt;
	if (Path.Num() == 0 || FVector::Dist(PathGoal, Goal) > 200.f || ReplanT <= 0.f) Replan(Goal);

	if (HandleBlockers(Dt)) return false;

	FVector Next = Goal;
	if (Path.IsValidIndex(PathIdx))
	{
		Next = L->Nodes[Path[PathIdx]].Pos;
		if (FVector::Dist2D(Feet, Next) < 70.f && FMath::Abs(Feet.Z - Next.Z) < 120.f)
		{
			++PathIdx;
			if (Path.IsValidIndex(PathIdx)) Next = L->Nodes[Path[PathIdx]].Pos; else Next = Goal;
		}
	}
	FVector Dir = Next - Feet; Dir.Z = 0;
	if (Dir.SizeSquared() < 1.f) return false;
	Dir.Normalize();

	// stuck handling
	StuckT += Dt;
	if (StuckT > 1.5f)
	{
		if (FVector::Dist2D(LastProgressPos, Feet) < 40.f) { UnstickT = 0.6f; ReplanT = 0.f; StrafeDir = FMath::RandBool() ? 1.f : -1.f; }
		LastProgressPos = Feet; StuckT = 0.f;
	}
	if (UnstickT > 0.f) { UnstickT -= Dt; Dir = (Dir + FRotationMatrix(Dir.Rotation()).GetUnitAxis(EAxis::Y) * StrafeDir).GetSafeNormal(); }

	const FVector Local = FRotationMatrix(FRotator(0, GetControlRotation().Yaw, 0)).InverseTransformVector(Dir);
	In.Forward = Local.X; In.Right = Local.Y;
	In.bSprint = !bCareful && Local.X > 0.7f;
	if (!Target.IsValid())
	{
		const FVector LookP = FVector(Next.X, Next.Y, C->EyeLocation().Z - 10.f);
		AimAt(Feet + Dir * 500.f + FVector(0, 0, C->EyeLocation().Z - Feet.Z), Dt, 280.f);
		(void)LookP;
	}
	return false;
}
