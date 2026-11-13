#include "CEGameMode.h"
#include "ColdEntry.h"
#include "CETuning.h"
#include "CEMap.h"
#include "CECharacter.h"
#include "CEEntities.h"
#include "CEGadgets.h"
#include "CESound.h"
#include "CEPlayerController.h"
#include "CEBotController.h"
#include "CEHUD.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"
#include "DistanceFieldAtlas.h"

static const TCHAR* SquadNames[5] = { TEXT("You"), TEXT("Kestrel"), TEXT("Vance"), TEXT("Moss"), TEXT("Okafor") };
static const TCHAR* EnemyNames[5] = { TEXT("Hale"), TEXT("Brandt"), TEXT("Sato"), TEXT("Ilic"), TEXT("Mercer") };

ACEGameMode::ACEGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ACEPlayerController::StaticClass();
	HUDClass = ACEHUD::StaticClass();
}

float ACEGameMode::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f; }

void ACEGameMode::StartPlay()
{
	Super::StartPlay();
	const UCETuning* T = UCETuning::Get();
	bShotTour = FParse::Param(FCommandLine::Get(), TEXT("ceshots")) || FParse::Param(FCommandLine::Get(), TEXT("ceenvironmentshots"));
	bAutoPlay = bShotTour || T->bAutoPlay || FParse::Param(FCommandLine::Get(), TEXT("ceauto"));
	bSquadAttacking = T->bPlayerStartsAttacking;

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Level = GetWorld()->SpawnActor<ACELevelBuilder>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	Level->Build();

	Overview = GetWorld()->SpawnActor<ACameraActor>(FVector(4800, -2550, 185), FRotator(5, 126, 0), P);
	Overview->GetCameraComponent()->SetFieldOfView(68.f);

	for (int32 i = 0; i < 9; ++i)
	{
		ACEBotController* B = GetWorld()->SpawnActor<ACEBotController>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		B->SquadIndex = i < 4 ? i + 1 : i - 4;
		B->bPlayerSquad = i < 4;
		Bots.Add(B);
	}
	Phase = ECEPhase::Menu;
	if (bAutoPlay) StartMatchFlow();
	UE_LOG(LogColdEntry, Log, TEXT("COLD ENTRY ready (autoplay=%d)"), bAutoPlay ? 1 : 0);
}

void ACEGameMode::StartMatchFlow()
{
	Round = 0; ScoreSquad = 0; ScoreEnemy = 0;
	bSquadAttacking = UCETuning::Get()->bPlayerStartsAttacking;
	if (bAutoPlay) GetWorldSettings()->SetTimeDilation(UCETuning::Get()->AutoPlayTimeDilation);
	StartRound();
}

void ACEGameMode::CleanupRound()
{
	for (ACEBotController* B : Bots) if (B) B->UnPossess();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->UnPossess();
	for (ACECharacter* C : Characters) if (C) C->Destroy();
	Characters.Reset();
	PlayerChar = nullptr;
	TArray<AActor*> Temp;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) if (It->ActorHasTag(TEXT("CERound"))) Temp.Add(*It);
	for (AActor* A : Temp) A->Destroy();
	Gadgets.Reset(); Smokes.Reset(); Device = nullptr;
	Intel[0].Reset(); Intel[1].Reset();
	SpawnAnchor.Reset(); OutsideTime.Reset(); CamSeenTime.Reset();
	KillFeed.Reset();
	if (Level) Level->ResetEntities();
}

void ACEGameMode::SpawnFixedCameras()
{
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FCEPlacement& Pl : Level->FixedCameras)
	{
		ACESecurityCamera* C = GetWorld()->SpawnActorDeferred<ACESecurityCamera>(ACESecurityCamera::StaticClass(), FTransform(Pl.Rot, Pl.Pos));
		C->bFixed = true;
		C->Team = ECETeam::Defend;
		C->Callout = Level->CalloutAt(Pl.Pos);
		UGameplayStatics::FinishSpawningActor(C, FTransform(Pl.Rot, Pl.Pos));
		Gadgets.Add(C);
	}
}

