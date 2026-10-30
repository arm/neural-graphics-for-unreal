// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "NGShared.h"

#include "RHICommandList.h"

struct FNGSDKFrameSync
{
	// Unregister destroys the next view slot, leaving queued-frames minus one live submissions.
	static constexpr uint32 SlotCount = FFX_MAX_QUEUED_FRAMES - 1;
#if UE_VERSION_AT_LEAST(5, 6, 0)
	FGPUFenceRHIRef CompletionFences[SlotCount];
#else
	// Older RHIs lack GPUFence::Wait; a completed render query retires the submission instead.
	FRenderQueryRHIRef CompletionQueries[SlotCount];
#endif
	uint32 NextSlot = 0;

	void BeginWork(FRHICommandListImmediate& RHICmdList)
	{
#if UE_VERSION_AT_LEAST(5, 6, 0)
		FGPUFenceRHIRef& Fence = CompletionFences[NextSlot];
		if (Fence.IsValid())
		{
			if (Fence->NumPendingWriteCommands.GetValue() != 0 || !Fence->Poll(RHICmdList.GetGPUMask()))
			{
				Fence->Wait(RHICmdList, RHICmdList.GetGPUMask());
			}
			Fence->Clear();
		}
		else
		{
			Fence = RHICreateGPUFence(TEXT("ArmNG SDK frame completion"));
		}
#else
		FRenderQueryRHIRef& Query = CompletionQueries[NextSlot];
		if (Query.IsValid())
		{
			// Execute the previous EndRenderQuery before testing a reused query's result.
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			uint64 Result = 0;
			// Nonblocking legacy Vulkan queries can return an older timestamp from the pool.
			if (!RHIGetRenderQueryResult(Query, Result, true))
			{
				UE_LOG(LogRHI,
					Fatal,
					TEXT("ArmNG SDK submission completion query failed; cannot safely reuse resource views."));
			}
		}
		else
		{
			Query = RHICreateRenderQuery(RQT_AbsoluteTime);
			if (!Query.IsValid())
			{
				UE_LOG(LogRHI, Fatal, TEXT("ArmNG SDK submission completion query could not be created."));
			}
		}
#endif
	}

	void EndWork(FRHICommandListImmediate& RHICmdList)
	{
#if UE_VERSION_AT_LEAST(5, 6, 0)
		RHICmdList.WriteGPUFence(CompletionFences[NextSlot]);
#else
		RHICmdList.EndRenderQuery(CompletionQueries[NextSlot]);
#endif
		NextSlot = (NextSlot + 1) % SlotCount;
	}
};
