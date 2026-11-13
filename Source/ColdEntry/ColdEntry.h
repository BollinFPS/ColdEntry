#pragma once
#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogColdEntry, Log, All);

// Custom trace channel defined in Config/DefaultEngine.ini ("Weapon")
#define CE_TRACE_WEAPON ECC_GameTraceChannel1
// metres -> centimetres
#define CEM(x) ((x) * 100.f)
