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

class RENDERCORE_API FRDGBuilder : FRDGAllocatorScope
{
public:
	FRDGBuilder(
		FRHICommandListImmediate& RHICmdList, FRDGEventName Name = {}, ERDGBuilderFlags Flags = ERDGBuilderFlags::None);
	FRDGBuilder(const FRDGBuilder&) = delete;
	~FRDGBuilder();

	/** Finds an RDG texture associated with the external texture, or returns null if none is found. */
	FRDGTextureRef FindExternalTexture(FRHITexture* Texture) const;
	FRDGTextureRef FindExternalTexture(IPooledRenderTarget* ExternalPooledTexture) const;

	/** Registers a external pooled render target texture to be tracked by the render graph. The name of the registered
	 * RDG texture is pulled from the pooled render target. */
	FRDGTextureRef RegisterExternalTexture(const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		ERDGTextureFlags Flags = ERDGTextureFlags::None);

	/** Register an external texture with a custom name. The name is only used if the texture has not already been
	 * registered. */
	FRDGTextureRef RegisterExternalTexture(const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* NameIfNotRegistered,
		ERDGTextureFlags Flags = ERDGTextureFlags::None);

	/** Register a external buffer to be tracked by the render graph. */
	FRDGBufferRef RegisterExternalBuffer(
		const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer, ERDGBufferFlags Flags = ERDGBufferFlags::None);
	FRDGBufferRef RegisterExternalBuffer(
		const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer, ERDGBufferFlags Flags, ERHIAccess AccessFinal);

	/** Register an external buffer with a custom name. The name is only used if the buffer has not already been
	 * registered. */
	FRDGBufferRef RegisterExternalBuffer(const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer,
		const TCHAR* NameIfNotRegistered,
		ERDGBufferFlags Flags = ERDGBufferFlags::None);

	/** Create graph tracked texture from a descriptor. The CPU memory is guaranteed to be valid through execution of
	 *  the graph, at which point it is released. The underlying RHI texture lifetime is only guaranteed for passes
	 * which declare the texture in the pass parameter struct. The name is the name used for GPU debugging tools and the
	 * the VisualizeTexture/Vis command.
	 */
	FRDGTextureRef CreateTexture(
		const FRDGTextureDesc& Desc, const TCHAR* Name, ERDGTextureFlags Flags = ERDGTextureFlags::None);

	/** Create graph tracked buffer from a descriptor. The CPU memory is guaranteed to be valid through execution of
	 *  the graph, at which point it is released. The underlying RHI buffer lifetime is only guaranteed for passes which
	 *  declare the buffer in the pass parameter struct. The name is the name used for GPU debugging tools.
	 */
	FRDGBufferRef CreateBuffer(
		const FRDGBufferDesc& Desc, const TCHAR* Name, ERDGBufferFlags Flags = ERDGBufferFlags::None);

	/** A variant of CreateBuffer where users supply NumElements through a callback. This allows creating buffers with
	 *  sizes unknown at creation time. The callback is called before executing the most recent RDG pass that references
	 *  the buffer so data must be ready before that.
	 */
	FRDGBufferRef CreateBuffer(const FRDGBufferDesc& Desc,
		const TCHAR* Name,
		FRDGBufferNumElementsCallback&& NumElementsCallback,
		ERDGBufferFlags Flags = ERDGBufferFlags::None);

	/** Create graph tracked SRV for a texture from a descriptor. */
	FRDGTextureSRVRef CreateSRV(const FRDGTextureSRVDesc& Desc);

	/** Create graph tracked SRV for a buffer from a descriptor. */
	FRDGBufferSRVRef CreateSRV(const FRDGBufferSRVDesc& Desc);

	FORCEINLINE FRDGBufferSRVRef CreateSRV(FRDGBufferRef Buffer, EPixelFormat Format)
	{
		return CreateSRV(FRDGBufferSRVDesc(Buffer, Format));
	}

	/** Create graph tracked UAV for a texture from a descriptor. */
	FRDGTextureUAVRef CreateUAV(
		const FRDGTextureUAVDesc& Desc, ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None);

	FORCEINLINE FRDGTextureUAVRef CreateUAV(
		FRDGTextureRef Texture, ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None)
	{
		return CreateUAV(FRDGTextureUAVDesc(Texture), Flags);
	}

	/** Create graph tracked UAV for a buffer from a descriptor. */
	FRDGBufferUAVRef CreateUAV(
		const FRDGBufferUAVDesc& Desc, ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None);

	FORCEINLINE FRDGBufferUAVRef CreateUAV(FRDGBufferRef Buffer,
		EPixelFormat Format,
		ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None)
	{
		return CreateUAV(FRDGBufferUAVDesc(Buffer, Format), Flags);
	}

