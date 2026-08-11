#include "lowering.h"

using namespace elena_lang;
using namespace elena_lang::codegen;
using namespace elena_lang::codegen::x86;

enum class DispatchEntryStackSlot : unsigned char
{
   CachedArgument0 = 1,
   CachedArgument1 = 2
};

template <class Slot>
static int stackSlotOffset(int wordSize, Slot slot)
{
   return wordSize * (int)slot;
}

static int stackSpan(int wordSize, int slotCount)
{
   return wordSize * slotCount;
}

static unsigned int architectureWordSize(Architecture architecture)
{
   return architecture == Architecture::X86 ? 4 : 8;
}

static Operand emptyOperand()
{
   return {
      .reg = Register::None,
      .size = OperandSize::None,
      .kind = ValueKind::None
   };
}

static Operand operand(Register reg, OperandSize size, ValueKind kind)
{
   return {
      .reg = reg,
      .size = size,
      .kind = kind
   };
}

static Instruction unary(Opcode opcode, Operand destination, MIREffect effects)
{
   return {
      .opcode = opcode,
      .destination = destination,
      .source = emptyOperand(),
      .immediate = 0,
      .condition = Condition::None,
      .effects = effects
   };
}

static Instruction binary(
   Opcode opcode,
   Operand destination,
   Operand source,
   Condition condition,
   MIREffect effects)
{
   return {
      .opcode = opcode,
      .destination = destination,
      .source = source,
      .immediate = 0,
      .condition = condition,
      .effects = effects
   };
}

static Instruction immediate(Opcode opcode, Operand destination, int value, MIREffect effects)
{
   return {
      .opcode = opcode,
      .destination = destination,
      .source = emptyOperand(),
      .immediate = value,
      .condition = Condition::None,
      .effects = effects
   };
}

static Instruction load(Operand destination, Operand source, int offset, Opcode opcode = Opcode::LoadOffset)
{
   return {
      .opcode = opcode,
      .destination = destination,
      .source = source,
      .immediate = offset,
      .condition = Condition::None,
      .effects = MIREffect::ReadMemory
   };
}

static Instruction noOperation()
{
   return {
      .opcode = Opcode::Nop,
      .destination = emptyOperand(),
      .source = emptyOperand(),
      .immediate = 0,
      .condition = Condition::None,
      .effects = MIREffect::None
   };
}

static Instruction store(Operand address, Operand value, int offset, Opcode opcode = Opcode::StoreOffset)
{
   return {
      .opcode = opcode,
      .destination = address,
      .source = value,
      .immediate = offset,
      .condition = Condition::None,
      .effects = MIREffect::WriteMemory
   };
}

static Instruction moveRuntimeData(Operand destination, RuntimeDataReference reference)
{
   return immediate(Opcode::MoveRuntimeData, destination, (int)reference, MIREffect::None);
}

static Instruction atomicDWord(Opcode opcode, Operand address, Operand value, int offset = 0)
{
   return {
      .opcode = opcode,
      .destination = address,
      .source = value,
      .immediate = offset,
      .condition = Condition::None,
      .effects = MIREffect::ReadMemory | MIREffect::WriteMemory | MIREffect::WriteFlags | MIREffect::Synchronize
   };
}

static Instruction atomicByte(Opcode opcode, Operand address, Operand value, int offset)
{
   return {
      .opcode = opcode,
      .destination = address,
      .source = value,
      .immediate = offset,
      .condition = Condition::None,
      .effects = MIREffect::ReadMemory | MIREffect::WriteMemory
         | MIREffect::WriteFlags | MIREffect::Synchronize
   };
}

static void acquireGCLock(Operand lockAddress, Sequence& sequence);
static void releaseGCLock(Operand lockAddress, Sequence& sequence);

static Instruction callRuntime(RuntimeOperation operation, Operand argument, const RuntimeCallABI& runtimeABI)
{
   return {
      .opcode = Opcode::CallRuntime,
      .destination = emptyOperand(),
      .source = argument,
      .immediate = (int)operation,
      .condition = Condition::None,
      .effects = machineEffects(runtimeABI.effects)
   };
}

static void loadThreadContent(Sequence& sequence, const RuntimeSpec& runtime, Operand thread)
{
   if (runtime.threadingMode == ThreadingMode::MultiThread) {
      sequence.add(immediate(
         Opcode::LoadCurrentThread,
         thread,
         runtime.dataLayout.threadContent.size,
         MIREffect::ReadMemory | MIREffect::ReadTLS));
   }
   else {
      sequence.add(moveRuntimeData(thread, RuntimeDataReference::SingleContent));
   }
}

static Instruction indexedLoad(Operand destination, Operand source, Register index, unsigned char scale, int offset)
{
   return {
      .opcode = Opcode::LoadMemory,
      .destination = destination,
      .source = source,
      .immediate = offset,
      .condition = Condition::None,
      .effects = MIREffect::ReadMemory,
      .index = index,
      .scale = scale
   };
}

static Instruction compareMemory(Operand value, Operand source, int offset)
{
   return {
      .opcode = Opcode::CompareMemory,
      .destination = value,
      .source = source,
      .immediate = offset,
      .condition = Condition::None,
      .effects = MIREffect::ReadMemory | MIREffect::WriteFlags
   };
}

static Instruction local(Opcode opcode, int label, MIREffect effects)
{
   return {
      .opcode = opcode,
      .destination = emptyOperand(),
      .source = emptyOperand(),
      .immediate = label,
      .condition = Condition::None,
      .effects = effects
   };
}

static Instruction multiply(Operand destination, Operand source, int value)
{
   return {
      .opcode = Opcode::MultiplyImmediate,
      .destination = destination,
      .source = source,
      .immediate = value,
      .condition = Condition::None,
      .effects = MIREffect::WriteFlags
   };
}

static bool scaleIndex(long long value, int scale, int& result)
{
   long long scaled = (long long)value * scale;
   if (scaled < -0x80000000LL || scaled > 0x7FFFFFFFLL)
      return false;

   result = (int)scaled;

   return true;
}

static LowerError selectExceptionHook(
   EIRFunction& function,
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   Sequence& sequence);

static bool isValidDispatchRuntime(const RuntimeSpec& runtime, const ManagedABI& abi)
{
   int word = architectureWordSize(abi.architecture);

   return abi.isValid()
      && runtime.objectLayout.fieldSize == word
      && runtime.objectLayout.headerSize == runtime.objectLayout.vmtOffset
      && runtime.vmtLayout.sizeOffset == RuntimeLayout::offsetOf(word, VMTHeaderField::Size)
      && runtime.vmtLayout.parentOffset == RuntimeLayout::offsetOf(word, VMTHeaderField::Parent)
      && runtime.vmtLayout.methodOffset == RuntimeLayout::offsetOf(word, VMTTableField::FirstMethod)
      && runtime.vmtLayout.entrySize == RuntimeLayout::offsetOf(word, VMTTableField::EntryEnd);
}

static bool getPowerOfTwoShift(unsigned int value, unsigned char& shift)
{
   shift = 0;
   if (value == 0)
      return false;

   while ((value & 1) == 0) {
      value >>= 1;
      shift++;
   }

   return value == 1;
}

