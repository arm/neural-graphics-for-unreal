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

#pragma once

#include "CoreMinimal.h"
#include "FFXSlateApplication.h"
#include "Framework/Application/SlateApplication.h"
#include "NGShared.h"
#include "Rendering/SlateDrawBuffer.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/SlateTextures.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#if UE_VERSION_OLDER_THAN(5, 0, 0)
#include "PixelFormat.h"
#endif

//------------------------------------------------------------------------------------------------------
// Slate override code that allows Frame Interpolation to re-render and present both the Interpolated and Real frame.
//------------------------------------------------------------------------------------------------------
class NFRUSlateRenderer : public FSlateRenderer
{
	static const uint32 NumDrawBuffers = 6;

public:
	void OnSlateWindowRenderedThunk(SWindow& Window, void* Ptr)
	{
		return SlateWindowRendered.Broadcast(Window, Ptr);
	}

	void OnSlateWindowDestroyedThunk(void* Ptr)
	{
		return OnSlateWindowDestroyedDelegate.Broadcast(Ptr);
	}

	void OnPreResizeWindowBackBufferThunk(void* Ptr)
	{
		return PreResizeBackBufferDelegate.Broadcast(Ptr);
	}

	void OnPostResizeWindowBackBufferThunk(void* Ptr)
	{
		return PostResizeBackBufferDelegate.Broadcast(Ptr);
	}

#if UE_VERSION_AT_LEAST(5, 5, 0)
	void OnBackBufferReadyToPresentThunk(SWindow& Window, const FTextureRHIRef& Texture)
	{
		return OnBackBufferReadyToPresentDelegate.Broadcast(Window, Texture);
	}
#else
	void OnBackBufferReadyToPresentThunk(SWindow& Window, const FTexture2DRHIRef& Texture)
	{
		return OnBackBufferReadyToPresentDelegate.Broadcast(Window, Texture);
	}
#endif

	NFRUSlateRenderer(TSharedRef<FSlateRenderer> InUnderlyingRenderer);
	virtual ~NFRUSlateRenderer();

#if UE_VERSION_AT_LEAST(5, 1, 0)
	/** Returns a draw buffer that can be used by Slate windows to draw window elements */
	virtual FSlateDrawBuffer& AcquireDrawBuffer();

	virtual void ReleaseDrawBuffer(FSlateDrawBuffer& InWindowDrawBuffer);
#else
	/** Returns a draw buffer that can be used by Slate windows to draw window elements */
	virtual FSlateDrawBuffer& GetDrawBuffer();
#endif

	virtual bool Initialize();
	virtual void Destroy();
	virtual void CreateViewport(const TSharedRef<SWindow> InWindow);
	virtual void RequestResize(const TSharedPtr<SWindow>& InWindow, uint32 NewSizeX, uint32 NewSizeY);
	virtual void UpdateFullscreenState(
		const TSharedRef<SWindow> InWindow, uint32 OverrideResX = 0, uint32 OverrideResY = 0);
	virtual void SetSystemResolution(uint32 Width, uint32 Height);
	virtual void RestoreSystemResolution(const TSharedRef<SWindow> InWindow);
	virtual void DrawWindows(FSlateDrawBuffer& InWindowDrawBuffer);
	virtual void SetColorVisionDeficiencyType(
		EColorVisionDeficiency Type, int32 Severity, bool bCorrectDeficiency, bool bShowCorrectionWithDeficiency);
	virtual FIntPoint GenerateDynamicImageResource(const FName InTextureName);
	virtual bool GenerateDynamicImageResource(
		FName ResourceName, uint32 Width, uint32 Height, const TArray<uint8>& Bytes);

	virtual bool GenerateDynamicImageResource(FName ResourceName, FSlateTextureDataRef TextureData);

#if UE_VERSION_AT_LEAST(5, 1, 0)
	virtual FSlateResourceHandle GetResourceHandle(const FSlateBrush& Brush, FVector2f LocalSize, float DrawScale);
#elif UE_VERSION_AT_LEAST(5, 0, 0)
	virtual FSlateResourceHandle GetResourceHandle(const FSlateBrush& Brush, FVector2D LocalSize, float DrawScale);
#endif

	virtual FSlateResourceHandle GetResourceHandle(const FSlateBrush& Brush);

	virtual bool CanRenderResource(UObject& InResourceObject) const;

	virtual void RemoveDynamicBrushResource(TSharedPtr<FSlateDynamicImageBrush> BrushToRemove);

	virtual void ReleaseDynamicResource(const FSlateBrush& Brush);

	virtual void OnWindowDestroyed(const TSharedRef<SWindow>& InWindow);

	virtual void OnWindowFinishReshaped(const TSharedPtr<SWindow>& InWindow);

	virtual void* GetViewportResource(const SWindow& Window);

	virtual void FlushCommands() const;

	virtual void Sync() const;

	virtual void BeginFrame() const;

	virtual void EndFrame() const;

	virtual void ReloadTextureResources();

	virtual void LoadStyleResources(const ISlateStyle& Style);

	virtual bool AreShadersInitialized() const;

	virtual void InvalidateAllViewports();

	virtual void ReleaseAccessedResources(bool bImmediatelyFlush);

	virtual void PrepareToTakeScreenshot(
		const FIntRect& Rect, TArray<FColor>* OutColorData, SWindow* InScreenshotWindow);

#if UE_VERSION_OLDER_THAN(5, 5, 0)
	virtual void SetWindowRenderTarget(const SWindow& Window, class IViewportRenderTargetProvider* Provider);
#endif

	virtual FSlateUpdatableTexture* CreateUpdatableTexture(uint32 Width, uint32 Height);

#if UE_VERSION_AT_LEAST(5, 0, 0)
	virtual FSlateUpdatableTexture* CreateSharedHandleTexture(void* SharedHandle);
#endif

	virtual void ReleaseUpdatableTexture(FSlateUpdatableTexture* Texture);

	virtual ISlateAtlasProvider* GetTextureAtlasProvider();

	virtual ISlateAtlasProvider* GetFontAtlasProvider();

	virtual void CopyWindowsToVirtualScreenBuffer(const TArray<FString>& KeypressBuffer);

	virtual void MapVirtualScreenBuffer(FMappedTextureBuffer* OutImageData);
	virtual void UnmapVirtualScreenBuffer();

	virtual FCriticalSection* GetResourceCriticalSection();

	virtual int32 RegisterCurrentScene(FSceneInterface* Scene);

	virtual int32 GetCurrentSceneIndex() const;

	virtual void ClearScenes();

	virtual void DestroyCachedFastPathRenderingData(struct FSlateCachedFastPathRenderingData* VertexData);
	virtual void DestroyCachedFastPathElementData(struct FSlateCachedElementData* ElementData);

	virtual bool HasLostDevice() const;

	virtual void AddWidgetRendererUpdate(
		const struct FRenderThreadUpdateContext& Context, bool bDeferredRenderTargetUpdate);

	virtual EPixelFormat GetSlateRecommendedColorFormat();

#if UE_VERSION_AT_LEAST(5, 5, 0)
	void SetCurrentSceneIndex(int32 index) override;
#endif

private:
	FSlateDrawBuffer DrawBuffers[NumDrawBuffers];
	TArray<TSharedPtr<FSlateDynamicImageBrush>> DynamicBrushesToRemove[NumDrawBuffers];
	TSharedPtr<FSlateRenderer> UnderlyingRenderer;
	uint32 FreeBufferIndex;
	uint32 ResourceVersion;
};