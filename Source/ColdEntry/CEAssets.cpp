#include "CEAssets.h"
#include "ColdEntry.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#endif

UCEAssets* UCEAssets::Get(const UObject* WorldContext)
{
	if (!WorldContext) return nullptr;
	const UWorld* World = WorldContext->GetWorld();
	if (!World || !World->GetGameInstance()) return nullptr;
	return World->GetGameInstance()->GetSubsystem<UCEAssets>();
}

void UCEAssets::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Fallback = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BuildInfos();
	BuildMasters();

	SurfMats.SetNum((int32)ECESurf::Count);
	for (int32 i = 0; i < (int32)ECESurf::Count; ++i)
	{
		const ECESurf S = (ECESurf)i;
		if (S == ECESurf::Glass) SurfMats[i] = MakeTranslucent(FLinearColor(0.55f, 0.62f, 0.6f), 0.16f);
		else if (S == ECESurf::Emissive) SurfMats[i] = Emissive(FLinearColor(1.f, 0.97f, 0.9f), 12.f);
		else if (S == ECESurf::RedDot) SurfMats[i] = Emissive(FLinearColor(1.f, 0.05f, 0.02f), 40.f);
		else SurfMats[i] = MakeOpaque(Infos[i]);
	}
	// Photoreal pass: scanned PBR material instances imported by Tools/ue_import_photoreal.py override the procedural ones.
	static const TCHAR* SurfNames[] = { TEXT("Concrete"), TEXT("Block"), TEXT("Floor"), TEXT("Drywall"), TEXT("Wood"), TEXT("Metal"), TEXT("Steel"),
		TEXT("Glass"), TEXT("Grate"), TEXT("Crate"), TEXT("Asphalt"), TEXT("Ceiling"), TEXT("Hazard"), TEXT("Emissive"), TEXT("Tan"), TEXT("Navy"),
		TEXT("Olive"), TEXT("Charcoal"), TEXT("Gunmetal"), TEXT("Polymer"), TEXT("Skin"), TEXT("RedDot"), TEXT("Rubber"), TEXT("Paint") };
	static_assert(UE_ARRAY_COUNT(SurfNames) == (int32)ECESurf::Count, "SurfNames must match ECESurf");
	PhotorealCount = 0;
	for (int32 i = 0; i < (int32)ECESurf::Count; ++i)
	{
		const FString Path = FString::Printf(TEXT("/Game/Photoreal/Materials/MI_%s.MI_%s"), SurfNames[i], SurfNames[i]);
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			SurfMats[i] = M;
			++PhotorealCount;
		}
	}
	UE_LOG(LogColdEntry, Log, TEXT("CEAssets ready. Procedural masters: %s. Photoreal surfaces: %d"), MasterOpaque ? TEXT("yes") : TEXT("no (fallback)"), PhotorealCount);
}

