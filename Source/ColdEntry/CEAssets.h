#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CETypes.h"
#include "CEAssets.generated.h"

class UStaticMesh;
class UMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;

struct FCESurfInfo
{
	FLinearColor Color;
	float Roughness = 0.7f;
	float Metallic = 0.f;
	float Variation = 0.25f;   // strength of procedural mottling (grime / wear)
	ECEPen Pen = ECEPen::Solid;
	ECEStep Step = ECEStep::Concrete;
};

/**
 * Runtime asset library: engine basic meshes + procedurally built materials.
 * PLACEHOLDER ART: these materials exist so the graybox reads as physical surfaces
 * (concrete, drywall, steel...). They are NOT the final visual target.
 */
UCLASS()
class COLDENTRY_API UCEAssets : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	static UCEAssets* Get(const UObject* WorldContext);

	UPROPERTY() TObjectPtr<UStaticMesh> Cube;
	UPROPERTY() TObjectPtr<UStaticMesh> Sphere;
	UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;

	UMaterialInterface* Surf(ECESurf S);
	// Procedural variant for parts that move (characters, gadgets): the scanned surfaces are projected in
	// world space, so on a moving part the texture would slide across it.
	UMaterialInterface* SurfMoving(ECESurf S);
	UMaterialInterface* WeaponMaterial(bool bPistol);   // dark finish for the template weapons; null if unavailable
	UMaterialInterface* Emissive(const FLinearColor& Color, float Intensity);
	UMaterialInstanceDynamic* MakeTranslucent(const FLinearColor& Color, float Opacity, bool bLit = true);
	UMaterialInterface* DecalMaterial(bool bScorch);
	const FCESurfInfo& Info(ECESurf S) const { return Infos[(int32)S]; }
	bool HasTranslucency() const { return MasterTranslucent != nullptr; }
	int32 PhotorealCount = 0; // surfaces using imported scanned materials

private:
	void BuildInfos();
	void BuildMasters();
	UMaterialInstanceDynamic* MakeOpaque(const FCESurfInfo& I);

	TArray<FCESurfInfo> Infos;
	UPROPERTY() TObjectPtr<UMaterial> MasterOpaque;
	UPROPERTY() TObjectPtr<UMaterial> MasterTranslucent;
	UPROPERTY() TObjectPtr<UMaterial> MasterTranslucentUnlit;
	UPROPERTY() TObjectPtr<UMaterial> MasterEmissive;
	UPROPERTY() TObjectPtr<UMaterial> MasterDecal;
	UPROPERTY() TObjectPtr<UMaterialInterface> Fallback;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> SurfMats;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> ProcMats;
	UPROPERTY() TObjectPtr<UMaterialInterface> WeaponMats[2];
	UPROPERTY() TMap<uint32, TObjectPtr<UMaterialInterface>> EmissiveCache;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> DecalMats;
};
