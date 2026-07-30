// This file is part of the FidelityFX Super Resolution 3.1 Unreal Engine Plugin.
//
// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
// SPDX-License-Identifier: MIT

#include "NFRUSlate.h"
#include "RenderingThread.h"

//------------------------------------------------------------------------------------------------------
// Helper definitions.
//------------------------------------------------------------------------------------------------------
struct FSlateReleaseDrawBufferCommandString
{
	static const TCHAR* TStr()
	{
		return TEXT("FSlateReleaseDrawBufferCommand");
	}
};

struct FFISlateReleaseDrawBufferCommand final :
	public FRHICommand<FFISlateReleaseDrawBufferCommand, FSlateReleaseDrawBufferCommandString>
{
	FSlateDrawBuffer* DrawBuffer;

	FFISlateReleaseDrawBufferCommand(FSlateDrawBuffer* InDrawBuffer) : DrawBuffer(InDrawBuffer) {}

	void Execute(FRHICommandListBase& CmdList)
	{
		DrawBuffer->Unlock();
	}

	static void ReleaseDrawBuffer(FRHICommandListImmediate& RHICmdList, FSlateDrawBuffer* InDrawBuffer)
	{
		if (!RHICmdList.Bypass())
		{
			ALLOC_COMMAND_CL(RHICmdList, FFISlateReleaseDrawBufferCommand)(InDrawBuffer);
		}
		else
		{
			FFISlateReleaseDrawBufferCommand Cmd(InDrawBuffer);
			Cmd.Execute(RHICmdList);
		}
	}
};

FDelegateHandle OnSlateWindowRenderedHandle;
FDelegateHandle OnSlateWindowDestroyedHandle;
FDelegateHandle OnPreResizeWindowBackBufferHandle;
FDelegateHandle OnPostResizeWindowBackBufferHandle;
FDelegateHandle OnBackBufferReadyToPresentHandle;

