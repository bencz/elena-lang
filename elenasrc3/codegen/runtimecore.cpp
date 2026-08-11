#include "runtimecore.h"

using namespace elena_lang::codegen;

namespace
{
   constexpr RuntimeCallEffect RootEffects = RuntimeCallEffect::ReadHeap
      | RuntimeCallEffect::ReadGlobal
      | RuntimeCallEffect::RelocateRoots;

   constexpr RuntimeCallEffect SynchronizationEffects = RuntimeCallEffect::ReadGlobal
      | RuntimeCallEffect::WriteGlobal
      | RuntimeCallEffect::Synchronize;

   constexpr RuntimeCallEffect CollectorEffects = RuntimeCallEffect::Call
      | RuntimeCallEffect::WriteHeap
      | RuntimeCallEffect::RelocateRoots;

   constexpr RuntimeCoreStep SingleThreadCollectSteps[] = {
      { RuntimeCoreAction::PublishFrame, RuntimeCallEffect::WriteGlobal },

      { RuntimeCoreAction::BeginRoots, RootEffects },
      { RuntimeCoreAction::AppendStaticRoots, RootEffects },
      { RuntimeCoreAction::AppendPermanentRoots, RootEffects },
      { RuntimeCoreAction::AppendFrameRoots, RootEffects },

      { RuntimeCoreAction::InvokeCollector, CollectorEffects },
      { RuntimeCoreAction::Return, RuntimeCallEffect::None }
   };

   constexpr RuntimeCoreStep MultiThreadCollectSteps[] = {
      { RuntimeCoreAction::PublishFrame, RuntimeCallEffect::WriteGlobal },

      { RuntimeCoreAction::ObserveCollection, SynchronizationEffects },
      { RuntimeCoreAction::EnterSafeRegion, SynchronizationEffects },
      {
         RuntimeCoreAction::ParkMutator,
         SynchronizationEffects | RuntimeCallEffect::Call
      },
      {
         RuntimeCoreAction::AwaitCollection,
         SynchronizationEffects | RuntimeCallEffect::Call
      },
      { RuntimeCoreAction::LeaveSafeRegion, SynchronizationEffects },
      { RuntimeCoreAction::RetryOperation, SynchronizationEffects },
      { RuntimeCoreAction::PublishCollector, SynchronizationEffects },
      {
         RuntimeCoreAction::EnumerateMutators,
         SynchronizationEffects | RuntimeCallEffect::ReadTLS
      },
      {
         RuntimeCoreAction::WaitForMutators,
         SynchronizationEffects | RuntimeCallEffect::Call
      },
      { RuntimeCoreAction::AcquireRootLock, SynchronizationEffects },

      { RuntimeCoreAction::BeginRoots, RootEffects },
      { RuntimeCoreAction::AppendStaticRoots, RootEffects },
      { RuntimeCoreAction::AppendPermanentRoots, RootEffects },
      {
         RuntimeCoreAction::AppendTLSRoots,
         RootEffects | RuntimeCallEffect::ReadTLS
      },
      { RuntimeCoreAction::AppendFrameRoots, RootEffects },

      { RuntimeCoreAction::InvokeCollector, CollectorEffects },
      {
         RuntimeCoreAction::ResumeMutators,
         SynchronizationEffects | RuntimeCallEffect::Call
      },
      { RuntimeCoreAction::ReleaseRootLock, SynchronizationEffects },
      { RuntimeCoreAction::Return, RuntimeCallEffect::None }
   };

   constexpr RuntimeCoreStep SingleThreadPermanentSteps[] = {
      { RuntimeCoreAction::PublishFrame, RuntimeCallEffect::WriteGlobal },

      { RuntimeCoreAction::InvokeCollector, CollectorEffects },
      { RuntimeCoreAction::Return, RuntimeCallEffect::None }
   };