	/** Creates a graph tracked uniform buffer which can be attached to passes. These uniform buffers require some care
	 *  because they will bulk transition all resources. The graph will only transition resources which are not also
	 *  bound for write access by the pass.
	 */
	template <typename ParameterStructType>
	TRDGUniformBufferRef<ParameterStructType> CreateUniformBuffer(const ParameterStructType* ParameterStruct);

	//////////////////////////////////////////////////////////////////////////
	// Allocation Methods

	/** Allocates raw memory using an allocator tied to the lifetime of the graph. */
	void* Alloc(uint64 SizeInBytes, uint32 AlignInBytes);

	/** Allocates POD memory using an allocator tied to the lifetime of the graph. Does not construct / destruct. */
	template <typename PODType> PODType* AllocPOD();

	/** Allocates POD memory using an allocator tied to the lifetime of the graph. Does not construct / destruct. */
	template <typename PODType> PODType* AllocPODArray(uint32 Count);

	/** Allocates a C++ object using an allocator tied to the lifetime of the graph. Will destruct the object. */
	template <typename ObjectType, typename... TArgs> ObjectType* AllocObject(TArgs&&... Args);

	/** Allocates a parameter struct with a lifetime tied to graph execution. */
	template <typename ParameterStructType> ParameterStructType* AllocParameters();

	//////////////////////////////////////////////////////////////////////////

	/** Adds a lambda pass to the graph with an accompanied pass parameter struct.
	 *
	 *  RDG resources declared in the struct (via _RDG parameter macros) are safe to access in the lambda. The pass
	 * parameter struct should be allocated by AllocParameters(), and once passed in, should not be mutated. It is safe
	 * to provide the same parameter struct to multiple passes, so long as it is kept immutable. The lambda is deferred
	 * until execution unless the immediate debug mode is enabled. All lambda captures should assume deferral of
	 * execution.
	 *
	 *  The lambda must include a single RHI command list as its parameter. The exact type of command list depends on
	 * the workload. For example, use FRHIComputeCommandList& for Compute / AsyncCompute workloads. Raster passes should
	 * use FRHICommandList&. Prefer not to use FRHICommandListImmediate& unless actually required.
	 *
	 *  Declare the type of GPU workload (i.e. Copy, Compute / AsyncCompute, Graphics) to the pass via the Flags
	 * argument. This is used to determine async compute regions, render pass setup / merging, RHI transition accesses,
	 * etc. Other flags exist for specialized purposes, like forcing a pass to never be culled (NeverCull). See
	 * ERDGPassFlags for more info.
	 *
	 *  The pass name is used by debugging / profiling tools.
	 */
	template <typename ParameterStructType, typename ExecuteLambdaType>
	FRDGPassRef AddPass(FRDGEventName&& Name,
		const ParameterStructType* ParameterStruct,
		ERDGPassFlags Flags,
		ExecuteLambdaType&& ExecuteLambda);

	/** Adds a lambda pass to the graph with a runtime-generated parameter struct. */
	template <typename ExecuteLambdaType>
	FRDGPassRef AddPass(FRDGEventName&& Name,
		const FShaderParametersMetadata* ParametersMetadata,
		const void* ParameterStruct,
		ERDGPassFlags Flags,
		ExecuteLambdaType&& ExecuteLambda);

	/** Adds a lambda pass to the graph without any parameters. This useful for deferring RHI work onto the graph
	 * timeline, or incrementally porting code to use the graph system. NeverCull and SkipRenderPass (if Raster) are
	 * implicitly added to Flags. AsyncCompute is not allowed. It is never permitted to access a created (i.e. not
	 * externally registered) RDG resource outside of passes it is registered with, as the RHI lifetime is not
	 * guaranteed.
	 */
	template <typename ExecuteLambdaType>
	FRDGPassRef AddPass(FRDGEventName&& Name, ERDGPassFlags Flags, ExecuteLambdaType&& ExecuteLambda);

#if WITH_MGPU
	void SetNameForTemporalEffect(FName InNameForTemporalEffect)
	{
		NameForTemporalEffect = InNameForTemporalEffect;
	}
#endif

	/** Sets the current command list stat for all subsequent passes. */
	void SetCommandListStat(TStatId StatId);

	/** A hint to the builder to flush work to the RHI thread after the last queued pass on the execution timeline. */
	void AddDispatchHint();

	/** Tells the builder to delete unused RHI resources. The behavior of this method depends on whether RDG immediate
	 * mode is enabled: Deferred:  RHI resource flushes are performed prior to execution. Immediate: RHI resource
	 * flushes are performed immediately.
	 */
	void SetFlushResourcesRHI();