//------------------------------------------------------------------------------------------------------
// Implementation for the SlateRenderer override that allows for more draw buffers.
//------------------------------------------------------------------------------------------------------
NFRUSlateRenderer::NFRUSlateRenderer(TSharedRef<FSlateRenderer> InUnderlyingRenderer)
	: FSlateRenderer(InUnderlyingRenderer->GetFontServices())
	, UnderlyingRenderer(InUnderlyingRenderer)
	, FreeBufferIndex(0)
	, ResourceVersion(0)
{
	SlateWindowRendered = MoveTemp(InUnderlyingRenderer->OnSlateWindowRendered());
	OnSlateWindowDestroyedDelegate = MoveTemp(InUnderlyingRenderer->OnSlateWindowDestroyed());
	PreResizeBackBufferDelegate = MoveTemp(InUnderlyingRenderer->OnPreResizeWindowBackBuffer());
	PostResizeBackBufferDelegate = MoveTemp(InUnderlyingRenderer->OnPostResizeWindowBackBuffer());
	OnBackBufferReadyToPresentDelegate = MoveTemp(InUnderlyingRenderer->OnBackBufferReadyToPresent());

	InUnderlyingRenderer->OnSlateWindowRendered().Clear();
	InUnderlyingRenderer->OnSlateWindowDestroyed().Clear();
	InUnderlyingRenderer->OnPreResizeWindowBackBuffer().Clear();
	InUnderlyingRenderer->OnPostResizeWindowBackBuffer().Clear();
	InUnderlyingRenderer->OnBackBufferReadyToPresent().Clear();

	OnSlateWindowRenderedHandle =
		InUnderlyingRenderer->OnSlateWindowRendered().AddRaw(this, &NFRUSlateRenderer::OnSlateWindowRenderedThunk);
	OnSlateWindowDestroyedHandle =
		InUnderlyingRenderer->OnSlateWindowDestroyed().AddRaw(this, &NFRUSlateRenderer::OnSlateWindowDestroyedThunk);
	OnPreResizeWindowBackBufferHandle = InUnderlyingRenderer->OnPreResizeWindowBackBuffer().AddRaw(
		this, &NFRUSlateRenderer::OnPreResizeWindowBackBufferThunk);
	OnPostResizeWindowBackBufferHandle = InUnderlyingRenderer->OnPostResizeWindowBackBuffer().AddRaw(
		this, &NFRUSlateRenderer::OnPostResizeWindowBackBufferThunk);
	OnBackBufferReadyToPresentHandle = InUnderlyingRenderer->OnBackBufferReadyToPresent().AddRaw(
		this, &NFRUSlateRenderer::OnBackBufferReadyToPresentThunk);

	FSlateApplication::Get().OnPreShutdown().AddLambda(
		[InUnderlyingRenderer]()
		{
			InUnderlyingRenderer->OnSlateWindowRendered().Remove(OnSlateWindowRenderedHandle);
			InUnderlyingRenderer->OnSlateWindowDestroyed().Remove(OnSlateWindowDestroyedHandle);
			InUnderlyingRenderer->OnPreResizeWindowBackBuffer().Remove(OnPreResizeWindowBackBufferHandle);
			InUnderlyingRenderer->OnPostResizeWindowBackBuffer().Remove(OnPostResizeWindowBackBufferHandle);
			InUnderlyingRenderer->OnBackBufferReadyToPresent().Remove(OnBackBufferReadyToPresentHandle);
		});
}
NFRUSlateRenderer::~NFRUSlateRenderer()
{
	UnderlyingRenderer->OnSlateWindowRendered().Remove(OnSlateWindowRenderedHandle);
	UnderlyingRenderer->OnSlateWindowDestroyed().Remove(OnSlateWindowDestroyedHandle);
	UnderlyingRenderer->OnPreResizeWindowBackBuffer().Remove(OnPreResizeWindowBackBufferHandle);
	UnderlyingRenderer->OnPostResizeWindowBackBuffer().Remove(OnPostResizeWindowBackBufferHandle);
	UnderlyingRenderer->OnBackBufferReadyToPresent().Remove(OnBackBufferReadyToPresentHandle);
}

#if UE_VERSION_AT_LEAST(5, 1, 0)
/** Returns a draw buffer that can be used by Slate windows to draw window elements */
FSlateDrawBuffer& NFRUSlateRenderer::AcquireDrawBuffer()
{
	FreeBufferIndex = (FreeBufferIndex + 1) % NumDrawBuffers;

	FSlateDrawBuffer* Buffer = &DrawBuffers[FreeBufferIndex];

	while (!Buffer->Lock())
	{
		// If the buffer cannot be locked then the buffer is still in use.  If we are here all buffers are in use
		// so wait until one is free.
		if (IsInSlateThread())
		{
			// We can't flush commands on the slate thread, so simply spinlock until we're done
			// this happens if the render thread becomes completely blocked by expensive tasks when the Slate thread is
			// running in this case we cannot tick Slate.
			FPlatformProcess::Sleep(0.001f);
		}
		else
		{
			FlushCommands();
			UE_LOG(LogSlate, Warning, TEXT("Slate: Had to block on waiting for a draw buffer"));
			FreeBufferIndex = (FreeBufferIndex + 1) % NumDrawBuffers;
		}

		Buffer = &DrawBuffers[FreeBufferIndex];
	}

	// Safely remove brushes by emptying the array and releasing references
	DynamicBrushesToRemove[FreeBufferIndex].Empty();

	Buffer->ClearBuffer();
	Buffer->UpdateResourceVersion(ResourceVersion);
	return *Buffer;
}

