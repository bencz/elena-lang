#ifndef CODEGEN_DISPATCH_H
#define CODEGEN_DISPATCH_H

#include "../engine/bytecode.h"

namespace elena_lang::codegen
{
   enum class DispatchOption : unsigned char
   {
      None           = 0x00,
      VirtualTarget  = 0x01,
      Variadic       = 0x02,
      ReceiverLists  = 0x04,
      AlternativeVMT = 0x08
   };

   enum class DispatchAction : unsigned char
   {
      PreserveState,
      LocateArguments,
      CountArguments,
      SelectList,
      SelectOverload,
      LoadSignature,
      MatchArgument,
      WalkAncestors,
      ResolveDirectTarget,
      ResolveVirtualTarget,
      ResolveAlternativeVMT,
      RestoreState,
      BranchTarget,
      Fallthrough
   };

   enum class DispatchPhase : unsigned char
   {
      PreserveState,
      LocateArguments,
      CountArguments,
      AdvanceArgumentCount,
      TestArgumentSentinel,
      SelectList,
      SelectOverload,
      LoadSignature,
      AdvanceParameter,
      TestArgumentsComplete,
      LoadArgumentTypes,
      TestArgumentType,
      LoadParent,
      TestParent,
      ResolveTarget,
      AdvanceOverload,
      AdvanceList,
      RestoreSuccess,
      RestoreFailure,
      LoadReceiverVMT,
      SelectAlternativeVMT,
      ResolveVirtualTarget,
      BranchTarget,
      Fallthrough,
      Count
   };

   enum class DispatchFrameSlot : unsigned char
   {
      Object,
      ListIndex,
      SignatureCursor,
      ArgumentCount,
      Count,
      Invalid = 0xFF
   };

   enum class DispatchBlockProperty : unsigned char
   {
      None        = 0x00,
      Conditional = 0x01,
      Terminal    = 0x02
   };

   constexpr DispatchOption operator | (DispatchOption left, DispatchOption right)
   {
      return (DispatchOption)((unsigned char)left | (unsigned char)right);
   }

   constexpr bool test(DispatchOption value, DispatchOption mask)
   {
      return ((unsigned char)value & (unsigned char)mask) == (unsigned char)mask;
   }

   constexpr DispatchBlockProperty operator | (DispatchBlockProperty left, DispatchBlockProperty right)
   {
      return (DispatchBlockProperty)((unsigned char)left | (unsigned char)right);
   }

   constexpr bool test(DispatchBlockProperty value, DispatchBlockProperty mask)
   {
      return ((unsigned char)value & (unsigned char)mask) == (unsigned char)mask;
   }

   struct DispatchBlock
   {
      DispatchPhase phase;
      DispatchBlockProperty properties;
      unsigned char next;
      unsigned char alternate;

      bool has(DispatchBlockProperty property) const;
   };

   struct DispatchControlFlow
   {
      static constexpr unsigned char InvalidBlock = 0xFF;

      DispatchBlock blocks[(unsigned int)DispatchPhase::Count];
      unsigned char blockCount;

      unsigned char blockId(DispatchPhase phase) const;
      bool isValid() const;
   };

   struct DispatchSpec
   {
      DispatchOption options;
      mssg_t         message;
      ref_t          listReference;
      unsigned char  firstArgument;
      unsigned char  fixedArgumentCount;
      DispatchAction actions[14];
      unsigned char  actionCount;

      bool has(DispatchOption option) const;
   };

   struct DispatchFrameLayout
   {
      unsigned char slots[(unsigned int)DispatchFrameSlot::Count];
      unsigned char slotCount;

      unsigned char operator [](DispatchFrameSlot slot) const;
      bool contains(DispatchFrameSlot slot) const;
      bool isValid(const DispatchSpec& spec) const;
   };

   class DispatchProvider
   {
   public:
      static bool get(const ByteCommand& command, bool alternativeMode, DispatchSpec& spec);
      static DispatchBlockProperty getBlockProperties(DispatchPhase phase);
      static bool buildControlFlow(const DispatchSpec& spec, DispatchControlFlow& controlFlow);
      static bool buildFrameLayout(const DispatchSpec& spec, DispatchFrameLayout& layout);
   };
}

#endif