	/** Queues a buffer upload operation prior to execution. The resource lifetime is extended and the data is uploaded
	 * prior to executing passes. */
	void QueueBufferUpload(FRDGBufferRef Buffer,
		const void* InitialData,
		uint64 InitialDataSize,
		ERDGInitialDataFlags InitialDataFlags = ERDGInitialDataFlags::None);

	template <typename ElementType>
	inline void QueueBufferUpload(FRDGBufferRef Buffer,
		TArrayView<ElementType, int32> Container,
		ERDGInitialDataFlags InitialDataFlags = ERDGInitialDataFlags::None)
	{
		QueueBufferUpload(Buffer, Container.GetData(), Container.Num() * sizeof(ElementType), InitialDataFlags);
	}

	/** Queues a buffer upload operation prior to execution. The resource lifetime is extended and the data is uploaded
	 * prior to executing passes. */
	void QueueBufferUpload(FRDGBufferRef Buffer,
		const void* InitialData,
		uint64 InitialDataSize,
		FRDGBufferInitialDataFreeCallback&& InitialDataFreeCallback);

	template <typename ElementType>
	inline void QueueBufferUpload(FRDGBufferRef Buffer,
		TArrayView<ElementType, int32> Container,
		FRDGBufferInitialDataFreeCallback&& InitialDataFreeCallback)
	{
		QueueBufferUpload(Buffer, Container.GetData(), Container.Num() * sizeof(ElementType), InitialDataFreeCallback);
	}

	/** A variant where InitialData and InitialDataSize are supplied through callbacks. This allows queuing an upload
	 * with information unknown at creation time. The callbacks are called before RDG pass execution so data must be
	 * ready before that.
	 */
	void QueueBufferUpload(FRDGBufferRef Buffer,
		FRDGBufferInitialDataCallback&& InitialDataCallback,
		FRDGBufferInitialDataSizeCallback&& InitialDataSizeCallback);
	void QueueBufferUpload(FRDGBufferRef Buffer,
		FRDGBufferInitialDataCallback&& InitialDataCallback,
		FRDGBufferInitialDataSizeCallback&& InitialDataSizeCallback,
		FRDGBufferInitialDataFreeCallback&& InitialDataFreeCallback);

	/** Queues a pooled render target extraction to happen at the end of graph execution. For graph-created textures,
	 * this extends the lifetime of the GPU resource until execution, at which point the pointer is filled. If
	 * specified, the texture is transitioned to the AccessFinal state, or kDefaultAccessFinal otherwise.
	 */
	void QueueTextureExtraction(FRDGTextureRef Texture,
		TRefCountPtr<IPooledRenderTarget>* OutPooledTexturePtr,
		ERDGResourceExtractionFlags Flags = ERDGResourceExtractionFlags::None);
	void QueueTextureExtraction(FRDGTextureRef Texture,
		TRefCountPtr<IPooledRenderTarget>* OutPooledTexturePtr,
		ERHIAccess AccessFinal,
		ERDGResourceExtractionFlags Flags = ERDGResourceExtractionFlags::None);

