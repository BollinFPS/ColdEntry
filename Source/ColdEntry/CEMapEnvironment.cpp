#include "CEMap.h"
#include "CEAssets.h"
#include "CETuning.h"
#include "ColdEntry.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace CEForest
{
	// Same metre-space surface as Tools/prepare_forest.py: flat tactical yard, soft hills beyond.
	float Height(float X, float Y)
	{
		const float DX = FMath::Max(FMath::Abs(X - 20.f) - 53.f, 0.f);
		const float DY = FMath::Max(FMath::Abs(Y - 14.f) - 45.f, 0.f);
		float Fade = FMath::Clamp(FMath::Sqrt(DX * DX + DY * DY) / 28.f, 0.f, 1.f);
		Fade = Fade * Fade * (3.f - 2.f * Fade);
		return -0.12f + Fade * (2.8f + 2.1f * FMath::Sin(X * .031f + .7f) * FMath::Cos(Y * .027f)
			+ 1.1f * FMath::Sin(X * .077f + Y * .041f));
	}
	bool InYard(float X, float Y) { return X > -30.f && X < 70.f && Y > -28.f && Y < 56.f; }
	float Ground(float X,float Y)
	{
		if(InYard(X,Y)) return -.02f;
		// Sample the actual four-metre triangles, not their smooth source function:
		// this removes the small air gaps that appeared on sloped forest terrain.
		const float X0=FMath::FloorToFloat((X+220.f)/4.f)*4.f-220.f;
		const float Y0=FMath::FloorToFloat((Y+226.f)/4.f)*4.f-226.f;
		const float U=(X-X0)/4.f,V=(Y-Y0)/4.f;
		return V<=U ? Height(X0,Y0)*(1-U)+Height(X0+4,Y0)*(U-V)+Height(X0+4,Y0+4)*V
			: Height(X0,Y0)*(1-V)+Height(X0+4,Y0+4)*U+Height(X0,Y0+4)*(V-U);
	}
	UStaticMesh* Load(const FString& Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Forest/Meshes/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

void ACELevelBuilder::BuildEnvironment()
{
	if (!UCETuning::Get()->bForestEnvironment) return;
	FRandomStream Random(UCETuning::Get()->ForestSeed);
	const float Density = FMath::Clamp(UCETuning::Get()->ForestDensity, .25f, 1.5f);
	TMap<FString, UHierarchicalInstancedStaticMeshComponent*> Groups;
	int32 Trees = 0, Plants = 0, Rocks = 0;
	auto Group = [&](const FString& Name, bool bShadow = true) -> UHierarchicalInstancedStaticMeshComponent*
	{
		if (auto** Existing = Groups.Find(Name)) return *Existing;
		UStaticMesh* Mesh = CEForest::Load(Name);
		if (!Mesh) return nullptr;
		auto* Inst = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		Inst->SetupAttachment(RootComponent);
		Inst->SetMobility(EComponentMobility::Static);
		Inst->SetStaticMesh(Mesh);
		Inst->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Inst->SetCanEverAffectNavigation(false);
		Inst->SetCastShadow(bShadow);
		const bool bSmall = Name.Contains(TEXT("Grass")) || Name.Contains(TEXT("Fern"));
		// Trees and boulders contribute to Lumen reflections in the wet service lane.
		Inst->SetVisibleInRayTracing(!bSmall && !Name.Contains(TEXT("Fence")));
		Inst->SetCullDistances(bSmall ? 7500 : 26000, bSmall ? 12500 : 34000);
		Inst->bAutoRebuildTreeOnInstanceChanges = false;
		Inst->RegisterComponent(); AddInstanceComponent(Inst);
		Groups.Add(Name, Inst);
		return Inst;
	};
	auto Place = [&](const FString& Name, float X, float Y, float Scale, float Yaw, bool bShadow = true)
	{
		if (auto* Inst = Group(Name, bShadow))
		{
			const FBox Bounds=Inst->GetStaticMesh()->GetBoundingBox();
			const bool bRock=Name.Contains(TEXT("Boulder"));
			const float Embed=bRock ? Bounds.GetSize().Z*Scale*.2f : Name.Contains(TEXT("Fir")) ? 8.f : 2.f;
			FQuat Rotation=FRotator(0,Yaw,0).Quaternion();
			if(Name.Contains(TEXT("Grass"))||Name.Contains(TEXT("Fern")))
			{
				const FVector Normal(-(CEForest::Ground(X+.15f,Y)-CEForest::Ground(X-.15f,Y))/.3f,
					-(CEForest::Ground(X,Y+.15f)-CEForest::Ground(X,Y-.15f))/.3f,1.f);
				Rotation=FQuat::FindBetweenNormals(FVector::UpVector,Normal.GetSafeNormal())*Rotation;
			}
			const float Z=CEForest::Ground(X,Y)*100.f-Bounds.Min.Z*Scale-Embed;
			Inst->AddInstance(FTransform(Rotation, FVector(X*100,Y*100,Z), FVector(Scale)), true);
		}
	};
	if (UStaticMesh* Terrain = CEForest::Load(TEXT("SM_ForestTerrain")))
		// FBX's handedness conversion mirrors Blender Y; restore the authored heightfield coordinates.
		AddPropComp(Terrain, FVector::ZeroVector, 0, FVector(1,-1,1), false, ECESurf::Asphalt);
	else UE_LOG(LogColdEntry, Error, TEXT("Forest terrain missing. Run Tools/ue_import_forest.py."));
	if (auto* Yard = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Forest/Materials/M_WetYard.M_WetYard")))
		GetISM(ECESurf::Asphalt, ECECol::Solid)->SetMaterial(0, Yard);

	auto ClearOfGameplay = [&](float X, float Y)
	{
		const FVector Pos = M(X, Y, 0);
		if(FMath::PointDistToSegmentSquared(Pos,M(48,-25.5f,0),M(28.5f,-2,0))<FMath::Square(280.f)) return false;
		for (const FCENavNode& N : Nodes) if (FVector::DistSquared2D(Pos, N.Pos) < FMath::Square(480.f)) return false;
		for (const auto& Site : AttackSpawns) for (const FVector& Spawn : Site)
			if (FVector::DistSquared2D(Pos, Spawn) < FMath::Square(650.f)) return false;
		return true;
	};
	// Irregular, reproducible groves with three distinct scanned silhouettes and scale variation.
	const float Step = 6.1f / FMath::Sqrt(Density);
	for (float X = -138; X <= 178; X += Step) for (float Y = -145; Y <= 173; Y += Step)
	{
		const float PX = X + Random.FRandRange(-2.4f, 2.4f), PY = Y + Random.FRandRange(-2.4f, 2.4f);
		if (PX > -31 && PX < 71 && PY > -29 && PY < 57) continue;
		if (Random.FRand() < .055f) continue;
		if(FMath::PointDistToSegmentSquared(M(PX,PY,0),M(48,-25.5f,0),M(28.5f,-2,0))<FMath::Square(280.f)) continue;
		// An access lane through the trees lines up with the south gate.
		if (PY < -28 && FMath::Abs(PX - 24.f) < 6.5f) continue;
		Place(FString::Printf(TEXT("SM_Fir_%d"), Random.RandRange(1, 3)), PX, PY,
			Random.FRandRange(1.05f, 1.72f), Random.FRandRange(0, 360));
		++Trees;
		if (Random.FRand() < .52f)
			Place(FString::Printf(TEXT("SM_Sapling_%d"), Random.RandRange(1, 3)), PX + 2.f, PY - 2.f, Random.FRandRange(1.5f, 3.6f), Random.FRandRange(0, 360));
	}
	// Near-field trunks and saplings make the forest meet the fence rather than form a distant backdrop.
	for (const FVector2D& P : { FVector2D(-26,-20), FVector2D(-27,-6), FVector2D(-27,34), FVector2D(-21,49),
		FVector2D(5,51), FVector2D(48,51), FVector2D(64,36), FVector2D(65,1), FVector2D(58,-21), FVector2D(7,-23) })
	{
		if (!ClearOfGameplay(P.X,P.Y)) continue;
		Place(FString::Printf(TEXT("SM_Fir_%d"), Random.RandRange(1,3)), P.X,P.Y,Random.FRandRange(.95f,1.2f),Random.FRandRange(0,360));
		++Trees;
	}
	// Close groves soften the clearing without blocking the established approaches.
	for(int32 i=0;i<120;++i)
	{
		const float X=Random.FRandRange(-29,69),Y=Random.FRandRange(-27,55);
		if(X>-19 && X<59 && Y>-17 && Y<45) continue;
		if(!ClearOfGameplay(X,Y)) continue;
		Place(FString::Printf(TEXT("SM_Fir_%d"),Random.RandRange(1,3)),X,Y,Random.FRandRange(.95f,1.45f),Random.FRandRange(0,360));++Trees;
	}
	for(const FVector2D P:{FVector2D(46.5f,-18),FVector2D(44,-13)})
		if(ClearOfGameplay(P.X,P.Y)) {Place(TEXT("SM_Fir_3"),P.X,P.Y,1.18f,35.f);++Trees;}
	for (int32 i=0; i < FMath::RoundToInt(42000*Density); ++i)
	{
		const float X=Random.FRandRange(-62,102), Y=Random.FRandRange(-58,86);
		const bool bInside=CEForest::InYard(X,Y);
		if (bInside && X > -17 && X < 57 && Y > -15 && Y < 43) continue;
		if (Y < -26 && FMath::Abs(X-24) < 5.5f) continue;
		const bool bFern=Random.FRand() < .17f;
		if(bFern && bInside && !ClearOfGameplay(X,Y)) continue;
		Place(FString::Printf(TEXT("SM_%s_%d"), bFern ? TEXT("Fern") : TEXT("Grass"), Random.RandRange(1,3)),
			X,Y,Random.FRandRange(bFern?1.1f:2.0f,bFern?2.1f:3.7f),Random.FRandRange(0,360),bFern);
		++Plants;
	}
	// Small tufts and ferns along the building's footing join architecture to the earth.
	for(int32 i=0;i<480;++i)
	{
		const float X=Random.FRandRange(-.6f,40.6f),Y=Random.FRandRange(-2.f,-.35f);
		if((X>1.8f&&X<5.2f)||(X>26.5f&&X<30.5f)) continue;
		Place(FString::Printf(TEXT("SM_Grass_%d"),Random.RandRange(1,3)),X,Y,Random.FRandRange(.7f,1.4f),Random.FRandRange(0,360),false);++Plants;
	}
	for(const FVector2D P:{FVector2D(49,-23),FVector2D(50,-21),FVector2D(51,-24),FVector2D(45,-24),FVector2D(42,-23),FVector2D(46,-26)})
		{Place(TEXT("SM_Fern_2"),P.X,P.Y,2.05f,Random.FRandRange(0,360));++Plants;}
	for (int32 i=0;i<115;++i)
	{
		const float X=Random.FRandRange(-58,98),Y=Random.FRandRange(-56,84);
		if (X > -25 && X < 65 && Y > -23 && Y < 51) continue;
		if (CEForest::InYard(X,Y) && !ClearOfGameplay(X,Y)) continue;
		if (Y < -26 && FMath::Abs(X-24) < 6.5f) continue;
		Place(FString::Printf(TEXT("SM_Boulder_%d"),Random.RandRange(1,3)),X,Y,Random.FRandRange(.65f,1.65f),Random.FRandRange(0,360));
		++Rocks;
		if (i%11==0) Place(TEXT("SM_Deadwood_1"),X+1,Y+1,1.f,Random.FRandRange(0,360));
	}

	// Fine wire security perimeter; entrances read as gates while collision keeps match bounds intact.
	auto Fence = [&](float X, float Y, float Yaw)
	{
		if (auto* Inst=Group(TEXT("SM_SecurityFence")))
			Inst->AddInstance(FTransform(FRotator(0,Yaw,0),M(X,Y,0),FVector(1)),true);
		Box(X-.04f,Y-.04f,0,X+.04f,Y+.04f,2.65f,ECESurf::Gunmetal,ECECol::None);
	};
	for(float X=-30;X<70;X+=4) { Fence(X,-28,0); Fence(X,56,0); }
	for(float Y=-28;Y<56;Y+=4) { Fence(-30,Y,90); Fence(70,Y,90); }
	Box(-30,-28,2.42f,70,-27.96f,2.46f,ECESurf::Steel,ECECol::None);
	Box(-30,56,2.42f,70,56.04f,2.46f,ECESurf::Steel,ECECol::None);
	Box(-30,-28,2.42f,-29.96f,56,2.46f,ECESurf::Steel,ECECol::None);
	Box(70,-28,2.42f,70.04f,56,2.46f,ECESurf::Steel,ECECol::None);
	for(auto& Pair:Groups) { Pair.Value->bAutoRebuildTreeOnInstanceChanges=true; Pair.Value->BuildTreeIfOutdated(true,true); }
	UE_LOG(LogColdEntry,Log,TEXT("Forest environment: %d trees, %d understory plants, %d rocks, %d instance groups"),Trees,Plants,Rocks,Groups.Num());
}

void ACELevelBuilder::BuildFacadeDetail()
{
	if(auto* Facade=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Forest/Materials/M_WeatheredFacade.M_WeatheredFacade")))
		for(ECECol C:{ECECol::Solid,ECECol::None}) GetISM(ECESurf::Concrete,C)->SetMaterial(0,Facade);
	const ECESurf Trim=ECESurf::Steel;
	// Metal coping and a lightweight roof safety rail break up the old featureless roof silhouette.
	Box(-.24f,-.24f,7.35f,40.24f,.18f,7.4f,Trim,ECECol::None);
	Box(-.24f,27.82f,7.35f,40.24f,28.24f,7.4f,Trim,ECECol::None);
	Box(-.24f,.18f,7.35f,.18f,27.82f,7.4f,Trim,ECECol::None);
	Box(39.82f,.18f,7.35f,40.24f,27.82f,7.4f,Trim,ECECol::None);
	auto RoofRail=[&](float X0,float Y0,float X1,float Y1)
	{
		for(float Z:{7.76f,8.15f}) Box(X0-.022f,Y0-.022f,Z,X1+.022f,Y1+.022f,Z+.045f,ECESurf::Gunmetal,ECECol::None);
		const int N=FMath::CeilToInt(FVector2D(X1-X0,Y1-Y0).Size()/2.4f);
		for(int i=0;i<=N;++i){const float A=float(i)/N,X=FMath::Lerp(X0,X1,A),Y=FMath::Lerp(Y0,Y1,A);
			Box(X-.025f,Y-.025f,7.35f,X+.025f,Y+.025f,8.19f,ECESurf::Gunmetal,ECECol::None);}
	};
	RoofRail(.08f,.08f,39.92f,.08f);RoofRail(.08f,27.92f,39.92f,27.92f);
	RoofRail(.08f,.08f,.08f,27.92f);RoofRail(39.92f,.08f,39.92f,27.92f);
	for(const FVector2D& P:{FVector2D(12,3),FVector2D(26,3),FVector2D(29,20),FVector2D(31,20)})
	{
		Box(P.X-.75f,P.Y-.6f,7,P.X+.75f,P.Y+.6f,7.14f,ECESurf::Concrete,ECECol::None);
		Prop(TEXT("aircon"),P.X,P.Y,7.14f,0,ECESurf::Metal,1.6f,false);
	}
	// Downpipes, expansion joints, sill drips and a shallow steel entrance canopy.
	for(float X:{.65f,13.8f,31.4f,39.35f})
	{
		Box(X-.065f,-.3f,.08f,X+.065f,-.17f,7.32f,ECESurf::Metal,ECECol::None);
		for(float Z:{.55f,2.7f,4.7f,6.6f}) Box(X-.1f,-.32f,Z,X+.1f,-.15f,Z+.055f,Trim,ECECol::None);
	}
	for(float X:{8.f,16.f,24.f,32.f})
		Box(X-.012f,-.157f,.12f,X+.012f,-.151f,7.f,ECESurf::Charcoal,ECECol::None);
	Box(26.7f,-2.05f,2.56f,30.4f,-.14f,2.65f,ECESurf::Metal,ECECol::None);
	Box(26.7f,-2.05f,2.51f,30.4f,-1.97f,2.7f,Trim,ECECol::None);
	for(float X:{26.77f,30.32f}) Box(X-.035f,-2.02f,-.02f,X+.035f,-1.95f,2.56f,Trim,ECECol::None);
	for(float X=26.75f;X<30.4f;X+=.19f) Box(X,-2.05f,2.65f,X+.035f,-.14f,2.68f,Trim,ECECol::None);
	for(float Y:{2.f,10.7f,18.f,26.f}) Box(40.16f,Y-.055f,.12f,40.28f,Y+.055f,7.3f,ECESurf::Metal,ECECol::None);
	if(auto* Dirt=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Forest/Materials/M_WallStreaks.M_WallStreaks")))
	{
		auto Streak=[&](FVector Pos,FRotator Rotation,float Width,float Height)
		{
			auto* Decal=NewObject<UDecalComponent>(this);
			Decal->SetupAttachment(RootComponent);Decal->SetDecalMaterial(Dirt);
			Decal->DecalSize=FVector(18,Width*50,Height*50);
			Decal->SetRelativeLocation(Pos);Decal->SetRelativeRotation(Rotation);
			Decal->SetFadeScreenSize(.001f);Decal->RegisterComponent();AddInstanceComponent(Decal);
		};
		for(float X:{4.f,11.f,18.f,25.f,34.f}) Streak(M(X,-.15f,4.3f),FRotator(0,90,0),6.f,5.2f);
		for(float Y:{4.f,12.f,21.f}) Streak(M(40.15f,Y,4.3f),FRotator(0,180,0),6.f,5.2f);
	}
}
