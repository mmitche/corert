// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
#include "forward_declarations.h"

struct gc_alloc_context;
class RuntimeInstance;
class ThreadStore;
class CLREventStatic;
class Thread;

// The offsets of some fields in the thread (in particular, m_pTransitionFrame) are known to the compiler and get 
// inlined into the code.  Let's make sure they don't change just because we enable/disable server GC in a particular
// runtime build.
#define KEEP_THREAD_LAYOUT_CONSTANT

#if defined(_X86_) || defined(_ARM_)
# if defined(FEATURE_SVR_GC) || defined(KEEP_THREAD_LAYOUT_CONSTANT)
#  define SIZEOF_ALLOC_CONTEXT 40
# else // FEATURE_SVR_GC
#  define SIZEOF_ALLOC_CONTEXT 28
# endif // FEATURE_SVR_GC
#elif defined(_AMD64_) || defined(_ARM64_)
# if defined(FEATURE_SVR_GC) || defined(KEEP_THREAD_LAYOUT_CONSTANT)
#  define SIZEOF_ALLOC_CONTEXT 56
# else // FEATURE_SVR_GC
#  define SIZEOF_ALLOC_CONTEXT 40
# endif // FEATURE_SVR_GC
#endif // _AMD64_

#define TOP_OF_STACK_MARKER ((PTR_VOID)(UIntNative)(IntNative)-1)

#define DYNAMIC_TYPE_TLS_OFFSET_FLAG 0x80000000


enum SyncRequestResult
{
    TryAgain,
    SuccessUnmanaged,
    SuccessManaged,
};

typedef DPTR(PAL_LIMITED_CONTEXT) PTR_PAL_LIMITED_CONTEXT;

struct ExInfo;
typedef DPTR(ExInfo) PTR_ExInfo;


// also defined in ExceptionHandling.cs, layouts must match
struct ExInfo
{

    PTR_ExInfo              m_pPrevExInfo;
    PTR_PAL_LIMITED_CONTEXT m_pExContext;
    PTR_Object              m_exception;  // actual object reference, specially reported by GcScanRootsWorker
    ExKind                  m_kind;
    UInt8                   m_passNumber;
    UInt32                  m_idxCurClause;
    StackFrameIterator      m_frameIter;
    volatile void*          m_notifyDebuggerSP;
};



struct ThreadBuffer
{
    UInt8                   m_rgbAllocContextBuffer[SIZEOF_ALLOC_CONTEXT];
    UInt32 volatile         m_ThreadStateFlags;                     // see Thread::ThreadStateFlags enum
#if DACCESS_COMPILE
    PTR_VOID                m_pTransitionFrame;
#else
    PTR_VOID volatile       m_pTransitionFrame;
#endif
    PTR_VOID                m_pHackPInvokeTunnel;                   // see Thread::EnablePreemptiveMode
    PTR_VOID                m_pCachedTransitionFrame;
    PTR_Thread              m_pNext;                                // used by ThreadStore's SList<Thread>
    HANDLE                  m_hPalThread;                           // WARNING: this may legitimately be INVALID_HANDLE_VALUE
    void **                 m_ppvHijackedReturnAddressLocation;
    void *                  m_pvHijackedReturnAddress;
    PTR_ExInfo              m_pExInfoStackHead;
    PTR_VOID                m_pStackLow;
    PTR_VOID                m_pStackHigh;
    PTR_UInt8               m_pTEB;                                 // Pointer to OS TEB structure for this thread
    UInt64                  m_uPalThreadIdForLogging;               // @TODO: likely debug-only 
    EEThreadId              m_threadId;               
    PTR_VOID                m_pThreadStressLog;                     // pointer to head of thread's StressLogChunks
#ifdef FEATURE_GC_STRESS
    UInt32                  m_uRand;                                // current per-thread random number
#endif // FEATURE_GC_STRESS

    // Thread Statics Storage for dynamic types
    UInt32          m_numDynamicTypesTlsCells;
    PTR_PTR_UInt8   m_pDynamicTypesTlsCells;

#if CORERT
    PTR_PTR_VOID    m_pThreadLocalModuleStatics;
    UInt32          m_numThreadLocalModuleStatics;
#endif // CORERT
};