	/** Queues a pooled buffer extraction to happen at the end of graph execution. For graph-created buffers, this
	 * extends the lifetime of the GPU resource until execution, at which point the pointer is filled. If specified, the
	 * buffer is transitioned to the AccessFinal state, or kDefaultAccessFinal otherwise.
	 */
	void QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>* OutPooledBufferPtr);
	void QueueBufferExtraction(
		FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>* OutPooledBufferPtr, ERHIAccess AccessFinal);

	/** For graph-created resources, this forces immediate allocation of the underlying pooled resource, effectively
	 * promoting it to an external resource. This will increase memory pressure, but allows for querying the pooled
	 * resource with GetPooled{Texture, Buffer}. This is primarily used as an aid for porting code incrementally to RDG.
	 */
	const TRefCountPtr<IPooledRenderTarget>& ConvertToExternalTexture(FRDGTextureRef Texture);
	const TRefCountPtr<FRDGPooledBuffer>& ConvertToExternalBuffer(FRDGBufferRef Buffer);

	/** Performs an immediate query for the underlying pooled resource. This is only allowed for external or extracted
	 * resources. */
	const TRefCountPtr<IPooledRenderTarget>& GetPooledTexture(FRDGTextureRef Texture) const;
	const TRefCountPtr<FRDGPooledBuffer>& GetPooledBuffer(FRDGBufferRef Buffer) const;

	/** (External | Extracted only) Sets the access to transition to after execution at the end of the graph. Overwrites
	 * any previously set final access. */
	void SetTextureAccessFinal(FRDGTextureRef Texture, ERHIAccess Access);

	/** (External | Extracted only) Sets the access to transition to after execution at the end of the graph. Overwrites
	 * any previously set final access. */
	void SetBufferAccessFinal(FRDGBufferRef Buffer, ERHIAccess Access);

	/** Finalizes the access of multiple resources so that they are immutable for the remainder of the graph. This
	 * immediately forces all resources into their finalized states. The resources cannot be used in any other state
	 * within the graph and must be used on the graphics pipe. This is designed for complex cases where resources are
	 * produced early in the graph and transitioned to a read-only state. Finalized access resources can be used outside
	 * of RDG pass parameters on any future pass without invoking RHI validation failures.
	 */
	void FinalizeResourceAccess(FRDGTextureAccessArray&& Textures, FRDGBufferAccessArray&& Buffers);

	inline void FinalizeTextureAccess(FRDGTextureAccessArray&& InTextures)
	{
		FinalizeResourceAccess(Forward<FRDGTextureAccessArray&&>(InTextures), {});
	}

	inline void FinalizeBufferAccess(FRDGBufferAccessArray&& InBuffers)
	{
		FinalizeResourceAccess({}, Forward<FRDGBufferAccessArray&&>(InBuffers));
	}

	inline void FinalizeTextureAccess(FRDGTextureRef Texture, ERHIAccess Access)
	{
		FinalizeResourceAccess({FRDGTextureAccess(Texture, Access)}, {});
	}

	inline void FinalizeBufferAccess(FRDGBufferRef Buffer, ERHIAccess Access)
	{
		FinalizeResourceAccess({}, {FRDGBufferAccess(Buffer, Access)});
	}

	/** Flag a resource that is produced by a pass but never used or extracted to not emit an 'unused' warning. */
	void RemoveUnusedTextureWarning(FRDGTextureRef Texture);
	void RemoveUnusedBufferWarning(FRDGBufferRef Buffer);

	/** Manually begins a new GPU event scope. */
	void BeginEventScope(FRDGEventName&& Name);

	/** Manually ends the current GPU event scope. */
	void EndEventScope();

	/** Executes the queued passes, managing setting of render targets (RHI RenderPasses), resource transitions and
	 * queued texture extraction. */
	void Execute();

	/** Per-frame update of the render graph resource pool. */
	static void TickPoolElements();

	/** Whether RDG is running in immediate mode. */
	static bool IsImmediateMode();

	/** The RHI command list used for the render graph. */
	FRHICommandListImmediate& RHICmdList;

	/** The blackboard used to hold common data tied to the graph lifetime. */
	FRDGBlackboard Blackboard;

#if RDG_DUMP_RESOURCES
	static FString BeginResourceDump(const TArray<FString>& Args);
	static void InitResourceDump();
	static void EndResourceDump();
	static bool IsDumpingFrame();
#else
	static bool IsDumpingFrame()
	{
		return false;
	}
#endif

#if RDG_DUMP_RESOURCES_AT_EACH_DRAW
	static void DumpDraw(const FRDGEventName& DrawEventName);
	static bool IsDumpingDraws();
#else
	static inline bool IsDumpingDraws()
	{
		return false;
	}
#endif

#if WITH_MGPU
	/** Copy all cross GPU external resources (not marked MultiGPUGraphIgnore) at the end of execution (bad for perf,
	 * but useful for debugging). */
	void EnableForceCopyCrossGPU()
	{
		bForceCopyCrossGPU = true;
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	// Deprecated Functions
	UE_DEPRECATED(5.0, "PreallocateTexture has been renamed to ConvertToExternalTexture")
	inline void PreallocateTexture(FRDGTextureRef Texture)
	{
		ConvertToExternalTexture(Texture);
	}

	UE_DEPRECATED(5.0, "PreallocateBuffer has been renamed to ConvertToExternalBuffer")
	inline void PreallocateBuffer(FRDGBufferRef Buffer)
	{
		ConvertToExternalBuffer(Buffer);
	}

	UE_DEPRECATED(
		5.0, "RegisterExternalTexture with ERenderTargetTexture is deprecated. Use the variant without instead.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	inline FRDGTextureRef RegisterExternalTexture(const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		ERenderTargetTexture Texture,
		ERDGTextureFlags Flags = ERDGTextureFlags::None) PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		return RegisterExternalTexture(ExternalPooledTexture, Flags);
	}

	UE_DEPRECATED(
		5.0, "RegisterExternalTexture with ERenderTargetTexture is deprecated. Use the variant without instead.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	inline FRDGTextureRef RegisterExternalTexture(const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
		const TCHAR* NameIfNotRegistered,
		ERenderTargetTexture RenderTargetTexture,
		ERDGTextureFlags Flags = ERDGTextureFlags::None) PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		return RegisterExternalTexture(ExternalPooledTexture, NameIfNotRegistered, Flags);
	}

	UE_DEPRECATED(5.0, "FindExternalTexture with ERenderTargetTexture is deprecated. Use the variant without instead.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FRDGTextureRef FindExternalTexture(IPooledRenderTarget* ExternalPooledTexture,
		ERenderTargetTexture Texture) const PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		return FindExternalTexture(ExternalPooledTexture);
	}
	//////////////////////////////////////////////////////////////////////////