void ACEGameMode::SpawnTeams()
{
	const UCETuning* T = UCETuning::Get();
	const ECETeam SquadTeam = PlayerTeam();
	const ECETeam EnemyTeam = SquadTeam == ECETeam::Attack ? ECETeam::Defend : ECETeam::Attack;
	const int32 EnemySpawnSite = FMath::RandRange(0, Level->AttackSpawns.Num() - 1);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	auto SpawnOne = [&](ECETeam Team, int32 Idx, const FString& Name, bool bPlayer) -> ACECharacter*
	{
		FVector Loc;
		float Yaw;
		if (Team == ECETeam::Attack)
		{
			const int32 Site = (Team == SquadTeam) ? AttackSpawnChoice : EnemySpawnSite;
			Loc = Level->AttackSpawns[Site][Idx % Level->AttackSpawns[Site].Num()];
			Yaw = (Level->BuildingBox.GetCenter() - Loc).Rotation().Yaw;
		}
		else
		{
			Loc = Level->DefendSpawns[Idx % Level->DefendSpawns.Num()];
			Yaw = FMath::FRandRange(0.f, 360.f);
		}
		Loc.Z += 95.f;
		ACECharacter* C = GetWorld()->SpawnActor<ACECharacter>(ACECharacter::StaticClass(), Loc, FRotator(0, Yaw, 0), P);
		if (!C) return nullptr;
		const ECEWeaponId Prim = bPlayer ? PlayerPrimary : ((Team == ECETeam::Attack) ? (Idx % 3 == 2 ? ECEWeaponId::SMG9 : ECEWeaponId::R4) : (Idx % 2 == 0 ? ECEWeaponId::SMG9 : ECEWeaponId::R4));
		C->InitLoadout(Team, Prim, Name, bPlayer);
		Characters.Add(C);
		SpawnAnchor.Add(C, Loc);
		return C;
	};

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	for (int32 i = 0; i < 5; ++i)
	{
		const bool bPlayerSlot = i == 0;
		ACECharacter* C = SpawnOne(SquadTeam, i, SquadNames[i], bPlayerSlot && !bAutoPlay);
		if (!C) continue;
		if (bPlayerSlot)
		{
			PlayerChar = C;
			if (PC)
			{
				PC->Possess(C);
				PC->SetControlRotation(C->GetActorRotation());
			}
			if (bAutoPlay)
			{
				// in autoplay the player's pawn is driven by an extra bot brain
				ACEBotController* Auto = nullptr;
				for (ACEBotController* B : Bots) if (B && B->bAutoPilot) Auto = B;
				if (!Auto)
				{
					FActorSpawnParameters BP; BP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					Auto = GetWorld()->SpawnActor<ACEBotController>(FVector::ZeroVector, FRotator::ZeroRotator, BP);
					Auto->bAutoPilot = true; Auto->bPlayerSquad = true; Auto->SquadIndex = 0;
					Bots.Add(Auto);
				}
				if (PC) PC->UnPossess();
				Auto->Possess(C);
				Auto->OnRoundStart();
				if (PC) PC->SetViewTarget(C);
			}
		}
		else
		{
			for (ACEBotController* B : Bots) if (B && B->bPlayerSquad && !B->bAutoPilot && B->SquadIndex == i) { B->Possess(C); B->OnRoundStart(); }
		}
	}
	for (int32 i = 0; i < 5; ++i)
	{
		ACECharacter* C = SpawnOne(EnemyTeam, i, EnemyNames[i], false);
		if (!C) continue;
		for (ACEBotController* B : Bots) if (B && !B->bPlayerSquad && B->SquadIndex == i) { B->Possess(C); B->OnRoundStart(); }
	}
}

void ACEGameMode::StartRound()
{
	CleanupRound();
	++Round;
	if (Round > 1) bSquadAttacking = !bSquadAttacking;
	ReinforcePool = UCETuning::Get()->ReinforcePool;
	SpawnFixedCameras();
	SpawnTeams();
	Phase = ECEPhase::Prep;
	PhaseLeft = UCETuning::Get()->PrepTime;
	Announce(FString::Printf(TEXT("ROUND %d  -  %s"), Round, bSquadAttacking ? TEXT("ATTACK: plant the interceptor in Comms") : TEXT("DEFEND: protect Comms")), 4.f);
	if (UCESound* S = UCESound::Get(this)) S->Play2D(ECESnd::RoundStart, 0.6f);
	UE_LOG(LogColdEntry, Log, TEXT("Round %d start. Squad %s. Score %d-%d"), Round, bSquadAttacking ? TEXT("attacking") : TEXT("defending"), ScoreSquad, ScoreEnemy);
}

void ACEGameMode::BeginActionPhase()
{
	Phase = ECEPhase::Action;
	PhaseLeft = UCETuning::Get()->ActionTime;
	Announce(TEXT("ACTION PHASE"), 2.f);
	if (UCESound* S = UCESound::Get(this)) S->Play2D(ECESnd::RoundStart, 0.6f);
	// imperfect drone intel for the attacking side: roughly half the defenders were spotted in prep
	for (ACECharacter* C : Characters)
		if (C && C->Team == ECETeam::Defend && FMath::FRand() < 0.5f)
			AddIntel(ECETeam::Attack, C, C->GetActorLocation(), false, false);
	for (ACEBotController* B : Bots) if (B) B->OnActionStart();
}

void ACEGameMode::CycleSpawn()
{
	if (Phase != ECEPhase::Prep || !PlayerChar || PlayerTeam() != ECETeam::Attack) return;
	if (PhaseLeft < UCETuning::Get()->PrepTime - 12.f) { Announce(TEXT("Spawn locked"), 1.2f); return; }
	AttackSpawnChoice = (AttackSpawnChoice + 1) % Level->AttackSpawns.Num();
	int32 i = 0;
	for (ACECharacter* C : Characters)
	{
		if (!C || C->Team != ECETeam::Attack) continue;
		const bool bSquad = C == PlayerChar || C->CEName == SquadNames[1] || C->CEName == SquadNames[2] || C->CEName == SquadNames[3] || C->CEName == SquadNames[4];
		if (!bSquad) continue;
		FVector L = Level->AttackSpawns[AttackSpawnChoice][i++ % 5]; L.Z += 95.f;
		C->SetActorLocation(L, false, nullptr, ETeleportType::TeleportPhysics);
		SpawnAnchor.Add(C, L);
		if (C->GetController()) C->GetController()->SetControlRotation(FRotator(0, (Level->BuildingBox.GetCenter() - L).Rotation().Yaw, 0));
	}
	Announce(FString::Printf(TEXT("Spawn: %s"), *Level->AttackSpawnNames[AttackSpawnChoice]), 1.5f);
}

void ACEGameMode::CyclePrimary()
{
	if (Phase != ECEPhase::Prep || !PlayerChar) return;
	PlayerPrimary = PlayerPrimary == ECEWeaponId::R4 ? ECEWeaponId::SMG9 : ECEWeaponId::R4;
	const UCETuning* T = UCETuning::Get();
	PlayerChar->Weapons[0].Id = PlayerPrimary;
	PlayerChar->Weapons[0].Mag = T->Weapon(PlayerPrimary).Mag;
	PlayerChar->Weapons[0].Reserve = T->Weapon(PlayerPrimary).Reserve;
	Announce(FString::Printf(TEXT("Primary: %s"), *T->Weapon(PlayerPrimary).Name), 1.5f);
}