struct ReversePInvokeFrame
{
    void*   m_savedPInvokeTransitionFrame;
    Thread* m_savedThread;
};

class Thread : private ThreadBuffer
{
    friend class AsmOffsets;
    friend struct DefaultSListTraits<Thread>;
    friend class ThreadStore;
    IN_DAC(friend class ClrDataAccess;)

public:
    enum ThreadStateFlags
    {
        TSF_Unknown             = 0x00000000,       // Threads are created in this state
        TSF_Attached            = 0x00000001,       // Thread was inited by first U->M transition on this thread
        TSF_Detached            = 0x00000002,       // Thread was detached by DllMain
        TSF_SuppressGcStress    = 0x00000008,       // Do not allow gc stress on this thread, used in DllMain
                                                    // ...and on the Finalizer thread
        TSF_DoNotTriggerGc      = 0x00000010,       // Do not allow hijacking of this thread, also intended to
                                                    // ...be checked during allocations in debug builds.
        TSF_IsGcSpecialThread   = 0x00000020,       // Set to indicate a GC worker thread used for background GC
#ifdef FEATURE_GC_STRESS
        TSF_IsRandSeedSet       = 0x00000040,       // set to indicate the random number generator for GCStress was inited
#endif // FEATURE_GC_STRESS
    };
private:

    void Construct();

    void SetState(ThreadStateFlags flags);
    void ClearState(ThreadStateFlags flags);
    bool IsStateSet(ThreadStateFlags flags);

    static UInt32_BOOL HijackCallback(HANDLE hThread, PAL_LIMITED_CONTEXT* pThreadContext, void* pCallbackContext);
    bool InternalHijack(PAL_LIMITED_CONTEXT * pCtx, void* HijackTargets[3]);

    bool CacheTransitionFrameForSuspend();
    void ResetCachedTransitionFrame();
    void CrossThreadUnhijack();
    void UnhijackWorker();
#ifdef _DEBUG
    bool DebugIsSuspended();
#endif

    // 
    // SyncState members
    //
    PTR_VOID    GetTransitionFrame();

    void GcScanRootsWorker(void * pfnEnumCallback, void * pvCallbackData, StackFrameIterator & sfIter);

public:


    void Destroy();

    bool                IsInitialized();

    gc_alloc_context *     GetAllocContext();  // @TODO: I would prefer to not expose this in this way

#ifndef DACCESS_COMPILE
    UInt64              GetPalThreadIdForLogging();
    bool                IsCurrentThread();

    void                GcScanRoots(void * pfnEnumCallback, void * pvCallbackData);
#else
    typedef void GcScanRootsCallbackFunc(PTR_RtuObjectRef ppObject, void* token, UInt32 flags);
    bool GcScanRoots(GcScanRootsCallbackFunc * pfnCallback, void * token, PTR_PAL_LIMITED_CONTEXT pInitialContext);
#endif

    bool                Hijack();
    void                Unhijack();
#ifdef FEATURE_GC_STRESS
    static void         HijackForGcStress(PAL_LIMITED_CONTEXT * pCtx);
#endif // FEATURE_GC_STRESS
    bool                IsHijacked();
    void *              GetHijackedReturnAddress();
    void *              GetUnhijackedReturnAddress(void** ppvReturnAddressLocation);
    bool                DangerousCrossThreadIsHijacked();

    bool                IsSuppressGcStressSet();
    void                SetSuppressGcStress();
    void                ClearSuppressGcStress();
    bool                IsWithinStackBounds(PTR_VOID p);

    void                GetStackBounds(PTR_VOID * ppStackLow, PTR_VOID * ppStackHigh);

    PTR_UInt8           AllocateThreadLocalStorageForDynamicType(UInt32 uTlsTypeOffset, UInt32 tlsStorageSize, UInt32 numTlsCells);
    // mrt100 Debugger (dac) has dependencies on the GetThreadLocalStorageForDynamicType method.
    PTR_UInt8           GetThreadLocalStorageForDynamicType(UInt32 uTlsTypeOffset);
    PTR_UInt8           GetThreadLocalStorage(UInt32 uTlsIndex, UInt32 uTlsStartOffset);
    PTR_UInt8           GetTEB();