void UCEAssets::BuildInfos()
{
	Infos.SetNum((int32)ECESurf::Count);
	auto Set = [this](ECESurf S, FLinearColor C, float R, float M, float Var, ECEPen Pen, ECEStep Step)
	{
		FCESurfInfo& I = Infos[(int32)S];
		I.Color = C; I.Roughness = R; I.Metallic = M; I.Variation = Var; I.Pen = Pen; I.Step = Step;
	};
	Set(ECESurf::Concrete, FLinearColor(0.30f, 0.29f, 0.27f), 0.88f, 0.f, 0.38f, ECEPen::Solid, ECEStep::Concrete);
	Set(ECESurf::Block,    FLinearColor(0.40f, 0.41f, 0.39f), 0.82f, 0.f, 0.30f, ECEPen::Solid, ECEStep::Concrete);
	Set(ECESurf::Floor,    FLinearColor(0.19f, 0.20f, 0.19f), 0.58f, 0.f, 0.42f, ECEPen::Solid, ECEStep::Concrete);
	Set(ECESurf::Drywall,  FLinearColor(0.56f, 0.54f, 0.49f), 0.86f, 0.f, 0.18f, ECEPen::Drywall, ECEStep::Concrete);
	Set(ECESurf::Wood,     FLinearColor(0.30f, 0.19f, 0.10f), 0.72f, 0.f, 0.30f, ECEPen::Wood, ECEStep::Wood);
	Set(ECESurf::Metal,    FLinearColor(0.15f, 0.18f, 0.20f), 0.52f, 0.55f, 0.36f, ECEPen::Sheet, ECEStep::Metal);
	Set(ECESurf::Steel,    FLinearColor(0.22f, 0.22f, 0.21f), 0.42f, 1.0f, 0.30f, ECEPen::Solid, ECEStep::Metal);
	Set(ECESurf::Glass,    FLinearColor(0.55f, 0.62f, 0.60f), 0.05f, 0.f, 0.f, ECEPen::Glass, ECEStep::Tile);
	Set(ECESurf::Grate,    FLinearColor(0.11f, 0.11f, 0.10f), 0.55f, 0.8f, 0.3f, ECEPen::Solid, ECEStep::Metal);
	Set(ECESurf::Crate,    FLinearColor(0.36f, 0.27f, 0.15f), 0.82f, 0.f, 0.32f, ECEPen::Crate, ECEStep::Wood);
	Set(ECESurf::Asphalt,  FLinearColor(0.07f, 0.07f, 0.068f), 0.92f, 0.f, 0.45f, ECEPen::Solid, ECEStep::Gravel);
	Set(ECESurf::Ceiling,  FLinearColor(0.48f, 0.48f, 0.46f), 0.92f, 0.f, 0.12f, ECEPen::Solid, ECEStep::Concrete);
	Set(ECESurf::Hazard,   FLinearColor(0.55f, 0.38f, 0.02f), 0.62f, 0.f, 0.42f, ECEPen::Solid, ECEStep::Concrete);
	Set(ECESurf::Emissive, FLinearColor::White, 1.f, 0.f, 0.f, ECEPen::Solid, ECEStep::Metal);
	Set(ECESurf::Tan,      FLinearColor(0.34f, 0.26f, 0.16f), 0.92f, 0.f, 0.18f, ECEPen::None, ECEStep::Concrete);
	Set(ECESurf::Navy,     FLinearColor(0.07f, 0.09f, 0.14f), 0.9f, 0.f, 0.15f, ECEPen::None, ECEStep::Concrete);
	Set(ECESurf::Olive,    FLinearColor(0.15f, 0.17f, 0.09f), 0.9f, 0.f, 0.18f, ECEPen::None, ECEStep::Concrete);
	Set(ECESurf::Charcoal, FLinearColor(0.055f, 0.055f, 0.055f), 0.7f, 0.f, 0.15f, ECEPen::None, ECEStep::Concrete);
	Set(ECESurf::Gunmetal, FLinearColor(0.035f, 0.036f, 0.04f), 0.42f, 0.9f, 0.25f, ECEPen::Solid, ECEStep::Metal);
	Set(ECESurf::Polymer,  FLinearColor(0.028f, 0.028f, 0.026f), 0.62f, 0.f, 0.12f, ECEPen::Solid, ECEStep::Metal);
	Set(ECESurf::Skin,     FLinearColor(0.42f, 0.28f, 0.2f), 0.6f, 0.f, 0.1f, ECEPen::None, ECEStep::Concrete);
	Set(ECESurf::RedDot,   FLinearColor::Red, 1.f, 0.f, 0.f, ECEPen::None, ECEStep::Metal);
	Set(ECESurf::Rubber,   FLinearColor(0.02f, 0.02f, 0.02f), 0.9f, 0.f, 0.1f, ECEPen::Solid, ECEStep::Concrete);
	Set(ECESurf::Paint,    FLinearColor(0.33f, 0.34f, 0.32f), 0.56f, 0.3f, 0.34f, ECEPen::Sheet, ECEStep::Metal);
}

#if WITH_EDITOR
template <typename T>
static T* AddExpr(UMaterial* M)
{
	T* E = NewObject<T>(M);
	M->GetExpressionCollection().AddExpression(E);
	return E;
}
#endif

