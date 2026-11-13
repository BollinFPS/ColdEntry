#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CETypes.h"
#include "CEMap.generated.h"

class ACEEntity;
class ACEDoor;
class ACEBreakable;
class UInstancedStaticMeshComponent;
class UPointLightComponent;

UENUM()
enum class ECECol : uint8 { Solid, PawnOnly, None, HiddenPawn };

UENUM()
enum class ECEOpen : uint8 { Open, Door, Breach, Glass, Barricade };

struct FCEOpening
{
	float A, B, Bot, Top;   // along-wall range and vertical range, metres
	ECEOpen Kind;
};

struct FCEZone
{
	FString Name;
	FBox Box;
	ECEReverb Reverb = ECEReverb::Office;
	bool bIndoor = true;
};

struct FCENavEdge
{
	int32 A = 0, B = 0;
	float Len = 0.f;
	TArray<TWeakObjectPtr<ACEEntity>> Blockers;
};

struct FCENavNode
{
	FVector Pos;
	TArray<int32> Edges; // indices into Edges
	int32 Zone = -1;
};

struct FCEHold
{
	FVector Pos;
	FVector LookAt;
	bool bCrouch = false;
};

struct FCEPlacement
{
	FVector Pos;
	FRotator Rot;
};

/**
 * Builds the vertical-slice map "Kessler Annex" — a weathered two-floor industrial security facility —
 * entirely from code: graybox geometry (engine cubes + procedural materials), destructible entities,
 * lighting, sky, post-process, callout zones, the bot navigation graph and spawns.
 */
UCLASS()
class COLDENTRY_API ACELevelBuilder : public AActor
{
	GENERATED_BODY()
public:
	ACELevelBuilder();
	void Build();
	virtual void Tick(float DeltaTime) override;

	// ------------- queries -------------
	int32 ZoneAt(const FVector& P) const;
	FString CalloutAt(const FVector& P) const;
	ECEReverb ReverbAt(const FVector& P) const;
	bool InObjective(const FVector& P) const { return ObjectiveBox.IsInsideOrOn(P); }
	bool InBuilding(const FVector& P) const { return BuildingBox.IsInsideOrOn(P); }
	ECEPen PenOf(const UPrimitiveComponent* C) const;
	ECEStep StepOf(const UPrimitiveComponent* C) const;
	void ResetEntities();
	int32 NearestNode(const FVector& P, bool bRequireSight = true) const;
	// EdgeCost returns < 0 for impassable.
	bool FindPath(int32 From, int32 To, TFunctionRef<float(const FCENavEdge&)> EdgeCost, TArray<int32>& OutNodes) const;
	const FCENavEdge* EdgeBetween(int32 A, int32 B) const;

	TArray<FCENavNode> Nodes;
	TArray<FCENavEdge> Edges;
	TArray<FCEZone> Zones;
	TArray<TArray<FVector>> AttackSpawns;   // per spawn site
	TArray<FString> AttackSpawnNames;
	TArray<FVector> DefendSpawns;
	TArray<FVector> EntryPoints;            // outside approach points for attacker bots
	TArray<FString> EntryNames;
	TArray<FCEHold> DefendHolds;
	TArray<FCEHold> PlantHolds;
	TArray<FVector> AlarmSpots;
	TArray<FCEPlacement> CameraSpots;       // defender deployable camera spots for bots
	TArray<FCEPlacement> FixedCameras;      // pre-installed security cameras
	TArray<FVector> PlantSpots;
	FBox ObjectiveBox;
	FBox BuildingBox;
	FVector ObjectiveCentre;

	UPROPERTY() TArray<TObjectPtr<ACEEntity>> Entities;
	UPROPERTY() TArray<TObjectPtr<ACEBreakable>> BreachWalls;
	UPROPERTY() TArray<TObjectPtr<ACEBreakable>> Barricades;
	UPROPERTY() TArray<TObjectPtr<ACEDoor>> Doors;

private:
	FVector M(float X, float Y, float Z) const { return FVector(X * 100.f, Y * 100.f, Z * 100.f); }
	void Box(float X0, float Y0, float Z0, float X1, float Y1, float Z1, ECESurf S, ECECol C = ECECol::Solid);
	void WallX(float X, float Y0, float Y1, float Z0, float Z1, float T, ECESurf S, TArray<FCEOpening> Ops);
	void WallY(float Y, float X0, float X1, float Z0, float Z1, float T, ECESurf S, TArray<FCEOpening> Ops);
	void Opening(bool bAlongY, float Fixed, float T, const FCEOpening& O);
	void Rail(float X0, float Y0, float X1, float Y1, float Z);
	void Stairs(float X0, float Y0, float Y1, float Dir, int32 Steps, ECESurf S); // along X
	void Light(float X, float Y, float Z, float Candela, float RadiusM, float TempK, float LengthM, bool bFixture, bool bShadows = true);
	void Prop(const TCHAR* Key, float X, float Y, float Z, float Yaw, ECESurf Surf, float Scale = 1.f);
	void BuildProps();
	void Zone(const TCHAR* Name, float X0, float Y0, float Z0, float X1, float Y1, float Z1, ECEReverb R, bool bIndoor = true);
	int32 Node(float X, float Y, float Z);
	void Link(int32 A, int32 B);
	void BuildGeometry();
	void BuildLighting();
	void BuildGameplayData();
	void BuildNav();
	UInstancedStaticMeshComponent* GetISM(ECESurf S, ECECol C);

	TMap<int32, TObjectPtr<UInstancedStaticMeshComponent>> ISMs;
	TMap<const UPrimitiveComponent*, ECESurf> CompSurf;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lights;
	TObjectPtr<UPointLightComponent> FlickerLight;
	float FlickerT = 0.f;
	TArray<TPair<int32, int32>> ForcedLinks;
};