void ACEGameMode::EndRound(ECETeam Winner, const FString& Reason)
{
	if (Phase == ECEPhase::RoundEnd || Phase == ECEPhase::MatchEnd) return;
	const bool bSquadWon = Winner == PlayerTeam();
	if (bSquadWon) ++ScoreSquad; else ++ScoreEnemy;
	LastSummary = BuildSummary(Winner, Reason);
	Phase = ECEPhase::RoundEnd;
	PhaseLeft = UCETuning::Get()->RoundEndTime;
	if (Device) Device->bActive = false;
	if (UCESound* S = UCESound::Get(this)) S->Play2D(ECESnd::RoundEnd, 0.6f);
	UE_LOG(LogColdEntry, Log, TEXT("Round %d end: %s win (%s). Score %d-%d"), Round, Winner == ECETeam::Attack ? TEXT("Attackers") : TEXT("Defenders"), *Reason, ScoreSquad, ScoreEnemy);
}

FCERoundSummary ACEGameMode::BuildSummary(ECETeam Winner, const FString& Reason) const
{
	FCERoundSummary S;
	S.Winner = Winner; S.Reason = Reason; S.bPlayerWon = Winner == PlayerTeam();
	int32 BestKills = -1;
	TMap<FString, int32> Kills;
	for (const ACECharacter* C : Characters)
	{
		if (!C) continue;
		int32 K = 0;
		for (const FCEHitRecord& R : C->HitsDealt) if (R.bKill) ++K;
		if (K > BestKills) { BestKills = K; S.Mvp = FString::Printf(TEXT("%s (%d)"), *C->CEName, K); }
	}
	if (PlayerChar)
	{
		S.PlayerHitsTaken = PlayerChar->HitsTaken;
		S.PlayerHitsDealt = PlayerChar->HitsDealt;
		for (const FCEHitRecord& R : PlayerChar->HitsDealt) if (R.bKill) ++S.PlayerKills;
		S.bPlayerDied = !PlayerChar->IsAlive();
		if (PlayerChar->Killer.IsValid()) S.KilledBy = PlayerChar->Killer->CEName;
	}
	return S;
}

void ACEGameMode::Announce(const FString& Text, float Seconds)
{
	Banner = Text;
	BannerUntil = Now() + Seconds;
}

// =============================================================================================
// Tick & rules
// =============================================================================================
void ACEGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	if (bDebugNav) DrawNavDebug();
	if (bShotTour) TickShotTour();
	if (Phase == ECEPhase::Menu || Phase == ECEPhase::MatchEnd) return;
	PhaseLeft -= Dt;
	UpdateRules(Dt);
	UpdateCameraIntel(Dt);
}

// ---------------------------------------------------------------------------------------------
// Shot tour (-ceshots): a repeatable set of views for judging the visual pass. Prep is held so the
// teams stay put; each view waits for shaders/distance fields and exposure before it is saved.
// Add -celineup to also photograph one of every scanned prop, unrotated, outside the south wall.
// ---------------------------------------------------------------------------------------------
namespace
{
	enum class ECEShotKind { Fixed, Attacker, Defender, FirstPerson, Death };
	struct FCEShot { FString Name; ECEShotKind Kind; FVector PosM; FRotator Rot; };
	TArray<FCEShot> GShots;
	TWeakObjectPtr<ACECharacter> GShotSubject;
	FVector GShotOffset = FVector::ZeroVector;
}

bool ACEGameMode::SetupShot(int32 Idx)
{
	const FCEShot& S = GShots[Idx];
	GShotSubject.Reset();
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->GetHUD()) PC->GetHUD()->bShowHUD = S.Kind == ECEShotKind::FirstPerson;
	if (S.Kind == ECEShotKind::FirstPerson)
	{
		ViewOverride = PlayerChar.Get();
		return PlayerChar != nullptr;
	}
	FVector Pos = S.PosM * 100.f;
	FRotator Rot = S.Rot;
	if (S.Kind != ECEShotKind::Fixed)
	{
		const ECETeam Want = S.Kind == ECEShotKind::Defender ? ECETeam::Defend : ECETeam::Attack;
		ACECharacter* Subject = nullptr;
		for (ACECharacter* C : Characters) if (C && C->IsAlive() && C->Team == Want && C != PlayerChar) { Subject = C; break; }
		if (!Subject) return false;
		// three-quarter front view from the first direction with a clear line to the subject; the camera
		// then keeps that offset while the bot goes about its business
		const FVector Focus = Subject->GetActorLocation() + FVector(0, 0, 8.f);
		const float Base = Subject->GetActorRotation().Yaw;
		GShotOffset = FRotator(0, Base + 25.f, 0).Vector() * 230.f + FVector(0, 0, 12.f);
		for (float Off : { 25.f, -25.f, 60.f, -60.f, 100.f, -100.f, 150.f, -150.f, 180.f })
		{
			const FVector Try = FRotator(0, Base + Off, 0).Vector() * 230.f + FVector(0, 0, 12.f);
			if (HasLineOfSight(Focus + Try, Focus, Subject)) { GShotOffset = Try; break; }
		}
		GShotSubject = Subject;
		if (S.Kind == ECEShotKind::Death) Subject->ApplyExplosion(1000.f, nullptr, Subject->GetActorLocation() + Subject->GetActorForwardVector() * 100.f, TEXT("shot tour"));
		Pos = Focus + GShotOffset;
		Rot = (-GShotOffset).Rotation();
	}
	ShotCam->SetActorLocationAndRotation(Pos, Rot);
	ViewOverride = ShotCam.Get();
	return true;
}