private:
	static const ERHIAccess kDefaultAccessFinal = ERHIAccess::SRVMask;
	static const char* const kDefaultUnaccountedCSVStat;

	FRHIAsyncComputeCommandListImmediate& RHICmdListAsyncCompute;

	const FRDGEventName BuilderName;

	template <typename ParameterStructType, typename ExecuteLambdaType>
	FRDGPassRef AddPassInternal(FRDGEventName&& Name,
		const FShaderParametersMetadata* ParametersMetadata,
		const ParameterStructType* ParameterStruct,
		ERDGPassFlags Flags,
		ExecuteLambdaType&& ExecuteLambda);

	static ERDGPassFlags OverridePassFlags(const TCHAR* PassName, ERDGPassFlags Flags, bool bAsyncComputeSupported);

	void AddProloguePass();

	FORCEINLINE FRDGPassHandle ClampToPrologue(FRDGPassHandle PassHandle) const
	{
		// Preserve null inputs as outputs. Null is the highest value.
		return ProloguePassHandle.GetIndexUnchecked() > PassHandle.GetIndexUnchecked() ? ProloguePassHandle :
																						 PassHandle;
	}

	FORCEINLINE FRDGPassHandlesByPipeline ClampToPrologue(FRDGPassHandlesByPipeline PassHandles) const
	{
		PassHandles[ERHIPipeline::Graphics] = ClampToPrologue(PassHandles[ERHIPipeline::Graphics]);
		PassHandles[ERHIPipeline::AsyncCompute] = ClampToPrologue(PassHandles[ERHIPipeline::AsyncCompute]);
		return PassHandles;
	}

	FORCEINLINE FRDGPass* GetProloguePass() const
	{
		return ProloguePass;
	}

	/** Returns the graph prologue pass handle. */
	FORCEINLINE FRDGPassHandle GetProloguePassHandle() const
	{
		return ProloguePassHandle;
	}

	/** Returns the graph epilogue pass handle. */
	FORCEINLINE FRDGPassHandle GetEpiloguePassHandle() const
	{
		checkf(EpiloguePass,
			TEXT("The handle is not valid until the epilogue has been added to the graph during execution."));
		return Passes.Last();
	}

	/** Prologue and Epilogue barrier passes are used to plan transitions around RHI render pass merging,
	 *  as it is illegal to issue a barrier during a render pass. If passes [A, B, C] are merged together,
	 *  'A' becomes 'B's prologue pass and 'C' becomes 'A's epilogue pass. This way, any transitions that
	 *  need to happen before the merged pass (i.e. in the prologue) are done in A. Any transitions after
	 *  the render pass merge are done in C.
	 */
	FRDGPassHandle GetEpilogueBarrierPassHandle(FRDGPassHandle Handle)
	{
		return Passes[Handle]->EpilogueBarrierPass;
	}

	FRDGPassHandle GetPrologueBarrierPassHandle(FRDGPassHandle Handle)
	{
		return Passes[Handle]->PrologueBarrierPass;
	}

	FRDGPass* GetEpilogueBarrierPass(FRDGPassHandle Handle)
	{
		return Passes[GetEpilogueBarrierPassHandle(Handle)];
	}

	FRDGPass* GetPrologueBarrierPass(FRDGPassHandle Handle)
	{
		return Passes[GetPrologueBarrierPassHandle(Handle)];
	}

	/** Ends the barrier batch in the prologue of the provided pass. */
	void AddToPrologueBarriersToEnd(FRDGPassHandle Handle, FRDGBarrierBatchBegin& BarriersToBegin)
	{
		FRDGPass* Pass = GetPrologueBarrierPass(Handle);
		Pass->GetPrologueBarriersToEnd(Allocator).AddDependency(&BarriersToBegin);
	}

	/** Ends the barrier batch in the epilogue of the provided pass. */
	void AddToEpilogueBarriersToEnd(FRDGPassHandle Handle, FRDGBarrierBatchBegin& BarriersToBegin)
	{
		FRDGPass* Pass = GetEpilogueBarrierPass(Handle);
		Pass->GetEpilogueBarriersToEnd(Allocator).AddDependency(&BarriersToBegin);
	}

	/** Utility function to add an immediate barrier dependency in the prologue of the provided pass. */
	template <typename FunctionType> void AddToPrologueBarriers(FRDGPassHandle PassHandle, FunctionType Function)
	{
		FRDGPass* Pass = GetPrologueBarrierPass(PassHandle);
		FRDGBarrierBatchBegin& BarriersToBegin = Pass->GetPrologueBarriersToBegin(Allocator, TransitionCreateQueue);
		Function(BarriersToBegin);
		Pass->GetPrologueBarriersToEnd(Allocator).AddDependency(&BarriersToBegin);
	}

	/** Utility function to add an immediate barrier dependency in the epilogue of the provided pass. */
	template <typename FunctionType> void AddToEpilogueBarriers(FRDGPassHandle PassHandle, FunctionType Function)
	{
		FRDGPass* Pass = GetEpilogueBarrierPass(PassHandle);
		FRDGBarrierBatchBegin& BarriersToBegin =
			Pass->GetEpilogueBarriersToBeginFor(Allocator, TransitionCreateQueue, Pass->GetPipeline());
		Function(BarriersToBegin);
		Pass->GetEpilogueBarriersToEnd(Allocator).AddDependency(&BarriersToBegin);
	}