void NFRUSlateRenderer::ReleaseDrawBuffer(FSlateDrawBuffer& InWindowDrawBuffer)
{
#if DO_CHECK
	bool bFound = false;
	for (int32 Index = 0; Index < NumDrawBuffers; ++Index)
	{
		if (&DrawBuffers[Index] == &InWindowDrawBuffer)
		{
			bFound = true;
			break;
		}
	}
	ensureMsgf(bFound, TEXT("It release a DrawBuffer that is not a member of the SlateRHIRenderer"));
#endif

	FSlateDrawBuffer* DrawBuffer = &InWindowDrawBuffer;
	ENQUEUE_RENDER_COMMAND(SlateReleaseDrawBufferCommand)
	([DrawBuffer](FRHICommandListImmediate& RHICmdList)
		{ FFISlateReleaseDrawBufferCommand::ReleaseDrawBuffer(RHICmdList, DrawBuffer); });
}
#else
/** Returns a draw buffer that can be used by Slate windows to draw window elements */
FSlateDrawBuffer& NFRUSlateRenderer::GetDrawBuffer()
{
	FreeBufferIndex = (FreeBufferIndex + 1) % NumDrawBuffers;

	FSlateDrawBuffer* Buffer = &DrawBuffers[FreeBufferIndex];

	while (!Buffer->Lock())
	{
		// If the buffer cannot be locked then the buffer is still in use.  If we are here all buffers are in use
		// so wait until one is free.
		if (IsInSlateThread())
		{
			// We can't flush commands on the slate thread, so simply spinlock until we're done
			// this happens if the render thread becomes completely blocked by expensive tasks when the Slate thread is
			// running in this case we cannot tick Slate.
			FPlatformProcess::Sleep(0.001f);
		}
		else
		{
			FlushCommands();
			UE_LOG(LogSlate, Warning, TEXT("Slate: Had to block on waiting for a draw buffer"));
			FreeBufferIndex = (FreeBufferIndex + 1) % NumDrawBuffers;
		}

		Buffer = &DrawBuffers[FreeBufferIndex];
	}

	// Safely remove brushes by emptying the array and releasing references
	DynamicBrushesToRemove[FreeBufferIndex].Empty();

	Buffer->ClearBuffer();
	Buffer->UpdateResourceVersion(ResourceVersion);
	return *Buffer;
}
#endif

bool NFRUSlateRenderer::Initialize()
{
	// Already initialized
	return true;
}
void NFRUSlateRenderer::Destroy()
{
	UnderlyingRenderer->Destroy();
}
void NFRUSlateRenderer::CreateViewport(const TSharedRef<SWindow> InWindow)
{
	UnderlyingRenderer->CreateViewport(InWindow);
}
void NFRUSlateRenderer::RequestResize(const TSharedPtr<SWindow>& InWindow, uint32 NewSizeX, uint32 NewSizeY)
{
	UnderlyingRenderer->RequestResize(InWindow, NewSizeX, NewSizeY);
}
void NFRUSlateRenderer::UpdateFullscreenState(
	const TSharedRef<SWindow> InWindow, uint32 OverrideResX, uint32 OverrideResY)
{
	UnderlyingRenderer->UpdateFullscreenState(InWindow, OverrideResX, OverrideResY);
}
void NFRUSlateRenderer::SetSystemResolution(uint32 Width, uint32 Height)
{
	UnderlyingRenderer->SetSystemResolution(Width, Height);
}
void NFRUSlateRenderer::RestoreSystemResolution(const TSharedRef<SWindow> InWindow)
{
	UnderlyingRenderer->RestoreSystemResolution(InWindow);
}
void NFRUSlateRenderer::DrawWindows(FSlateDrawBuffer& InWindowDrawBuffer)
{
	UnderlyingRenderer->DrawWindows(InWindowDrawBuffer);
}
void NFRUSlateRenderer::SetColorVisionDeficiencyType(
	EColorVisionDeficiency Type, int32 Severity, bool bCorrectDeficiency, bool bShowCorrectionWithDeficiency)
{
	UnderlyingRenderer->SetColorVisionDeficiencyType(Type, Severity, bCorrectDeficiency, bShowCorrectionWithDeficiency);
}
FIntPoint NFRUSlateRenderer::GenerateDynamicImageResource(const FName InTextureName)
{
	return UnderlyingRenderer->GenerateDynamicImageResource(InTextureName);
}
bool NFRUSlateRenderer::GenerateDynamicImageResource(
	FName ResourceName, uint32 Width, uint32 Height, const TArray<uint8>& Bytes)
{
	return UnderlyingRenderer->GenerateDynamicImageResource(ResourceName, Width, Height, Bytes);
}

