#include "CETuning.h"

static FCEWeaponDef MakeWeapon(const TCHAR* Name, const TCHAR* Kind, bool bAuto, float RPM, float Dmg, float FS, float FE, float MinD,
	float Pen, int32 Mag, int32 Reserve, float Rel, float RelE, float Equip, float Ads, float Hip, float AdsS, float Move,
	float Var, float Recover, float Muzzle)
{
	FCEWeaponDef W;
	W.Name = Name; W.Kind = Kind; W.bAuto = bAuto; W.RPM = RPM; W.Damage = Dmg; W.FalloffStart = FS; W.FalloffEnd = FE;
	W.MinDamage = MinD; W.PenPower = Pen; W.Mag = Mag; W.Reserve = Reserve; W.Reload = Rel; W.ReloadEmpty = RelE;
	W.Equip = Equip; W.AdsTime = Ads; W.HipSpread = Hip; W.AdsSpread = AdsS; W.MoveSpread = Move;
	W.RecoilVariance = Var; W.RecoilRecover = Recover; W.MuzzleLight = Muzzle;
	return W;
}

UCETuning::UCETuning()
{
	// Halden R4 — assault rifle. 4 torso hits (38*0.8=30.4) = ~250 ms TTK.
	R4 = MakeWeapon(TEXT("Halden R4"), TEXT("Assault rifle"), true, 720, 38, 20, 42, 30, 32, 30, 120, 2.35f, 2.9f, 0.4f, 0.22f, 2.8f, 0.08f, 1.6f, 0.08f, 0.45f, 1.f);
	// Strix SMG-9 — compact SMG. 5 torso hits at 860 rpm = ~280 ms TTK, weaker at range and through walls.
	SMG9 = MakeWeapon(TEXT("Strix SMG-9"), TEXT("Compact SMG"), true, 860, 29, 12, 26, 21, 20, 32, 128, 2.0f, 2.5f, 0.35f, 0.17f, 2.2f, 0.12f, 1.0f, 0.10f, 0.5f, 0.75f);
	// Marten P9 — pistol, semi-auto.
	P9 = MakeWeapon(TEXT("Marten P9"), TEXT("Pistol"), false, 420, 40, 14, 30, 28, 16, 15, 45, 1.7f, 2.05f, 0.25f, 0.14f, 1.8f, 0.1f, 0.8f, 0.12f, 0.75f, 0.6f);

	for (int32 i = 0; i < 40; ++i)
	{
		// R4: strong first 6 vertical, drifts right (6-13), swings left (14-23), then small wobble.
		const float P = i < 6 ? 0.62f - i * 0.03f : (i < 14 ? 0.44f : 0.36f);
		float Y = 0.f;
		if (i >= 6 && i < 14) Y = 0.16f;
		else if (i >= 14 && i < 24) Y = -0.2f;
		else if (i >= 24) Y = (i % 4 < 2) ? 0.12f : -0.12f;
		R4.Pattern.Add(FVector2D(P, Y));

		// SMG-9: lighter vertical, early left pull, then a zig-zag every 3 rounds.
		const float SP = i < 5 ? 0.42f : 0.3f;
		const float SY = i < 5 ? -0.06f : (((i / 3) % 2) ? 0.16f : -0.14f);
		SMG9.Pattern.Add(FVector2D(SP, SY));

		// P9: big single kicks, slight right.
		P9.Pattern.Add(FVector2D(1.25f, 0.12f));
	}
}

const FCEWeaponDef& UCETuning::Weapon(ECEWeaponId Id) const
{
	switch (Id)
	{
	case ECEWeaponId::SMG9: return SMG9;
	case ECEWeaponId::P9: return P9;
	default: return R4;
	}
}