void ACEGameMode::TickShotTour()
{
	const double Real = FPlatformTime::Seconds();
	if (TourStart == 0.0)
	{
		TourStart = QuietSince = Real;
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ShotCam = GetWorld()->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		ShotCam->GetCameraComponent()->bConstrainAspectRatio = false;
		ShotCam->GetCameraComponent()->SetFieldOfView(80.f);

		GShots.Reset();
		auto Add = [](const TCHAR* Name, ECEShotKind Kind, const FVector& PosM = FVector::ZeroVector, const FRotator& Rot = FRotator::ZeroRotator)
		{
			GShots.Add({ Name, Kind, PosM, Rot });
		};
		Add(TEXT("01_attacker"), ECEShotKind::Attacker);
		Add(TEXT("02_defender"), ECEShotKind::Defender);
		Add(TEXT("03_first_person"), ECEShotKind::FirstPerson);
		Add(TEXT("03a_anim_reload"), ECEShotKind::Attacker);   // the subject is put mid-animation as the picture is taken
		Add(TEXT("03b_anim_hit"), ECEShotKind::Attacker);
		Add(TEXT("03c_anim_throw"), ECEShotKind::Attacker);
		Add(TEXT("04_exterior_wide"), ECEShotKind::Fixed, FVector(-22.f, -16.f, 15.f), FRotator(-24.f, 38.f, 0.f));
		Add(TEXT("04a_forest_approach"), ECEShotKind::Fixed, FVector(-24.5f, -24.f, 3.6f), FRotator(1.f, 37.f, 0.f));
		Add(TEXT("04b_forest_perimeter"), ECEShotKind::Fixed, FVector(-24.f, 3.f, 1.7f), FRotator(4.f, 160.f, 0.f));
		Add(TEXT("05_west_yard"), ECEShotKind::Fixed, FVector(-20.f, 16.f, 1.7f), FRotator(2.f, -8.f, 0.f));
		Add(TEXT("06_loading_bay"), ECEShotKind::Fixed, FVector(1.5f, 2.2f, 1.7f), FRotator(-2.f, 62.f, 0.f));
		Add(TEXT("07_corridor"), ECEShotKind::Fixed, FVector(15.5f, 1.f, 1.7f), FRotator(0.f, 90.f, 0.f));
		Add(TEXT("08_workshop"), ECEShotKind::Fixed, FVector(37.f, 9.f, 1.7f), FRotator(-4.f, 212.f, 0.f));
		Add(TEXT("09_comms"), ECEShotKind::Fixed, FVector(30.3f, 17.3f, 1.7f), FRotator(-6.f, 218.f, 0.f));
		Add(TEXT("10_office"), ECEShotKind::Fixed, FVector(17.6f, 27.2f, 1.7f), FRotator(-8.f, -48.f, 0.f));
		Add(TEXT("11_break_room"), ECEShotKind::Fixed, FVector(24.8f, 14.8f, 5.2f), FRotator(-8.f, 55.f, 0.f));
		Add(TEXT("12_south_yard"), ECEShotKind::Fixed, FVector(27.f, -20.f, 1.7f), FRotator(3.f, 100.f, 0.f));
		// prep is released from here on, so these catch the teams on the move; the last one drops a bot to show the death
		Add(TEXT("13_attacker_moving_a"), ECEShotKind::Attacker);
		Add(TEXT("14_attacker_moving_b"), ECEShotKind::Attacker);
		Add(TEXT("15_attacker_moving_c"), ECEShotKind::Attacker);
		Add(TEXT("16_defender_moving"), ECEShotKind::Defender);
		Add(TEXT("17_death"), ECEShotKind::Death);
		if(FParse::Param(FCommandLine::Get(),TEXT("ceenvironmentshots")))
		{
			GShots.Reset();
			ShotCam->GetCameraComponent()->SetFieldOfView(68.f);
			Add(TEXT("forest_01_reference_approach"),ECEShotKind::Fixed,FVector(48,-25.5f,1.85f),FRotator(5,126,0));
			Add(TEXT("forest_02_ground_entry"),ECEShotKind::Fixed,FVector(31,-12,1.7f),FRotator(-3,104,0));
			Add(TEXT("forest_03_woodland"),ECEShotKind::Fixed,FVector(-25,0,1.7f),FRotator(3,166,0));
			Add(TEXT("forest_04_west_apron"),ECEShotKind::Fixed,FVector(-18,16,1.7f),FRotator(2,-8,0));
			Add(TEXT("forest_05_overview"),ECEShotKind::Fixed,FVector(68,-38,30),FRotator(-24,132,0));
			Add(TEXT("forest_06_comms_props"),ECEShotKind::Fixed,FVector(30.3f,17.3f,1.7f),FRotator(-6,218,0));
			Add(TEXT("forest_07_workshop_props"),ECEShotKind::Fixed,FVector(37,9,1.7f),FRotator(-4,212,0));
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("celineup")) && Level)
		{
			// the camera looks north, so stepping west keeps the list order left to right
			const TArray<FVector> At = Level->SpawnPropLineup(FVector(6000.f, -4000.f, 0.f), FVector(-220.f, 0.f, 0.f));
			for (int32 i = 0; i < At.Num(); i += 5)
			{
				const int32 Last = FMath::Min(i + 4, At.Num() - 1);
				const FVector Mid = (At[i] + At[Last]) * 0.5f;
				GShots.Add({ FString::Printf(TEXT("props_%02d_to_%02d"), i, Last), ECEShotKind::Fixed,
					FVector(Mid.X / 100.f, Mid.Y / 100.f - 7.6f, 1.5f), FRotator(-5.f, 90.f, 0.f) });
			}
		}
	}
	const bool bHoldPrep = ShotIdx < 0 || (GShots.IsValidIndex(ShotIdx) && !GShots[ShotIdx].Name.Contains(TEXT("moving")) && GShots[ShotIdx].Kind != ECEShotKind::Death);
	if (Phase == ECEPhase::Prep && bHoldPrep) PhaseLeft = FMath::Max(PhaseLeft, 8.f);
	if (GShotSubject.IsValid() && ShotCam && ViewOverride.Get() == ShotCam.Get())
	{
		const FVector Focus = GShotSubject->GetActorLocation() + FVector(0, 0, 8.f);
		if (!HasLineOfSight(Focus + GShotOffset, Focus, GShotSubject.Get()))
		{
			const float Base = GShotOffset.Rotation().Yaw;
			for (float Off : { 30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 135.f, -135.f, 180.f })
			{
				const FVector Try = FRotator(0, Base + Off, 0).Vector() * 230.f + FVector(0, 0, 12.f);
				if (HasLineOfSight(Focus + Try, Focus, GShotSubject.Get())) { GShotOffset = Try; break; }
			}
		}
		ShotCam->SetActorLocationAndRotation(Focus + GShotOffset, (-GShotOffset).Rotation());
	}
	const bool bBusy = (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
		|| (GDistanceFieldAsyncQueue && GDistanceFieldAsyncQueue->GetNumOutstandingTasks() > 0);
	if (bBusy) QuietSince = Real;
	const bool bQuiet = Real - QuietSince > 3.0;

	if (ShotIdx < 0)
	{
		// warm-up: everything compiled and the round has spawned its teams
		if (Phase != ECEPhase::Prep) return;
		if ((Real - TourStart > 15.0 && bQuiet) || Real - TourStart > 900.0)
		{
			ShotIdx = 0; bShotTaken = false; ShotStart = Real;
			if (!SetupShot(ShotIdx)) ShotStart = Real - 200.0;
		}
		return;
	}
	if (ShotIdx >= GShots.Num())
	{
		if (Real - ShotStart > 1.5) { bShotTour = false; FPlatformMisc::RequestExit(false); }
		return;
	}
	if (!bShotTaken)
	{
		// let exposure, Lumen and texture streaming settle on the new view
		if (Real - ShotStart > 100.0) { bShotTaken = true; ShotStart = Real - 10.0; }   // subject missing or never quiet: skip
		else if (Real - ShotStart > 5.0 && bQuiet)
		{
			const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Shots") / GShots[ShotIdx].Name + TEXT(".png"));
			if (GShotSubject.IsValid() && GShots[ShotIdx].Name.Contains(TEXT("_anim_")))
				GShotSubject->DebugAnim(GShots[ShotIdx].Name.Contains(TEXT("reload")) ? 0 : GShots[ShotIdx].Name.Contains(TEXT("hit")) ? 1 : 2);
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogColdEntry, Log, TEXT("[CE-Shot] %s"), *File);
			bShotTaken = true; ShotStart = Real;
		}
		return;
	}
	if (Real - ShotStart > 1.0)
	{
		++ShotIdx; bShotTaken = false; ShotStart = Real;
		if (ShotIdx < GShots.Num() && !SetupShot(ShotIdx)) ShotStart = Real - 200.0;
	}
}

