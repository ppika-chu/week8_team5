#include "EnginePCH.h"
#include "ShadowGpuProfiler.h"
#include "RenderCommand.h"

bool FShadowGpuProfiler::EnsureQueries(FFrameQueries& Frame)
{
	ID3D11Device* Device = RenderCommand::GetDevice();
	if (!Device)
		return false;

	D3D11_QUERY_DESC Desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
	if (!Frame.Disjoint && FAILED(Device->CreateQuery(&Desc, Frame.Disjoint.GetAddressOf())))
		return false;

	Desc.Query = D3D11_QUERY_TIMESTAMP;
	for (int32 ViewIndex = 0; ViewIndex < MaxShadowStatsViewCount; ++ViewIndex)
	{
		if (!Frame.Starts[ViewIndex] && FAILED(Device->CreateQuery(&Desc, Frame.Starts[ViewIndex].GetAddressOf())))
			return false;
		if (!Frame.Ends[ViewIndex] && FAILED(Device->CreateQuery(&Desc, Frame.Ends[ViewIndex].GetAddressOf())))
			return false;
	}
	return true;
}

void FShadowGpuProfiler::BeginFrame(uint64 FrameNumber, bool bEnabled)
{
	ResolvePendingFrames();
	ActiveFrameIndex = -1;
	if (!bEnabled)
		return;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FFrameQueries& Frame = Frames[Index];
		if (Frame.bPending || !EnsureQueries(Frame))
			continue;

		Frame.FrameNumber = FrameNumber;
		for (int32 ViewIndex = 0; ViewIndex < MaxShadowStatsViewCount; ++ViewIndex)
		{
			Frame.ViewStarted[ViewIndex] = false;
			Frame.ViewEnded[ViewIndex] = false;
		}
		ActiveFrameIndex = Index;
		RenderCommand::GetContext()->Begin(Frame.Disjoint.Get());
		return;
	}
}

void FShadowGpuProfiler::BeginView(int32 ViewIndex)
{
	if (ActiveFrameIndex < 0 || ViewIndex < 0 || ViewIndex >= MaxShadowStatsViewCount)
		return;
	FFrameQueries& Frame = Frames[ActiveFrameIndex];
	if (Frame.ViewStarted[ViewIndex])
		return;

	RenderCommand::GetContext()->End(Frame.Starts[ViewIndex].Get());
	Frame.ViewStarted[ViewIndex] = true;
}

void FShadowGpuProfiler::EndView(int32 ViewIndex)
{
	if (ActiveFrameIndex < 0 || ViewIndex < 0 || ViewIndex >= MaxShadowStatsViewCount)
		return;
	FFrameQueries& Frame = Frames[ActiveFrameIndex];
	if (!Frame.ViewStarted[ViewIndex] || Frame.ViewEnded[ViewIndex])
		return;

	RenderCommand::GetContext()->End(Frame.Ends[ViewIndex].Get());
	Frame.ViewEnded[ViewIndex] = true;
}

void FShadowGpuProfiler::EndFrame()
{
	if (ActiveFrameIndex < 0)
		return;
	FFrameQueries& Frame = Frames[ActiveFrameIndex];
	RenderCommand::GetContext()->End(Frame.Disjoint.Get());
	Frame.bPending = true;
	ActiveFrameIndex = -1;
}

void FShadowGpuProfiler::ResolvePendingFrames()
{
	ID3D11DeviceContext* Context = RenderCommand::GetContext();
	if (!Context)
		return;

	for (FFrameQueries& Frame : Frames)
	{
		if (!Frame.bPending)
			continue;

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT Disjoint{};
		const HRESULT Status = Context->GetData(Frame.Disjoint.Get(), &Disjoint, sizeof(Disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (Status == S_FALSE)
			continue;
		if (FAILED(Status) || Disjoint.Disjoint || Disjoint.Frequency == 0)
		{
			Frame.bPending = false;
			continue;
		}

		FShadowGpuFrameStats Result{};
		Result.FrameNumber = Frame.FrameNumber;
		bool bReady = true;
		bool bValid = true;
		for (int32 ViewIndex = 0; ViewIndex < MaxShadowStatsViewCount; ++ViewIndex)
		{
			if (!Frame.ViewStarted[ViewIndex])
				continue;
			if (!Frame.ViewEnded[ViewIndex])
			{
				bValid = false;
				break;
			}

			UINT64 Start = 0;
			UINT64 End = 0;
			const HRESULT StartStatus = Context->GetData(Frame.Starts[ViewIndex].Get(), &Start, sizeof(Start), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			const HRESULT EndStatus = Context->GetData(Frame.Ends[ViewIndex].Get(), &End, sizeof(End), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (FAILED(StartStatus) || FAILED(EndStatus))
			{
				bValid = false;
				break;
			}
			if (StartStatus == S_FALSE || EndStatus == S_FALSE)
			{
				bReady = false;
				continue;
			}
			if (End < Start)
			{
				bValid = false;
				break;
			}
			Result.ViewTimeMs[ViewIndex] = static_cast<double>(End - Start) * 1000.0 / static_cast<double>(Disjoint.Frequency);
			Result.TotalTimeMs += Result.ViewTimeMs[ViewIndex];
		}

		if (!bValid)
		{
			Frame.bPending = false;
			continue;
		}
		if (!bReady)
			continue;

		Frame.bPending = false;
		if (!LastCompletedFrame.bValid || Result.FrameNumber > LastCompletedFrame.FrameNumber)
		{
			Result.bValid = true;
			LastCompletedFrame = Result;
		}
	}
}
