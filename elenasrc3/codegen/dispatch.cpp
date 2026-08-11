#include "dispatch.h"

using namespace elena_lang;
using namespace elena_lang::codegen;

bool DispatchSpec :: has(DispatchOption option) const
{
   return test(options, option);
}

bool DispatchBlock :: has(DispatchBlockProperty property) const
{
   return test(properties, property);
}

unsigned char DispatchFrameLayout :: operator [](DispatchFrameSlot slot) const
{
   return slots[(unsigned int)slot];
}

bool DispatchFrameLayout :: contains(DispatchFrameSlot slot) const
{
   return (*this)[slot] != (unsigned char)DispatchFrameSlot::Invalid;
}

bool DispatchFrameLayout :: isValid(const DispatchSpec& spec) const
{
   if (slotCount == 0 || slotCount > (unsigned int)DispatchFrameSlot::Count
      || (*this)[DispatchFrameSlot::Object] != 0)
   {
      return false;
   }

   bool receiverLists = spec.has(DispatchOption::ReceiverLists);
   bool variadic = spec.has(DispatchOption::Variadic);
   if (contains(DispatchFrameSlot::ListIndex) != receiverLists
      || contains(DispatchFrameSlot::SignatureCursor) != variadic
      || contains(DispatchFrameSlot::ArgumentCount) != variadic)
   {
      return false;
   }

   for (unsigned int i = 0; i < (unsigned int)DispatchFrameSlot::Count; i++) {
      unsigned char slot = slots[i];
      if (slot != (unsigned char)DispatchFrameSlot::Invalid
         && slot >= slotCount)
      {
         return false;
      }
      for (unsigned int j = 0; j < i; j++) {
         if (slot != (unsigned char)DispatchFrameSlot::Invalid
            && slot == slots[j])
         {
            return false;
         }
      }
   }

   return true;
}

unsigned char DispatchControlFlow :: blockId(DispatchPhase phase) const
{
   for (unsigned char i = 0; i < blockCount; i++) {
      if (blocks[i].phase == phase)
         return i;
   }

   return InvalidBlock;
}

bool DispatchControlFlow :: isValid() const
{
   if (blockCount == 0 || blockCount > (unsigned int)DispatchPhase::Count)
      return false;

   for (unsigned char i = 0; i < blockCount; i++) {
      const DispatchBlock& block = blocks[i];
      if ((unsigned int)block.phase >= (unsigned int)DispatchPhase::Count)
         return false;

      for (unsigned char j = 0; j < i; j++) {
         if (blocks[j].phase == block.phase)
            return false;
      }

      if (block.has(DispatchBlockProperty::Terminal)) {
         if (block.next != InvalidBlock || block.alternate != InvalidBlock)
            return false;
      }
      else if (block.next >= blockCount) {
         return false;
      }

      if (block.has(DispatchBlockProperty::Conditional)) {
         if (block.alternate >= blockCount)
            return false;
      }
      else if (block.alternate != InvalidBlock) {
         return false;
      }
   }

   return true;
}

static void appendDispatchAction(DispatchSpec& spec, DispatchAction action)
{
   spec.actions[spec.actionCount++] = action;
}

DispatchBlockProperty DispatchProvider :: getBlockProperties(DispatchPhase phase)
{
   switch (phase) {
      case DispatchPhase::TestArgumentSentinel:
      case DispatchPhase::SelectList:
      case DispatchPhase::TestArgumentsComplete:
      case DispatchPhase::TestArgumentType:
      case DispatchPhase::TestParent:
      case DispatchPhase::AdvanceOverload:
         return DispatchBlockProperty::Conditional;
      case DispatchPhase::BranchTarget:
      case DispatchPhase::Fallthrough:
         return DispatchBlockProperty::Terminal;
      default:
         return DispatchBlockProperty::None;
   }
}