void ACEGameMode::UpdateRules(float Dt)
{
	const UCETuning* T = UCETuning::Get();
	switch (Phase)
	{
	case ECEPhase::Prep:
		// attackers are held at their spawn; defenders set up inside
		for (ACECharacter* C : Characters)
		{
			if (!C || C->Team != ECETeam::Attack) continue;
			const FVector* A = SpawnAnchor.Find(C);
			if (!A) continue;
			FVector D = C->GetActorLocation() - *A; D.Z = 0;
			if (D.Size() > 700.f) C->SetActorLocation(*A + D.GetSafeNormal() * 700.f + FVector(0, 0, C->GetActorLocation().Z - A->Z), true);
		}
		if (PhaseLeft <= 0.f) BeginActionPhase();
		break;
	case ECEPhase::Action:
	case ECEPhase::Planted:
	{
		const int32 Atk = AliveCount(ECETeam::Attack), Def = AliveCount(ECETeam::Defend);
		const bool bPlanted = Device && Device->bActive;
		if (Def == 0) { EndRound(ECETeam::Attack, TEXT("Defenders eliminated")); break; }
		if (Atk == 0 && !bPlanted) { EndRound(ECETeam::Defend, TEXT("Attackers eliminated")); break; }
		if (Phase == ECEPhase::Action && PhaseLeft <= 0.f) { EndRound(ECETeam::Defend, TEXT("Time expired")); break; }
		if (Phase == ECEPhase::Planted && Device && Device->TimeLeft <= 0.f) { EndRound(ECETeam::Attack, TEXT("Interceptor completed its upload")); break; }
		// defenders who roam outside the building are revealed
		for (ACECharacter* C : Characters)
		{
			if (!C || !C->IsAlive() || C->Team != ECETeam::Defend) continue;
			float& Out = OutsideTime.FindOrAdd(C);
			if (!Level->InBuilding(C->GetActorLocation())) { Out += Dt; if (Out > 2.f) { AddIntel(ECETeam::Attack, C, C->GetActorLocation(), true, true); Out = 1.f; } }
			else Out = 0.f;
		}
		break;
	}
	case ECEPhase::RoundEnd:
		if (PhaseLeft <= 0.f)
		{
			if (ScoreSquad >= T->RoundsToWin || ScoreEnemy >= T->RoundsToWin)
			{
				Phase = ECEPhase::MatchEnd;
				UE_LOG(LogColdEntry, Log, TEXT("Match over: %d-%d"), ScoreSquad, ScoreEnemy);
				if (bAutoPlay) { StartMatchFlow(); }
			}
			else StartRound();
		}
		break;
	default: break;
	}
}

