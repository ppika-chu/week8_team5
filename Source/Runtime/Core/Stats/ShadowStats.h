#pragma once

#include "Core/Types.h"

// Four editor views and one external PIE view.
constexpr int32 MaxShadowStatsViewCount = 5;

struct FShadowRenderStats
{
	double CpuTimeMs = 0.0;
	uint32 RequestedMapCount = 0;
	uint32 AllocatedMapCount = 0;
	uint32 UpdatedMapCount = 0;
	uint64 DrawCallCount = 0;
	uint32 AtlasSize = 0;
	uint32 AtlasLayerCount = 0;
	uint64 AllocatedTexelCount = 0;
};

struct FShadowFrameStats
{
	uint64 FrameNumber = 0;
	double CpuTimeMs = 0.0;
	uint64 RequestedMapCount = 0;
	uint64 AllocatedMapCount = 0;
	uint64 UpdatedMapCount = 0;
	uint64 DrawCallCount = 0;
	uint32 ViewCount = 0;
};

// GPU results are read asynchronously and identify their original frame.
struct FShadowGpuFrameStats
{
	uint64 FrameNumber = 0;
	double ViewTimeMs[MaxShadowStatsViewCount]{};
	double TotalTimeMs = 0.0;
	bool bValid = false;
};