static LowerError selectVirtualMethod(const VirtualMethodSpec& method,
   EIRFunction& function, const RuntimeSpec& runtime,
   const ManagedABI& abi, Sequence& sequence)
{
   int word = abi.architecture == Architecture::X86 ? 4 : 8;
   unsigned char entryShift = 0;
   if (!abi.isValid()
      || runtime.objectLayout.fieldSize != word
      || runtime.objectLayout.headerSize != runtime.objectLayout.vmtOffset
      || runtime.vmtLayout.sizeOffset != RuntimeLayout::offsetOf(
         word, VMTHeaderField::Size)
      || runtime.vmtLayout.methodOffset != RuntimeLayout::offsetOf(
         word, VMTTableField::FirstMethod)
      || runtime.vmtLayout.entrySize != RuntimeLayout::offsetOf(
         word, VMTTableField::EntryEnd)
      || !getPowerOfTwoShift(runtime.vmtLayout.entrySize, entryShift)
      || function.blockCount() != 1)
   {
      return LowerError::InvalidRuntime;
   }

   Operand object = operand(abi.object, abi.wordSize, ValueKind::Reference);
   Operand vmt = operand(Register::A, abi.wordSize, ValueKind::VMT);
   Operand vmtValue = operand(Register::A, abi.wordSize, ValueKind::Integer);
   Operand methodOffset = operand(Register::C, OperandSize::DWord,
      ValueKind::Integer);
   Operand vmtSize = operand(Register::DI, abi.wordSize, ValueKind::Integer);
   Operand target = operand(Register::A, abi.wordSize, ValueKind::Address);
   MIREffect managedCall = MIREffect::ReadMemory | MIREffect::WriteMemory
      | MIREffect::Call | MIREffect::Safepoint | MIREffect::MayThrow;

   bool loadedVMT = false;
   bool loadedMethodOffset = false;
   bool resolvedTarget = false;
   bool transferred = false;
   sequence.clear();

   EIRBlock& block = function.block(0);
   for (pos_t i = 0; i < block.instructionCount; i++) {
      EIRInstruction& instruction = function.instruction(
         block.firstInstruction + i);
      switch (instruction.opcode) {
         case EIROpcode::ObjectVMT:
            sequence.add(load(vmt, object,
               -(int)runtime.objectLayout.vmtOffset));
            loadedVMT = true;
            break;
         case EIROpcode::MethodOffset:
         {
            if (instruction.operandCount != 3)
               return LowerError::InvalidMIR;

            EIROperand& classReference = function.operand(
               instruction.firstOperand);
            EIROperand& message = function.operand(
               instruction.firstOperand + 1);
            EIROperand& options = function.operand(
               instruction.firstOperand + 2);
            if (classReference.value != method.classReference
               || message.value != method.message
               || options.value != (pos64_t)method.options)
            {
               return LowerError::InvalidMIR;
            }

            sequence.add(immediate(
               method.has(MethodLookupOption::AlternativeVMT)
                  ? Opcode::MoveHMTMethodOffset
                  : Opcode::MoveVMTMethodOffset,
               methodOffset,
               (int)method.message,
               MIREffect::None));
            loadedMethodOffset = true;
            break;
         }
         case EIROpcode::SelectAlternativeVMT:
            if (!loadedVMT
               || !method.has(MethodLookupOption::AlternativeVMT))
            {
               return LowerError::InvalidMIR;
            }

            sequence.add(load(vmtSize, vmt,
               -(int)runtime.vmtLayout.sizeOffset));
            sequence.add(immediate(Opcode::ShiftLeftImmediate, vmtSize,
               entryShift, MIREffect::WriteFlags));
            sequence.add(binary(Opcode::Add, vmtValue, vmtSize,
               Condition::None, MIREffect::WriteFlags));
            break;
         case EIROpcode::ResolveVirtualMethod:
            if (!loadedVMT || !loadedMethodOffset)
               return LowerError::InvalidMIR;

            sequence.add(indexedLoad(target, vmt, Register::C, 0,
               runtime.vmtLayout.methodOffset));
            resolvedTarget = true;
            break;
         case EIROpcode::CallIndirect:
            if (!resolvedTarget || method.transfer != MethodTransferKind::Call)
               return LowerError::InvalidMIR;

            sequence.add(unary(Opcode::CallRegister, target, managedCall));
            transferred = true;
            break;
         case EIROpcode::IndirectBranch:
            if (!resolvedTarget || method.transfer != MethodTransferKind::Jump)
               return LowerError::InvalidMIR;

            sequence.add(unary(Opcode::JumpRegister, target,
               MIREffect::None));
            transferred = true;
            break;
         case EIROpcode::Fallthrough:
            if (!transferred || method.transfer != MethodTransferKind::Call)
               return LowerError::InvalidMIR;
            break;
         default:
            return LowerError::InvalidMIR;
      }
   }

   if (!transferred)
      return LowerError::InvalidMIR;

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError selectManagedMethod(const ManagedMethodSpec& method,
   EIRFunction& function, const RuntimeSpec& runtime,
   const ManagedABI& abi, Sequence& sequence)
{
   if (!abi.isValid() || function.blockCount() != 1)
      return LowerError::InvalidABI;

   int word = abi.architecture == Architecture::X86 ? 4 : 8;
   if (method.target == ManagedMethodTarget::VMTIndex
      && (runtime.objectLayout.fieldSize != word
         || runtime.objectLayout.headerSize != runtime.objectLayout.vmtOffset
         || runtime.vmtLayout.methodOffset != RuntimeLayout::offsetOf(
            word, VMTTableField::FirstMethod)
         || runtime.vmtLayout.entrySize != RuntimeLayout::offsetOf(
            word, VMTTableField::EntryEnd)))
   {
      return LowerError::InvalidRuntime;
   }

   Operand object = operand(abi.object, abi.wordSize, ValueKind::Reference);
   Operand target = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand vmt = operand(Register::A, abi.wordSize, ValueKind::VMT);
   MIREffect managedCall = MIREffect::ReadMemory | MIREffect::WriteMemory
      | MIREffect::Call | MIREffect::Safepoint | MIREffect::MayThrow;

   bool loadedVMT = false;
   bool resolvedTarget = false;
   bool transferred = false;
   sequence.clear();

   EIRBlock& block = function.block(0);
   for (pos_t i = 0; i < block.instructionCount; i++) {
      EIRInstruction& instruction = function.instruction(
         block.firstInstruction + i);
      switch (instruction.opcode) {
         case EIROpcode::ObjectVMT:
            if (method.target != ManagedMethodTarget::VMTIndex)
               return LowerError::InvalidMIR;

            sequence.add(load(vmt, object,
               -(int)runtime.objectLayout.vmtOffset));
            loadedVMT = true;
            break;
         case EIROpcode::ResolveVirtualIndex:
         {
            if (!loadedVMT || method.target != ManagedMethodTarget::VMTIndex
               || instruction.operandCount != 2)
            {
               return LowerError::InvalidMIR;
            }

            EIROperand& index = function.operand(
               instruction.firstOperand + 1);
            int entryOffset = 0;
            if (index.value != (pos64_t)method.index
               || !scaleIndex(method.index,
                  runtime.vmtLayout.entrySize, entryOffset)
               || entryOffset > 0x7FFFFFFF - runtime.vmtLayout.methodOffset)
            {
               return LowerError::InvalidArgument;
            }

            sequence.add(load(target, vmt,
               entryOffset + runtime.vmtLayout.methodOffset));
            resolvedTarget = true;
            break;
         }
         case EIROpcode::MethodAddress:
         {
            if (method.target != ManagedMethodTarget::VMTMethod
               || instruction.operandCount != 3)
            {
               return LowerError::InvalidMIR;
            }

            EIROperand& classReference = function.operand(
               instruction.firstOperand);
            EIROperand& message = function.operand(
               instruction.firstOperand + 1);
            EIROperand& options = function.operand(
               instruction.firstOperand + 2);
            if (classReference.value != method.reference
               || message.value != method.message
               || options.value != (pos64_t)method.options)
            {
               return LowerError::InvalidMIR;
            }

            resolvedTarget = true;
            break;
         }
         case EIROpcode::CallDirect:
         {
            if (method.target != ManagedMethodTarget::Symbol
               || method.transfer != MethodTransferKind::Call
               || instruction.operandCount != 1)
            {
               return LowerError::InvalidMIR;
            }

            EIROperand& reference = function.operand(
               instruction.firstOperand);
            if (reference.value != method.reference)
               return LowerError::InvalidMIR;

            sequence.add(local(Opcode::CallCodeReference,
               (int)method.reference, managedCall));
            transferred = true;
            break;
         }
         case EIROpcode::CallIndirect:
            if (!resolvedTarget || method.transfer != MethodTransferKind::Call)
               return LowerError::InvalidMIR;

            if (method.target == ManagedMethodTarget::VMTIndex) {
               sequence.add(unary(Opcode::CallRegister, target, managedCall));
            }
            else {
               sequence.add(local(
                  method.has(MethodLookupOption::AlternativeVMT) ? Opcode::CallHMTMethod : Opcode::CallVMTMethod,
                  (int)method.message,
                  managedCall));
            }

            transferred = true;
            break;
         case EIROpcode::IndirectBranch:
            if (!resolvedTarget || method.transfer != MethodTransferKind::Jump)
               return LowerError::InvalidMIR;

            if (method.target == ManagedMethodTarget::VMTIndex) {
               sequence.add(unary(Opcode::JumpRegister, target, MIREffect::None));
            }
            else {
               sequence.add(local(
                  method.has(MethodLookupOption::AlternativeVMT) ? Opcode::JumpHMTMethod : Opcode::JumpVMTMethod,
                  (int)method.message,
                  MIREffect::None));
            }

            transferred = true;
            break;
         case EIROpcode::Fallthrough:
            if (!transferred || method.transfer != MethodTransferKind::Call)
               return LowerError::InvalidMIR;
            break;
         default:
            return LowerError::InvalidMIR;
      }
   }

   if (!transferred)
      return LowerError::InvalidMIR;

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static bool getDispatchCondition(DispatchPhase phase, Opcode& trueJump,
   Opcode& falseJump)
{
   switch (phase) {
      case DispatchPhase::TestArgumentSentinel:
      case DispatchPhase::TestArgumentsComplete:
      case DispatchPhase::TestArgumentType:
         trueJump = Opcode::JumpZero;
         falseJump = Opcode::JumpNotEqual;

         return true;
      case DispatchPhase::SelectList:
      case DispatchPhase::TestParent:
      case DispatchPhase::AdvanceOverload:
         trueJump = Opcode::JumpNotEqual;
         falseJump = Opcode::JumpZero;

         return true;
      default:
         return false;
   }
}

static void restoreDispatchState(const DispatchFrameLayout& frameLayout,
   int word, const ManagedABI& abi, Operand stack, Operand stackValue,
   Operand object, Sequence& sequence)
{
   sequence.add(unary(Opcode::Pop, object, MIREffect::ReadMemory));

   int localAreaSize = word * (frameLayout.slotCount - 1);
   if (localAreaSize != 0) {
      sequence.add(immediate(Opcode::AddImmediate, stackValue,
         localAreaSize, MIREffect::WriteFlags));
   }

   sequence.add(load(
      operand(abi.cachedArgument0, abi.wordSize, ValueKind::Reference),
      stack, stackSlotOffset(word, DispatchEntryStackSlot::CachedArgument0)));
   if (abi.cachedArgument1 != Register::None) {
      sequence.add(load(
         operand(abi.cachedArgument1, abi.wordSize, ValueKind::Reference),
         stack, stackSlotOffset(word,
            DispatchEntryStackSlot::CachedArgument1)));
   }
}

static LowerError selectXDispatch(const DispatchSpec& dispatch, EIRFunction& function,
   const RuntimeSpec& runtime, const ManagedABI& abi,
   Sequence& sequence)
{
   if (function.blockCount() == 0
      || function.blockCount() > 31
      || !isValidDispatchRuntime(runtime, abi))
   {
      return LowerError::InvalidRuntime;
   }

   DispatchFrameLayout frameLayout = {};
   if (!DispatchProvider::buildFrameLayout(dispatch, frameLayout))
      return LowerError::InvalidArgument;

   bool variadic = dispatch.has(DispatchOption::Variadic);
   bool receiverLists = dispatch.has(DispatchOption::ReceiverLists);
   bool virtualTarget = dispatch.has(DispatchOption::VirtualTarget);
   bool alternativeVMT = dispatch.has(DispatchOption::AlternativeVMT);

   int word = runtime.objectLayout.fieldSize;
   int argumentBaseOffset = 0;
   if (!scaleIndex(dispatch.firstArgument, word, argumentBaseOffset))
      return LowerError::InvalidArgument;

   unsigned char wordScale = abi.architecture == Architecture::X86 ? 2 : 3;
   int messageEntryShift = wordScale + 1;
   int objectOffset = stackSlotOffset(word,
      frameLayout[DispatchFrameSlot::Object]);
   int listIndexOffset = receiverLists
      ? stackSlotOffset(word, frameLayout[DispatchFrameSlot::ListIndex]) : 0;
   int signatureCursorOffset = variadic
      ? stackSlotOffset(word,
         frameLayout[DispatchFrameSlot::SignatureCursor]) : 0;
   int argumentCountOffset = variadic
      ? stackSlotOffset(word,
         frameLayout[DispatchFrameSlot::ArgumentCount]) : 0;

   Operand stack = operand(abi.stack, abi.wordSize, ValueKind::Address);
   Operand stackValue = operand(abi.stack, abi.wordSize, ValueKind::Integer);
   Operand object = operand(abi.object, abi.wordSize, ValueKind::Reference);
   Operand arguments = operand(Register::A, abi.wordSize, ValueKind::Reference);
   Operand target = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand method = operand(Register::A, abi.wordSize, ValueKind::Integer);
   Operand list = operand(Register::SI, abi.wordSize, ValueKind::Reference);
   Operand signature = operand(Register::B, abi.wordSize, ValueKind::Reference);
   Operand message = operand(Register::B, abi.wordSize, ValueKind::Integer);
   Operand parameter = operand(Register::C, abi.wordSize, ValueKind::Integer);
   Operand overload = operand(Register::D, abi.wordSize, ValueKind::Integer);
   Operand importedMessage = operand(Register::D, OperandSize::DWord,
      ValueKind::Integer);
   Operand actual = operand(Register::DI, abi.wordSize, ValueKind::Reference);
   Operand scanned = operand(Register::DI, abi.wordSize, ValueKind::Integer);
   Operand actualVMT = operand(Register::DI, abi.wordSize, ValueKind::VMT);
   Operand expectedVMT = operand(Register::SI, abi.wordSize, ValueKind::VMT);
   Operand signatureCursor = operand(Register::SI, abi.wordSize,
      ValueKind::Reference);
   Operand signatureCandidate = operand(Register::DI, abi.wordSize,
      ValueKind::Reference);
   Operand metadata = operand(Register::DI, abi.wordSize, ValueKind::Reference);
   Operand nil = operand(Register::SI, abi.wordSize, ValueKind::Reference);
   Operand vmt = operand(Register::C, abi.wordSize, ValueKind::VMT);
   Operand vmtValue = operand(Register::C, abi.wordSize, ValueKind::Integer);
   Operand vmtSize = operand(Register::DI, abi.wordSize, ValueKind::Integer);

   sequence.clear();

   for (pos_t i = 0; i < function.blockCount(); i++) {
      EIRBlock& block = function.block(i);
      DispatchPhase phase = DispatchPhase::Count;
      if (!DispatchEIRProvider::getPhase(function, block, phase))
         return LowerError::InvalidMIR;

      sequence.add(local(Opcode::Label, block.id, MIREffect::None));

      switch (phase) {
         case DispatchPhase::PreserveState:
            sequence.add({ Opcode::StoreOffset, stack,
               operand(abi.cachedArgument0, abi.wordSize,
                  ValueKind::Reference),
               stackSlotOffset(word,
                  DispatchEntryStackSlot::CachedArgument0),
               Condition::None, MIREffect::WriteMemory });
            if (abi.cachedArgument1 != Register::None) {
               sequence.add({ Opcode::StoreOffset, stack,
                  operand(abi.cachedArgument1, abi.wordSize,
                     ValueKind::Reference),
                  stackSlotOffset(word,
                     DispatchEntryStackSlot::CachedArgument1),
                  Condition::None, MIREffect::WriteMemory });
            }
            break;
         case DispatchPhase::LocateArguments:
            sequence.add({ Opcode::AddressOffsetFrom, arguments, stack,
               argumentBaseOffset, Condition::None,
               MIREffect::None });
            if (variadic) {
               sequence.add(unary(Opcode::Clear, parameter,
                  MIREffect::WriteFlags));
               sequence.add(unary(Opcode::Push, parameter,
                  MIREffect::WriteMemory));
               sequence.add(unary(Opcode::Push, parameter,
                  MIREffect::WriteMemory));
            }
            if (receiverLists) {
               sequence.add(immediate(Opcode::MoveImmediate, parameter,
                  dispatch.fixedArgumentCount, MIREffect::None));
               sequence.add(unary(Opcode::Push, parameter,
                  MIREffect::WriteMemory));
            }
            sequence.add(unary(Opcode::Push, object,
               MIREffect::WriteMemory));
            break;
         case DispatchPhase::CountArguments:
            sequence.add(unary(Opcode::Clear, parameter,
               MIREffect::WriteFlags));
            sequence.add(binary(Opcode::Move, signature, arguments,
               Condition::None, MIREffect::None));
            break;
         case DispatchPhase::AdvanceArgumentCount:
            sequence.add(immediate(Opcode::AddressOffset, signature, word,
               MIREffect::None));
            sequence.add(load(scanned, signature, 0));
            sequence.add(immediate(Opcode::AddImmediate, parameter, 1,
               MIREffect::WriteFlags));
            break;
         case DispatchPhase::TestArgumentSentinel:
            sequence.add(immediate(Opcode::CompareImmediate, scanned, -1,
               MIREffect::WriteFlags));
            sequence.add({ Opcode::StoreOffset, stack, parameter,
               argumentCountOffset, Condition::None,
               MIREffect::WriteMemory });
            break;
         case DispatchPhase::SelectList:
            sequence.add(load(object, stack, objectOffset));
            sequence.add(load(parameter, stack, listIndexOffset));
            sequence.add(indexedLoad(list, object, Register::C,
               wordScale, 0));
            sequence.add(binary(Opcode::Test, list, list,
               Condition::None, MIREffect::WriteFlags));
            break;
         case DispatchPhase::SelectOverload:
            if (!receiverLists) {
               sequence.add(immediate(Opcode::MoveReference, list, 2,
                  MIREffect::None));
            }
            sequence.add(unary(Opcode::Clear, overload,
               MIREffect::WriteFlags));
            sequence.add(load(message, list, 0));
            break;
         case DispatchPhase::LoadSignature:
            sequence.add(immediate(Opcode::ShiftRightImmediate, message,
               ACTION_ORDER, MIREffect::WriteFlags));
            sequence.add(immediate(Opcode::MoveMetadata, metadata, 0,
               MIREffect::None));
            sequence.add(binary(Opcode::Move, parameter, message,
               Condition::None, MIREffect::None));
            sequence.add(immediate(Opcode::ShiftLeftImmediate, parameter,
               messageEntryShift, MIREffect::WriteFlags));
            sequence.add(indexedLoad(signature, metadata, Register::C,
               0, word));
            if (variadic) {
               sequence.add(unary(Opcode::Clear, parameter,
                  MIREffect::WriteFlags));
               sequence.add(immediate(Opcode::AddressOffset, signature,
                  -word, MIREffect::None));
               sequence.add({ Opcode::StoreOffset, stack, signature,
                  signatureCursorOffset, Condition::None,
                  MIREffect::WriteMemory });
            }
            else {
               sequence.add(immediate(Opcode::MoveImmediate, parameter,
                  dispatch.fixedArgumentCount, MIREffect::None));
               sequence.add(immediate(Opcode::AddressOffset, signature,
                  -word, MIREffect::None));
            }
            break;
         case DispatchPhase::AdvanceParameter:
            sequence.add(immediate(variadic
                  ? Opcode::AddImmediate : Opcode::SubtractImmediate,
               parameter, 1, MIREffect::WriteFlags));
            break;
         case DispatchPhase::TestArgumentsComplete:
            if (variadic)
               sequence.add(compareMemory(parameter, stack,
                  argumentCountOffset));
            break;
         case DispatchPhase::LoadArgumentTypes:
            if (variadic) {
               sequence.add(load(signatureCursor, stack,
                  signatureCursorOffset));
               sequence.add(binary(Opcode::Move, signatureCandidate,
                  signatureCursor, Condition::None, MIREffect::None));
               sequence.add(immediate(Opcode::AddressOffset,
                  signatureCandidate, word, MIREffect::None));
               sequence.add(load(message, signatureCandidate, 0));
               sequence.add(immediate(Opcode::CompareImmediate, message, 0,
                  MIREffect::WriteFlags));
               sequence.add(binary(Opcode::ConditionalMove,
                  signatureCursor, signatureCandidate,
                  Condition::NotEqual, MIREffect::ReadFlags));
               sequence.add({ Opcode::StoreOffset, stack, signatureCursor,
                  signatureCursorOffset, Condition::None,
                  MIREffect::WriteMemory });
            }
            sequence.add(indexedLoad(actual, arguments, Register::C,
               wordScale, 0));
            sequence.add(immediate(Opcode::MoveRuntimeConstant, nil,
               (int)MachineRuntimeConstant::VoidReference,
               MIREffect::None));
            sequence.add(binary(Opcode::Test, actual, actual,
               Condition::None, MIREffect::WriteFlags));
            sequence.add(binary(Opcode::ConditionalMove, actual, nil,
               Condition::Equal, MIREffect::ReadFlags));
            sequence.add(load(actualVMT, actual,
               -(int)runtime.objectLayout.vmtOffset));
            if (variadic)
               sequence.add(load(expectedVMT, signatureCursor, 0));
            else {
               sequence.add(indexedLoad(expectedVMT, signature, Register::C, wordScale, 0));
            }
            break;
         case DispatchPhase::TestArgumentType:
            sequence.add(binary(Opcode::Compare, expectedVMT, actualVMT,
               Condition::None, MIREffect::WriteFlags));
            break;
         case DispatchPhase::LoadParent:
            sequence.add(load(actualVMT, actualVMT,
               -(int)runtime.vmtLayout.parentOffset));
            break;
         case DispatchPhase::TestParent:
            sequence.add(binary(Opcode::Test, actualVMT, actualVMT,
               Condition::None, MIREffect::WriteFlags));
            break;
         case DispatchPhase::ResolveTarget:
            if (receiverLists) {
               sequence.add(load(object, stack, objectOffset));
               sequence.add(load(parameter, stack, listIndexOffset));
               sequence.add(indexedLoad(list, object, Register::C,
                  wordScale, 0));
            }
            else {
               sequence.add(immediate(Opcode::MoveReference, list, 2, MIREffect::None));
            }

            sequence.add(binary(Opcode::Move, parameter, overload,
               Condition::None, MIREffect::None));
            sequence.add(immediate(Opcode::ShiftLeftImmediate, parameter,
               messageEntryShift, MIREffect::WriteFlags));
            sequence.add(indexedLoad(virtualTarget ? method : target,
               list, Register::C, 0, word));
            sequence.add(indexedLoad(overload, list, Register::C, 0, 0));
            break;
         case DispatchPhase::AdvanceOverload:
            if (receiverLists) {
               sequence.add(load(object, stack, objectOffset));
               sequence.add(load(parameter, stack, listIndexOffset));
               sequence.add(indexedLoad(list, object, Register::C,
                  wordScale, 0));
            }
            else {
               sequence.add(immediate(Opcode::MoveReference, list, 2, MIREffect::None));
            }

            sequence.add(immediate(Opcode::AddImmediate, overload, 1,
               MIREffect::WriteFlags));
            sequence.add(binary(Opcode::Move, parameter, overload,
               Condition::None, MIREffect::None));
            sequence.add(immediate(Opcode::ShiftLeftImmediate, parameter,
               messageEntryShift, MIREffect::WriteFlags));
            sequence.add(indexedLoad(message, list, Register::C, 0, 0));
            sequence.add(binary(Opcode::Test, message, message,
               Condition::None, MIREffect::WriteFlags));
            break;
         case DispatchPhase::AdvanceList:
            sequence.add(load(parameter, stack, listIndexOffset));
            sequence.add(immediate(Opcode::AddImmediate, parameter, 1,
               MIREffect::WriteFlags));
            sequence.add({ Opcode::StoreOffset, stack, parameter,
               listIndexOffset, Condition::None, MIREffect::WriteMemory });
            break;
         case DispatchPhase::RestoreSuccess:
            restoreDispatchState(frameLayout, word, abi, stack, stackValue,
               object, sequence);
            break;
         case DispatchPhase::RestoreFailure:
            restoreDispatchState(frameLayout, word, abi, stack, stackValue,
               object, sequence);
            sequence.add(immediate(Opcode::MoveMessage, importedMessage,
               dispatch.message, MIREffect::None));
            break;
         case DispatchPhase::LoadReceiverVMT:
            if (!virtualTarget)
               return LowerError::InvalidMIR;

            sequence.add(load(vmt, object,
               -(int)runtime.objectLayout.vmtOffset));
            break;
         case DispatchPhase::SelectAlternativeVMT:
            if (!alternativeVMT)
               return LowerError::InvalidMIR;

            sequence.add(load(vmtSize, vmt,
               -(int)runtime.vmtLayout.sizeOffset));
            sequence.add(immediate(Opcode::ShiftLeftImmediate, vmtSize,
               messageEntryShift, MIREffect::WriteFlags));
            sequence.add(binary(Opcode::Add, vmtValue, vmtSize,
               Condition::None, MIREffect::WriteFlags));
            break;
         case DispatchPhase::ResolveVirtualTarget:
            if (!virtualTarget)
               return LowerError::InvalidMIR;

            sequence.add(indexedLoad(target, vmt, Register::A, 0,
               runtime.vmtLayout.methodOffset));
            break;
         case DispatchPhase::BranchTarget:
         case DispatchPhase::Fallthrough:
            break;
         default:
            return LowerError::InvalidMIR;
      }

      EIRInstruction& terminator = function.instruction(
         block.firstInstruction + block.instructionCount - 1);
      pos_t followingBlock = i + 1;
      if (terminator.opcode == EIROpcode::Branch) {
         pos_t targetBlock = (pos_t)function.operand(
            terminator.firstOperand).value;
         if (targetBlock != followingBlock) {
            sequence.add(local(Opcode::Jump, targetBlock,
               MIREffect::None));
         }
      }
      else if (terminator.opcode == EIROpcode::ConditionalBranch) {
         Opcode trueJump = Opcode::Nop;
         Opcode falseJump = Opcode::Nop;
         if (!getDispatchCondition(phase, trueJump, falseJump))
            return LowerError::InvalidMIR;

         pos_t trueBlock = (pos_t)function.operand(
            terminator.firstOperand + 1).value;
         pos_t falseBlock = (pos_t)function.operand(
            terminator.firstOperand + 2).value;
         if (trueBlock == followingBlock) {
            sequence.add(local(falseJump, falseBlock,
               MIREffect::ReadFlags));
         }
         else if (falseBlock == followingBlock) {
            sequence.add(local(trueJump, trueBlock,
               MIREffect::ReadFlags));
         }
         else {
            sequence.add(local(trueJump, trueBlock,
               MIREffect::ReadFlags));
            sequence.add(local(Opcode::Jump, falseBlock,
               MIREffect::None));
         }
      }
      else if (terminator.opcode == EIROpcode::IndirectBranch) {
         sequence.add(unary(Opcode::JumpRegister, target,
            MIREffect::None));
      }
      else if (terminator.opcode != EIROpcode::Fallthrough) {
         return LowerError::InvalidMIR;
      }
   }

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError selectStackReference(
   EIRFunction& function,
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   const LoweringContext& context,
   Sequence& sequence)
{
   TargetSpec target = {};

   if (!TargetProvider::get(context.platform, target)
      || target.architecture != abi.architecture
      || runtime.objectLayout.fieldSize != target.pointerSize)
   {
      return LowerError::InvalidRuntime;
   }

   if (function.blockCount() != 1
      || function.instructionCount() != 2
      || function.instruction(0).opcode
         != EIROpcode::IsCurrentStackReference)
   {
      return LowerError::InvalidMIR;
   }

   bool multiThread = function.instruction(0).effects == EIREffect::ReadTLS;

   if (multiThread
      && (runtime.dataLayout.threadContent.stackRoot == 0
         || runtime.dataLayout.threadContent.size == 0
         || runtime.dataLayout.threadContent.size > 0x7F
         || (target.tlsModel != TLSModel::Windows
            && target.tlsModel != TLSModel::ELF)))
   {
      return LowerError::InvalidRuntime;
   }

   Operand accumulator = operand(
      Register::A,
      abi.wordSize,
      ValueKind::Integer);
   Operand condition = operand(
      Register::C,
      abi.wordSize,
      ValueKind::Integer);
   Operand object = operand(
      abi.object,
      abi.wordSize,
      ValueKind::Integer);
   Operand stack = operand(
      abi.stack,
      abi.wordSize,
      ValueKind::Integer);
   Operand stackRoot = operand(
      Register::DI,
      abi.wordSize,
      ValueKind::Integer);

   sequence.clear();

   if (multiThread) {
      Operand thread = operand(
         Register::A,
         abi.wordSize,
         ValueKind::Reference);

      sequence.add(immediate(
         Opcode::LoadCurrentThread,
         thread,
         runtime.dataLayout.threadContent.size,
         MIREffect::ReadMemory | MIREffect::ReadTLS));
      sequence.add(load(
         stackRoot,
         thread,
         runtime.dataLayout.threadContent.stackRoot));
   }
   else {
      Operand stackRootAddress = operand(
         Register::DI,
         abi.wordSize,
         ValueKind::Address);

      sequence.add(moveRuntimeData(
         stackRootAddress,
         RuntimeDataReference::SingleContentStackRoot));
      sequence.add(load(stackRoot, stackRootAddress, 0));
   }

   sequence.add(unary(
      Opcode::Clear,
      condition,
      MIREffect::WriteFlags));
   sequence.add(immediate(
      Opcode::MoveImmediate,
      accumulator,
      1,
      MIREffect::None));

   sequence.add(binary(
      Opcode::Compare,
      stack,
      object,
      Condition::None,
      MIREffect::WriteFlags));
   sequence.add(binary(
      Opcode::ConditionalMove,
      condition,
      accumulator,
      Condition::BelowEqual,
      MIREffect::ReadFlags));

   sequence.add(unary(
      Opcode::Clear,
      accumulator,
      MIREffect::WriteFlags));
   sequence.add(binary(
      Opcode::Compare,
      object,
      stackRoot,
      Condition::None,
      MIREffect::WriteFlags));
   sequence.add(binary(
      Opcode::ConditionalMove,
      accumulator,
      condition,
      Condition::BelowEqual,
      MIREffect::ReadFlags));

   sequence.add(immediate(
      Opcode::CompareImmediate,
      accumulator,
      1,
      MIREffect::WriteFlags));

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError selectMemoryCopy(EIRFunction& function, const ManagedABI& abi, Sequence& sequence)
{
   if (!abi.isValid())
   {
      return LowerError::InvalidABI;
   }

   if (function.instructionCount() != 2
      || function.instruction(0).opcode != EIROpcode::MemoryCopy
      || function.instruction(1).opcode != EIROpcode::Fallthrough)
   {
      return LowerError::InvalidMIR;
   }

   Operand source = operand(abi.dataSource, abi.wordSize, ValueKind::Reference);
   Operand destination = operand(abi.dataDestination, abi.wordSize, ValueKind::Reference);
   Operand byteCount = operand(abi.allocationSize, OperandSize::DWord, ValueKind::Integer);
   Operand savedByteCount = operand(abi.allocationSize, abi.wordSize, ValueKind::Integer);
   Operand object = operand(abi.object, abi.wordSize, ValueKind::Reference);
   EIROperand& copySize = function.operand(function.instruction(0).firstOperand);

   if (copySize.value > 0x7FFFFFFF)
      return LowerError::InvalidArgument;

   sequence.clear();
   sequence.add(unary(Opcode::Push, source, MIREffect::WriteMemory));
   sequence.add(unary(Opcode::Push, savedByteCount, MIREffect::WriteMemory));
   sequence.add(unary(Opcode::Push, destination, MIREffect::WriteMemory));

   if (abi.architecture == Architecture::AMD64) {
      Operand sourceArgument = operand(abi.cachedArgument0, abi.wordSize, ValueKind::Reference);

      sequence.add(binary(Opcode::Move, source, sourceArgument, Condition::None, MIREffect::None));
   }

   sequence.add(immediate(Opcode::MoveImmediate, byteCount, (int)copySize.value, MIREffect::None));
   sequence.add(binary(Opcode::Move, destination, object, Condition::None, MIREffect::None));
   sequence.add(binary(Opcode::RepeatMoveBytes, destination, source, Condition::None,
      MIREffect::ReadMemory | MIREffect::WriteMemory));

   sequence.add(unary(Opcode::Pop, destination, MIREffect::ReadMemory));
   sequence.add(unary(Opcode::Pop, savedByteCount, MIREffect::ReadMemory));
   sequence.add(unary(Opcode::Pop, source, MIREffect::ReadMemory));

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError lowerSystemStartup(EIRFunction& function, const RuntimeSpec& runtime,
   const ManagedABI& abi, const RuntimeCallABI& runtimeABI,
   const LoweringContext& context, Sequence& sequence)
{
   TargetSpec target = {};
   RuntimeCallSpec call = {};
   if (!TargetProvider::get(context.platform, target)
      || target.architecture != abi.architecture
      || runtimeABI.operation != RuntimeOperation::Prepare
      || !RuntimeCallProvider::get(RuntimeOperation::Prepare, runtime, call)
      || !runtimeABI.isValid(abi, call))
   {
      return LowerError::InvalidRuntime;
   }

   bool systemV = target.abi == PlatformABI::SystemVX86
      || target.abi == PlatformABI::SystemVAMD64;
   Operand nativeStack = operand(Register::A, abi.wordSize, ValueKind::Integer);
   Operand stackValue = operand(abi.stack, abi.wordSize, ValueKind::Integer);
   Operand stackAddress = operand(abi.stack, abi.wordSize, ValueKind::Address);
   Operand frame = operand(abi.frame, abi.wordSize, ValueKind::Integer);
   Operand data = operand(Register::DI, abi.wordSize, ValueKind::Address);

   sequence.clear();
   sequence.add({ Opcode::InitializeFPU, emptyOperand(), emptyOperand(), 0,
      Condition::None, MIREffect::WriteFPUState });
   if (target.operatingSystem == OperatingSystem::FreeBSD) {
      sequence.add(unary(Opcode::Clear, frame, MIREffect::WriteFlags));
      sequence.add(unary(Opcode::Push, frame, MIREffect::WriteMemory));
      sequence.add(binary(Opcode::Move, nativeStack,
         operand(Register::DI, abi.wordSize, ValueKind::Integer),
         Condition::None, MIREffect::None));
   }
   else {
      sequence.add(binary(Opcode::Move, nativeStack, stackValue, Condition::None, MIREffect::None));
   }

   EIROperand& publishStackRoot = function.operand(function.instruction(0).firstOperand);
   if (publishStackRoot.value != 0) {
      sequence.add(immediate(Opcode::MoveRuntimeData, data,
         (int)RuntimeDataReference::SingleContentStackRoot, MIREffect::None));
      sequence.add({ Opcode::StoreOffset, data, stackAddress, 0,
         Condition::None, MIREffect::WriteMemory });
   }

   sequence.add({ Opcode::CallRuntime, emptyOperand(), nativeStack,
      (int)RuntimeOperation::Prepare, Condition::None,
      machineEffects(runtimeABI.effects) });
   if (systemV) {
      sequence.add(unary(Opcode::Clear, frame, MIREffect::WriteFlags));
      sequence.add(unary(Opcode::Push, frame, MIREffect::WriteMemory));
   }

   return MIRVerifier::verify(sequence, abi, &runtimeABI)
      == MIRVerifyError::None ? LowerError::None : LowerError::InvalidMIR;
}

static void push(Sequence& sequence, Operand value)
{
   sequence.add(unary(Opcode::Push, value, MIREffect::WriteMemory));
}

static void pop(Sequence& sequence, Operand value)
{
   sequence.add(unary(Opcode::Pop, value, MIREffect::ReadMemory));
}

static LowerError selectFrameOpen(EIRFunction& function, const ManagedABI& abi, Sequence& sequence)
{
   if (!abi.isValid())
      return LowerError::InvalidABI;

   if (EIRVerifier::verify(function) != EIRVerifyError::None)
      return LowerError::InvalidMIR;

   Operand accumulator = operand(Register::A, abi.wordSize, ValueKind::Integer);
   Operand stack = operand(abi.stack, abi.wordSize, ValueKind::Integer);
   Operand frame = operand(abi.frame, abi.wordSize, ValueKind::Integer);

   sequence.clear();

   for (pos_t i = 0; i < function.instructionCount(); i++) {
      EIRInstruction& instruction = function.instruction(i);

      switch (instruction.opcode) {
         case EIROpcode::FrameOpen:
            push(sequence, frame);
            sequence.add(binary(Opcode::Move, frame, stack, Condition::None, MIREffect::None));
            break;

         case EIROpcode::Constant:
            sequence.add(unary(Opcode::Clear, accumulator, MIREffect::WriteFlags));
            break;

         case EIROpcode::StackReserve:
         {
            EIROperand& size = function.operand(instruction.firstOperand);
            if (size.value > 0x7FFFFFFF)
               return LowerError::InvalidArgument;

            sequence.add(immediate(
               Opcode::SubtractImmediate, stack, (int)size.value, MIREffect::WriteFlags));
            break;
         }

         case EIROpcode::FrameLink:
            push(sequence, frame);
            push(sequence, accumulator);
            sequence.add(binary(Opcode::Move, frame, stack, Condition::None, MIREffect::None));
            break;

         case EIROpcode::FrameClear:
         {
            EIROperand& slotCount = function.operand(instruction.firstOperand + 1);
            for (pos64_t slot = 0; slot < slotCount.value; slot++)
               push(sequence, accumulator);
            break;
         }

         case EIROpcode::Fallthrough:
            break;

         default:
            return LowerError::InvalidMIR;
      }
   }

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError selectFrameClose(EIRFunction& function, const ManagedABI& abi, Sequence& sequence)
{
   if (!abi.isValid())
      return LowerError::InvalidABI;

   if (function.instructionCount() != 2
      || function.instruction(0).opcode != EIROpcode::FrameClose
      || function.instruction(1).opcode != EIROpcode::Fallthrough)
   {
      return LowerError::InvalidMIR;
   }

   EIROperand& argumentSize = function.operand(function.instruction(0).firstOperand);
   if (argumentSize.value > 0x7FFFFFFF)
      return LowerError::InvalidArgument;

   Operand stack = operand(abi.stack, abi.wordSize, ValueKind::Integer);
   Operand frame = operand(abi.frame, abi.wordSize, ValueKind::Integer);

   sequence.clear();

   if (argumentSize.value != 0)
      sequence.add(immediate(Opcode::AddImmediate, frame, (int)argumentSize.value, MIREffect::WriteFlags));

   sequence.add(binary(Opcode::Move, stack, frame, Condition::None, MIREffect::None));
   pop(sequence, frame);

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError selectRootStackAllocation(EIRFunction& function, const RuntimeSpec& runtime,
   const ManagedABI& abi, Sequence& sequence)
{
   if (!abi.isValid())
   {
      return LowerError::InvalidABI;
   }

   if (function.instructionCount() != 2
      || function.instruction(0).opcode != EIROpcode::StackAllocate
      || function.instruction(1).opcode != EIROpcode::Fallthrough)
   {
      return LowerError::InvalidMIR;
   }

   Operand stackAddress = operand(abi.stack, abi.wordSize, ValueKind::Address);
   Operand stackValue = operand(abi.stack, abi.wordSize, ValueKind::Integer);
   Operand slotCount = operand(abi.value, abi.wordSize, ValueKind::Integer);
   Operand byteSize = operand(Register::A, abi.wordSize, ValueKind::Integer);
   Operand fillCount = operand(abi.allocationSize, abi.wordSize, ValueKind::Integer);
   Operand destinationValue = operand(abi.dataDestination, abi.wordSize, ValueKind::Integer);
   Operand destinationAddress = operand(abi.dataDestination, abi.wordSize, ValueKind::Address);
   Operand preservedTop = operand(abi.dataSource, abi.wordSize, ValueKind::Reference);

   sequence.clear();

   Operand savedDataSource = emptyOperand();
   if (abi.scratch != Register::None) {
      savedDataSource = operand(abi.scratch, abi.wordSize, ValueKind::Reference);
      sequence.add(binary(Opcode::Move, savedDataSource, preservedTop, Condition::None, MIREffect::None));
   }

   if (abi.architecture == Architecture::X86) {
      Operand cachedArgument = operand(abi.cachedArgument0, abi.wordSize, ValueKind::Reference);

      sequence.add(store(stackAddress, cachedArgument, runtime.objectLayout.fieldSize));
   }

   pop(sequence, preservedTop);

   if (abi.architecture == Architecture::AMD64) {
      sequence.add(immediate(Opcode::AddImmediate, slotCount, 1, MIREffect::WriteFlags));
      sequence.add(immediate(Opcode::AndImmediate, slotCount, -2, MIREffect::WriteFlags));
   }

   sequence.add(binary(Opcode::Move, byteSize, slotCount, Condition::None, MIREffect::None));
   sequence.add(multiply(byteSize, byteSize, runtime.objectLayout.fieldSize));
   sequence.add(binary(Opcode::Subtract, stackValue, byteSize, Condition::None, MIREffect::WriteFlags));
   sequence.add(binary(Opcode::Move, fillCount, slotCount, Condition::None, MIREffect::None));
   sequence.add(unary(Opcode::Clear, byteSize, MIREffect::WriteFlags));
   sequence.add(binary(Opcode::Move, destinationValue, stackValue, Condition::None, MIREffect::None));
   sequence.add(binary(Opcode::RepeatStore, destinationAddress, byteSize, Condition::None, MIREffect::WriteMemory));

   push(sequence, preservedTop);

   if (savedDataSource.reg != Register::None)
      sequence.add(binary(Opcode::Move, preservedTop, savedDataSource, Condition::None, MIREffect::None));

   if (abi.architecture == Architecture::X86) {
      Operand cachedArgument = operand(abi.cachedArgument0, abi.wordSize, ValueKind::Reference);

      sequence.add(load(cachedArgument, stackAddress, runtime.objectLayout.fieldSize));
   }

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError selectExternalFrameOpen(EIRFunction& function,
   const RuntimeSpec& runtime, const ManagedABI& abi,
   const LoweringContext& context, Sequence& sequence)
{
   bool threadLocal = test(function.instruction(0).effects, EIREffect::ReadTLS);
   TargetSpec target = {};
   if (!TargetProvider::get(context.platform, target)
      || target.architecture != abi.architecture)
   {
      return LowerError::InvalidArgument;
   }
   if (threadLocal
      && (runtime.dataLayout.threadContent.size == 0
         || runtime.dataLayout.threadContent.size > 0x7F
         || (target.tlsModel != TLSModel::Windows
            && target.tlsModel != TLSModel::ELF)))
   {
      return LowerError::InvalidRuntime;
   }

   Operand a = operand(Register::A, abi.wordSize, ValueKind::Integer);
   Operand c = operand(Register::C, abi.wordSize, ValueKind::Integer);
   Operand d = operand(Register::D, abi.wordSize, ValueKind::Integer);
   Operand stack = operand(abi.stack, abi.wordSize, ValueKind::Integer);
   Operand stackAddress = operand(abi.stack, abi.wordSize, ValueKind::Address);
   Operand frame = operand(abi.frame, abi.wordSize, ValueKind::Integer);
   Operand data = operand(Register::DI, abi.wordSize, ValueKind::Address);

   ExternalFrameLayout frameLayout = {};
   if (!ExternalFrameLayoutProvider::get(target, frameLayout))
      return LowerError::InvalidRuntime;

   sequence.clear();
   if (abi.architecture == Architecture::AMD64) {
      Operand r8 = operand(Register::R8, abi.wordSize, ValueKind::Integer);
      Operand r9 = operand(Register::R9, abi.wordSize, ValueKind::Integer);
      if (target.abi == PlatformABI::WindowsX64) {
         sequence.add({ Opcode::StoreOffset, stackAddress, c, 8,
            Condition::None, MIREffect::WriteMemory });
         sequence.add({ Opcode::StoreOffset, stackAddress, d, 16,
            Condition::None, MIREffect::WriteMemory });
         sequence.add({ Opcode::StoreOffset, stackAddress, r8, 24,
            Condition::None, MIREffect::WriteMemory });
         sequence.add({ Opcode::StoreOffset, stackAddress, r9, 32,
            Condition::None, MIREffect::WriteMemory });
      }

      sequence.add(unary(Opcode::Clear, a, MIREffect::WriteFlags));
   }

   for (unsigned int i = 0; i < frameLayout.savedRegisterCount; i++) {
      push(sequence, operand(
         frameLayout.savedRegisters[i],
         abi.wordSize,
         ValueKind::Integer));
   }
   push(sequence, frame);

   Operand oldFrame = operand(Register::A, abi.wordSize, ValueKind::Integer);
   if (threadLocal) {
      Operand thread = operand(Register::A, abi.wordSize, ValueKind::Reference);
      sequence.add(immediate(Opcode::LoadCurrentThread, thread,
         runtime.dataLayout.threadContent.size,
         MIREffect::ReadMemory | MIREffect::ReadTLS));
      sequence.add({ Opcode::LoadOffset, oldFrame, thread,
         runtime.dataLayout.threadContent.stackFrame,
         Condition::None, MIREffect::ReadMemory });
   }
   else {
      sequence.add(immediate(Opcode::MoveRuntimeData, data,
         (int)RuntimeDataReference::SingleContentStackFrame, MIREffect::None));
      sequence.add({ Opcode::LoadOffset, oldFrame, data, 0,
         Condition::None, MIREffect::ReadMemory });
   }
   push(sequence, oldFrame);

   sequence.add(binary(Opcode::Move, frame, oldFrame,
      Condition::None, MIREffect::None));
   sequence.add(unary(Opcode::Clear, a, MIREffect::WriteFlags));
   push(sequence, frame);
   push(sequence, a);
   sequence.add(binary(Opcode::Move, frame, stack,
      Condition::None, MIREffect::None));
   push(sequence, frame);
   sequence.add(binary(Opcode::Move, frame, stack,
      Condition::None, MIREffect::None));

   for (pos_t i = 1; i < function.instructionCount(); i++) {
      EIRInstruction& instruction = function.instruction(i);

      switch (instruction.opcode) {
         case EIROpcode::Constant:
            sequence.add(unary(Opcode::Clear, a, MIREffect::WriteFlags));
            break;

         case EIROpcode::StackReserve:
         {
            EIROperand& size = function.operand(instruction.firstOperand);
            if (size.value > 0x7FFFFFFF)
               return LowerError::InvalidArgument;

            sequence.add(immediate(
               Opcode::SubtractImmediate, stack, (int)size.value, MIREffect::WriteFlags));
            break;
         }

         case EIROpcode::FrameLink:
            push(sequence, frame);
            push(sequence, a);
            sequence.add(binary(Opcode::Move, frame, stack, Condition::None, MIREffect::None));
            break;

         case EIROpcode::FrameClear:
         {
            EIROperand& slotCount = function.operand(instruction.firstOperand + 1);
            unsigned long long byteSize = slotCount.value * runtime.objectLayout.fieldSize;
            if (slotCount.value > 0x7FFFFFFF || byteSize > 0x7FFFFFFF)
               return LowerError::InvalidArgument;

            sequence.add(immediate(Opcode::MoveImmediate, c, (int)slotCount.value, MIREffect::None));
            sequence.add(immediate(Opcode::SubtractImmediate, stack, (int)byteSize, MIREffect::WriteFlags));
            sequence.add(binary(Opcode::Move, data, stackAddress, Condition::None, MIREffect::None));
            sequence.add(binary(Opcode::RepeatStore, data, a, Condition::None, MIREffect::WriteMemory));
            break;
         }

         case EIROpcode::Fallthrough:
            break;

         default:
            return LowerError::InvalidMIR;
      }
   }
   if (abi.architecture == Architecture::AMD64) {
      sequence.add(binary(Opcode::Move,
         operand(Register::R10, abi.wordSize, ValueKind::Integer), a,
         Condition::None, MIREffect::None));
      sequence.add(binary(Opcode::Move,
         operand(Register::R11, abi.wordSize, ValueKind::Integer), a,
         Condition::None, MIREffect::None));
   }
   else {
      sequence.add(binary(
         Opcode::Move,
         operand(Register::SI, abi.wordSize, ValueKind::Integer),
         a,
         Condition::None,
         MIREffect::None));
   }

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError selectExternalFrameClose(EIRFunction& function,
   const RuntimeSpec& runtime, const ManagedABI& abi,
   const LoweringContext& context, Sequence& sequence)
{
   EIRInstruction& operation = function.instruction(0);
   bool threadLocal = test(operation.effects, EIREffect::ReadTLS);
   EIROperand& argumentOperand = function.operand(operation.firstOperand);
   if (argumentOperand.value > 0x7FFFFFFF)
      return LowerError::InvalidArgument;

   int argumentSize = (int)argumentOperand.value;
   TargetSpec target = {};
   if (!TargetProvider::get(context.platform, target)
      || target.architecture != abi.architecture)
   {
      return LowerError::InvalidArgument;
   }
   if (threadLocal
      && (runtime.dataLayout.threadContent.size == 0
         || runtime.dataLayout.threadContent.size > 0x7F
         || (target.tlsModel != TLSModel::Windows
            && target.tlsModel != TLSModel::ELF)))
   {
      return LowerError::InvalidRuntime;
   }

   Operand b = operand(Register::B, abi.wordSize, ValueKind::Integer);
   Operand c = operand(Register::C, abi.wordSize, ValueKind::Integer);
   Operand linkedFrame = operand(Register::C, abi.wordSize, ValueKind::Address);
   Operand si = operand(Register::SI, abi.wordSize, ValueKind::Integer);
   Operand di = operand(Register::DI, abi.wordSize, ValueKind::Integer);
   Operand stack = operand(abi.stack, abi.wordSize, ValueKind::Integer);
   Operand stackAddress = operand(abi.stack, abi.wordSize, ValueKind::Address);
   Operand frame = operand(abi.frame, abi.wordSize, ValueKind::Integer);
   Operand frameAddress = operand(abi.frame, abi.wordSize, ValueKind::Address);
   Operand data = operand(Register::DI, abi.wordSize, ValueKind::Address);

   sequence.clear();
   if (argumentSize != 0) {
      sequence.add(immediate(Opcode::AddImmediate, frame,
         argumentSize, MIREffect::WriteFlags));
   }

   sequence.add(load(
      linkedFrame,
      frameAddress,
      stackSpan(
         runtime.objectLayout.fieldSize,
         ExternalFrameLayout::ManagedFrameLinkSlot)));
   sequence.add(load(linkedFrame, linkedFrame, 0));
   sequence.add(load(
      b,
      linkedFrame,
      stackSpan(
         runtime.objectLayout.fieldSize,
         ExternalFrameLayout::ManagedFrameLinkSlot)));

   if (threadLocal) {
      Operand thread = operand(Register::A, abi.wordSize, ValueKind::Reference);
      sequence.add(immediate(Opcode::LoadCurrentThread, thread,
         runtime.dataLayout.threadContent.size,
         MIREffect::ReadMemory | MIREffect::ReadTLS));
      sequence.add({ Opcode::StoreOffset, thread, b,
         runtime.dataLayout.threadContent.stackFrame,
         Condition::None, MIREffect::WriteMemory });
   }
   else {
      sequence.add(immediate(Opcode::MoveRuntimeData, data,
         (int)RuntimeDataReference::SingleContentStackFrame, MIREffect::None));
      sequence.add({ Opcode::StoreOffset, data, b, 0,
         Condition::None, MIREffect::WriteMemory });
   }

   sequence.add(binary(
      Opcode::Move,
      stackAddress,
      linkedFrame,
      Condition::None,
      MIREffect::None));
   sequence.add(immediate(
      Opcode::AddImmediate,
      stackAddress,
      stackSpan(
         runtime.objectLayout.fieldSize,
         ExternalFrameLayout::NativeFrameSlot),
      MIREffect::WriteFlags));

   pop(sequence, frame);
   if (abi.architecture == Architecture::AMD64) {
      pop(sequence, operand(Register::R15, abi.wordSize, ValueKind::Integer));
      pop(sequence, operand(Register::R14, abi.wordSize, ValueKind::Integer));
      pop(sequence, operand(Register::R13, abi.wordSize, ValueKind::Integer));
      pop(sequence, operand(Register::R12, abi.wordSize, ValueKind::Integer));
      pop(sequence, b);
      if (target.abi == PlatformABI::WindowsX64) {
         pop(sequence, di);
         pop(sequence, si);
         sequence.add(immediate(Opcode::AddImmediate, stack,
            runtime.objectLayout.fieldSize, MIREffect::WriteFlags));
      }
      else {
         sequence.add(immediate(Opcode::AddImmediate, stack,
            stackSpan(runtime.objectLayout.fieldSize,
               target.externalABI.integerArgumentRegisters - 1),
            MIREffect::WriteFlags));
      }
   }
   else {
      pop(sequence, b);
      pop(sequence, c);
      pop(sequence, di);
      pop(sequence, si);
   }

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError lowerDynamicAllocation(const AllocationSpec& allocationSpec,
   const RuntimeSpec& runtime, const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI, Sequence& sequence)
{
   bool binaryObject = allocationSpec.kind == AllocationKind::DynamicBinary;
   int elementSize = (int)allocationSpec.elementSize;
   unsigned int payloadMask = allocationSpec.payloadMask;
   if (elementSize <= 0 || (unsigned int)elementSize > payloadMask)
      return LowerError::InvalidArgument;

   RuntimeCallSpec call = {};
   if (!RuntimeCallProvider::get(RuntimeOperation::AllocateYoung, runtime, call)
      || !runtimeABI.isValid(abi, call))
   {
      return LowerError::InvalidRuntime;
   }

   Operand count = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
   Operand payload = operand(abi.value, OperandSize::DWord, ValueKind::Integer);
   Operand allocation = operand(abi.allocationSize, OperandSize::DWord,
      ValueKind::Integer);
   Operand source = operand(abi.cachedArgument0, OperandSize::DWord,
      ValueKind::Reference);
   Operand object = operand(abi.object, OperandSize::DWord, ValueKind::Reference);
   Operand vmt = operand(abi.wideLow, OperandSize::DWord, ValueKind::VMT);

   sequence.add(load(count, source, 0));
   sequence.add(binary(Opcode::Move, payload, count, Condition::None, MIREffect::None));
   sequence.add(multiply(payload, payload, elementSize));
   sequence.add(immediate(Opcode::AddImmediate, payload,
      runtime.objectLayout.headerSize + runtime.objectLayout.allocationAlignment - 1,
      MIREffect::WriteFlags));
   sequence.add(immediate(Opcode::AndImmediate, payload,
      -(int)runtime.objectLayout.allocationAlignment, MIREffect::WriteFlags));
   sequence.add(immediate(Opcode::MoveImmediate, allocation, -1, MIREffect::None));
   sequence.add(immediate(Opcode::CompareImmediate, count,
      (int)(payloadMask / (unsigned int)elementSize), MIREffect::WriteFlags));
   sequence.add(binary(Opcode::ConditionalMove, allocation, payload,
      Condition::BelowEqual, MIREffect::ReadFlags));
   sequence.add({
      Opcode::CallRuntime, object, allocation,
      (int)RuntimeOperation::AllocateYoung, Condition::None,
      machineEffects(runtimeABI.effects)
   });
   sequence.add(load(allocation, source, 0));
   sequence.add(multiply(allocation, allocation, elementSize));
   if (binaryObject) {
      sequence.add(immediate(Opcode::OrImmediate, allocation,
         (int)runtime.objectLayout.structMask, MIREffect::WriteFlags));
   }
   sequence.add(immediate(
      Opcode::MoveReferenceValue,
      vmt,
      (int)allocationSpec.vmtReference,
      MIREffect::None));
   sequence.add({
      Opcode::StoreOffset, object, allocation,
      -(int)runtime.objectLayout.sizeOffset, Condition::None,
      MIREffect::WriteMemory
   });
   sequence.add({
      Opcode::StoreOffset, object, vmt, -(int)runtime.objectLayout.vmtOffset,
      Condition::None, MIREffect::WriteMemory
   });

   return MIRVerifier::verify(sequence, abi, &runtimeABI) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError lowerPermanentAllocation(const AllocationSpec& allocationSpec,
   const RuntimeSpec& runtime, const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI, Sequence& sequence)
{
   RuntimeCallSpec call = {};
   if (!RuntimeCallProvider::get(RuntimeOperation::AllocatePermanent,
      runtime, call) || !runtimeABI.isValid(abi, call))
   {
      return LowerError::InvalidRuntime;
   }

   unsigned int maximumCount = runtime.objectLayout.objectSizeMask
      / runtime.objectLayout.fieldSize;
   Operand count = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
   Operand size = operand(abi.allocationSize, OperandSize::DWord,
      ValueKind::Integer);
   Operand payload = operand(abi.object, OperandSize::DWord, ValueKind::Integer);
   Operand source = operand(abi.cachedArgument0, abi.wordSize,
      ValueKind::Reference);
   Operand object = operand(abi.object, abi.wordSize, ValueKind::Reference);
   Operand vmt = operand(abi.wideLow, abi.wordSize, ValueKind::VMT);

   sequence.add(abi.architecture == Architecture::X86
      ? load(count, source, 0)
      : Instruction { Opcode::LoadDWordOffset, count, source, 0,
         Condition::None, MIREffect::ReadMemory });
   sequence.add(binary(Opcode::Move, payload, count,
      Condition::None, MIREffect::None));
   sequence.add(multiply(payload, payload, runtime.objectLayout.fieldSize));
   sequence.add(immediate(Opcode::AddImmediate, payload,
      runtime.objectLayout.headerSize
         + runtime.objectLayout.allocationAlignment - 1,
      MIREffect::WriteFlags));
   sequence.add(immediate(Opcode::AndImmediate, payload,
      -(int)runtime.objectLayout.allocationAlignment, MIREffect::WriteFlags));
   sequence.add(immediate(Opcode::MoveImmediate, size, -1, MIREffect::None));
   sequence.add(immediate(Opcode::CompareImmediate, count,
      (int)maximumCount, MIREffect::WriteFlags));
   sequence.add(binary(Opcode::ConditionalMove, size, payload,
      Condition::BelowEqual, MIREffect::ReadFlags));
   sequence.add({
      Opcode::CallRuntime, object, size,
      (int)RuntimeOperation::AllocatePermanent, Condition::None,
      machineEffects(runtimeABI.effects)
   });
   sequence.add(abi.architecture == Architecture::X86
      ? load(size, source, 0)
      : Instruction { Opcode::LoadDWordOffset, size, source, 0,
         Condition::None, MIREffect::ReadMemory });
   sequence.add(multiply(size, size, runtime.objectLayout.fieldSize));
   sequence.add(immediate(
      Opcode::MoveReferenceValue,
      vmt,
      (int)allocationSpec.vmtReference,
      MIREffect::None));
   sequence.add({
      abi.architecture == Architecture::X86
         ? Opcode::StoreOffset : Opcode::StoreDWordOffset,
      object, size, -(int)runtime.objectLayout.sizeOffset,
      Condition::None, MIREffect::WriteMemory
   });
   sequence.add({
      Opcode::StoreOffset, object, vmt, -(int)runtime.objectLayout.vmtOffset,
      Condition::None, MIREffect::WriteMemory
   });

   return MIRVerifier::verify(sequence, abi, &runtimeABI) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError selectAllocation(
   const AllocationSpec& allocationSpec,
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI,
   Sequence& sequence)
{
   sequence.clear();
   if (!abi.isValid())
      return LowerError::InvalidABI;

   if (allocationSpec.kind == AllocationKind::Permanent) {
      return lowerPermanentAllocation(
         allocationSpec, runtime, abi, runtimeABI, sequence);
   }

   if (abi.architecture != Architecture::X86)
      return LowerError::InvalidABI;

   if (allocationSpec.kind == AllocationKind::DynamicReference
      || allocationSpec.kind == AllocationKind::DynamicBinary)
   {
      return lowerDynamicAllocation(
         allocationSpec, runtime, abi, runtimeABI, sequence);
   }

   Operand size = operand(abi.allocationSize, OperandSize::DWord, ValueKind::Integer);
   Operand object = operand(abi.object, OperandSize::DWord, ValueKind::Reference);
   Operand vmt = operand(abi.wideLow, OperandSize::DWord, ValueKind::VMT);

   if (allocationSpec.kind == AllocationKind::InlineBinary) {
      sequence.add(immediate(
         Opcode::AddressOffset,
         object,
         runtime.objectLayout.headerSize,
         MIREffect::None));
      sequence.add(immediate(
         Opcode::MoveImmediate,
         size,
         (int)allocationSpec.payloadSize,
         MIREffect::None));
      sequence.add(immediate(
         Opcode::MoveReferenceValue,
         vmt,
         (int)allocationSpec.vmtReference,
         MIREffect::None));
      sequence.add(store(
         object,
         size,
         -(int)runtime.objectLayout.sizeOffset));
      sequence.add(store(
         object,
         vmt,
         -(int)runtime.objectLayout.vmtOffset));

      return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
         ? LowerError::None
         : LowerError::InvalidMIR;
   }

   RuntimeCallSpec call = {};
   if (!RuntimeCallProvider::get(RuntimeOperation::AllocateYoung, runtime, call)
      || !runtimeABI.isValid(abi, call))
   {
      return LowerError::InvalidRuntime;
   }

   sequence.add(immediate(
      Opcode::MoveImmediate,
      size,
      (int)allocationSpec.allocationSize,
      MIREffect::None));
   sequence.add({
      Opcode::CallRuntime,
      object,
      size,
      (int)RuntimeOperation::AllocateYoung,
      Condition::None,
      machineEffects(runtimeABI.effects)
   });
   sequence.add(immediate(
      Opcode::MoveImmediate,
      size,
      (int)allocationSpec.payloadSize,
      MIREffect::None));
   sequence.add(immediate(
      Opcode::MoveReferenceValue,
      vmt,
      (int)allocationSpec.vmtReference,
      MIREffect::None));
   sequence.add(store(
      object,
      size,
      -(int)runtime.objectLayout.sizeOffset));
   sequence.add(store(
      object,
      vmt,
      -(int)runtime.objectLayout.vmtOffset));

   return MIRVerifier::verify(sequence, abi, &runtimeABI) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError selectCollection(EIRFunction& function, const RuntimeSpec& runtime,
   const ManagedABI& abi, const RuntimeCallABI& runtimeABI, Sequence& sequence)
{
   RuntimeCallSpec collect = {};
   if (!abi.isValid() || runtimeABI.operation != RuntimeOperation::Collect
      || !RuntimeCallProvider::get(RuntimeOperation::Collect, runtime, collect)
      || !runtimeABI.isValid(abi, collect))
   {
      return LowerError::InvalidRuntime;
   }

   EIRInstruction& operation = function.instruction(0);
   EIROperand& fullCollection = function.operand(operation.firstOperand);

   Operand object = operand(abi.object, abi.wordSize, ValueKind::Reference);
   Operand size = operand(abi.allocationSize, OperandSize::DWord, ValueKind::Integer);
   Operand full = operand(abi.value, OperandSize::DWord, ValueKind::Integer);

   sequence.clear();

   if (test(operation.effects, EIREffect::Synchronize)) {
      Operand data = operand(Register::DI, abi.wordSize, ValueKind::Address);
      Operand accumulator = operand(Register::A, OperandSize::DWord, ValueKind::Integer);
      MIREffect atomic = MIREffect::ReadMemory | MIREffect::WriteMemory
         | MIREffect::WriteFlags | MIREffect::Synchronize;

      sequence.add(moveRuntimeData(data, RuntimeDataReference::GCDataLock));
      sequence.add(immediate(Opcode::Label, emptyOperand(), 0, MIREffect::None));
      sequence.add(immediate(Opcode::MoveImmediate, size, 1, MIREffect::None));
      sequence.add(immediate(Opcode::MoveImmediate, accumulator, 0, MIREffect::None));
      sequence.add({
         .opcode = Opcode::AtomicCompareExchangeDWord,
         .destination = data,
         .source = size,
         .immediate = 0,
         .condition = Condition::None,
         .effects = atomic
      });
      sequence.add(immediate(Opcode::JumpNotEqual, emptyOperand(), 0, MIREffect::ReadFlags));
   }

   sequence.add(immediate(Opcode::MoveImmediate, size, 0, MIREffect::None));
   sequence.add(immediate(Opcode::MoveImmediate, full, (int)fullCollection.value, MIREffect::None));

   push(sequence, object);
   if (abi.architecture == Architecture::AMD64)
      push(sequence, operand(Register::C, abi.wordSize, ValueKind::Integer));

   sequence.add({
      .opcode = Opcode::CallRuntime,
      .destination = object,
      .source = size,
      .immediate = (int)RuntimeOperation::Collect,
      .condition = Condition::None,
      .effects = machineEffects(runtimeABI.effects)
   });

   if (abi.architecture == Architecture::AMD64)
      pop(sequence, operand(Register::C, abi.wordSize, ValueKind::Integer));
   pop(sequence, object);

   return MIRVerifier::verify(sequence, abi, &runtimeABI) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError selectThreadPublication(const RuntimeSpec& runtime,
   const ManagedABI& abi, Sequence& sequence)
{
   if (!abi.isValid() || runtime.threadingMode != ThreadingMode::MultiThread
      || runtime.dataLayout.threadContent.size == 0
      || runtime.dataLayout.threadContent.size > 0x7F)
   {
      return LowerError::InvalidRuntime;
   }

   unsigned int slotSize = runtime.dataLayout.threadTable.slotSize;
   int slotScale = 0;
   while (slotSize > 1 && (slotSize & 1) == 0) {
      slotSize >>= 1;
      slotScale++;
   }
   if (slotSize != 1 || slotScale > 4)
      return LowerError::InvalidRuntime;

   Operand thread = operand(Register::A, abi.wordSize, ValueKind::Reference);
   Operand slots = operand(Register::DI, abi.wordSize, ValueKind::Address);
   Operand stack = operand(abi.stack, abi.wordSize, ValueKind::Address);
   Operand frame = operand(abi.frame, abi.wordSize, ValueKind::Address);

   sequence.clear();
   sequence.add(immediate(Opcode::LoadCurrentThread, thread,
      runtime.dataLayout.threadContent.size, MIREffect::ReadMemory | MIREffect::ReadTLS));
   sequence.add(moveRuntimeData(slots, RuntimeDataReference::ThreadTableSlots));
   sequence.add({
      .opcode = Opcode::StoreScaledIndex,
      .destination = slots,
      .source = thread,
      .immediate = slotScale,
      .condition = Condition::None,
      .effects = slotScale == 4
         ? MIREffect::WriteMemory | MIREffect::WriteFlags
         : MIREffect::WriteMemory
   });
   sequence.add(store(thread, stack, runtime.dataLayout.threadContent.stackRoot));
   sequence.add(store(thread, frame, runtime.dataLayout.threadContent.stackFrame));

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError selectSafeRegion(EIRInstruction& operation, const RuntimeSpec& runtime,
   const ManagedABI& abi, const RuntimeCallABI& runtimeABI, Sequence& sequence)
{
   if (operation.effects == EIREffect::None) {
      sequence.clear();
      sequence.add(noOperation());

      return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
         ? LowerError::None : LowerError::InvalidMIR;
   }

   RuntimeCallSpec wait = {};
   if (!abi.isValid() || runtime.threadingMode != ThreadingMode::MultiThread
      || runtimeABI.operation != RuntimeOperation::WaitForGC
      || !RuntimeCallProvider::get(RuntimeOperation::WaitForGC, runtime, wait)
      || !runtimeABI.isValid(abi, wait)
      || runtime.dataLayout.threadContent.size == 0
      || runtime.dataLayout.threadContent.size > 0x7F)
   {
      return LowerError::InvalidRuntime;
   }

   Operand thread = operand(Register::A, abi.wordSize, ValueKind::Reference);
   Operand data = operand(Register::DI, abi.wordSize, ValueKind::Address);
   Operand value = operand(abi.value, abi.wordSize, ValueKind::Integer);
   Operand desired = operand(Register::C, OperandSize::DWord, ValueKind::Integer);
   Operand accumulator = operand(Register::A, OperandSize::DWord, ValueKind::Integer);
   MIREffect atomic = MIREffect::ReadMemory | MIREffect::WriteMemory
      | MIREffect::WriteFlags | MIREffect::Synchronize;

   sequence.clear();
   sequence.add(immediate(Opcode::Label, emptyOperand(), 0, MIREffect::None));
   sequence.add(moveRuntimeData(data, RuntimeDataReference::GCDataLock));
   sequence.add(immediate(Opcode::Label, emptyOperand(), 1, MIREffect::None));
   sequence.add(immediate(Opcode::MoveImmediate, desired, 1, MIREffect::None));
   sequence.add(immediate(Opcode::MoveImmediate, accumulator, 0, MIREffect::None));
   sequence.add(atomicDWord(Opcode::AtomicCompareExchangeDWord, data, desired));
   sequence.add(immediate(Opcode::JumpNotEqual, emptyOperand(), 1, MIREffect::ReadFlags));
   sequence.add(moveRuntimeData(data, RuntimeDataReference::GCDataSignal));
   sequence.add(load(value, data, 0));
   sequence.add(binary(Opcode::Test, value, value, Condition::None, MIREffect::WriteFlags));
   sequence.add(immediate(Opcode::JumpZero, emptyOperand(), 2, MIREffect::ReadFlags));
   sequence.add(moveRuntimeData(data, RuntimeDataReference::GCDataLock));
   sequence.add(immediate(Opcode::MoveImmediate, desired, -1, MIREffect::None));
   sequence.add(atomicDWord(Opcode::AtomicExchangeAddDWord, data, desired));
   sequence.add(callRuntime(RuntimeOperation::WaitForGC, value, runtimeABI));
   sequence.add(immediate(Opcode::Jump, emptyOperand(), 0, MIREffect::None));
   sequence.add(immediate(Opcode::Label, emptyOperand(), 2, MIREffect::None));
   sequence.add(immediate(Opcode::LoadCurrentThread, thread,
      runtime.dataLayout.threadContent.size, MIREffect::ReadMemory | MIREffect::ReadTLS));
   sequence.add(immediate(Opcode::MoveImmediate, desired,
      operation.opcode == EIROpcode::SafeRegionEnter ? 1 : 0, MIREffect::None));
   sequence.add(store(thread, desired, runtime.dataLayout.threadContent.flags,
      abi.architecture == Architecture::X86 ? Opcode::StoreOffset : Opcode::StoreDWordOffset));
   sequence.add(moveRuntimeData(data, RuntimeDataReference::GCDataLock));
   sequence.add(immediate(Opcode::MoveImmediate, desired, -1, MIREffect::None));
   sequence.add({
      .opcode = Opcode::AtomicExchangeAddDWord,
      .destination = data,
      .source = desired,
      .immediate = 0,
      .condition = Condition::None,
      .effects = atomic
   });

   return MIRVerifier::verify(sequence, abi, &runtimeABI) == MIRVerifyError::None
      ? LowerError::None : LowerError::InvalidMIR;
}

static LowerError lowerExceptionControl(EIROpcode opcode, const RuntimeSpec& runtime,
   const ManagedABI& abi, const RuntimeCallABI& runtimeABI, Sequence& sequence);

static bool selectManagedLocation(
   const EIROperand& location,
   const ManagedABI& abi,
   Operand& selected)
{
   if (location.kind != EIROperandKind::Location)
      return false;

   switch ((EIRLocation)location.value) {
      case EIRLocation::ManagedValue:
         if (location.type == EIRType::Word) {
            selected = operand(abi.value, abi.wordSize, ValueKind::Integer);

            return true;
         }

         if ((location.type == EIRType::Int64 || location.type == EIRType::UInt64)
            && abi.architecture == Architecture::AMD64)
         {
            selected = operand(abi.value, OperandSize::QWord, ValueKind::Integer);

            return true;
         }

         if (location.type == EIRType::Pointer) {
            selected = operand(abi.value, abi.wordSize, ValueKind::Address);

            return true;
         }

         if (location.type == EIRType::Int32
            || location.type == EIRType::UInt32
            || location.type == EIRType::Message)
         {
            selected = operand(abi.value, OperandSize::DWord, ValueKind::Integer);

            return true;
         }
         break;

      case EIRLocation::ManagedObject:
         if (location.type == EIRType::Reference) {
            selected = operand(abi.object, abi.wordSize, ValueKind::Reference);

            return true;
         }
         if (location.type == EIRType::Pointer) {
            selected = operand(abi.object, abi.wordSize, ValueKind::Address);

            return true;
         }
         if (location.type == EIRType::VMT) {
            selected = operand(abi.object, abi.wordSize, ValueKind::VMT);

            return true;
         }
         break;

      case EIRLocation::CachedArgument0:
      case EIRLocation::CachedArgument1:
      {
         Register reg = (EIRLocation)location.value == EIRLocation::CachedArgument0
            ? abi.cachedArgument0
            : abi.cachedArgument1;
         if (reg == Register::None)
            break;

         if (location.type == EIRType::Reference) {
            selected = operand(reg, abi.wordSize, ValueKind::Reference);

            return true;
         }

         if (location.type == EIRType::Word) {
            selected = operand(reg, abi.wordSize, ValueKind::Integer);

            return true;
         }

         if (location.type == EIRType::UInt64) {
            selected = operand(
               reg,
               abi.architecture == Architecture::X86
                  ? OperandSize::DWord
                  : OperandSize::QWord,
               ValueKind::Integer);

            return true;
         }
         break;
      }

      case EIRLocation::StackPointer:
         if (location.type == EIRType::Pointer) {
            selected = operand(abi.stack, abi.wordSize, ValueKind::Address);

            return true;
         }
         break;

      case EIRLocation::FramePointer:
         if (location.type == EIRType::Pointer) {
            selected = operand(abi.frame, abi.wordSize, ValueKind::Address);

            return true;
         }
         break;

      default:
         break;
   }

   return false;
}

static LowerError selectImplicitState(
   EIRFunction& function,
   const ManagedABI& abi,
   Sequence& sequence)
{
   if (!abi.isValid() || function.instructionCount() == 0)
      return LowerError::InvalidMIR;

   EIRInstruction& operation = function.instruction(0);
   sequence.clear();

   switch (operation.opcode) {
      case EIROpcode::Return:
         if (operation.operandCount == 1) {
            Operand value = emptyOperand();
            if (!selectManagedLocation(
                  function.operand(operation.firstOperand), abi, value))
            {
               return LowerError::InvalidMIR;
            }

            Operand result = operand(Register::A, abi.wordSize, ValueKind::Integer);
            sequence.add(binary(
               Opcode::Move, result, value, Condition::None, MIREffect::None));
         }
         else if (operation.operandCount != 0) {
            return LowerError::InvalidMIR;
         }

         sequence.add({
            .opcode = Opcode::Return,
            .destination = emptyOperand(),
            .source = emptyOperand(),
            .immediate = 0,
            .condition = Condition::None,
            .effects = MIREffect::ReadMemory
         });
         break;

      case EIROpcode::CallIndirect:
      case EIROpcode::IndirectBranch:
      {
         Operand target = emptyOperand();
         if (operation.operandCount != 1
            || !selectManagedLocation(
               function.operand(operation.firstOperand), abi, target))
         {
            return LowerError::InvalidMIR;
         }

         sequence.add(unary(
            operation.opcode == EIROpcode::CallIndirect
               ? Opcode::CallRegister
               : Opcode::JumpRegister,
            target,
            operation.opcode == EIROpcode::CallIndirect
               ? MIREffect::ReadMemory | MIREffect::WriteMemory
                  | MIREffect::Call | MIREffect::Safepoint | MIREffect::MayThrow
               : MIREffect::None));
         break;
      }

      case EIROpcode::BitNot:
      case EIROpcode::Negate:
      {
         if (operation.operandCount != 1)
            return LowerError::InvalidMIR;

         EIROperand& location = function.operand(operation.firstOperand);
         if (location.type == EIRType::Int64
            && abi.architecture == Architecture::X86)
         {
            Operand low = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
            Operand high = operand(abi.wideHigh, OperandSize::DWord, ValueKind::Integer);
            sequence.add(unary(Opcode::BitNot, high, MIREffect::None));
            sequence.add(unary(Opcode::BitNot, low, MIREffect::None));
            sequence.add(immediate(Opcode::AddImmediate, low, 1, MIREffect::WriteFlags));
            sequence.add(immediate(
               Opcode::AddCarryImmediate,
               high,
               0,
               MIREffect::ReadFlags | MIREffect::WriteFlags));
         }
         else {
            Operand value = emptyOperand();
            if (!selectManagedLocation(location, abi, value))
               return LowerError::InvalidMIR;

            sequence.add(unary(
               operation.opcode == EIROpcode::BitNot ? Opcode::BitNot : Opcode::Negate,
               value,
               operation.opcode == EIROpcode::BitNot
                  ? MIREffect::None
                  : MIREffect::WriteFlags));
         }
         break;
      }

      case EIROpcode::Convert:
      {
         if (operation.operandCount != 2)
            return LowerError::InvalidMIR;

         if (abi.architecture == Architecture::X86) {
            Operand value = operand(abi.value, OperandSize::DWord, ValueKind::Integer);
            Operand low = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
            Operand high = operand(abi.wideHigh, OperandSize::DWord, ValueKind::Integer);
            sequence.add(binary(Opcode::Move, low, value, Condition::None, MIREffect::None));
            sequence.add(binary(
               Opcode::SignExtend, high, low, Condition::None, MIREffect::None));
         }
         else {
            Operand nativeValue = operand(abi.value, abi.wordSize, ValueKind::Integer);
            Operand value = operand(abi.value, OperandSize::DWord, ValueKind::Integer);
            sequence.add(binary(
               Opcode::SignExtendDWord,
               nativeValue,
               value,
               Condition::None,
               MIREffect::None));
         }
         break;
      }

      case EIROpcode::Coalesce:
      case EIROpcode::SelectEqual:
      {
         if (operation.operandCount != 2)
            return LowerError::InvalidMIR;

         Operand object = emptyOperand();
         Operand argument = emptyOperand();
         if (!selectManagedLocation(function.operand(operation.firstOperand), abi, object)
            || !selectManagedLocation(
               function.operand(operation.firstOperand + 1), abi, argument))
         {
            return LowerError::InvalidMIR;
         }

         if (operation.opcode == EIROpcode::Coalesce) {
            sequence.add(binary(
               Opcode::Test, object, object, Condition::None, MIREffect::WriteFlags));
         }
         sequence.add(binary(
            Opcode::ConditionalMove,
            object,
            argument,
            Condition::Equal,
            MIREffect::ReadFlags));
         break;
      }

      case EIROpcode::SystemEnvironment:
      {
         Operand destination = emptyOperand();
         if (operation.operandCount != 1
            || !selectManagedLocation(
               function.operand(operation.firstOperand), abi, destination))
         {
            return LowerError::InvalidMIR;
         }

         sequence.add(moveRuntimeData(
            destination, RuntimeDataReference::SystemEnvironment));
         break;
      }

      case EIROpcode::ComposeMessage:
      {
         if (operation.operandCount != 3)
            return LowerError::InvalidMIR;

         Operand value = emptyOperand();
         Operand object = emptyOperand();
         EIROperand& maskOperand = function.operand(operation.firstOperand + 2);
         if (!selectManagedLocation(function.operand(operation.firstOperand), abi, value)
            || !selectManagedLocation(
               function.operand(operation.firstOperand + 1), abi, object))
         {
            return LowerError::InvalidMIR;
         }

         Operand messagePart = operand(Register::C, OperandSize::DWord, ValueKind::Integer);
         sequence.add(immediate(
            Opcode::AndImmediate, value, (int)maskOperand.value, MIREffect::WriteFlags));
         sequence.add(load(
            messagePart,
            object,
            0,
            abi.architecture == Architecture::X86
               ? Opcode::LoadOffset
               : Opcode::LoadDWordOffset));
         sequence.add(immediate(
            Opcode::AndImmediate,
            messagePart,
            ~(int)maskOperand.value,
            MIREffect::WriteFlags));
         sequence.add(binary(
            Opcode::Or, value, messagePart, Condition::None, MIREffect::WriteFlags));
         break;
      }

      case EIROpcode::LoadObjectVMT:
      {
         if (operation.operandCount != 3)
            return LowerError::InvalidMIR;

         Operand destination = emptyOperand();
         Operand object = emptyOperand();
         EIROperand& offset = function.operand(operation.firstOperand + 2);
         if (!selectManagedLocation(function.operand(operation.firstOperand), abi, destination)
            || !selectManagedLocation(
               function.operand(operation.firstOperand + 1), abi, object))
         {
            return LowerError::InvalidMIR;
         }

         sequence.add(load(destination, object, (int)offset.value));
         break;
      }

      case EIROpcode::LoadZeroExtend:
      case EIROpcode::LoadSignExtend:
      {
         if (operation.operandCount != 4)
            return LowerError::UnsupportedOpcode;

         EIROperand& destinationLocation = function.operand(operation.firstOperand);
         EIROperand& sourceLocation = function.operand(operation.firstOperand + 1);
         EIROperand& displacement = function.operand(operation.firstOperand + 2);
         EIROperand& width = function.operand(operation.firstOperand + 3);
         Operand source = emptyOperand();
         if (!selectManagedLocation(sourceLocation, abi, source))
            return LowerError::InvalidMIR;

         Operand destination = operand(
            abi.value, OperandSize::DWord, ValueKind::Integer);
         if (destinationLocation.type == EIRType::UInt64
            && abi.architecture == Architecture::X86)
         {
            destination = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
         }

         Opcode opcode;
         if (width.value == 1) {
            opcode = Opcode::LoadZeroExtendByteOffset;
         }
         else if (width.value == 2) {
            opcode = operation.opcode == EIROpcode::LoadSignExtend
               ? Opcode::LoadSignExtendWordOffset
               : Opcode::LoadZeroExtendWordOffset;
         }
         else if (width.value == 4
            && operation.opcode == EIROpcode::LoadZeroExtend)
         {
            opcode = abi.architecture == Architecture::X86
               ? Opcode::LoadOffset
               : Opcode::LoadDWordOffset;
         }
         else {
            return LowerError::InvalidMIR;
         }

         sequence.add(load(destination, source, (int)displacement.value, opcode));
         if (destinationLocation.type == EIRType::UInt64
            && abi.architecture == Architecture::X86)
         {
            Operand high = operand(abi.wideHigh, OperandSize::DWord, ValueKind::Integer);
            sequence.add(immediate(Opcode::MoveImmediate, high, 0, MIREffect::None));
         }
         break;
      }

      case EIROpcode::LoadIndexed:
      {
         if (operation.operandCount != 3)
            return LowerError::InvalidMIR;

         EIROperand& destinationLocation = function.operand(operation.firstOperand);
         Operand source = emptyOperand();
         if (!selectManagedLocation(
               function.operand(operation.firstOperand + 1), abi, source))
         {
            return LowerError::InvalidMIR;
         }

         if (destinationLocation.type == EIRType::Int64
            && abi.architecture == Architecture::X86)
         {
            Operand low = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
            Operand high = operand(abi.wideHigh, OperandSize::DWord, ValueKind::Integer);
            sequence.add({
               Opcode::LoadIndexedOffset, low, source, 0,
               Condition::None, MIREffect::ReadMemory
            });
            sequence.add({
               Opcode::LoadIndexedOffset, high, source, 4,
               Condition::None, MIREffect::ReadMemory
            });
         }
         else {
            Operand destination = emptyOperand();
            if (!selectManagedLocation(destinationLocation, abi, destination))
               return LowerError::InvalidMIR;

            sequence.add({
               destinationLocation.type == EIRType::Reference
                  ? Opcode::LoadReferenceIndex
                  : Opcode::LoadIndexedOffset,
               destination,
               source,
               0,
               Condition::None, MIREffect::ReadMemory
            });
         }
         break;
      }

      case EIROpcode::StoreIndexed:
      {
         if (operation.operandCount != 3)
            return LowerError::InvalidMIR;

         Operand object = emptyOperand();
         Operand argument = emptyOperand();
         if (!selectManagedLocation(function.operand(operation.firstOperand), abi, object)
            || !selectManagedLocation(
               function.operand(operation.firstOperand + 1), abi, argument))
         {
            return LowerError::InvalidMIR;
         }

         sequence.add({
            Opcode::StoreReferenceIndex, object, argument, 0,
            Condition::None, MIREffect::WriteMemory
         });
         break;
      }

      default:
         return LowerError::UnsupportedOpcode;
   }

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError selectManagedState(
   EIRFunction& function,
   const ManagedABI& abi,
   Sequence& sequence)
{
   if (!abi.isValid()
      || function.blockCount() != 1
      || function.instructionCount() != 2)
   {
      return LowerError::InvalidMIR;
   }

   EIRInstruction& operation = function.instruction(0);
   if (operation.operandCount < 2 || operation.operandCount > 5)
      return LowerError::InvalidMIR;

   EIROperand& first = function.operand(operation.firstOperand);
   EIROperand& second = function.operand(operation.firstOperand + 1);
   Operand destination = emptyOperand();

   sequence.clear();

   switch (operation.opcode) {
      case EIROpcode::Copy:
         if (abi.architecture == Architecture::X86
            && first.kind == EIROperandKind::Location
            && first.value == (pos64_t)EIRLocation::ManagedValue
            && first.type == EIRType::UInt64
            && second.kind == EIROperandKind::Location)
         {
            Operand source = emptyOperand();
            if (!selectManagedLocation(second, abi, source))
               return LowerError::InvalidMIR;

            Operand low = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
            Operand high = operand(abi.wideHigh, OperandSize::DWord, ValueKind::Integer);
            sequence.add(binary(
               Opcode::Move, low, source, Condition::None, MIREffect::None));
            sequence.add(unary(Opcode::Clear, high, MIREffect::WriteFlags));

            break;
         }

         if (!selectManagedLocation(first, abi, destination))
            return LowerError::InvalidMIR;

         if (second.kind == EIROperandKind::Immediate) {
            sequence.add(immediate(
               Opcode::MoveImmediate,
               destination,
               (int)second.value,
               MIREffect::None));
         }
         else if (second.kind == EIROperandKind::Reference
            && second.type == EIRType::Message)
         {
            sequence.add(immediate(
               Opcode::MoveMessage,
               destination,
               (int)second.value,
               MIREffect::None));
         }
         else if (second.kind == EIROperandKind::Reference
            && second.type == EIRType::Reference)
         {
            sequence.add(immediate(
               Opcode::MoveReferenceValue,
               destination,
               (int)second.value,
               MIREffect::None));
         }
         else if (second.kind == EIROperandKind::Location) {
            Operand source = emptyOperand();
            if (!selectManagedLocation(second, abi, source))
               return LowerError::InvalidMIR;

            sequence.add(binary(
               Opcode::Move,
               destination,
               source,
               Condition::None,
               MIREffect::None));
         }
         else {
            return LowerError::InvalidMIR;
         }
         break;

      case EIROpcode::Add:
      case EIROpcode::Subtract:
      case EIROpcode::Multiply:
      case EIROpcode::BitAnd:
      case EIROpcode::BitOr:
      case EIROpcode::Compare:
      case EIROpcode::ShiftLeft:
      case EIROpcode::ShiftRightUnsigned:
      {
         if ((operation.opcode == EIROpcode::Add
               || operation.opcode == EIROpcode::Compare)
            && operation.operandCount == 3)
         {
            EIROperand& displacement = function.operand(operation.firstOperand + 2);
            Operand address = emptyOperand();
            if (!selectManagedLocation(first, abi, destination)
               || !selectManagedLocation(second, abi, address)
               || displacement.kind != EIROperandKind::Immediate)
            {
               return LowerError::InvalidMIR;
            }

            Operand scratch = operand(
               Register::C, OperandSize::DWord, ValueKind::Integer);
            sequence.add(load(
               scratch,
               address,
               (int)displacement.value,
               abi.architecture == Architecture::X86
                  ? Opcode::LoadOffset
                  : Opcode::LoadDWordOffset));
            sequence.add(binary(
               operation.opcode == EIROpcode::Add ? Opcode::Add : Opcode::Compare,
               destination,
               scratch,
               Condition::None,
               MIREffect::WriteFlags));

            break;
         }

         if (!selectManagedLocation(first, abi, destination)
            || second.kind != EIROperandKind::Immediate)
         {
            return LowerError::InvalidMIR;
         }

         if (operation.opcode == EIROpcode::Multiply) {
            sequence.add(multiply(destination, destination, (int)second.value));

            break;
         }

         Opcode opcode;
         switch (operation.opcode) {
            case EIROpcode::Add:
               opcode = Opcode::AddImmediate;
               break;
            case EIROpcode::Subtract:
               opcode = Opcode::SubtractImmediate;
               break;
            case EIROpcode::BitAnd:
               opcode = Opcode::AndImmediate;
               break;
            case EIROpcode::BitOr:
               opcode = Opcode::OrImmediate;
               break;
            case EIROpcode::Compare:
               opcode = Opcode::CompareImmediate;
               break;
            case EIROpcode::ShiftLeft:
               opcode = Opcode::ShiftLeftImmediate;
               break;
            case EIROpcode::ShiftRightUnsigned:
               opcode = Opcode::ShiftRightImmediate;
               break;
            default:
               return LowerError::InvalidMIR;
         }

         sequence.add(immediate(
            opcode,
            destination,
            (int)second.value,
            MIREffect::WriteFlags));
         break;
      }

      case EIROpcode::FrameAddress:
      case EIROpcode::StackAddress:
      {
         EIROperand& displacement = function.operand(operation.firstOperand + 2);
         Operand address = emptyOperand();
         if (!selectManagedLocation(first, abi, destination)
            || !selectManagedLocation(second, abi, address)
            || displacement.kind != EIROperandKind::Immediate)
         {
            return LowerError::InvalidMIR;
         }

         sequence.add({
            .opcode = Opcode::AddressOffsetFrom,
            .destination = destination,
            .source = address,
            .immediate = (int)displacement.value,
            .condition = Condition::None,
            .effects = MIREffect::None
         });
         break;
      }

      case EIROpcode::IndexedFrameAddress:
      {
         EIROperand& index = function.operand(operation.firstOperand + 2);
         EIROperand& displacement = function.operand(operation.firstOperand + 3);
         Operand address = emptyOperand();
         Operand indexValue = emptyOperand();
         if (!selectManagedLocation(first, abi, destination)
            || !selectManagedLocation(second, abi, address)
            || !selectManagedLocation(index, abi, indexValue)
            || indexValue.reg != abi.value
            || displacement.kind != EIROperandKind::Immediate)
         {
            return LowerError::InvalidMIR;
         }

         sequence.add({
            .opcode = Opcode::AddressScaledIndex,
            .destination = destination,
            .source = address,
            .immediate = (int)displacement.value,
            .condition = Condition::None,
            .effects = MIREffect::None
         });
         break;
      }

      case EIROpcode::MemoryFill:
      {
         EIROperand& countOperand = function.operand(operation.firstOperand + 1);
         EIROperand& fillOperand = function.operand(operation.firstOperand + 2);
         if (!selectManagedLocation(first, abi, destination)
            || countOperand.kind != EIROperandKind::Immediate)
         {
            return LowerError::InvalidMIR;
         }

         Operand fill = operand(Register::A, abi.wordSize, ValueKind::Reference);
         Operand fillDestination = operand(
            Register::DI, abi.wordSize, ValueKind::Reference);
         Operand count = operand(Register::C, OperandSize::DWord, ValueKind::Integer);

         if (fillOperand.kind == EIROperandKind::Immediate) {
            sequence.add(immediate(
               Opcode::MoveImmediate, fill, (int)fillOperand.value, MIREffect::None));
         }
         else if (fillOperand.kind == EIROperandKind::Reference) {
            sequence.add(immediate(
               Opcode::MoveReferenceValue, fill, (int)fillOperand.value, MIREffect::None));
         }
         else {
            return LowerError::InvalidMIR;
         }

         sequence.add(binary(
            Opcode::Move, fillDestination, destination, Condition::None, MIREffect::None));
         sequence.add(immediate(
            Opcode::MoveImmediate, count, (int)countOperand.value, MIREffect::None));
         sequence.add(binary(
            Opcode::RepeatStore,
            fillDestination,
            fill,
            Condition::None,
            MIREffect::WriteMemory));
         break;
      }

      case EIROpcode::ObjectSize:
      {
         EIROperand& divisorOperand = function.operand(operation.firstOperand + 2);
         EIROperand& maskOperand = function.operand(operation.firstOperand + 3);
         EIROperand& offsetOperand = function.operand(operation.firstOperand + 4);
         Operand object = emptyOperand();
         if (!selectManagedLocation(first, abi, destination)
            || !selectManagedLocation(second, abi, object))
         {
            return LowerError::InvalidMIR;
         }

         Operand size = operand(
            abi.allocationSize, OperandSize::DWord, ValueKind::Integer);
         Operand low = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
         sequence.add(immediate(
            Opcode::MoveImmediate, destination, (int)maskOperand.value, MIREffect::None));
         sequence.add(load(
            size,
            object,
            (int)offsetOperand.value,
            abi.architecture == Architecture::X86
               ? Opcode::LoadOffset
               : Opcode::LoadDWordOffset));
         sequence.add(binary(
            Opcode::And, destination, size, Condition::None, MIREffect::WriteFlags));

         unsigned int divisor = (unsigned int)divisorOperand.value;
         if ((divisor & (divisor - 1)) == 0) {
            int shift = 0;
            while (divisor > 1) {
               divisor >>= 1;
               shift++;
            }

            if (shift != 0) {
               sequence.add(immediate(
                  Opcode::ShiftRightImmediate,
                  destination,
                  shift,
                  MIREffect::WriteFlags));
            }
         }
         else {
            sequence.add(binary(
               Opcode::Move, low, destination, Condition::None, MIREffect::None));
            sequence.add(immediate(
               Opcode::MoveImmediate, destination, 0, MIREffect::None));
            sequence.add(immediate(
               Opcode::MoveImmediate,
               size,
               (int)divisorOperand.value,
               MIREffect::None));
            sequence.add(binary(
               Opcode::DivideUnsigned, low, size, Condition::None, MIREffect::WriteFlags));
            sequence.add(binary(
               Opcode::Move, destination, low, Condition::None, MIREffect::None));
         }
         break;
      }

      case EIROpcode::Load:
      case EIROpcode::LoadSignExtend:
      {
         if (operation.operandCount == 3) {
            EIROperand& displacement = function.operand(operation.firstOperand + 2);
            Operand address = emptyOperand();
            if (!selectManagedLocation(second, abi, address)
               || displacement.kind != EIROperandKind::Immediate)
            {
               return LowerError::InvalidMIR;
            }

            if (abi.architecture == Architecture::X86
               && first.value == (pos64_t)EIRLocation::ManagedValue
               && (first.type == EIRType::Int64 || first.type == EIRType::UInt64))
            {
               Operand low = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
               Operand high = operand(abi.wideHigh, OperandSize::DWord, ValueKind::Integer);
               sequence.add(load(low, address, (int)displacement.value));
               sequence.add(load(high, address, (int)displacement.value + 4));
            }
            else {
               if (!selectManagedLocation(first, abi, destination))
                  return LowerError::InvalidMIR;

               Opcode loadOpcode = operation.opcode == EIROpcode::LoadSignExtend
                  && abi.architecture == Architecture::AMD64
                  ? Opcode::LoadSignExtendDWordOffset
                  : Opcode::LoadOffset;
               sequence.add(load(
                  destination, address, (int)displacement.value, loadOpcode));
            }

            break;
         }

         if (!selectManagedLocation(first, abi, destination))
            return LowerError::InvalidMIR;

         if (second.kind != EIROperandKind::Reference)
            return LowerError::InvalidMIR;

         Operand address = operand(Register::A, abi.wordSize, ValueKind::Reference);
         sequence.add(immediate(
            Opcode::MoveReferenceValue,
            address,
            (int)second.value,
            MIREffect::None));
         sequence.add(load(destination, address, 0));
         break;
      }

      case EIROpcode::Store:
         if (operation.operandCount == 3) {
            EIROperand& displacement = function.operand(operation.firstOperand + 2);
            Operand source = emptyOperand();
            bool storedWideValue = false;
            if (!selectManagedLocation(first, abi, destination)
               || displacement.kind != EIROperandKind::Immediate)
            {
               return LowerError::InvalidMIR;
            }

            if (abi.architecture == Architecture::X86
               && second.value == (pos64_t)EIRLocation::ManagedValue
               && second.type == EIRType::Int64)
            {
               Operand low = operand(abi.wideLow, OperandSize::DWord, ValueKind::Integer);
               Operand high = operand(abi.wideHigh, OperandSize::DWord, ValueKind::Integer);
               sequence.add(store(destination, high, (int)displacement.value + 4));
               sequence.add(store(destination, low, (int)displacement.value));
               storedWideValue = true;
            }
            else {
               if (!selectManagedLocation(second, abi, source))
                  return LowerError::InvalidMIR;
            }

            if (!storedWideValue && abi.architecture == Architecture::AMD64
               && destination.reg == abi.stack
               && source.reg == abi.value
               && source.size == OperandSize::DWord)
            {
               Operand accumulator = operand(
                  Register::A, abi.wordSize, ValueKind::Integer);
               Operand accumulatorDWord = operand(
                  Register::A, OperandSize::DWord, ValueKind::Integer);

               sequence.add(binary(
                  Opcode::Move,
                  accumulatorDWord,
                  source,
                  Condition::None,
                  MIREffect::None));
               sequence.add(store(
                  destination,
                  accumulator,
                  (int)displacement.value));
            }
            else if (!storedWideValue) {
               Opcode storeOpcode = abi.architecture == Architecture::AMD64
                  && source.size == OperandSize::DWord
                  ? Opcode::StoreDWordOffset
                  : Opcode::StoreOffset;

               sequence.add(store(
                  destination,
                  source,
                  (int)displacement.value,
                  storeOpcode));
            }
         }
         else if (first.kind == EIROperandKind::Location) {
            if (!selectManagedLocation(first, abi, destination)
               || second.kind != EIROperandKind::Immediate)
            {
               return LowerError::InvalidMIR;
            }

            sequence.add(immediate(
               Opcode::StoreImmediateDWord,
               destination,
               (int)second.value,
               MIREffect::WriteMemory));
         }
         else {
            if (first.kind != EIROperandKind::Reference
               || !selectManagedLocation(second, abi, destination))
            {
               return LowerError::InvalidMIR;
            }

            Operand address = operand(Register::A, abi.wordSize, ValueKind::Reference);
            sequence.add(immediate(
               Opcode::MoveReferenceValue,
               address,
               (int)first.value,
               MIREffect::None));
            sequence.add(store(address, destination, 0));
         }
         break;

      default:
         return LowerError::UnsupportedOpcode;
   }

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError selectGenericEIR(EIRFunction& function, const ECodeEIRMetadata& metadata,
   const RuntimeSpec& runtime,
   const ManagedABI& abi, const RuntimeCallABI& runtimeABI,
   const LoweringContext& context, Sequence& sequence)
{
   if (function.instructionCount() == 0)
      return LowerError::InvalidMIR;

   switch (metadata.kind) {
      case ECodeEIRKind::ManagedMethod:
         return selectManagedMethod(metadata.managedMethod, function, runtime, abi, sequence);
      case ECodeEIRKind::VirtualMethod:
         return selectVirtualMethod(metadata.virtualMethod, function, runtime, abi, sequence);
      case ECodeEIRKind::Dispatch:
         return selectXDispatch(metadata.dispatch, function, runtime, abi, sequence);
      case ECodeEIRKind::Allocation:
         return selectAllocation(
            metadata.allocation, runtime, abi, runtimeABI, sequence);
      case ECodeEIRKind::Operation:
         break;
   }

   switch (function.instruction(0).opcode) {
      case EIROpcode::Return:
      case EIROpcode::CallIndirect:
      case EIROpcode::IndirectBranch:
      case EIROpcode::BitNot:
      case EIROpcode::Negate:
      case EIROpcode::Convert:
      case EIROpcode::Coalesce:
      case EIROpcode::SelectEqual:
      case EIROpcode::SystemEnvironment:
      case EIROpcode::ComposeMessage:
      case EIROpcode::LoadObjectVMT:
      case EIROpcode::LoadZeroExtend:
      case EIROpcode::LoadIndexed:
      case EIROpcode::StoreIndexed:
         return selectImplicitState(function, abi, sequence);

      case EIROpcode::Copy:
      case EIROpcode::Add:
      case EIROpcode::Subtract:
      case EIROpcode::Multiply:
      case EIROpcode::BitAnd:
      case EIROpcode::BitOr:
      case EIROpcode::Compare:
      case EIROpcode::ShiftLeft:
      case EIROpcode::ShiftRightUnsigned:
      case EIROpcode::Load:
      case EIROpcode::Store:
      case EIROpcode::FrameAddress:
      case EIROpcode::StackAddress:
      case EIROpcode::IndexedFrameAddress:
      case EIROpcode::MemoryFill:
      case EIROpcode::ObjectSize:
         return selectManagedState(function, abi, sequence);

      case EIROpcode::LoadSignExtend:
         return function.instruction(0).operandCount == 4
            ? selectImplicitState(function, abi, sequence)
            : selectManagedState(function, abi, sequence);

      case EIROpcode::FrameOpen:
         return selectFrameOpen(function, abi, sequence);

      case EIROpcode::FrameClose:
         return selectFrameClose(function, abi, sequence);

      case EIROpcode::ExternalFrameOpen:
         return selectExternalFrameOpen(function, runtime, abi, context, sequence);

      case EIROpcode::ExternalFrameClose:
         return selectExternalFrameClose(function, runtime, abi, context, sequence);

      case EIROpcode::MemoryCopy:
         return selectMemoryCopy(function, abi, sequence);

      case EIROpcode::IsCurrentStackReference:
         return selectStackReference(function, runtime, abi, context, sequence);

      case EIROpcode::StackAllocate:
         return selectRootStackAllocation(function, runtime, abi, sequence);

      case EIROpcode::NoOperation:
         sequence.clear();
         sequence.add(noOperation());

         return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
            ? LowerError::None : LowerError::InvalidMIR;

      case EIROpcode::Collect:
         return selectCollection(function, runtime, abi, runtimeABI, sequence);

      case EIROpcode::ThreadPublish:
         return selectThreadPublication(runtime, abi, sequence);

      case EIROpcode::ThreadLocalLoad:
      case EIROpcode::ThreadLocalStore:
      {
         EIROperand& reference = function.operand(
            function.instruction(0).firstOperand);
         bool load = function.instruction(0).opcode
            == EIROpcode::ThreadLocalLoad;

         sequence.clear();
         sequence.add({
            .opcode = load
               ? Opcode::LoadThreadLocal
               : Opcode::StoreThreadLocal,
            .destination = load
               ? operand(abi.object, abi.wordSize, ValueKind::Reference)
               : emptyOperand(),
            .source = load
               ? emptyOperand()
               : operand(abi.object, abi.wordSize, ValueKind::Reference),
            .immediate = (int)(ref_t)reference.value,
            .condition = Condition::None,
            .effects = load
               ? MIREffect::ReadMemory | MIREffect::ReadTLS
               : MIREffect::WriteMemory | MIREffect::ReadTLS
                  | MIREffect::WriteTLS
         });

         return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
            ? LowerError::None
            : LowerError::InvalidMIR;
      }

      case EIROpcode::SystemStartup:
         return lowerSystemStartup(function, runtime, abi, runtimeABI, context, sequence);

      case EIROpcode::SafeRegionEnter:
      case EIROpcode::SafeRegionLeave:
         return selectSafeRegion(function.instruction(0), runtime, abi, runtimeABI, sequence);

      case EIROpcode::GCLockAcquire:
      case EIROpcode::GCLockRelease:
      {
         sequence.clear();

         Operand lockAddress = operand(Register::DI, abi.wordSize, ValueKind::Address);
         sequence.add(moveRuntimeData(lockAddress, RuntimeDataReference::GCDataLock));

         if (function.instruction(0).opcode == EIROpcode::GCLockAcquire)
            acquireGCLock(lockAddress, sequence);
         else releaseGCLock(lockAddress, sequence);

         return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
            ? LowerError::None
            : LowerError::InvalidMIR;
      }

      case EIROpcode::ObjectLockTry:
      case EIROpcode::ObjectLockRelease:
      {
         Operand objectAddress = operand(
            abi.object,
            abi.wordSize,
            ValueKind::Address);
         Operand accumulator = operand(
            Register::A,
            OperandSize::DWord,
            ValueKind::Integer);
         Operand lockValue = operand(
            Register::C,
            OperandSize::DWord,
            ValueKind::Integer);
         int lockOffset = -(int)runtime.objectLayout.synchronizationOffset;

         sequence.clear();

         if (function.instruction(0).effects == EIREffect::None) {
            if (function.instruction(0).opcode == EIROpcode::ObjectLockTry) {
               sequence.add(unary(
                  Opcode::Clear,
                  accumulator,
                  MIREffect::WriteFlags));
            }
            else {
               sequence.add(noOperation());
            }

            return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
               ? LowerError::None
               : LowerError::InvalidMIR;
         }

         if (function.instruction(0).opcode == EIROpcode::ObjectLockTry) {
            sequence.add(unary(
               Opcode::Clear,
               accumulator,
               MIREffect::WriteFlags));
            sequence.add(immediate(
               Opcode::MoveImmediate,
               lockValue,
               1,
               MIREffect::None));
            sequence.add(atomicByte(
               Opcode::AtomicCompareExchangeByte,
               objectAddress,
               lockValue,
               lockOffset));
            sequence.add(binary(
               Opcode::Test,
               accumulator,
               accumulator,
               Condition::None,
               MIREffect::WriteFlags));
         }
         else {
            sequence.add(immediate(
               Opcode::MoveImmediate,
               lockValue,
               -1,
               MIREffect::None));
            sequence.add(atomicByte(
               Opcode::AtomicExchangeAddByte,
               objectAddress,
               lockValue,
               lockOffset));
         }

         return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
            ? LowerError::None
            : LowerError::InvalidMIR;
      }

      case EIROpcode::ExceptionRaise:
      case EIROpcode::ExceptionUnhook:
      case EIROpcode::ThreadExclude:
      case EIROpcode::ThreadInclude:
         return lowerExceptionControl(function.instruction(0).opcode, runtime, abi, runtimeABI, sequence);

      case EIROpcode::ExceptionHook:
         return selectExceptionHook(function, runtime, abi, sequence);

      default:
         return LowerError::UnsupportedOpcode;
   }
}

LowerError ECodeLowering :: lower(const ByteCommand& command,
   const RuntimeSpec& runtime, const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI, const LoweringContext& context,
   Sequence& sequence)
{
   TargetSpec target = {};
   if (!TargetProvider::get(context.platform, target))
      return LowerError::InvalidRuntime;

   EIRFunction genericEIR;
   ECodeEIRMetadata metadata = {};
   ECodeEIRLowerError genericError = ECodeEIRProvider::lower(
      command, runtime, target, context, genericEIR, &metadata);

   switch (genericError) {
      case ECodeEIRLowerError::None:
         return selectGenericEIR(
            genericEIR, metadata, runtime, abi, runtimeABI, context, sequence);
      case ECodeEIRLowerError::InvalidArgument:
         return LowerError::InvalidArgument;
      case ECodeEIRLowerError::InvalidRuntime:
         return LowerError::InvalidRuntime;
      case ECodeEIRLowerError::InvalidEIR:
         return LowerError::InvalidMIR;
      case ECodeEIRLowerError::UnsupportedOpcode:
         break;
   }

   return LowerError::UnsupportedOpcode;
}

static LowerError selectExceptionHook(
   EIRFunction& function,
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   Sequence& sequence)
{
   if (!abi.isValid())
   {
      return LowerError::InvalidABI;
   }

   EIRInstruction& operation = function.instruction(0);
   bool threadLocal = test(operation.effects, EIREffect::ReadTLS);

   if (runtime.objectLayout.fieldSize != architectureWordSize(abi.architecture)
      || runtime.dataLayout.threadContent.size == 0
      || runtime.dataLayout.threadContent.size > 0x7F
      || threadLocal != (runtime.threadingMode == ThreadingMode::MultiThread))
   {
      return LowerError::InvalidRuntime;
   }

   if (function.blockCount() != 1
      || function.instructionCount() != 2
      || function.instruction(0).opcode != EIROpcode::ExceptionHook
      || function.instruction(1).opcode != EIROpcode::Fallthrough)
   {
      return LowerError::InvalidMIR;
   }

   Operand exception = operand(Register::DI, abi.wordSize, ValueKind::Address);
   Operand frame = operand(abi.frame, abi.wordSize, ValueKind::Address);
   Operand stack = operand(abi.stack, abi.wordSize, ValueKind::Address);
   Operand thread = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand link = operand(Register::C, abi.wordSize, ValueKind::Address);
   EIROperand& frameOffset = function.operand(operation.firstOperand);
   EIROperand& target = function.operand(operation.firstOperand + 1);
   ExceptionTargetKind targetKind = (ExceptionTargetKind)
      function.operand(operation.firstOperand + 2).value;

   sequence.clear();

   sequence.add({
      Opcode::AddressOffsetFrom,
      exception,
      frame,
      (int)frameOffset.value,
      Condition::None,
      MIREffect::None
   });

   loadThreadContent(sequence, runtime, thread);

   sequence.add(load(
      link,
      thread,
      runtime.dataLayout.threadContent.currentException));

   sequence.add(store(
      exception,
      link,
      RuntimeLayout::offsetOf(
         runtime.objectLayout.fieldSize,
         ExceptionStructField::Previous)));

   sequence.add(store(
      exception,
      frame,
      RuntimeLayout::offsetOf(
         runtime.objectLayout.fieldSize,
         ExceptionStructField::CatchFrame)));

   sequence.add(store(
      exception,
      stack,
      RuntimeLayout::offsetOf(
         runtime.objectLayout.fieldSize,
         ExceptionStructField::CatchLevel)));

   sequence.add(immediate(
      targetKind == ExceptionTargetKind::ProcedureLabel
         ? Opcode::MoveLabelAddress
         : Opcode::MoveReferenceAddress,
      link,
      (int)target.value,
      MIREffect::None));

   sequence.add(store(
      exception,
      link,
      RuntimeLayout::offsetOf(
         runtime.objectLayout.fieldSize,
         ExceptionStructField::CatchAddress)));

   sequence.add(store(
      thread,
      exception,
      runtime.dataLayout.threadContent.currentException));

   return MIRVerifier::verify(sequence, abi) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

static LowerError validateExceptionControlRuntime(
   EIROpcode opcode,
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI)
{
   if (!abi.isValid())
      return LowerError::InvalidABI;

   if (runtime.objectLayout.fieldSize != architectureWordSize(abi.architecture)
      || runtime.dataLayout.threadContent.size == 0
      || runtime.dataLayout.threadContent.size > 0x7F)
   {
      return LowerError::InvalidRuntime;
   }

   bool waitsForGC = opcode == EIROpcode::ThreadExclude
      && runtime.threadingMode == ThreadingMode::MultiThread;
   if (!waitsForGC)
      return LowerError::None;

   RuntimeCallSpec waitForGC = {};

   if (runtimeABI.operation != RuntimeOperation::WaitForGC
      || !RuntimeCallProvider::get(RuntimeOperation::WaitForGC, runtime, waitForGC)
      || !runtimeABI.isValid(abi, waitForGC))
   {
      return LowerError::InvalidRuntime;
   }

   return LowerError::None;
}

static void lowerThrow(const RuntimeSpec& runtime, const ManagedABI& abi, Sequence& sequence)
{
   Operand threadContent = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand exception = operand(Register::DI, abi.wordSize, ValueKind::Address);
   Operand catchAddress = operand(Register::A, abi.wordSize, ValueKind::Address);

   loadThreadContent(sequence, runtime, threadContent);

   sequence.add(load(exception, threadContent, runtime.dataLayout.threadContent.currentException));
   sequence.add(load(
      catchAddress,
      exception,
      RuntimeLayout::offsetOf(runtime.objectLayout.fieldSize, ExceptionStructField::CatchAddress)));

   sequence.add(unary(Opcode::JumpRegister, catchAddress, MIREffect::None));
}

static void lowerUnhook(const RuntimeSpec& runtime, const ManagedABI& abi, Sequence& sequence)
{
   Operand currentThread = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand savedThread = operand(Register::C, abi.wordSize, ValueKind::Address);
   Operand exception = operand(Register::DI, abi.wordSize, ValueKind::Address);
   Operand previousException = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand catchFrame = operand(Register::BP, abi.wordSize, ValueKind::Address);
   Operand catchStack = operand(Register::SP, abi.wordSize, ValueKind::Address);

   loadThreadContent(sequence, runtime, currentThread);
   sequence.add(binary(Opcode::Move, savedThread, currentThread, Condition::None, MIREffect::None));

   sequence.add(load(exception, savedThread, runtime.dataLayout.threadContent.currentException));
   sequence.add(load(
      previousException,
      exception,
      RuntimeLayout::offsetOf(runtime.objectLayout.fieldSize, ExceptionStructField::Previous)));
   sequence.add(load(
      catchFrame,
      exception,
      RuntimeLayout::offsetOf(runtime.objectLayout.fieldSize, ExceptionStructField::CatchFrame)));
   sequence.add(load(
      catchStack,
      exception,
      RuntimeLayout::offsetOf(runtime.objectLayout.fieldSize, ExceptionStructField::CatchLevel)));

   sequence.add(store(savedThread, previousException, runtime.dataLayout.threadContent.currentException));
}

static void publishStackFrame(const RuntimeSpec& runtime, const ManagedABI& abi, Sequence& sequence)
{
   Operand currentThread = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand savedThread = operand(Register::DI, abi.wordSize, ValueKind::Address);
   Operand previousFrame = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand frame = operand(Register::BP, abi.wordSize, ValueKind::Address);
   Operand stack = operand(Register::SP, abi.wordSize, ValueKind::Address);

   loadThreadContent(sequence, runtime, currentThread);
   sequence.add(binary(Opcode::Move, savedThread, currentThread, Condition::None, MIREffect::None));

   sequence.add(load(previousFrame, savedThread, runtime.dataLayout.threadContent.stackFrame));
   sequence.add(unary(Opcode::Push, previousFrame, MIREffect::WriteMemory));
   sequence.add(unary(Opcode::Push, frame, MIREffect::WriteMemory));

   sequence.add(store(savedThread, stack, runtime.dataLayout.threadContent.stackFrame));
}

static void enterMutatorSafeRegion(
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI,
   Sequence& sequence)
{
   Operand threadContent = operand(Register::DI, abi.wordSize, ValueKind::Address);
   Operand safeState = operand(Register::C, OperandSize::DWord, ValueKind::Integer);
   Operand previousState = operand(Register::A, OperandSize::DWord, ValueKind::Integer);
   Operand signalAddress = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand signalValue = operand(Register::D, abi.wordSize, ValueKind::Integer);

   sequence.add(immediate(Opcode::MoveImmediate, safeState, 1, MIREffect::None));
   sequence.add(immediate(Opcode::MoveImmediate, previousState, 0, MIREffect::None));

   sequence.add(atomicDWord(
      Opcode::AtomicCompareExchangeDWord,
      threadContent,
      safeState,
      runtime.dataLayout.threadContent.flags));

   sequence.add(moveRuntimeData(signalAddress, RuntimeDataReference::GCDataSignal));
   sequence.add(load(signalValue, signalAddress, 0));
   sequence.add(binary(Opcode::Test, signalValue, signalValue, Condition::None, MIREffect::WriteFlags));

   sequence.add(local(Opcode::JumpZero, 0, MIREffect::ReadFlags));
   sequence.add(callRuntime(RuntimeOperation::WaitForGC, signalValue, runtimeABI));
   sequence.add(local(Opcode::Label, 0, MIREffect::None));
}

static void lowerExclude(
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI,
   Sequence& sequence)
{
   publishStackFrame(runtime, abi, sequence);

   if (runtime.threadingMode == ThreadingMode::MultiThread)
      enterMutatorSafeRegion(runtime, abi, runtimeABI, sequence);
}

static void restoreStackFrame(
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   Operand threadContent,
   Sequence& sequence)
{
   Operand previousFrame = operand(Register::A, abi.wordSize, ValueKind::Address);

   sequence.add(unary(Opcode::Pop, previousFrame, MIREffect::ReadMemory));
   sequence.add(store(threadContent, previousFrame, runtime.dataLayout.threadContent.stackFrame));
}

static void lowerSingleThreadInclude(const RuntimeSpec& runtime, const ManagedABI& abi, Sequence& sequence)
{
   Operand currentThread = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand savedThread = operand(Register::D, abi.wordSize, ValueKind::Address);

   loadThreadContent(sequence, runtime, currentThread);
   sequence.add(binary(Opcode::Move, savedThread, currentThread, Condition::None, MIREffect::None));

   restoreStackFrame(runtime, abi, savedThread, sequence);
}

static void acquireGCLock(Operand lockAddress, Sequence& sequence)
{
   Operand lockedState = operand(Register::C, OperandSize::DWord, ValueKind::Integer);
   Operand previousState = operand(Register::A, OperandSize::DWord, ValueKind::Integer);

   sequence.add(local(Opcode::Label, 0, MIREffect::None));
   sequence.add(immediate(Opcode::MoveImmediate, lockedState, 1, MIREffect::None));
   sequence.add(immediate(Opcode::MoveImmediate, previousState, 0, MIREffect::None));
   sequence.add(atomicDWord(Opcode::AtomicCompareExchangeDWord, lockAddress, lockedState));
   sequence.add(local(Opcode::JumpNotEqual, 0, MIREffect::ReadFlags));
}

static void releaseGCLock(Operand lockAddress, Sequence& sequence)
{
   Operand decrement = operand(Register::C, OperandSize::DWord, ValueKind::Integer);

   sequence.add(immediate(Opcode::MoveImmediate, decrement, -1, MIREffect::None));
   sequence.add(atomicDWord(Opcode::AtomicExchangeAddDWord, lockAddress, decrement));
}

static void lowerMultiThreadInclude(const RuntimeSpec& runtime, const ManagedABI& abi, Sequence& sequence)
{
   Operand lockAddress = operand(Register::DI, abi.wordSize, ValueKind::Address);
   Operand currentThread = operand(Register::A, abi.wordSize, ValueKind::Address);
   Operand savedThread = operand(Register::D, abi.wordSize, ValueKind::Address);
   Operand runningState = operand(Register::C, OperandSize::DWord, ValueKind::Integer);

   sequence.add(moveRuntimeData(lockAddress, RuntimeDataReference::GCDataLock));
   acquireGCLock(lockAddress, sequence);

   loadThreadContent(sequence, runtime, currentThread);
   sequence.add(binary(Opcode::Move, savedThread, currentThread, Condition::None, MIREffect::None));

   sequence.add(immediate(Opcode::MoveImmediate, runningState, 0, MIREffect::None));
   sequence.add(store(
      savedThread,
      runningState,
      runtime.dataLayout.threadContent.flags,
      abi.architecture == Architecture::X86 ? Opcode::StoreOffset : Opcode::StoreDWordOffset));

   restoreStackFrame(runtime, abi, savedThread, sequence);
   releaseGCLock(lockAddress, sequence);
}

static void lowerInclude(const RuntimeSpec& runtime, const ManagedABI& abi, Sequence& sequence)
{
   Operand stack = operand(Register::SP, abi.wordSize, ValueKind::Integer);

   sequence.add(immediate(
      Opcode::AddImmediate,
      stack,
      runtime.objectLayout.fieldSize,
      MIREffect::WriteFlags));

   if (runtime.threadingMode == ThreadingMode::MultiThread)
      lowerMultiThreadInclude(runtime, abi, sequence);
   else
      lowerSingleThreadInclude(runtime, abi, sequence);
}

static LowerError lowerExceptionControl(
   EIROpcode opcode,
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI,
   Sequence& sequence)
{
   LowerError validationError = validateExceptionControlRuntime(opcode, runtime, abi, runtimeABI);
   if (validationError != LowerError::None)
      return validationError;

   sequence.clear();

   switch (opcode) {
      case EIROpcode::ExceptionRaise:
         lowerThrow(runtime, abi, sequence);
         break;
      case EIROpcode::ExceptionUnhook:
         lowerUnhook(runtime, abi, sequence);
         break;
      case EIROpcode::ThreadExclude:
         lowerExclude(runtime, abi, runtimeABI, sequence);
         break;
      case EIROpcode::ThreadInclude:
         lowerInclude(runtime, abi, sequence);
         break;
      default:
         return LowerError::UnsupportedOpcode;
   }

   const RuntimeCallABI* verifierABI = opcode == EIROpcode::ThreadExclude
      && runtime.threadingMode == ThreadingMode::MultiThread
      ? &runtimeABI
      : nullptr;

   return MIRVerifier::verify(sequence, abi, verifierABI) == MIRVerifyError::None
      ? LowerError::None
      : LowerError::InvalidMIR;
}

LowerError ECodeLowering :: lower(
   const ByteCommand& command,
   const RuntimeSpec& runtime,
   const ManagedABI& abi,
   const RuntimeCallABI& runtimeABI,
   Sequence& sequence)
{
   TargetPlatform platform = abi.architecture == Architecture::X86
      ? TargetPlatform::LinuxX86
      : TargetPlatform::LinuxAMD64;
   LoweringContext context = {
      .frameOffset = 0,
      .stackOffset = 0,
      .vmtSize = 0,
      .platform = platform
   };

   return lower(command, runtime, abi, runtimeABI, context, sequence);
}