void ACEGameMode::UpdateCameraIntel(float Dt)
{
	CamIntelT -= Dt;
	if (CamIntelT > 0.f) return;
	CamIntelT = 0.25f;
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(40.f));
	for (ACEGadget* G : Gadgets)
	{
		if (!G || G->IsWrecked()) continue;
		const ACESecurityCamera* Cam = Cast<ACESecurityCamera>(G);
		const ACEDrone* Dr = Cast<ACEDrone>(G);
		if (!Cam && !Dr) continue;
		const FVector Eye = Cam ? Cam->View->GetComponentLocation() : Dr->View->GetComponentLocation();
		const FVector Fwd = Cam ? Cam->View->GetForwardVector() : Dr->View->GetForwardVector();
		for (ACECharacter* C : Characters)
		{
			if (!C || !C->IsAlive() || C->Team == G->Team) continue;
			const FVector To = C->ChestLocation() - Eye;
			const float D = To.Size();
			if (D > 2600.f || FVector::DotProduct(To / D, Fwd) < CosHalf) continue;
			if (!HasLineOfSight(Eye, C->ChestLocation(), G, C)) continue;
			// bots on the team only "notice" a camera sighting after ~1 s (someone has to be watching)
			float& Seen = CamSeenTime.FindOrAdd(C);
			Seen += 0.25f;
			if (Seen > 1.0f) AddIntel(G->Team, C, C->GetActorLocation(), true, false);
		}
	}
}

int32 ACEGameMode::AliveCount(ECETeam T) const
{
	int32 N = 0;
	for (const ACECharacter* C : Characters) if (C && C->Team == T && C->IsAlive()) ++N;
	return N;
}

bool ACEGameMode::IsMovementFrozen(const ACECharacter* C) const
{
	return Phase == ECEPhase::Menu || Phase == ECEPhase::MatchEnd || (Phase == ECEPhase::RoundEnd && PhaseLeft < UCETuning::Get()->RoundEndTime - 1.5f);
}

bool ACEGameMode::SmokeBlocks(const FVector& A, const FVector& B) const
{
	for (const ACESmoke* S : Smokes) if (S && S->Blocks(A, B)) return true;
	return false;
}

bool ACEGameMode::HasLineOfSight(const FVector& From, const FVector& To, const AActor* IgnoreA, const AActor* IgnoreB) const
{
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CELOS), false);
	if (IgnoreA) Q.AddIgnoredActor(IgnoreA);
	if (IgnoreB) Q.AddIgnoredActor(IgnoreB);
	if (GetWorld()->LineTraceTestByChannel(From, To, ECC_Visibility, Q)) return false;
	return !SmokeBlocks(From, To);
}

FString ACEGameMode::CalloutAt(const FVector& P) const { return Level ? Level->CalloutAt(P) : FString(); }

TArray<AActor*> ACEGameMode::FeedsFor(ECETeam T) const
{
	TArray<AActor*> Out;
	for (ACEGadget* G : Gadgets)
	{
		if (!G || G->IsWrecked() || G->Team != T) continue;
		if (Cast<ACESecurityCamera>(G) || Cast<ACEDrone>(G)) Out.Add(G);
	}
	return Out;
}

ACEDrone* ACEGameMode::DroneOf(const ACECharacter* C) const
{
	for (ACEGadget* G : Gadgets) if (ACEDrone* D = Cast<ACEDrone>(G)) if (!D->IsWrecked() && D->OwnerChar.Get() == C) return D;
	return nullptr;
}

// =============================================================================================
// Events
// =============================================================================================
void ACEGameMode::AddIntel(ECETeam Knower, ACECharacter* Enemy, const FVector& Pos, bool bVisual, bool bPing)
{
	if (!Enemy || Knower == ECETeam::None) return;
	FCEIntel& I = Intel[(int32)Knower].FindOrAdd(Enemy);
	I.Pos = Pos; I.Time = Now(); I.bVisual = bVisual;
	if (bPing) { I.bPinged = true; I.PingTime = Now(); }
}

void ACEGameMode::PingFromView(ECETeam Team, const FVector& From, const FVector& Dir)
{
	ACECharacter* Best = nullptr; float BestDot = FMath::Cos(FMath::DegreesToRadians(7.f));
	for (ACECharacter* C : Characters)
	{
		if (!C || !C->IsAlive() || C->Team == Team) continue;
		const FVector To = (C->ChestLocation() - From);
		const float Dot = FVector::DotProduct(To.GetSafeNormal(), Dir);
		if (Dot > BestDot && To.Size() < 4000.f && HasLineOfSight(From, C->ChestLocation(), nullptr, C)) { BestDot = Dot; Best = C; }
	}
	if (Best)
	{
		AddIntel(Team, Best, Best->GetActorLocation(), true, true);
		if (UCESound* S = UCESound::Get(this)) S->Play2D(ECESnd::PlantBeep, 0.5f);
		Announce(FString::Printf(TEXT("Enemy spotted: %s"), *CalloutAt(Best->GetActorLocation())), 1.5f);
	}
}