bool DispatchProvider :: get(const ByteCommand& command, bool alternativeMode, DispatchSpec& spec)
{
   if (command.code != ByteCode::XDispatchMR
      && command.code != ByteCode::DispatchMR)
   {
      return false;
   }

   bool virtualTarget = command.code == ByteCode::DispatchMR;
   bool variadic = ((unsigned int)command.arg1 & PREFIX_MESSAGE_MASK)
      == VARIADIC_MESSAGE;
   bool receiverLists = command.arg2 == 0;
   bool functionMode = elena_lang::test(
      (unsigned int)command.arg1, FUNCTION_MESSAGE);

   DispatchOption options = DispatchOption::None;
   if (virtualTarget)
      options = options | DispatchOption::VirtualTarget;
   if (variadic)
      options = options | DispatchOption::Variadic;
   if (receiverLists)
      options = options | DispatchOption::ReceiverLists;
   if (virtualTarget && alternativeMode)
      options = options | DispatchOption::AlternativeVMT;

   bool receiverListStartsWithObject = receiverLists && !variadic;

   spec = {
      .options = options,
      .message = (mssg_t)command.arg1,
      .listReference = receiverLists ? 0u : (ref_t)command.arg2,
      .firstArgument = (unsigned char)(
         receiverListStartsWithObject || functionMode ? 0 : 1),
      .fixedArgumentCount = (unsigned char)(((unsigned int)command.arg1 & ARG_MASK)
         + (functionMode ? 1 : 0)),
      .actions = {},
      .actionCount = 0
   };

   appendDispatchAction(spec, DispatchAction::PreserveState);
   appendDispatchAction(spec, DispatchAction::LocateArguments);

   if (variadic)
      appendDispatchAction(spec, DispatchAction::CountArguments);

   if (receiverLists)
      appendDispatchAction(spec, DispatchAction::SelectList);

   appendDispatchAction(spec, DispatchAction::SelectOverload);
   appendDispatchAction(spec, DispatchAction::LoadSignature);
   appendDispatchAction(spec, DispatchAction::MatchArgument);
   appendDispatchAction(spec, DispatchAction::WalkAncestors);

   if (virtualTarget) {
      if (alternativeMode)
         appendDispatchAction(spec, DispatchAction::ResolveAlternativeVMT);

      appendDispatchAction(spec, DispatchAction::ResolveVirtualTarget);
   }
   else {
      appendDispatchAction(spec, DispatchAction::ResolveDirectTarget);
   }

   appendDispatchAction(spec, DispatchAction::RestoreState);
   appendDispatchAction(spec, DispatchAction::BranchTarget);
   appendDispatchAction(spec, DispatchAction::Fallthrough);

   return true;
}

static void appendDispatchBlock(DispatchControlFlow& controlFlow, DispatchPhase phase)
{
   controlFlow.blocks[controlFlow.blockCount++] = {
      .phase = phase,
      .properties = DispatchProvider::getBlockProperties(phase),
      .next = DispatchControlFlow::InvalidBlock,
      .alternate = DispatchControlFlow::InvalidBlock
   };
}

static void setDispatchBranch(DispatchControlFlow& controlFlow, DispatchPhase phase, DispatchPhase next)
{
   unsigned char block = controlFlow.blockId(phase);
   controlFlow.blocks[block].next = controlFlow.blockId(next);
}

static void setDispatchCondition(
   DispatchControlFlow& controlFlow,
   DispatchPhase phase,
   DispatchPhase next,
   DispatchPhase alternate)
{
   unsigned char block = controlFlow.blockId(phase);
   controlFlow.blocks[block].next = controlFlow.blockId(next);
   controlFlow.blocks[block].alternate = controlFlow.blockId(alternate);
}