   constexpr RuntimeCoreStep MultiThreadPermanentSteps[] = {
      { RuntimeCoreAction::AcquireAllocationLock, SynchronizationEffects },
      { RuntimeCoreAction::PublishFrame, RuntimeCallEffect::WriteGlobal },

      { RuntimeCoreAction::InvokeCollector, CollectorEffects },
      { RuntimeCoreAction::ReleaseAllocationLock, SynchronizationEffects },
      { RuntimeCoreAction::Return, RuntimeCallEffect::None }
   };

   struct RuntimeCoreProtocolDefinition
   {
      const RuntimeCoreStep* steps;
      unsigned char          count;
   };

   template <unsigned int Count>
   constexpr unsigned char protocolStepCount(const RuntimeCoreStep (&)[Count])
   {
      return (unsigned char)Count;
   }

   RuntimeCoreProtocolDefinition getProtocolDefinition(
      RuntimeOperation operation, ThreadingMode threadingMode)
   {
      if (operation == RuntimeOperation::AllocatePermanent) {
         if (threadingMode == ThreadingMode::MultiThread) {
            return {
               .steps = MultiThreadPermanentSteps,
               .count = protocolStepCount(MultiThreadPermanentSteps)
            };
         }

         return {
            .steps = SingleThreadPermanentSteps,
            .count = protocolStepCount(SingleThreadPermanentSteps)
         };
      }

      if (operation != RuntimeOperation::Collect)
         return {};

      if (threadingMode == ThreadingMode::MultiThread) {
         return {
            .steps = MultiThreadCollectSteps,
            .count = protocolStepCount(MultiThreadCollectSteps)
         };
      }

      return {
         .steps = SingleThreadCollectSteps,
         .count = protocolStepCount(SingleThreadCollectSteps)
      };
   }

   bool addCoreStep(RuntimeCoreProtocol& protocol, const RuntimeCoreStep& step)
   {
      unsigned int actionMask = runtimeCoreAction(step.action);
      if (protocol.count >= RuntimeCoreProtocol::MaxStepCount
         || (protocol.actionMask & actionMask) != 0)
      {
         return false;
      }

      protocol.steps[protocol.count++] = step;
      protocol.actionMask |= actionMask;

      return true;
   }
}


bool RuntimeCoreProtocol :: contains(RuntimeCoreAction action) const
{
   return (actionMask & runtimeCoreAction(action)) != 0;
}

bool RuntimeCoreProtocol :: isValid(const RuntimeSpec& runtime) const
{
   if ((operation != RuntimeOperation::Collect
         && operation != RuntimeOperation::AllocatePermanent)
      || threadingMode != runtime.threadingMode
      || count == 0 || count > MaxStepCount)
   {
      return false;
   }

   RuntimeCoreProtocolDefinition definition = getProtocolDefinition(operation, threadingMode);

   if (count != definition.count)
      return false;

   unsigned int observedActions = 0;

   for (unsigned char i = 0; i < count; i++) {
      if ((unsigned char)steps[i].action >= (unsigned char)RuntimeCoreAction::Count)
         return false;

      unsigned int stepMask = runtimeCoreAction(steps[i].action);

      if ((observedActions & stepMask) != 0
         || steps[i].action != definition.steps[i].action
         || steps[i].effects != definition.steps[i].effects)
      {
         return false;
      }

      observedActions |= stepMask;
   }

   return observedActions == actionMask;
}

bool RuntimeCoreProtocolProvider :: get(
   RuntimeOperation operation,
   const RuntimeSpec& runtime,
   RuntimeCoreProtocol& protocol)
{
   if (operation != RuntimeOperation::Collect
      && operation != RuntimeOperation::AllocatePermanent)
   {
      return false;
   }

   protocol = {
      .operation = operation,
      .threadingMode = runtime.threadingMode,
      .steps = {},
      .actionMask = 0,
      .count = 0
   };

   RuntimeCoreProtocolDefinition definition = getProtocolDefinition(operation, runtime.threadingMode);

   for (unsigned char i = 0; i < definition.count; i++) {
      if (!addCoreStep(protocol, definition.steps[i]))
         return false;
   }

   return protocol.isValid(runtime);
}