void ACEGameMode::ReportNoise(ACECharacter* Src, const FVector& Loc, float Radius, bool bShot)
{
	if (!Src || Phase == ECEPhase::Prep) return;
	for (ACEBotController* B : Bots)
	{
		if (!B) continue;
		ACECharacter* Me = B->Char();
		if (!Me || !Me->IsAlive() || Me->Team == Src->Team) continue;
		const float D = FVector::Dist(Me->GetActorLocation(), Loc);
		if (D > Radius) continue;
		const bool bClear = HasLineOfSight(Me->EyeLocation(), Loc + FVector(0, 0, 60), Me, Src);
		if (!bClear && D > Radius * 0.55f) continue;  // walls muffle
		B->HearNoise(Src, Loc, bShot);
	}
}

void ACEGameMode::OnHit(ACECharacter* Victim, ACECharacter* Attacker, const FCEHitRecord& Rec)
{
	if (!Victim || !Attacker) return;
	AddIntel(Victim->Team, Attacker, Attacker->GetActorLocation(), true, false);
	for (ACEBotController* B : Bots) if (B && B->Char() == Victim) B->OnDamaged(Attacker);
}

void ACEGameMode::OnKilled(ACECharacter* Victim, ACECharacter* Killer, const FCEHitRecord& Rec)
{
	FCEKillFeedEntry K;
	K.Killer = Killer ? Killer->CEName : TEXT("?");
	K.Victim = Victim ? Victim->CEName : TEXT("?");
	K.Weapon = Rec.Weapon;
	K.bHeadshot = Rec.Zone == ECEHitZone::Head && !Rec.bExplosion;
	K.bThrough = !Rec.Through.IsEmpty();
	K.KillerTeam = Killer ? Killer->Team : ECETeam::None;
	K.Time = Now();
	KillFeed.Add(K);
	if (KillFeed.Num() > 6) KillFeed.RemoveAt(0);
	UE_LOG(LogColdEntry, Log, TEXT("KILL %s -> %s [%s%s%s, %.0fm]"), *K.Killer, *K.Victim, *Rec.Weapon,
		K.bHeadshot ? TEXT(", head") : TEXT(""), K.bThrough ? *FString::Printf(TEXT(", through %s"), *Rec.Through) : TEXT(""), Rec.Distance);
}

void ACEGameMode::PlantDevice(ACECharacter* By, const FVector& Loc)
{
	if (Device || Phase != ECEPhase::Action) return;
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Device = GetWorld()->SpawnActor<ACEDevice>(Loc + FVector(0, 0, 12), FRotator(0, By ? By->GetActorRotation().Yaw : 0.f, 0), P);
	Device->TimeLeft = UCETuning::Get()->DeviceTime;
	Phase = ECEPhase::Planted;
	PhaseLeft = Device->TimeLeft;
	Announce(TEXT("INTERCEPTOR PLANTED"), 3.f);
	UE_LOG(LogColdEntry, Log, TEXT("Device planted by %s at %s"), By ? *By->CEName : TEXT("?"), *CalloutAt(Loc));
	for (ACEBotController* B : Bots) if (B) B->OnPlanted();
}

void ACEGameMode::DefuseDevice(ACECharacter* By)
{
	if (!Device || !Device->bActive) return;
	Device->bActive = false;
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Defused, Device->GetActorLocation(), 1.f);
	EndRound(ECETeam::Defend, FString::Printf(TEXT("Interceptor disabled by %s"), By ? *By->CEName : TEXT("?")));
}

void ACEGameMode::SpawnGrenade(ACECharacter* By, ECEGadget Type, const FVector& Loc, const FVector& Vel)
{
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACEGrenade* G = GetWorld()->SpawnActor<ACEGrenade>(Loc, FRotator::ZeroRotator, P);
	if (!G) return;
	G->Team = By ? By->Team : ECETeam::None;
	G->OwnerChar = By;
	G->Launch(Type, Vel);
}

void ACEGameMode::GrenadeDetonate(ACEGrenade* G)
{
	const FVector Loc = G->GetActorLocation() + FVector(0, 0, 8);
	const UCETuning* T = UCETuning::Get();
	if (G->Type == ECEGadget::Smoke)
	{
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACESmoke* S = GetWorld()->SpawnActor<ACESmoke>(Loc, FRotator::ZeroRotator, P)) Smokes.Add(S);
		return;
	}
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Flash, Loc, 1.f);
	if (ACEFx* Fx = ACEFx::Get(GetWorld())) Fx->Burst(Loc, 18, 500.f, 1.f, true, 0.25f);
	for (ACECharacter* C : Characters)
	{
		if (!C || !C->IsAlive()) continue;
		const FVector Eye = C->EyeLocation();
		const float D = FVector::Dist(Eye, Loc);
		if (D > T->FlashRadius) continue;
		if (!HasLineOfSight(Loc, Eye, G, C)) continue;
		const float Facing = FVector::DotProduct(C->AimDir(), (Loc - Eye).GetSafeNormal());
		const float F = Facing > 0.5f ? 1.f : (Facing > 0.f ? 0.65f : 0.3f);
		const float Blind = T->FlashMaxBlind * F * (1.f - 0.6f * D / T->FlashRadius);
		if (Blind > 0.4f) C->Blind(Blind);
	}
	if (G->OwnerChar.IsValid()) ReportNoise(G->OwnerChar.Get(), Loc, 3000.f, true);
}

