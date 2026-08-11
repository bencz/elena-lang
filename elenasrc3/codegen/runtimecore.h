#ifndef CODEGEN_RUNTIMECORE_H
#define CODEGEN_RUNTIMECORE_H

#include "runtime.h"

namespace elena_lang::codegen
{
   enum class RuntimeCoreEntry : unsigned char
   {
      ExceptionDispatcher
   };

   enum class RuntimeCoreAction : unsigned char
   {
      PublishFrame,
      ObserveCollection,
      EnterSafeRegion,
      ParkMutator,
      AwaitCollection,
      LeaveSafeRegion,
      RetryOperation,
      PublishCollector,
      EnumerateMutators,
      WaitForMutators,
      AcquireRootLock,
      BeginRoots,
      AppendStaticRoots,
      AppendPermanentRoots,
      AppendTLSRoots,
      AppendFrameRoots,
      InvokeCollector,
      ResumeMutators,
      ReleaseRootLock,
      AcquireAllocationLock,
      ReleaseAllocationLock,
      Return,
      Count
   };

   static_assert((unsigned int)RuntimeCoreAction::Count <= 32);

   constexpr unsigned int runtimeCoreAction(RuntimeCoreAction action)
   {
      return 1u << (unsigned int)action;
   }

   struct RuntimeCoreStep
   {
      RuntimeCoreAction action;
      RuntimeCallEffect effects;
   };

   struct RuntimeCoreProtocol
   {
      static constexpr unsigned char MaxStepCount = 20;

      RuntimeOperation operation;
      ThreadingMode    threadingMode;
      RuntimeCoreStep  steps[MaxStepCount];
      unsigned int     actionMask;
      unsigned char    count;

      bool contains(RuntimeCoreAction action) const;
      bool isValid(const RuntimeSpec& runtime) const;
   };

   class RuntimeCoreProtocolProvider
   {
   public:
      static bool get(RuntimeOperation operation, const RuntimeSpec& runtime, RuntimeCoreProtocol& protocol);
   };

   enum class RuntimeCoreSymbol : unsigned char
   {
      GCData,
      SingleContent,
      ThreadTable,
      SystemEnvironment,
      StaticRoots,
      AllocateYoungRoutine,
      CollectYoung,
      CollectRuntime,
      CollectPermanent,
      PrepareRuntime,
      SignalStop,
      SignalClear,
      WaitForSignals,
      WaitForSignal,
      WaitForCollection,
      SignalCollectionEnd
   };

   enum class RuntimeCoreRelocationKind : unsigned char
   {
      Absolute32,
      Absolute64,
      Relative32,
      ExternalAbsolute32,
      ExternalRelative32
   };

   struct RuntimeCoreRelocation
   {
      RuntimeCoreRelocationKind kind;
      RuntimeCoreSymbol symbol;
      pos_t position;
      int addend;
   };

   enum class RuntimeCoreError : unsigned char
   {
      None,
      InvalidRuntime,
      InvalidABI,
      InvalidOperation,
      InvalidLabel,
      WriteFailed
   };
}

#endif