bool NFRUSlateRenderer::GenerateDynamicImageResource(FName ResourceName, FSlateTextureDataRef TextureData)
{
	return UnderlyingRenderer->GenerateDynamicImageResource(ResourceName, TextureData);
}

#if UE_VERSION_AT_LEAST(5, 0, 0)
#if UE_VERSION_AT_LEAST(5, 1, 0)
FSlateResourceHandle NFRUSlateRenderer::GetResourceHandle(
	const FSlateBrush& Brush, FVector2f LocalSize, float DrawScale)
#else
FSlateResourceHandle NFRUSlateRenderer::GetResourceHandle(
	const FSlateBrush& Brush, FVector2D LocalSize, float DrawScale)
#endif
{
	return UnderlyingRenderer->GetResourceHandle(Brush, LocalSize, DrawScale);
}
#endif

FSlateResourceHandle NFRUSlateRenderer::GetResourceHandle(const FSlateBrush& Brush)
{
	return UnderlyingRenderer->GetResourceHandle(Brush);
}

bool NFRUSlateRenderer::CanRenderResource(UObject& InResourceObject) const
{
	return UnderlyingRenderer->CanRenderResource(InResourceObject);
}

void NFRUSlateRenderer::RemoveDynamicBrushResource(TSharedPtr<FSlateDynamicImageBrush> BrushToRemove)
{
	if (BrushToRemove.IsValid())
	{
		DynamicBrushesToRemove[FreeBufferIndex].Add(BrushToRemove);
	}
}

void NFRUSlateRenderer::ReleaseDynamicResource(const FSlateBrush& Brush)
{
	return UnderlyingRenderer->ReleaseDynamicResource(Brush);
}

void NFRUSlateRenderer::OnWindowDestroyed(const TSharedRef<SWindow>& InWindow)
{
	return UnderlyingRenderer->OnWindowDestroyed(InWindow);
}

void NFRUSlateRenderer::OnWindowFinishReshaped(const TSharedPtr<SWindow>& InWindow)
{
	return UnderlyingRenderer->OnWindowFinishReshaped(InWindow);
}

void* NFRUSlateRenderer::GetViewportResource(const SWindow& Window)
{
	return UnderlyingRenderer->GetViewportResource(Window);
}

void NFRUSlateRenderer::FlushCommands() const
{
	return UnderlyingRenderer->FlushCommands();
}

void NFRUSlateRenderer::Sync() const
{
	return UnderlyingRenderer->Sync();
}

void NFRUSlateRenderer::BeginFrame() const
{
	return UnderlyingRenderer->BeginFrame();
}

void NFRUSlateRenderer::EndFrame() const
{
	return UnderlyingRenderer->EndFrame();
}

void NFRUSlateRenderer::ReloadTextureResources()
{
	return UnderlyingRenderer->ReloadTextureResources();
}

void NFRUSlateRenderer::LoadStyleResources(const ISlateStyle& Style)
{
	return UnderlyingRenderer->LoadStyleResources(Style);
}

bool NFRUSlateRenderer::AreShadersInitialized() const
{
	return UnderlyingRenderer->AreShadersInitialized();
}

void NFRUSlateRenderer::InvalidateAllViewports()
{
	return UnderlyingRenderer->InvalidateAllViewports();
}

void NFRUSlateRenderer::ReleaseAccessedResources(bool bImmediatelyFlush)
{
	UnderlyingRenderer->ReleaseAccessedResources(bImmediatelyFlush);

	if (bImmediatelyFlush)
	{
		// Increment resource version to allow buffers to shrink or cached structures to clean up.
		ResourceVersion++;
	}
}