#if WITH_MGPU
	void ForceCopyCrossGPU();
#endif

	/** Registry of graph objects. */
	FRDGPassRegistry Passes;
	FRDGTextureRegistry Textures;
	FRDGBufferRegistry Buffers;
	FRDGViewRegistry Views;
	FRDGUniformBufferRegistry UniformBuffers;

	/** Uniform buffers which were used in a pass. */
	TArray<FRDGUniformBufferHandle, TInlineAllocator<32, FRDGArrayAllocator>> UniformBuffersToCreate;

	/** Tracks external resources to their registered render graph counterparts for de-duplication. */
	TSortedMap<FRHITexture*, FRDGTexture*, FRDGArrayAllocator> ExternalTextures;
	TSortedMap<const FRDGPooledBuffer*, FRDGBuffer*, FRDGArrayAllocator> ExternalBuffers;

	/** Map of barrier batches begun from more than one pipe. */
	TMap<FRDGBarrierBatchBeginId, FRDGBarrierBatchBegin*, FRDGSetAllocator> BarrierBatchMap;

	/** Set of all active barrier batch begin instances; used to create transitions. */
	FRDGTransitionCreateQueue TransitionCreateQueue;

	/** The epilogue and prologue passes are sentinels that are used to simplify graph logic around barriers
	 *  and traversal. The prologue pass is used exclusively for barriers before the graph executes, while the
	 *  epilogue pass is used for resource extraction barriers--a property that also makes it the main root of
	 *  the graph for culling purposes. The epilogue pass is added to the very end of the pass array for traversal
	 *  purposes. The prologue does not need to participate in any graph traversal behavior.
	 */
	FRDGPassHandle ProloguePassHandle;
	FRDGPass* ProloguePass = nullptr;
	FRDGPass* EpiloguePass = nullptr;

	struct FExtractedTexture
	{
		FExtractedTexture() = default;

		FExtractedTexture(FRDGTexture* InTexture, TRefCountPtr<IPooledRenderTarget>* InPooledTexture)
			: Texture(InTexture), PooledTexture(InPooledTexture)
		{}

		FRDGTexture* Texture{};
		TRefCountPtr<IPooledRenderTarget>* PooledTexture{};
	};

	TArray<FExtractedTexture, FRDGArrayAllocator> ExtractedTextures;

	struct FExtractedBuffer
	{
		FExtractedBuffer() = default;

		FExtractedBuffer(FRDGBuffer* InBuffer, TRefCountPtr<FRDGPooledBuffer>* InPooledBuffer)
			: Buffer(InBuffer), PooledBuffer(InPooledBuffer)
		{}

		FRDGBuffer* Buffer{};
		TRefCountPtr<FRDGPooledBuffer>* PooledBuffer{};
	};

	TArray<FExtractedBuffer, FRDGArrayAllocator> ExtractedBuffers;

	struct FUploadedBuffer
	{
		FUploadedBuffer() = default;

		FUploadedBuffer(FRDGBuffer* InBuffer, const void* InData, uint64 InDataSize)
			: bUseDataCallbacks(false), bUseFreeCallbacks(false), Buffer(InBuffer), Data(InData), DataSize(InDataSize)
		{}

		FUploadedBuffer(FRDGBuffer* InBuffer,
			const void* InData,
			uint64 InDataSize,
			FRDGBufferInitialDataFreeCallback&& InDataFreeCallback)
			: bUseDataCallbacks(false)
			, bUseFreeCallbacks(true)
			, Buffer(InBuffer)
			, Data(InData)
			, DataSize(InDataSize)
			, DataFreeCallback(MoveTemp(InDataFreeCallback))
		{}

		FUploadedBuffer(FRDGBuffer* InBuffer,
			FRDGBufferInitialDataCallback&& InDataCallback,
			FRDGBufferInitialDataSizeCallback&& InDataSizeCallback)
			: bUseDataCallbacks(true)
			, bUseFreeCallbacks(false)
			, Buffer(InBuffer)
			, DataCallback(MoveTemp(InDataCallback))
			, DataSizeCallback(MoveTemp(InDataSizeCallback))
		{}

		FUploadedBuffer(FRDGBuffer* InBuffer,
			FRDGBufferInitialDataCallback&& InDataCallback,
			FRDGBufferInitialDataSizeCallback&& InDataSizeCallback,
			FRDGBufferInitialDataFreeCallback&& InDataFreeCallback)
			: bUseDataCallbacks(true)
			, bUseFreeCallbacks(true)
			, Buffer(InBuffer)
			, DataCallback(MoveTemp(InDataCallback))
			, DataSizeCallback(MoveTemp(InDataSizeCallback))
			, DataFreeCallback(MoveTemp(InDataFreeCallback))
		{}

		bool bUseDataCallbacks = false;
		bool bUseFreeCallbacks = false;
		FRDGBuffer* Buffer{};
		const void* Data{};
		uint64 DataSize{};
		FRDGBufferInitialDataCallback DataCallback;
		FRDGBufferInitialDataSizeCallback DataSizeCallback;
		FRDGBufferInitialDataFreeCallback DataFreeCallback;
	};

	TArray<FUploadedBuffer, FRDGArrayAllocator> UploadedBuffers;

	struct FParallelPassSet
	{
		FParallelPassSet() = default;

		TArray<FRDGPass*, FRDGArrayAllocator> Passes;
		FGraphEventRef Event;
		FRHICommandList* RHICmdList{};
		IF_RHI_WANT_BREADCRUMB_EVENTS(FRDGBreadcrumbState* BreadcrumbStateBegin{});
		IF_RHI_WANT_BREADCRUMB_EVENTS(FRDGBreadcrumbState* BreadcrumbStateEnd{});
		int8 bInitialized = 0;
		bool bDispatchAfterExecute = false;
	};

	TArray<FParallelPassSet, FRDGArrayAllocator> ParallelPassSets;

	/** Array of all active parallel execute tasks. */
	FGraphEventArray ParallelExecuteEvents;

	/** Array of all pooled references held during execution. */
	FRDGPooledTextureArray ActivePooledTextures;
	FRDGPooledBufferArray ActivePooledBuffers;

	/** Texture state used for intermediate operations. Held here to avoid re-allocating. */
	FRDGTextureTransientSubresourceStateIndirect ScratchTextureState;

	/** Current scope's async compute budget. This is passed on to every pass created. */
	EAsyncComputeBudget AsyncComputeBudgetScope = EAsyncComputeBudget::EAll_4;
	EAsyncComputeBudget AsyncComputeBudgetState = EAsyncComputeBudget(~0u);

	IF_RDG_CPU_SCOPES(FRDGCPUScopeStacks CPUScopeStacks);
	IF_RDG_GPU_SCOPES(FRDGGPUScopeStacksByPipeline GPUScopeStacks);
	IF_RHI_WANT_BREADCRUMB_EVENTS(FRDGBreadcrumbState* BreadcrumbState{});

	IF_RDG_ENABLE_TRACE(FRDGTrace Trace);

	bool bDispatchHint = false;
	bool bFlushResourcesRHI = false;
	bool bParallelExecuteEnabled = false;

