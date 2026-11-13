#include "CESound.h"
#include "ColdEntry.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/ReverbEffect.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

// ----------------------------------------------------------------------------------------------
// Tiny DSP toolkit
// ----------------------------------------------------------------------------------------------
namespace CESynth
{
	struct FBuf
	{
		TArray<float> S;
		int32 SR = 44100;
		FRandomStream R;
		FBuf(float Seconds, int32 Seed) : R(Seed) { S.SetNumZeroed(FMath::Max(1, (int32)(Seconds * SR))); }
		int32 N() const { return S.Num(); }
		float T(int32 i) const { return (float)i / SR; }
		float Noise() { return R.FRandRange(-1.f, 1.f); }
	};
	static float Coef(float Hz, int32 SR) { return 1.f - FMath::Exp(-2.f * PI * Hz / SR); }

	// Filtered noise burst. Lo/Hi = band (Hz, 0 = off). Decay = time constant (s).
	static void NoiseBurst(FBuf& B, float Start, float Amp, float Attack, float Decay, float Lo, float Hi, float Length = -1.f)
	{
		const int32 i0 = (int32)(Start * B.SR);
		const int32 i1 = Length > 0 ? FMath::Min(B.N(), i0 + (int32)(Length * B.SR)) : B.N();
		float Lp = 0.f, Lp2 = 0.f;
		const float AHi = Hi > 0 ? Coef(Hi, B.SR) : 1.f;
		const float ALo = Lo > 0 ? Coef(Lo, B.SR) : 0.f;
		for (int32 i = i0; i < i1; ++i)
		{
			const float t = (float)(i - i0) / B.SR;
			const float Env = (Attack > 0 ? FMath::Min(1.f, t / Attack) : 1.f) * FMath::Exp(-t / Decay);
			float x = B.Noise();
			Lp += AHi * (x - Lp);              // lowpass at Hi
			Lp2 += ALo * (Lp - Lp2);           // lowpass at Lo -> subtract for highpass
			const float y = Lo > 0 ? (Lp - Lp2) : Lp;
			B.S[i] += y * Amp * Env;
		}
	}
	// Sine with exponential pitch glide and decay.
	static void Tone(FBuf& B, float Start, float Amp, float F0, float F1, float Glide, float Attack, float Decay, float Length = -1.f)
	{
		const int32 i0 = (int32)(Start * B.SR);
		const int32 i1 = Length > 0 ? FMath::Min(B.N(), i0 + (int32)(Length * B.SR)) : B.N();
		double Ph = 0.0;
		for (int32 i = i0; i < i1; ++i)
		{
			const float t = (float)(i - i0) / B.SR;
			const float F = F1 + (F0 - F1) * FMath::Exp(-t / FMath::Max(0.0001f, Glide));
			Ph += 2.0 * PI * F / B.SR;
			const float Env = (Attack > 0 ? FMath::Min(1.f, t / Attack) : 1.f) * FMath::Exp(-t / Decay);
			B.S[i] += FMath::Sin((float)Ph) * Amp * Env;
		}
	}
	// Square-ish beep
	static void Beep(FBuf& B, float Start, float Amp, float F, float Len)
	{
		const int32 i0 = (int32)(Start * B.SR), i1 = FMath::Min(B.N(), i0 + (int32)(Len * B.SR));
		for (int32 i = i0; i < i1; ++i)
		{
			const float t = (float)(i - i0) / B.SR;
			const float Env = FMath::Min(1.f, t / 0.003f) * FMath::Min(1.f, (Len - t) / 0.006f);
			const float s = FMath::Sin(2.f * PI * F * t);
			B.S[i] += Amp * Env * FMath::Clamp(s * 3.f, -1.f, 1.f) * 0.6f;
		}
	}
	static TArray<int16> Finish(FBuf& B, float Gain = 1.f)
	{
		TArray<int16> Out; Out.SetNumUninitialized(B.N());
		// short fade-out to avoid clicks
		const int32 Fade = FMath::Min(B.N(), 400);
		for (int32 i = 0; i < B.N(); ++i)
		{
			float v = FMath::Tanh(B.S[i] * Gain);
			if (i > B.N() - Fade) v *= (float)(B.N() - i) / Fade;
			Out[i] = (int16)FMath::Clamp(v * 32000.f, -32767.f, 32767.f);
		}
		return Out;
	}
}
using namespace CESynth;

