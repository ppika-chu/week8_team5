#pragma once

#include "Core/Types.h"

struct FShadowCasterState
{
    uint64 CasterID = 0;
    // 0은 Proxy의 월드 변환이 아직 확인되지 않은 상태다.
    uint64 TransformRevision = 0;
};