void UCEAssets::BuildMasters()
{
#if WITH_EDITOR
	// ---------- Opaque lit master: Color x two-scale world noise, roughness tied to grime ----------
	{
		UMaterial* M = NewObject<UMaterial>(this, TEXT("CE_MasterOpaque"));
		M->bUsedWithInstancedStaticMeshes = true;
		UMaterialEditorOnlyData* ED = M->GetEditorOnlyData();

		auto* Color = AddExpr<UMaterialExpressionVectorParameter>(M);
		Color->ParameterName = TEXT("Color"); Color->DefaultValue = FLinearColor(0.5f, 0.5f, 0.5f);
		auto* Rough = AddExpr<UMaterialExpressionScalarParameter>(M);
		Rough->ParameterName = TEXT("Roughness"); Rough->DefaultValue = 0.7f;
		auto* Metal = AddExpr<UMaterialExpressionScalarParameter>(M);
		Metal->ParameterName = TEXT("Metallic"); Metal->DefaultValue = 0.f;
		auto* Var = AddExpr<UMaterialExpressionScalarParameter>(M);
		Var->ParameterName = TEXT("Variation"); Var->DefaultValue = 0.25f;

		auto* N1 = AddExpr<UMaterialExpressionNoise>(M);   // large blotches: water stains, grime, sun fade
		N1->Scale = 0.006f; N1->Levels = 4; N1->OutputMin = 0.f; N1->OutputMax = 1.f; N1->bTurbulence = false;
		auto* N2 = AddExpr<UMaterialExpressionNoise>(M);   // fine grain: aggregate, scuffs
		N2->Scale = 0.11f; N2->Levels = 2; N2->OutputMin = 0.f; N2->OutputMax = 1.f; N2->bTurbulence = true;

		auto* OneMinusVar = AddExpr<UMaterialExpressionOneMinus>(M);
		OneMinusVar->Input.Connect(0, Var);
		auto* Blotch = AddExpr<UMaterialExpressionLinearInterpolate>(M);
		Blotch->A.Connect(0, OneMinusVar); Blotch->ConstB = 1.06f; Blotch->Alpha.Connect(0, N1);
		auto* Grain = AddExpr<UMaterialExpressionLinearInterpolate>(M);
		Grain->ConstA = 0.88f; Grain->ConstB = 1.05f; Grain->Alpha.Connect(0, N2);
		auto* Mul1 = AddExpr<UMaterialExpressionMultiply>(M);
		Mul1->A.Connect(0, Color); Mul1->B.Connect(0, Blotch);
		auto* Mul2 = AddExpr<UMaterialExpressionMultiply>(M);
		Mul2->A.Connect(0, Mul1); Mul2->B.Connect(0, Grain);
		ED->BaseColor.Connect(0, Mul2);

		auto* RoughVar = AddExpr<UMaterialExpressionLinearInterpolate>(M);
		RoughVar->ConstA = 1.12f; RoughVar->ConstB = 0.86f; RoughVar->Alpha.Connect(0, N1);
		auto* RoughMul = AddExpr<UMaterialExpressionMultiply>(M);
		RoughMul->A.Connect(0, Rough); RoughMul->B.Connect(0, RoughVar);
		ED->Roughness.Connect(0, RoughMul);
		ED->Metallic.Connect(0, Metal);

		M->PreEditChange(nullptr);
		M->PostEditChange();
		MasterOpaque = M;
	}
	// ---------- Translucent lit (glass, smoke) ----------
	auto MakeTransMaster = [this](const TCHAR* Name, bool bLit) -> UMaterial*
	{
		UMaterial* M = NewObject<UMaterial>(this, Name);
		M->BlendMode = BLEND_Translucent;
		M->bUsedWithInstancedStaticMeshes = true;
		if (!bLit) M->SetShadingModel(MSM_Unlit);
		else M->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
		UMaterialEditorOnlyData* ED = M->GetEditorOnlyData();
		auto* Color = AddExpr<UMaterialExpressionVectorParameter>(M);
		Color->ParameterName = TEXT("Color"); Color->DefaultValue = FLinearColor(0.6f, 0.6f, 0.6f);
		auto* Op = AddExpr<UMaterialExpressionScalarParameter>(M);
		Op->ParameterName = TEXT("Opacity"); Op->DefaultValue = 0.3f;
		if (bLit)
		{
			ED->BaseColor.Connect(0, Color);
			auto* Rough = AddExpr<UMaterialExpressionScalarParameter>(M);
			Rough->ParameterName = TEXT("Roughness"); Rough->DefaultValue = 0.1f;
			ED->Roughness.Connect(0, Rough);
		}
		else
		{
			ED->EmissiveColor.Connect(0, Color);
		}
		ED->Opacity.Connect(0, Op);
		M->PreEditChange(nullptr);
		M->PostEditChange();
		return M;
	};
	MasterTranslucent = MakeTransMaster(TEXT("CE_MasterTranslucent"), true);
	MasterTranslucentUnlit = MakeTransMaster(TEXT("CE_MasterTranslucentUnlit"), false);

	// ---------- Emissive unlit (light fixtures, red dot, LEDs, muzzle flash) ----------
	{
		UMaterial* M = NewObject<UMaterial>(this, TEXT("CE_MasterEmissive"));
		M->SetShadingModel(MSM_Unlit);
		M->bUsedWithInstancedStaticMeshes = true;
		UMaterialEditorOnlyData* ED = M->GetEditorOnlyData();
		auto* Color = AddExpr<UMaterialExpressionVectorParameter>(M);
		Color->ParameterName = TEXT("Color"); Color->DefaultValue = FLinearColor::White;
		auto* Int = AddExpr<UMaterialExpressionScalarParameter>(M);
		Int->ParameterName = TEXT("Intensity"); Int->DefaultValue = 5.f;
		auto* Mul = AddExpr<UMaterialExpressionMultiply>(M);
		Mul->A.Connect(0, Color); Mul->B.Connect(0, Int);
		ED->EmissiveColor.Connect(0, Mul);
		M->PreEditChange(nullptr);
		M->PostEditChange();
		MasterEmissive = M;
	}
	// ---------- Deferred decal: soft round mark (bullet holes, scorch) ----------
	{
		UMaterial* M = NewObject<UMaterial>(this, TEXT("CE_MasterDecal"));
		M->MaterialDomain = MD_DeferredDecal;
		M->BlendMode = BLEND_Translucent;
		UMaterialEditorOnlyData* ED = M->GetEditorOnlyData();
		auto* Color = AddExpr<UMaterialExpressionVectorParameter>(M);
		Color->ParameterName = TEXT("Color"); Color->DefaultValue = FLinearColor(0.02f, 0.02f, 0.02f);
		auto* Hard = AddExpr<UMaterialExpressionScalarParameter>(M);
		Hard->ParameterName = TEXT("Hardness"); Hard->DefaultValue = 2.2f;
		auto* UV = AddExpr<UMaterialExpressionTextureCoordinate>(M);
		auto* Centre = AddExpr<UMaterialExpressionConstant2Vector>(M);
		Centre->R = 0.5f; Centre->G = 0.5f;
		auto* Dist = AddExpr<UMaterialExpressionDistance>(M);
		Dist->A.Connect(0, UV); Dist->B.Connect(0, Centre);
		auto* Scaled = AddExpr<UMaterialExpressionMultiply>(M);
		Scaled->A.Connect(0, Dist); Scaled->B.Connect(0, Hard);
		auto* Inv = AddExpr<UMaterialExpressionOneMinus>(M);
		Inv->Input.Connect(0, Scaled);
		auto* Sat = AddExpr<UMaterialExpressionSaturate>(M);
		Sat->Input.Connect(0, Inv);
		ED->BaseColor.Connect(0, Color);
		ED->Opacity.Connect(0, Sat);
		M->PreEditChange(nullptr);
		M->PostEditChange();
		MasterDecal = M;
	}
#endif
}