#if RDG_ENABLE_DEBUG
	FRDGUserValidation UserValidation;
	FRDGBarrierValidation BarrierValidation;
	FRDGLogFile LogFile;
#endif

	/** Tracks whether we are in a scope of adding passes to the builder. Used to avoid recursion. */
	bool bInDebugPassScope = false;

#if WITH_MGPU
	/** Name for the temporal effect used to synchronize multi-frame resources. */
	FName NameForTemporalEffect;

	/** Whether we performed the wait for the temporal effect yet. */
	bool bWaitedForTemporalEffect = false;

	/** Copy all cross GPU external resources (not marked MultiGPUGraphIgnore) at the end of execution (bad for perf,
	 * but useful for debugging). */
	bool bForceCopyCrossGPU = false;
#endif

	uint32 AsyncComputePassCount = 0;
	uint32 RasterPassCount = 0;

	IF_RDG_CMDLIST_STATS(TStatId CommandListStatScope);
	IF_RDG_CMDLIST_STATS(TStatId CommandListStatState);

	IRHITransientResourceAllocator* TransientResourceAllocator = nullptr;

	void Compile();
	void Clear();

	void BeginResourcesRHI(FRDGPass* ResourcePass, FRDGPassHandle ExecutePassHandle);
	void BeginResourceRHI(FRDGPassHandle, FRDGTexture* Texture);
	void BeginResourceRHI(FRDGPassHandle, FRDGTextureSRV* SRV);
	void BeginResourceRHI(FRDGPassHandle, FRDGTextureUAV* UAV);
	void BeginResourceRHI(FRDGPassHandle, FRDGBuffer* Buffer);
	void BeginResourceRHI(FRDGPassHandle, FRDGBufferSRV* SRV);
	void BeginResourceRHI(FRDGPassHandle, FRDGBufferUAV* UAV);
	void BeginResourceRHI(FRDGPassHandle, FRDGView* View);

	void EndResourcesRHI(FRDGPass* ResourcePass, FRDGPassHandle ExecutePassHandle);
	void EndResourceRHI(FRDGPassHandle, FRDGTexture* Texture, uint32 ReferenceCount);
	void EndResourceRHI(FRDGPassHandle, FRDGBuffer* Buffer, uint32 ReferenceCount);

	void SetupParallelExecute();
	void DispatchParallelExecute(IRHICommandContext* RHICmdContext);

	void SetupBufferUploads();
	void SubmitBufferUploads();
	void BeginFlushResourcesRHI();
	void EndFlushResourcesRHI();

	void SetupPassInternal(FRDGPass* Pass, FRDGPassHandle PassHandle, ERHIPipeline PassPipeline, bool bEmptyParameters);
	FRDGPass* SetupPass(FRDGPass* Pass);
	FRDGPass* SetupEmptyPass(FRDGPass* Pass);
	void CompilePassOps(FRDGPass* Pass);
	void ExecutePass(FRDGPass* Pass, FRHIComputeCommandList& RHICmdListPass);

	void ExecutePassPrologue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass);
	void ExecutePassEpilogue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass);

	void CollectPassBarriers(FRDGPass* Pass, FRDGPassHandle PassHandle);

	void CreatePassBarriers();

	void CreateUniformBuffers();

	void AddPassDependency(FRDGPassHandle ProducerHandle, FRDGPassHandle ConsumerHandle);

	void AddEpilogueTransition(FRDGTextureRef Texture);
	void AddEpilogueTransition(FRDGBufferRef Buffer);

	void AddTransition(FRDGPassHandle PassHandle,
		FRDGTextureRef Texture,
		const FRDGTextureTransientSubresourceStateIndirect& StateAfter);

	void AddTransition(FRDGPassHandle PassHandle, FRDGBufferRef Buffer, FRDGSubresourceState StateAfter);

	void AddTransition(FRDGParentResource* Resource,
		FRDGSubresourceState StateBefore,
		FRDGSubresourceState StateAfter,
		const FRHITransitionInfo& TransitionInfo);

	void AddAliasingTransition(FRDGPassHandle BeginPassHandle,
		FRDGPassHandle EndPassHandle,
		FRDGParentResourceRef Resource,
		const FRHITransientAliasingInfo& Info);

	bool IsTransient(FRDGTextureRef Texture) const;
	bool IsTransient(FRDGBufferRef Buffer) const;
	bool IsTransientInternal(FRDGParentResourceRef Resource, bool bFastVRAM) const;

	FRHIRenderPassInfo GetRenderPassInfo(const FRDGPass* Pass) const;

	FRDGSubresourceState* AllocSubresource(const FRDGSubresourceState& Other);

#if RDG_DUMP_RESOURCES
	void DumpResourcePassOutputs(const FRDGPass* Pass);

#if RDG_DUMP_RESOURCES_AT_EACH_DRAW
	void BeginPassDump(const FRDGPass* Pass);
	void EndPassDump(const FRDGPass* Pass);
#endif
#endif

#if RDG_ENABLE_DEBUG
	void VisualizePassOutputs(const FRDGPass* Pass);
	void ClobberPassOutputs(const FRDGPass* Pass);
#endif

	friend FRDGTrace;
	friend FRDGEventScopeGuard;
	friend FRDGGPUStatScopeGuard;
	friend FRDGAsyncComputeBudgetScopeGuard;
	friend FRDGScopedCsvStatExclusive;
	friend FRDGScopedCsvStatExclusiveConditional;
};