UCESound* UCESound::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<UCESound>() : nullptr;
}

bool UCESound::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UCESound::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCESound, STATGROUP_Tickables);
}

void UCESound::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ---------------- attenuation presets ----------------
	auto MakeAtt = [this](float Inner, float Falloff, float LPFMax)
	{
		USoundAttenuation* A = NewObject<USoundAttenuation>(this);
		FSoundAttenuationSettings& S = A->Attenuation;
		S.bAttenuate = true;
		S.bSpatialize = true;
		S.AttenuationShape = EAttenuationShape::Sphere;
		S.AttenuationShapeExtents = FVector(Inner, 0, 0);
		S.FalloffDistance = Falloff;
		S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		S.bAttenuateWithLPF = true;
		S.LPFRadiusMin = Inner;
		S.LPFRadiusMax = Inner + Falloff;
		S.LPFFrequencyAtMin = 20000.f;
		S.LPFFrequencyAtMax = LPFMax;
		S.bEnableOcclusion = true;
		S.OcclusionTraceChannel = ECC_Visibility;
		S.OcclusionLowPassFilterFrequency = 900.f;
		S.OcclusionVolumeAttenuation = 0.42f;
		S.OcclusionInterpolationTime = 0.08f;
		S.bEnableReverbSend = true;
		return A;
	};
	Atten.Add(MakeAtt(700.f, 9500.f, 2500.f));  // Loud
	Atten.Add(MakeAtt(150.f, 2400.f, 3000.f));  // Medium
	Atten.Add(MakeAtt(80.f, 1300.f, 4000.f));   // Quiet

	// ---------------- reverb presets (indexed by ECEReverb) ----------------
	auto MakeRev = [this](float Decay, float Density, float Diffusion, float Gain, float GainHF, float Refl, float Late, float LateDelay)
	{
		UReverbEffect* R = NewObject<UReverbEffect>(this);
		R->DecayTime = Decay; R->Density = Density; R->Diffusion = Diffusion; R->Gain = Gain; R->GainHF = GainHF;
		R->ReflectionsGain = Refl; R->LateGain = Late; R->LateDelay = LateDelay; R->DecayHFRatio = 0.6f;
		return R;
	};
	Reverbs.Add(MakeRev(0.45f, 0.4f, 0.4f, 0.18f, 0.4f, 0.05f, 0.25f, 0.03f));   // Outdoor
	Reverbs.Add(MakeRev(0.55f, 1.0f, 1.0f, 0.32f, 0.7f, 0.5f, 0.9f, 0.01f));     // SmallRoom
	Reverbs.Add(MakeRev(0.8f, 1.0f, 1.0f, 0.32f, 0.75f, 0.45f, 1.0f, 0.015f));   // Office
	Reverbs.Add(MakeRev(1.25f, 0.9f, 0.8f, 0.34f, 0.7f, 0.6f, 1.2f, 0.02f));     // Corridor
	Reverbs.Add(MakeRev(2.6f, 1.0f, 1.0f, 0.36f, 0.6f, 0.4f, 1.5f, 0.04f));      // LargeHall
	Reverbs.Add(MakeRev(1.9f, 0.8f, 0.6f, 0.36f, 0.85f, 0.7f, 1.4f, 0.025f));    // Stairwell

	Generate();
}