UMaterialInstanceDynamic* UCEAssets::MakeOpaque(const FCESurfInfo& I)
{
	UMaterialInterface* Parent = MasterOpaque ? (UMaterialInterface*)MasterOpaque : Fallback.Get();
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
	MID->SetVectorParameterValue(TEXT("Color"), I.Color);
	MID->SetScalarParameterValue(TEXT("Roughness"), I.Roughness);
	MID->SetScalarParameterValue(TEXT("Metallic"), I.Metallic);
	MID->SetScalarParameterValue(TEXT("Variation"), I.Variation);
	return MID;
}

UMaterialInterface* UCEAssets::Surf(ECESurf S)
{
	const int32 Idx = (int32)S;
	return SurfMats.IsValidIndex(Idx) ? SurfMats[Idx].Get() : Fallback.Get();
}

UMaterialInterface* UCEAssets::Emissive(const FLinearColor& Color, float Intensity)
{
	const uint32 Key = HashCombine(GetTypeHash(Color.ToFColor(false)), GetTypeHash(FMath::RoundToInt(Intensity * 10.f)));
	if (TObjectPtr<UMaterialInterface>* Found = EmissiveCache.Find(Key)) return Found->Get();
	UMaterialInstanceDynamic* MID;
	if (MasterEmissive)
	{
		MID = UMaterialInstanceDynamic::Create(MasterEmissive, this);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	}
	else
	{
		MID = UMaterialInstanceDynamic::Create(Fallback, this);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
	EmissiveCache.Add(Key, MID);
	return MID;
}

UMaterialInstanceDynamic* UCEAssets::MakeTranslucent(const FLinearColor& Color, float Opacity, bool bLit)
{
	UMaterial* Master = bLit ? MasterTranslucent.Get() : MasterTranslucentUnlit.Get();
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Master ? (UMaterialInterface*)Master : Fallback.Get(), this);
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	return MID;
}

UMaterialInterface* UCEAssets::DecalMaterial(bool bScorch)
{
	if (!MasterDecal) return nullptr;
	if (DecalMats.Num() == 0)
	{
		UMaterialInstanceDynamic* Hole = UMaterialInstanceDynamic::Create(MasterDecal, this);
		Hole->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.012f, 0.011f, 0.01f));
		Hole->SetScalarParameterValue(TEXT("Hardness"), 2.6f);
		UMaterialInstanceDynamic* Scorch = UMaterialInstanceDynamic::Create(MasterDecal, this);
		Scorch->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.02f, 0.018f, 0.016f));
		Scorch->SetScalarParameterValue(TEXT("Hardness"), 1.6f);
		DecalMats.Add(Hole);
		DecalMats.Add(Scorch);
	}
	return DecalMats[bScorch ? 1 : 0];
}