    void                PushExInfo(ExInfo * pExInfo);
    void                ValidateExInfoPop(ExInfo * pExInfo, void * limitSP);
    void                ValidateExInfoStack();
    bool                IsDoNotTriggerGcSet();
    void                SetDoNotTriggerGc();
    void                ClearDoNotTriggerGc();

    bool                IsDetached();
    void                SetDetached();

    PTR_VOID            GetThreadStressLog() const;
#ifndef DACCESS_COMPILE
    void                SetThreadStressLog(void * ptsl);
#endif // DACCESS_COMPILE
#ifdef FEATURE_GC_STRESS
    void                SetRandomSeed(UInt32 seed);
    UInt32              NextRand();
    bool                IsRandInited();
#endif // FEATURE_GC_STRESS
    PTR_ExInfo          GetCurExInfo();

    bool                IsCurrentThreadInCooperativeMode();

    PTR_VOID            GetTransitionFrameForStackTrace();
    void *              GetCurrentThreadPInvokeReturnAddress();

    static bool         IsHijackTarget(void * address);

    //
    // The set of operations used to support unmanaged code running in cooperative mode
    //
    void                EnablePreemptiveMode();
    void                DisablePreemptiveMode();

    // Set the m_pHackPInvokeTunnel field for GC allocation helpers that setup transition frame 
    // in assembly code. Do not use anywhere else.
    void                SetCurrentThreadPInvokeTunnelForGcAlloc(void * pTransitionFrame);

    // Setup the m_pHackPInvokeTunnel field for GC helpers entered via regular PInvoke.
    // Do not use anywhere else.
    void                SetupHackPInvokeTunnel();

    //
    // GC support APIs - do not use except from GC itself
    //
    void SetGCSpecial(bool isGCSpecial);
    bool IsGCSpecial();
    bool CatchAtSafePoint();

    //
    // Managed/unmanaged interop transitions support APIs
    //
    void WaitForSuspend();
    void WaitForGC(void * pTransitionFrame);

    void ReversePInvokeAttachOrTrapThread(ReversePInvokeFrame * pFrame);

    bool InlineTryFastReversePInvoke(ReversePInvokeFrame * pFrame);
    void InlineReversePInvokeReturn(ReversePInvokeFrame * pFrame);

#if CORERT
    Object* GetThreadStaticStorageForModule(UInt32 moduleIndex);
    Boolean SetThreadStaticStorageForModule(Object * pStorage, UInt32 moduleIndex);
#endif // CORERT
};

#ifndef GCENV_INCLUDED
typedef DPTR(Object) PTR_Object;
typedef DPTR(PTR_Object) PTR_PTR_Object;
#endif // !GCENV_INCLUDED
#ifdef DACCESS_COMPILE

// The DAC uses DebuggerEnumGcRefContext in place of a GCCONTEXT when doing reference
// enumeration. The GC passes through additional data in the ScanContext which the debugger
// neither has nor needs. While we could refactor the GC code to make an interface
// with less coupling, that might affect perf or make integration messier. Instead
// we use some typedefs so DAC and runtime can get strong yet distinct types.


// Ideally we wouldn't need this wrapper, but PromoteCarefully needs access to the
// thread and a promotion field. We aren't assuming the user's token will have this data.
struct DacScanCallbackData
{
    Thread* thread_under_crawl;               // the thread being scanned
    bool promotion;                           // are we emulating the GC promote phase or relocate phase?
                                              // different references are reported for each
    void* token;                              // the callback data passed to GCScanRoots
    void* pfnUserCallback;                    // the callback passed in to GcScanRoots
    uintptr_t stack_limit;                    // Lowest point on the thread stack that the scanning logic is permitted to read
};

typedef DacScanCallbackData EnumGcRefScanContext;
typedef void EnumGcRefCallbackFunc(PTR_PTR_Object, EnumGcRefScanContext* callbackData, UInt32 flags);

#else // DACCESS_COMPILE
#ifndef GCENV_INCLUDED
struct ScanContext;
typedef void promote_func(PTR_PTR_Object, ScanContext*, unsigned);
#endif // !GCENV_INCLUDED
typedef promote_func EnumGcRefCallbackFunc;
typedef ScanContext  EnumGcRefScanContext;

#endif // DACCESS_COMPILE