void UCESound::Generate()
{
	Bank.SetNum((int32)ECESnd::Count);
	int32 Seed = 1337;
	auto Add = [this](ECESnd S, TArray<int16>&& Data) { Bank[(int32)S].Add(MoveTemp(Data)); };

	// ---------------- gunshots ----------------
	for (int32 v = 0; v < 3; ++v)
	{
		{ // R4: sharp supersonic crack, heavy body, deep thump
			FBuf B(0.55f, Seed++);
			NoiseBurst(B, 0.f, 1.6f, 0.0004f, 0.006f, 900.f, 0.f);
			NoiseBurst(B, 0.f, 1.3f, 0.001f, 0.045f + v * 0.004f, 0.f, 1500.f);
			Tone(B, 0.f, 1.1f, 95.f, 42.f, 0.03f, 0.001f, 0.075f);
			NoiseBurst(B, 0.004f, 0.35f, 0.01f, 0.16f, 0.f, 450.f);
			Tone(B, 0.028f, 0.08f, 3100.f, 3100.f, 1.f, 0.f, 0.004f, 0.02f);
			Add(ECESnd::ShotR4, Finish(B, 1.3f));
		}
		{ // SMG-9: lighter, snappier 9mm
			FBuf B(0.4f, Seed++);
			NoiseBurst(B, 0.f, 1.2f, 0.0004f, 0.004f, 1200.f, 0.f);
			NoiseBurst(B, 0.f, 1.0f, 0.001f, 0.03f + v * 0.003f, 0.f, 2200.f);
			Tone(B, 0.f, 0.7f, 140.f, 60.f, 0.02f, 0.001f, 0.045f);
			NoiseBurst(B, 0.003f, 0.25f, 0.01f, 0.11f, 0.f, 600.f);
			Tone(B, 0.022f, 0.1f, 3600.f, 3600.f, 1.f, 0.f, 0.003f, 0.015f);
			Add(ECESnd::ShotSMG, Finish(B, 1.2f));
		}
		{ // P9 pistol: snappy mid, short
			FBuf B(0.42f, Seed++);
			NoiseBurst(B, 0.f, 1.3f, 0.0004f, 0.005f, 1000.f, 0.f);
			NoiseBurst(B, 0.f, 1.1f, 0.001f, 0.035f + v * 0.003f, 0.f, 1900.f);
			Tone(B, 0.f, 0.8f, 120.f, 55.f, 0.02f, 0.001f, 0.05f);
			NoiseBurst(B, 0.004f, 0.25f, 0.01f, 0.12f, 0.f, 500.f);
			Add(ECESnd::ShotP9, Finish(B, 1.25f));
		}
	}
	// ---------------- footsteps (4 variants per surface) ----------------
	for (int32 v = 0; v < 4; ++v)
	{
		const float J = v * 0.07f;
		{ FBuf B(0.16f, Seed++); // concrete: gritty heel + toe scuff
			NoiseBurst(B, 0.f, 0.7f, 0.001f, 0.02f, 300.f, 3200.f + v * 300.f);
			Tone(B, 0.f, 0.35f, 110.f + v * 8.f, 70.f, 0.02f, 0.001f, 0.025f);
			NoiseBurst(B, 0.045f + J * 0.1f, 0.25f, 0.003f, 0.02f, 1200.f, 5000.f);
			Add(ECESnd::StepConcrete, Finish(B)); }
		{ FBuf B(0.45f, Seed++); // metal grate / stairs: ringing
			NoiseBurst(B, 0.f, 0.5f, 0.0005f, 0.012f, 600.f, 6000.f);
			Tone(B, 0.f, 0.25f, 420.f + v * 30.f, 420.f + v * 30.f, 1.f, 0.f, 0.12f);
			Tone(B, 0.f, 0.18f, 980.f + v * 45.f, 980.f + v * 45.f, 1.f, 0.f, 0.09f);
			Tone(B, 0.f, 0.12f, 1730.f + v * 60.f, 1730.f, 1.f, 0.f, 0.07f);
			Tone(B, 0.f, 0.08f, 2650.f, 2650.f, 1.f, 0.f, 0.05f);
			Tone(B, 0.f, 0.4f, 90.f, 60.f, 0.02f, 0.001f, 0.03f);
			Add(ECESnd::StepMetal, Finish(B)); }
		{ FBuf B(0.2f, Seed++); // wood: hollow knock
			NoiseBurst(B, 0.f, 0.55f, 0.001f, 0.03f, 0.f, 1000.f + v * 100.f);
			Tone(B, 0.f, 0.45f, 190.f + v * 12.f, 150.f, 0.03f, 0.001f, 0.05f);
			Add(ECESnd::StepWood, Finish(B)); }
		{ FBuf B(0.12f, Seed++); // tile: crisp click
			NoiseBurst(B, 0.f, 0.6f, 0.0005f, 0.01f, 1500.f, 0.f);
			Tone(B, 0.f, 0.25f, 140.f, 90.f, 0.02f, 0.001f, 0.02f);
			Add(ECESnd::StepTile, Finish(B)); }
		{ FBuf B(0.2f, Seed++); // gravel / asphalt grit: granular
			for (int32 g = 0; g < 10; ++g)
				NoiseBurst(B, B.R.FRandRange(0.f, 0.09f), B.R.FRandRange(0.15f, 0.4f), 0.0005f, 0.006f, 1100.f, 7000.f, 0.03f);
			Tone(B, 0.f, 0.3f, 100.f, 70.f, 0.02f, 0.001f, 0.02f);
			Add(ECESnd::StepGravel, Finish(B)); }
	}
	// ---------------- weapon handling ----------------
	{ FBuf B(0.2f, Seed++); NoiseBurst(B, 0.f, 0.5f, 0.0005f, 0.006f, 1500.f, 0.f); Tone(B, 0.f, 0.25f, 2500.f, 2400.f, 1.f, 0.f, 0.01f);
	  NoiseBurst(B, 0.06f, 0.35f, 0.001f, 0.012f, 600.f, 4000.f); Add(ECESnd::MagOut, Finish(B)); }
	{ FBuf B(0.25f, Seed++); NoiseBurst(B, 0.f, 0.6f, 0.001f, 0.02f, 0.f, 1600.f); Tone(B, 0.f, 0.4f, 320.f, 260.f, 0.02f, 0.f, 0.03f);
	  NoiseBurst(B, 0.05f, 0.45f, 0.0005f, 0.006f, 2000.f, 0.f); Tone(B, 0.05f, 0.2f, 3100.f, 3000.f, 1.f, 0.f, 0.008f); Add(ECESnd::MagIn, Finish(B)); }
	{ FBuf B(0.3f, Seed++); NoiseBurst(B, 0.f, 0.3f, 0.01f, 0.03f, 900.f, 5000.f, 0.06f);
	  NoiseBurst(B, 0.07f, 0.8f, 0.0003f, 0.008f, 1500.f, 0.f); Tone(B, 0.07f, 0.35f, 1800.f, 1750.f, 1.f, 0.f, 0.03f);
	  Tone(B, 0.07f, 0.2f, 3100.f, 3100.f, 1.f, 0.f, 0.02f); Add(ECESnd::Bolt, Finish(B)); }
	{ FBuf B(0.08f, Seed++); NoiseBurst(B, 0.f, 0.4f, 0.0003f, 0.004f, 2500.f, 0.f); Tone(B, 0.f, 0.15f, 2800.f, 2800.f, 1.f, 0.f, 0.006f); Add(ECESnd::DryFire, Finish(B)); }
	{ FBuf B(0.3f, Seed++); NoiseBurst(B, 0.f, 0.2f, 0.02f, 0.06f, 500.f, 3000.f, 0.15f); NoiseBurst(B, 0.12f, 0.35f, 0.0005f, 0.01f, 1200.f, 0.f);
	  Tone(B, 0.12f, 0.15f, 2200.f, 2100.f, 1.f, 0.f, 0.015f); Add(ECESnd::Equip, Finish(B)); }
	// ---------------- doors ----------------
	{ FBuf B(0.75f, Seed++); // latch + creak
	  NoiseBurst(B, 0.f, 0.5f, 0.0005f, 0.008f, 1500.f, 0.f);
	  for (int32 i = 0; i < B.N(); ++i)
	  { const float t = B.T(i); if (t < 0.06f) continue; const float tt = t - 0.06f;
	    const float F = 330.f + 180.f * tt + 14.f * FMath::Sin(2 * PI * 7.f * tt);
	    const float Env = FMath::Min(1.f, tt / 0.05f) * FMath::Exp(-tt / 0.35f);
	    B.S[i] += 0.12f * Env * (FMath::Sin(2 * PI * F * tt) + 0.5f * FMath::Sin(2 * PI * F * 2.01f * tt)) * (0.6f + 0.4f * B.Noise()); }
	  Add(ECESnd::DoorOpen, Finish(B)); }
	{ FBuf B(0.4f, Seed++); NoiseBurst(B, 0.f, 0.8f, 0.001f, 0.05f, 0.f, 700.f); Tone(B, 0.f, 0.6f, 85.f, 60.f, 0.03f, 0.001f, 0.08f);
	  NoiseBurst(B, 0.02f, 0.4f, 0.0005f, 0.008f, 1500.f, 0.f); Add(ECESnd::DoorClose, Finish(B)); }
	// ---------------- explosives ----------------
	{ FBuf B(2.0f, Seed++); // breach charge
	  NoiseBurst(B, 0.f, 1.8f, 0.0005f, 0.03f, 500.f, 0.f);
	  NoiseBurst(B, 0.f, 1.6f, 0.002f, 0.35f, 0.f, 700.f);
	  Tone(B, 0.f, 1.5f, 70.f, 32.f, 0.08f, 0.002f, 0.4f);
	  for (int32 c = 0; c < 60; ++c) NoiseBurst(B, B.R.FRandRange(0.05f, 1.4f), B.R.FRandRange(0.05f, 0.35f) * FMath::Exp(-c / 30.f), 0.0005f, 0.01f, 800.f, 6000.f, 0.04f);
	  Add(ECESnd::Breach, Finish(B, 1.4f)); }
	{ FBuf B(0.9f, Seed++); // flashbang
	  NoiseBurst(B, 0.f, 2.f, 0.0003f, 0.05f, 700.f, 0.f); NoiseBurst(B, 0.f, 1.f, 0.002f, 0.2f, 0.f, 900.f); Tone(B, 0.f, 1.f, 110.f, 45.f, 0.05f, 0.001f, 0.2f);
	  Add(ECESnd::Flash, Finish(B, 1.5f)); }
	{ FBuf B(0.8f, Seed++); // glass break
	  NoiseBurst(B, 0.f, 1.2f, 0.0005f, 0.05f, 2000.f, 0.f);
	  for (int32 c = 0; c < 28; ++c) Tone(B, B.R.FRandRange(0.01f, 0.6f), B.R.FRandRange(0.05f, 0.18f), B.R.FRandRange(2800.f, 7500.f), 0, 1.f, 0.f, 0.03f, 0.08f);
	  Add(ECESnd::GlassBreak, Finish(B)); }
	{ FBuf B(0.6f, Seed++); // wood splinter
	  for (int32 c = 0; c < 8; ++c) NoiseBurst(B, B.R.FRandRange(0.f, 0.25f), B.R.FRandRange(0.3f, 0.8f), 0.0005f, 0.02f, 300.f, 2500.f, 0.08f);
	  Tone(B, 0.f, 0.6f, 120.f, 80.f, 0.03f, 0.001f, 0.08f); Add(ECESnd::WoodBreak, Finish(B)); }
	// ---------------- electronics / UI ----------------
	{ FBuf B(0.42f, Seed++); Beep(B, 0.f, 0.7f, 2300.f, 0.08f); Beep(B, 0.14f, 0.7f, 2300.f, 0.08f); Beep(B, 0.28f, 0.7f, 2300.f, 0.08f); Add(ECESnd::Beep, Finish(B)); }
	{ FBuf B(0.1f, Seed++); Beep(B, 0.f, 0.5f, 1400.f, 0.06f); Add(ECESnd::PlantBeep, Finish(B)); }
	{ FBuf B(0.6f, Seed++); Beep(B, 0.f, 0.5f, 1200.f, 0.12f); Beep(B, 0.16f, 0.5f, 900.f, 0.12f); Beep(B, 0.32f, 0.5f, 600.f, 0.2f); Add(ECESnd::Defused, Finish(B)); }
	{ FBuf B(0.05f, Seed++); Tone(B, 0.f, 0.5f, 2900.f, 2900.f, 1.f, 0.f, 0.01f); NoiseBurst(B, 0.f, 0.2f, 0.f, 0.004f, 3000.f, 0.f); Add(ECESnd::HitMarker, Finish(B)); }
	{ FBuf B(0.16f, Seed++); Tone(B, 0.f, 0.45f, 1650.f, 1650.f, 1.f, 0.f, 0.05f); Tone(B, 0.f, 0.3f, 3300.f, 3300.f, 1.f, 0.f, 0.04f); Add(ECESnd::HitHead, Finish(B)); }
	{ FBuf B(0.2f, Seed++); Tone(B, 0.f, 0.9f, 90.f, 55.f, 0.03f, 0.001f, 0.06f); NoiseBurst(B, 0.f, 0.5f, 0.001f, 0.04f, 0.f, 900.f); Add(ECESnd::Hurt, Finish(B)); }
	for (int32 v = 0; v < 2; ++v)
	{
		{ FBuf B(0.12f, Seed++); NoiseBurst(B, 0.f, 0.8f, 0.0003f, 0.012f, 1500.f, 0.f); Tone(B, 0.f, 0.2f, 400.f, 200.f, 0.01f, 0.f, 0.02f); Add(ECESnd::ImpactHard, Finish(B)); }
		{ FBuf B(0.25f, Seed++); NoiseBurst(B, 0.f, 0.6f, 0.0003f, 0.008f, 2000.f, 0.f);
		  Tone(B, 0.f, 0.22f, 2100.f + v * 300.f, 2100.f, 1.f, 0.f, 0.06f); Tone(B, 0.f, 0.15f, 4300.f + v * 200.f, 4300.f, 1.f, 0.f, 0.04f); Add(ECESnd::ImpactMetal, Finish(B)); }
		{ FBuf B(0.12f, Seed++); NoiseBurst(B, 0.f, 0.7f, 0.0005f, 0.02f, 200.f, 2200.f); Tone(B, 0.f, 0.3f, 160.f, 100.f, 0.02f, 0.001f, 0.03f); Add(ECESnd::ImpactSoft, Finish(B)); }
	}
	{ FBuf B(0.35f, Seed++); NoiseBurst(B, 0.f, 0.4f, 0.08f, 0.08f, 400.f, 2500.f); Add(ECESnd::Throw, Finish(B)); }
	{ FBuf B(0.25f, Seed++); NoiseBurst(B, 0.f, 0.5f, 0.0005f, 0.01f, 1500.f, 0.f); Tone(B, 0.03f, 0.4f, 260.f, 200.f, 0.02f, 0.001f, 0.04f); Beep(B, 0.1f, 0.15f, 2600.f, 0.04f); Add(ECESnd::Deploy, Finish(B)); }
	{ FBuf B(1.0f, Seed++); // reinforcement: heavy steel clang + scrape
	  NoiseBurst(B, 0.f, 0.3f, 0.05f, 0.15f, 500.f, 3000.f, 0.35f);
	  Tone(B, 0.32f, 0.6f, 220.f, 210.f, 1.f, 0.001f, 0.35f); Tone(B, 0.32f, 0.4f, 610.f, 600.f, 1.f, 0.001f, 0.25f); Tone(B, 0.32f, 0.25f, 1130.f, 1120.f, 1.f, 0.001f, 0.18f);
	  NoiseBurst(B, 0.32f, 0.7f, 0.0005f, 0.02f, 300.f, 0.f); Add(ECESnd::Reinforce, Finish(B)); }
	{ FBuf B(0.6f, Seed++); Beep(B, 0.f, 0.35f, 880.f, 0.12f); Beep(B, 0.2f, 0.35f, 1320.f, 0.25f); Add(ECESnd::RoundStart, Finish(B)); }
	{ FBuf B(0.7f, Seed++); Beep(B, 0.f, 0.35f, 1320.f, 0.15f); Beep(B, 0.22f, 0.35f, 660.f, 0.35f); Add(ECESnd::RoundEnd, Finish(B)); }
	{ FBuf B(3.2f, Seed++); Tone(B, 0.f, 0.25f, 3520.f, 3480.f, 2.f, 0.02f, 1.3f); Add(ECESnd::Ring, Finish(B)); }
	{ FBuf B(0.5f, Seed++); // drone motor whine (played repeatedly while moving)
	  for (int32 i = 0; i < B.N(); ++i) { const float t = B.T(i); B.S[i] += 0.12f * FMath::Sin(2 * PI * 210.f * t) * (0.7f + 0.3f * FMath::Sin(2 * PI * 33.f * t)) + 0.05f * B.Noise(); }
	  Add(ECESnd::DroneHum, Finish(B)); }

	bGenerated = true;
}