bool DispatchProvider :: buildControlFlow(const DispatchSpec& spec, DispatchControlFlow& controlFlow)
{
   bool variadic = spec.has(DispatchOption::Variadic);
   bool receiverLists = spec.has(DispatchOption::ReceiverLists);

   controlFlow = {};

   appendDispatchBlock(controlFlow, DispatchPhase::PreserveState);
   appendDispatchBlock(controlFlow, DispatchPhase::LocateArguments);

   if (variadic) {
      appendDispatchBlock(controlFlow, DispatchPhase::CountArguments);
      appendDispatchBlock(controlFlow, DispatchPhase::AdvanceArgumentCount);
      appendDispatchBlock(controlFlow, DispatchPhase::TestArgumentSentinel);
   }

   if (receiverLists)
      appendDispatchBlock(controlFlow, DispatchPhase::SelectList);

   appendDispatchBlock(controlFlow, DispatchPhase::SelectOverload);
   appendDispatchBlock(controlFlow, DispatchPhase::LoadSignature);
   appendDispatchBlock(controlFlow, DispatchPhase::AdvanceParameter);
   appendDispatchBlock(controlFlow, DispatchPhase::TestArgumentsComplete);
   appendDispatchBlock(controlFlow, DispatchPhase::LoadArgumentTypes);
   appendDispatchBlock(controlFlow, DispatchPhase::TestArgumentType);
   appendDispatchBlock(controlFlow, DispatchPhase::LoadParent);
   appendDispatchBlock(controlFlow, DispatchPhase::TestParent);
   appendDispatchBlock(controlFlow, DispatchPhase::ResolveTarget);
   appendDispatchBlock(controlFlow, DispatchPhase::AdvanceOverload);
   if (receiverLists)
      appendDispatchBlock(controlFlow, DispatchPhase::AdvanceList);
   appendDispatchBlock(controlFlow, DispatchPhase::RestoreSuccess);
   appendDispatchBlock(controlFlow, DispatchPhase::RestoreFailure);
   if (spec.has(DispatchOption::VirtualTarget)) {
      appendDispatchBlock(controlFlow, DispatchPhase::LoadReceiverVMT);
      if (spec.has(DispatchOption::AlternativeVMT)) {
         appendDispatchBlock(controlFlow,
            DispatchPhase::SelectAlternativeVMT);
      }
      appendDispatchBlock(controlFlow, DispatchPhase::ResolveVirtualTarget);
   }
   appendDispatchBlock(controlFlow, DispatchPhase::BranchTarget);
   appendDispatchBlock(controlFlow, DispatchPhase::Fallthrough);

   setDispatchBranch(controlFlow, DispatchPhase::PreserveState,
      DispatchPhase::LocateArguments);
   setDispatchBranch(controlFlow, DispatchPhase::LocateArguments,
      variadic ? DispatchPhase::CountArguments
         : receiverLists ? DispatchPhase::SelectList
            : DispatchPhase::SelectOverload);
   if (variadic) {
      setDispatchBranch(controlFlow, DispatchPhase::CountArguments,
         DispatchPhase::AdvanceArgumentCount);
      setDispatchBranch(controlFlow, DispatchPhase::AdvanceArgumentCount,
         DispatchPhase::TestArgumentSentinel);
      setDispatchCondition(controlFlow, DispatchPhase::TestArgumentSentinel,
         receiverLists ? DispatchPhase::SelectList
            : DispatchPhase::SelectOverload,
         DispatchPhase::AdvanceArgumentCount);
   }
   if (receiverLists) {
      setDispatchCondition(controlFlow, DispatchPhase::SelectList,
         DispatchPhase::SelectOverload, DispatchPhase::RestoreFailure);
   }
   setDispatchBranch(controlFlow, DispatchPhase::SelectOverload,
      DispatchPhase::LoadSignature);
   setDispatchBranch(controlFlow, DispatchPhase::LoadSignature,
      DispatchPhase::AdvanceParameter);
   setDispatchBranch(controlFlow, DispatchPhase::AdvanceParameter,
      DispatchPhase::TestArgumentsComplete);
   setDispatchCondition(controlFlow, DispatchPhase::TestArgumentsComplete,
      DispatchPhase::ResolveTarget, DispatchPhase::LoadArgumentTypes);
   setDispatchBranch(controlFlow, DispatchPhase::LoadArgumentTypes,
      DispatchPhase::TestArgumentType);
   setDispatchCondition(controlFlow, DispatchPhase::TestArgumentType,
      DispatchPhase::AdvanceParameter, DispatchPhase::LoadParent);
   setDispatchBranch(controlFlow, DispatchPhase::LoadParent,
      DispatchPhase::TestParent);
   setDispatchCondition(controlFlow, DispatchPhase::TestParent,
      DispatchPhase::TestArgumentType, DispatchPhase::AdvanceOverload);
   setDispatchBranch(controlFlow, DispatchPhase::ResolveTarget,
      DispatchPhase::RestoreSuccess);
   setDispatchCondition(controlFlow, DispatchPhase::AdvanceOverload,
      DispatchPhase::LoadSignature,
      receiverLists ? DispatchPhase::AdvanceList
         : DispatchPhase::RestoreFailure);
   if (receiverLists) {
      setDispatchBranch(controlFlow, DispatchPhase::AdvanceList,
         DispatchPhase::SelectList);
   }
   setDispatchBranch(controlFlow, DispatchPhase::RestoreSuccess,
      spec.has(DispatchOption::VirtualTarget)
         ? DispatchPhase::LoadReceiverVMT : DispatchPhase::BranchTarget);
   setDispatchBranch(controlFlow, DispatchPhase::RestoreFailure,
      DispatchPhase::Fallthrough);
   if (spec.has(DispatchOption::VirtualTarget)) {
      setDispatchBranch(controlFlow, DispatchPhase::LoadReceiverVMT,
         spec.has(DispatchOption::AlternativeVMT)
            ? DispatchPhase::SelectAlternativeVMT
            : DispatchPhase::ResolveVirtualTarget);
      if (spec.has(DispatchOption::AlternativeVMT)) {
         setDispatchBranch(controlFlow, DispatchPhase::SelectAlternativeVMT,
            DispatchPhase::ResolveVirtualTarget);
      }
      setDispatchBranch(controlFlow, DispatchPhase::ResolveVirtualTarget,
         DispatchPhase::BranchTarget);
   }
   return controlFlow.isValid();
}

bool DispatchProvider :: buildFrameLayout(const DispatchSpec& spec,
   DispatchFrameLayout& layout)
{
   layout = {};
   for (unsigned int i = 0; i < (unsigned int)DispatchFrameSlot::Count; i++)
      layout.slots[i] = (unsigned char)DispatchFrameSlot::Invalid;

   layout.slots[(unsigned int)DispatchFrameSlot::Object] = layout.slotCount++;
   if (spec.has(DispatchOption::ReceiverLists)) {
      layout.slots[(unsigned int)DispatchFrameSlot::ListIndex]
         = layout.slotCount++;
   }
   if (spec.has(DispatchOption::Variadic)) {
      layout.slots[(unsigned int)DispatchFrameSlot::SignatureCursor]
         = layout.slotCount++;
      layout.slots[(unsigned int)DispatchFrameSlot::ArgumentCount]
         = layout.slotCount++;
   }

   return layout.isValid(spec);
}