void NFRUSlateRenderer::PrepareToTakeScreenshot(
	const FIntRect& Rect, TArray<FColor>* OutColorData, SWindow* InScreenshotWindow)
{
	return UnderlyingRenderer->PrepareToTakeScreenshot(Rect, OutColorData, InScreenshotWindow);
}

void NFRUSlateRenderer::SetWindowRenderTarget(const SWindow& Window, class IViewportRenderTargetProvider* Provider)
{
	return UnderlyingRenderer->SetWindowRenderTarget(Window, Provider);
}

FSlateUpdatableTexture* NFRUSlateRenderer::CreateUpdatableTexture(uint32 Width, uint32 Height)
{
	return UnderlyingRenderer->CreateUpdatableTexture(Width, Height);
}

#if UE_VERSION_AT_LEAST(5, 0, 0)
FSlateUpdatableTexture* NFRUSlateRenderer::CreateSharedHandleTexture(void* SharedHandle)
{
	return UnderlyingRenderer->CreateSharedHandleTexture(SharedHandle);
}
#endif

void NFRUSlateRenderer::ReleaseUpdatableTexture(FSlateUpdatableTexture* Texture)
{
	return UnderlyingRenderer->ReleaseUpdatableTexture(Texture);
}

ISlateAtlasProvider* NFRUSlateRenderer::GetTextureAtlasProvider()
{
	return UnderlyingRenderer->GetTextureAtlasProvider();
}

ISlateAtlasProvider* NFRUSlateRenderer::GetFontAtlasProvider()
{
	return UnderlyingRenderer->GetFontAtlasProvider();
}

void NFRUSlateRenderer::CopyWindowsToVirtualScreenBuffer(const TArray<FString>& KeypressBuffer)
{
	return UnderlyingRenderer->CopyWindowsToVirtualScreenBuffer(KeypressBuffer);
}

void NFRUSlateRenderer::MapVirtualScreenBuffer(FMappedTextureBuffer* OutImageData)
{
	return UnderlyingRenderer->MapVirtualScreenBuffer(OutImageData);
}
void NFRUSlateRenderer::UnmapVirtualScreenBuffer()
{
	return UnderlyingRenderer->UnmapVirtualScreenBuffer();
}

FCriticalSection* NFRUSlateRenderer::GetResourceCriticalSection()
{
	return UnderlyingRenderer->GetResourceCriticalSection();
}

int32 NFRUSlateRenderer::RegisterCurrentScene(FSceneInterface* Scene)
{
	return UnderlyingRenderer->RegisterCurrentScene(Scene);
}

int32 NFRUSlateRenderer::GetCurrentSceneIndex() const
{
	return UnderlyingRenderer->GetCurrentSceneIndex();
}

void NFRUSlateRenderer::ClearScenes()
{
	return UnderlyingRenderer->ClearScenes();
}

void NFRUSlateRenderer::DestroyCachedFastPathRenderingData(struct FSlateCachedFastPathRenderingData* VertexData)
{
	return UnderlyingRenderer->DestroyCachedFastPathRenderingData(VertexData);
}
void NFRUSlateRenderer::DestroyCachedFastPathElementData(struct FSlateCachedElementData* ElementData)
{
	return UnderlyingRenderer->DestroyCachedFastPathElementData(ElementData);
}

bool NFRUSlateRenderer::HasLostDevice() const
{
	return UnderlyingRenderer->HasLostDevice();
}

void NFRUSlateRenderer::AddWidgetRendererUpdate(
	const struct FRenderThreadUpdateContext& Context, bool bDeferredRenderTargetUpdate)
{
	return UnderlyingRenderer->AddWidgetRendererUpdate(Context, bDeferredRenderTargetUpdate);
}

EPixelFormat NFRUSlateRenderer::GetSlateRecommendedColorFormat()
{
	return UnderlyingRenderer->GetSlateRecommendedColorFormat();
}