void ACEGameMode::SpawnCharge(ACECharacter* By, ACEEntity* Target, const FVector& Loc)
{
	const FRotator Face = (By ? (By->GetActorLocation() - Loc) : FVector::ForwardVector).GetSafeNormal2D().Rotation();
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACEBreachCharge* C = GetWorld()->SpawnActor<ACEBreachCharge>(Loc, Face, P);
	if (!C) return;
	C->Team = By ? By->Team : ECETeam::None;
	C->OwnerChar = By;
	C->Target = Target;
	Gadgets.Add(C);
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Deploy, Loc, 0.9f);
}

void ACEGameMode::DetonateCharges(ACECharacter* By)
{
	for (ACEGadget* G : Gadgets)
		if (ACEBreachCharge* C = Cast<ACEBreachCharge>(G))
			if (!C->IsWrecked() && C->OwnerChar.Get() == By) C->Arm();
}

void ACEGameMode::ExplodeCharge(ACEBreachCharge* C)
{
	const UCETuning* T = UCETuning::Get();
	const FVector Loc = C->GetActorLocation();
	ACECharacter* By = C->OwnerChar.Get();
	ACEEntity* Target = C->Target.Get();
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Breach, Loc, 1.f);
	if (ACEFx* Fx = ACEFx::Get(GetWorld()))
	{
		Fx->Burst(Loc, 30, 700.f, 1.2f, true, 0.35f);
		Fx->Burst(Loc, 30, 300.f, 25.f, false, 2.5f);
	}
	bool bBreached = false;
	if (Target && Target->CanBeBreached())
	{
		Target->Breach(By);
		bBreached = true;
		if (ACEFx* Fx = ACEFx::Get(GetWorld())) Fx->Decal(Loc, C->GetActorForwardVector(), 120.f, true);
	}
	for (ACECharacter* Ch : Characters)
	{
		if (!Ch || !Ch->IsAlive()) continue;
		const float D = FVector::Dist(Ch->ChestLocation(), Loc);
		if (D > T->BreachRadius) continue;
		if (D > 120.f && !HasLineOfSight(Loc, Ch->ChestLocation(), C, Ch)) continue;
		const float Dmg = T->BreachDamage * FMath::Pow(1.f - D / T->BreachRadius, 0.8f);
		if (Dmg > 1.f) Ch->ApplyExplosion(Dmg, By, Loc, TEXT("Breach charge"));
	}
	for (ACEGadget* G : Gadgets) if (G && G != C && !G->IsWrecked() && FVector::Dist(G->GetActorLocation(), Loc) < 220.f) G->Wreck();
	if (By) ReportNoise(By, Loc, 6000.f, true);
	if (!bBreached && Target) Announce(TEXT("Charge failed: wall reinforced"), 1.5f);
	C->Destroy();
}

ACESecurityCamera* ACEGameMode::SpawnCamera(ACECharacter* By, const FVector& Loc, const FRotator& Rot)
{
	ACESecurityCamera* C = GetWorld()->SpawnActorDeferred<ACESecurityCamera>(ACESecurityCamera::StaticClass(), FTransform(Rot, Loc));
	C->Team = By ? By->Team : ECETeam::Defend;
	C->OwnerChar = By;
	C->Callout = CalloutAt(Loc);
	UGameplayStatics::FinishSpawningActor(C, FTransform(Rot, Loc));
	Gadgets.Add(C);
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Deploy, Loc, 0.8f);
	return C;
}

ACEAlarm* ACEGameMode::SpawnAlarm(ACECharacter* By, const FVector& Loc, const FVector& Normal)
{
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FRotator Rot = Normal.Rotation(); Rot.Pitch -= 90.f;
	ACEAlarm* A = GetWorld()->SpawnActor<ACEAlarm>(Loc, Rot, P);
	if (!A) return nullptr;
	A->Team = By ? By->Team : ECETeam::Defend;
	A->OwnerChar = By;
	Gadgets.Add(A);
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Deploy, Loc, 0.8f);
	return A;
}

ACEDrone* ACEGameMode::SpawnDrone(ACECharacter* By)
{
	if (!By) return nullptr;
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector Loc = By->GetActorLocation() + By->GetActorForwardVector() * 70.f - FVector(0, 0, 60.f);
	ACEDrone* D = GetWorld()->SpawnActor<ACEDrone>(Loc, FRotator(0, By->GetActorRotation().Yaw, 0), P);
	if (!D) return nullptr;
	D->Team = By->Team;
	D->OwnerChar = By;
	D->Yaw = By->GetActorRotation().Yaw;
	Gadgets.Add(D);
	By->GadgetCount[(int32)ECEGadget::Drone]--;
	if (UCESound* S = UCESound::Get(this)) S->Play(ECESnd::Throw, Loc, 0.8f);
	return D;
}

void ACEGameMode::DrawNavDebug()
{
	if (!Level) return;
	for (const FCENavEdge& E : Level->Edges)
	{
		bool bBlocked = false;
		for (const TWeakObjectPtr<ACEEntity>& B : E.Blockers) if (B.IsValid() && B->BlocksMovement()) bBlocked = true;
		DrawDebugLine(GetWorld(), Level->Nodes[E.A].Pos + FVector(0, 0, 30), Level->Nodes[E.B].Pos + FVector(0, 0, 30), bBlocked ? FColor::Red : FColor::Green, false, -1.f, 0, 2.f);
	}
	for (const FCENavNode& N : Level->Nodes) DrawDebugSphere(GetWorld(), N.Pos + FVector(0, 0, 30), 15.f, 6, N.Edges.Num() ? FColor::Cyan : FColor::Magenta, false, -1.f);
}
