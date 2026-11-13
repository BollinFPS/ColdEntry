#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CETypes.h"
#include "CEHUD.generated.h"

class ACECharacter;
class ACEGameMode;
class ACEPlayerController;

/** Restrained combat HUD drawn with Canvas (no UMG assets needed). */
UCLASS()
class COLDENTRY_API ACEHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
private:
	void Txt(const FString& S, float X, float Y, const FLinearColor& C, float Scale = 1.f, bool bCenter = false, bool bLarge = false, bool bShadow = true);
	void Box(float X, float Y, float BW, float BH, const FLinearColor& C) { DrawRect(C, X, Y, BW, BH); }
	void Diamond(float X, float Y, float R, const FLinearColor& C);
	void DrawMenu(ACEGameMode* G);
	void DrawTop(ACEGameMode* G);
	void DrawVitals(ACEGameMode* G, ACECharacter* C);
	void DrawCrosshair(ACEGameMode* G, ACECharacter* C);
	void DrawMarkers(ACEGameMode* G, ACECharacter* C, ECETeam Team);
	void DrawKillFeed(ACEGameMode* G);
	void DrawDeathCard(ACEGameMode* G, ACEPlayerController* PC);
	void DrawRoundEnd(ACEGameMode* G);
	void DrawScores(ACEGameMode* G);
	void DrawSettings();
	FString Hit(const struct FCEHitRecord& R, bool bTaken) const;
	float S = 1.f; // ui scale
	float W = 1920.f, H = 1080.f;
};