ECERange UCESound::RangeOf(ECESnd S) const
{
	switch (S)
	{
	case ECESnd::ShotR4: case ECESnd::ShotSMG: case ECESnd::ShotP9:
	case ECESnd::Breach: case ECESnd::Flash:
		return ECERange::Loud;
	case ECESnd::MagOut: case ECESnd::MagIn: case ECESnd::Bolt: case ECESnd::DryFire: case ECESnd::Equip:
	case ECESnd::PlantBeep: case ECESnd::Deploy: case ECESnd::DroneHum:
		return ECERange::Quiet;
	default:
		return ECERange::Medium;
	}
}

UAudioComponent* UCESound::Spawn(ECESnd Snd, const FVector* Loc, float Volume)
{
	UWorld* W = GetWorld();
	if (!bGenerated || !W) return nullptr;
	const TArray<TArray<int16>>& Variants = Bank[(int32)Snd];
	if (Variants.Num() == 0) return nullptr;
	const TArray<int16>& Data = Variants[FMath::RandRange(0, Variants.Num() - 1)];

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = (float)Data.Num() / SampleRate;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Data.GetData()), Data.Num() * sizeof(int16));

	const float V = Volume * (1.f - 0.7f * Muffle);
	UAudioComponent* C = nullptr;
	if (Loc)
	{
		C = UGameplayStatics::SpawnSoundAtLocation(W, Wave, *Loc, FRotator::ZeroRotator, V, 1.f, 0.f, Atten[(int32)RangeOf(Snd)], nullptr, true);
	}
	else
	{
		C = UGameplayStatics::SpawnSound2D(W, Wave, V, 1.f, 0.f, nullptr, false, true);
	}
	if (C)
	{
		if (Muffle > 0.05f)
		{
			C->SetLowPassFilterEnabled(true);
			C->SetLowPassFilterFrequency(FMath::Lerp(20000.f, 500.f, Muffle));
		}
		Live.Add({ C, Now + Wave->Duration + 0.25f });
	}
	return C;
}

void UCESound::Play(ECESnd Snd, const FVector& Loc, float Volume) { Spawn(Snd, &Loc, Volume); }
void UCESound::Play2D(ECESnd Snd, float Volume) { Spawn(Snd, nullptr, Volume); }

void UCESound::SetListenerReverb(ECEReverb R)
{
	const int32 Idx = (int32)R;
	if (Idx == CurrentReverb || !Reverbs.IsValidIndex(Idx)) return;
	CurrentReverb = Idx;
	UGameplayStatics::ActivateReverbEffect(GetWorld(), Reverbs[Idx], TEXT("CEZone"), 1.f, 0.7f, 0.35f);
}

void UCESound::Tick(float DeltaTime)
{
	Now += DeltaTime;
	for (int32 i = Live.Num() - 1; i >= 0; --i)
	{
		if (!Live[i].Comp.IsValid()) { Live.RemoveAtSwap(i); continue; }
		if (Now >= Live[i].StopAt)
		{
			Live[i].Comp->Stop();
			Live[i].Comp->DestroyComponent();
			Live.RemoveAtSwap(i);
		}
	}
}
