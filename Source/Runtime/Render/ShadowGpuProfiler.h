#pragma once

#include "Core/Stats/ShadowStats.h"
#include <d3d11.h>
#include <wrl/client.h>

// Bounded asynchronous queries: a busy GPU causes a sample to be skipped, never a wait.
class FShadowGpuProfiler
{
public:
	void BeginFrame(uint64 FrameNumber, bool bEnabled);
	void BeginView(int32 ViewIndex);
	void EndView(int32 ViewIndex);
	void EndFrame();
	const FShadowGpuFrameStats& GetLastCompletedFrame() const { return LastCompletedFrame; }

private:
	struct FFrameQueries
	{
		Microsoft::WRL::ComPtr<ID3D11Query> Disjoint;
		Microsoft::WRL::ComPtr<ID3D11Query> Starts[MaxShadowStatsViewCount];
		Microsoft::WRL::ComPtr<ID3D11Query> Ends[MaxShadowStatsViewCount];
		bool ViewStarted[MaxShadowStatsViewCount]{};
		bool ViewEnded[MaxShadowStatsViewCount]{};
		uint64 FrameNumber = 0;
		bool bPending = false;
	};

	bool EnsureQueries(FFrameQueries& Frame);
	void ResolvePendingFrames();

	FFrameQueries Frames[4]{};
	int32 ActiveFrameIndex = -1;
	FShadowGpuFrameStats LastCompletedFrame{};
};
