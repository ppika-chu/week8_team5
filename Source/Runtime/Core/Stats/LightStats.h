#pragma once

#include "Core/Types.h"

struct FLightStats
{
	uint32 Count[6]{};                  // Directional, Point, Spot, Rect, Fireball, Sky
    uint32 ShadowCasting[6]{};
    uint32 Total = 0;
    uint32 TotalShadowCasting = 0;
};
