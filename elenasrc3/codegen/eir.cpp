#include "eir.h"

using namespace elena_lang;
using namespace elena_lang::codegen;

bool EIRValue :: isValid() const
{
   return id != INVALID_POS && type != EIRType::None && type != EIRType::Void;
}

static EIRValue eirValue(pos_t id, EIRType type)
{
   return {
      .id = id,
      .type = type
   };
}

static EIRValue noEIRValue()
{
   return eirValue(INVALID_POS, EIRType::None);
}

void EIRFunction :: clear()
{
   _operands.clear();
   _instructions.clear();
   _blocks.clear();
}

pos_t EIRFunction :: addOperand(EIROperand operand)
{
   pos_t index = _operands.count_pos();
   _operands.add(operand);

   return index;
}

pos_t EIRFunction :: addOperand(EIROperandKind kind, EIRType type, pos64_t value)
{
   return addOperand({
      .kind = kind,
      .type = type,
      .value = value
   });
}

pos_t EIRFunction :: addInstruction(EIRInstruction instruction)
{
   pos_t index = _instructions.count_pos();
   _instructions.add(instruction);

   return index;
}

pos_t EIRFunction :: addInstruction(
   EIROpcode opcode,
   EIREffect effects,
   EIRValue result,
   pos_t firstOperand,
   pos_t operandCount,
   pos_t sourceOffset)
{
   return addInstruction({
      .opcode = opcode,
      .effects = effects,
      .result = result,
      .firstOperand = firstOperand,
      .operandCount = operandCount,
      .sourceOffset = sourceOffset
   });
}

pos_t EIRFunction :: addBlock(EIRBlock block)
{
   pos_t index = _blocks.count_pos();
   _blocks.add(block);

   return index;
}

pos_t EIRFunction :: addBlock(pos_t id, pos_t firstInstruction, pos_t instructionCount, pos_t sourceOffset)
{
   return addBlock({
      .id = id,
      .firstInstruction = firstInstruction,
      .instructionCount = instructionCount,
      .sourceOffset = sourceOffset
   });
}

pos_t EIRFunction :: operandCount() const
{
   return _operands.count_pos();
}

pos_t EIRFunction :: instructionCount() const
{
   return _instructions.count_pos();
}

pos_t EIRFunction :: blockCount() const
{
   return _blocks.count_pos();
}

EIROperand& EIRFunction :: operand(pos_t index)
{
   return _operands.get(index);
}

EIRInstruction& EIRFunction :: instruction(pos_t index)
{
   return _instructions.get(index);
}

EIRBlock& EIRFunction :: block(pos_t index)
{
   return _blocks.get(index);
}

EIRInstruction* EIRVerifier :: findDefinition(EIRFunction& function, pos_t valueId)
{
   for (pos_t i = 0; i < function.instructionCount(); i++) {
      EIRInstruction& instruction = function.instruction(i);
      if (instruction.result.isValid() && instruction.result.id == valueId)
         return &instruction;
   }

   return nullptr;
}

EIRVerifyError EIRVerifier :: verifyInstruction(EIRFunction& function,
   EIRInstruction& instruction)
{
   if (instruction.firstOperand > function.operandCount()
      || instruction.operandCount > function.operandCount() - instruction.firstOperand)
   {
      return EIRVerifyError::InvalidOperandRange;
   }

   for (pos_t i = 0; i < instruction.operandCount; i++) {
      EIROperand& operand = function.operand(instruction.firstOperand + i);
      switch (operand.kind) {
         case EIROperandKind::Value:
         {
            EIRInstruction* definition = findDefinition(function, (pos_t)operand.value);
            if (!definition)
               return EIRVerifyError::UndefinedValue;
            if (operand.type == EIRType::None || definition->result.type != operand.type)
               return EIRVerifyError::ValueTypeMismatch;

            break;
         }
         case EIROperandKind::Block:
            if (operand.type != EIRType::None || operand.value >= function.blockCount())
               return EIRVerifyError::InvalidBlockTarget;
            break;
         case EIROperandKind::Immediate:
         case EIROperandKind::Reference:
            if (operand.type == EIRType::None || operand.type == EIRType::Void)
               return EIRVerifyError::InvalidOperand;
            break;
         case EIROperandKind::Location:
            if (operand.type == EIRType::None
               || operand.type == EIRType::Void
               || operand.value >= (unsigned int)EIRLocation::Count)
            {
               return EIRVerifyError::InvalidOperand;
            }
            break;
         default:
            return EIRVerifyError::InvalidOperand;
      }
   }

   if (instruction.opcode == EIROpcode::Dispatch) {
      if (instruction.operandCount == 0)
         return EIRVerifyError::InvalidDispatch;

      EIROperand& operand = function.operand(instruction.firstOperand);
      if (operand.kind != EIROperandKind::Immediate
         || operand.type != EIRType::UInt32
         || operand.value >= (unsigned int)DispatchPhase::Count)
      {
         return EIRVerifyError::InvalidDispatch;
      }

      DispatchPhase phase = (DispatchPhase)operand.value;
      DispatchBlockProperty properties
         = DispatchProvider::getBlockProperties(phase);
      if (test(properties, DispatchBlockProperty::Conditional)) {
         if (instruction.operandCount != 1
            || instruction.result.type != EIRType::Boolean)
            return EIRVerifyError::InvalidDispatch;
      }
      else if (phase == DispatchPhase::ResolveTarget) {
         if (instruction.operandCount != 1
            || (instruction.result.type != EIRType::Pointer
               && instruction.result.type != EIRType::Word))
         {
            return EIRVerifyError::InvalidDispatch;
         }
      }
      else if (phase == DispatchPhase::LoadReceiverVMT) {
         if (instruction.operandCount != 1
            || instruction.result.type != EIRType::VMT)
         {
            return EIRVerifyError::InvalidDispatch;
         }
      }
      else if (phase == DispatchPhase::SelectAlternativeVMT) {
         if (instruction.operandCount != 2
            || instruction.result.type != EIRType::VMT
            || function.operand(instruction.firstOperand + 1).kind
               != EIROperandKind::Value
            || function.operand(instruction.firstOperand + 1).type
               != EIRType::VMT)
         {
            return EIRVerifyError::InvalidDispatch;
         }
      }
      else if (phase == DispatchPhase::ResolveVirtualTarget) {
         if (instruction.operandCount != 3
            || instruction.result.type != EIRType::Pointer
            || function.operand(instruction.firstOperand + 1).kind
               != EIROperandKind::Value
            || function.operand(instruction.firstOperand + 1).type
               != EIRType::VMT
            || function.operand(instruction.firstOperand + 2).kind
               != EIROperandKind::Value
            || function.operand(instruction.firstOperand + 2).type
               != EIRType::Word)
         {
            return EIRVerifyError::InvalidDispatch;
         }
      }
      else if (instruction.operandCount != 1
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None)
      {
         return EIRVerifyError::InvalidDispatch;
      }
   }

   if (instruction.opcode == EIROpcode::Constant) {
      if (instruction.operandCount != 1
         || !instruction.result.isValid()
         || function.operand(instruction.firstOperand).kind != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand).type != instruction.result.type
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidOperand;
      }
   }
   else if (instruction.opcode == EIROpcode::Copy
      && instruction.operandCount != 0
      && function.operand(instruction.firstOperand).kind == EIROperandKind::Location)
   {
      if (instruction.operandCount != 2)
         return EIRVerifyError::InvalidOperand;

      EIROperand& destination = function.operand(instruction.firstOperand);
      EIROperand& source = function.operand(instruction.firstOperand + 1);

      if (instruction.result.isValid()
         || destination.type != source.type
         || (source.kind != EIROperandKind::Immediate
            && source.kind != EIROperandKind::Reference
            && source.kind != EIROperandKind::Location)
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidOperand;
      }
   }
   else if (((instruction.opcode == EIROpcode::Add
         && instruction.operandCount == 2)
      || instruction.opcode == EIROpcode::Subtract
      || instruction.opcode == EIROpcode::Multiply
      || instruction.opcode == EIROpcode::BitAnd
      || instruction.opcode == EIROpcode::BitOr
      || (instruction.opcode == EIROpcode::Compare
         && instruction.operandCount == 2))
      && instruction.operandCount != 0
      && function.operand(instruction.firstOperand).kind == EIROperandKind::Location)
   {
      if (instruction.operandCount != 2)
         return EIRVerifyError::InvalidOperand;

      EIROperand& destination = function.operand(instruction.firstOperand);
      EIROperand& source = function.operand(instruction.firstOperand + 1);

      if (instruction.result.isValid()
         || source.kind != EIROperandKind::Immediate
         || destination.type != source.type
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidOperand;
      }
   }
   else if ((instruction.opcode == EIROpcode::Add
      || instruction.opcode == EIROpcode::Compare)
      && instruction.operandCount == 3
      && function.operand(instruction.firstOperand).kind == EIROperandKind::Location)
   {
      EIROperand& value = function.operand(instruction.firstOperand);
      EIROperand& address = function.operand(instruction.firstOperand + 1);
      EIROperand& displacement = function.operand(instruction.firstOperand + 2);

      if (instruction.result.isValid()
         || value.type != EIRType::Int32
         || address.kind != EIROperandKind::Location
         || (address.type != EIRType::Pointer && address.type != EIRType::Reference)
         || displacement.kind != EIROperandKind::Immediate
         || displacement.type != EIRType::Int32
         || (address.type == EIRType::Pointer
            ? instruction.effects != EIREffect::ReadFrame
            : instruction.effects != EIREffect::ReadHeap))
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if ((instruction.opcode == EIROpcode::ShiftLeft
      || instruction.opcode == EIROpcode::ShiftRightUnsigned)
      && instruction.operandCount != 0
      && function.operand(instruction.firstOperand).kind == EIROperandKind::Location)
   {
      if (instruction.operandCount != 2)
         return EIRVerifyError::InvalidOperand;

      EIROperand& count = function.operand(instruction.firstOperand + 1);

      if (instruction.result.isValid()
         || count.kind != EIROperandKind::Immediate
         || count.type != EIRType::UInt8
         || count.value > 31
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidOperand;
      }
   }
   else if ((instruction.opcode == EIROpcode::Load
      || instruction.opcode == EIROpcode::LoadSignExtend
      || instruction.opcode == EIROpcode::LoadZeroExtend)
      && instruction.operandCount != 0
      && function.operand(instruction.firstOperand).kind == EIROperandKind::Location)
   {
      if (instruction.operandCount < 2 || instruction.operandCount > 4)
         return EIRVerifyError::InvalidMemory;

      EIROperand& destination = function.operand(instruction.firstOperand);
      EIROperand& source = function.operand(instruction.firstOperand + 1);
      bool staticReference = instruction.operandCount == 2
         && instruction.opcode == EIROpcode::Load
         && destination.type == EIRType::Reference
         && source.kind == EIROperandKind::Reference
         && source.type == EIRType::Pointer
         && instruction.effects == EIREffect::ReadGlobal;
      bool locationLoad = false;
      bool extendedLoad = false;

      if (instruction.operandCount == 3) {
         EIROperand& displacement = function.operand(instruction.firstOperand + 2);
         locationLoad = source.kind == EIROperandKind::Location
            && (source.type == EIRType::Pointer || source.type == EIRType::Reference)
            && displacement.kind == EIROperandKind::Immediate
            && displacement.type == EIRType::Int32
            && (instruction.opcode != EIROpcode::LoadSignExtend
               || destination.type == EIRType::Word)
            && (source.type == EIRType::Pointer
               ? instruction.effects == EIREffect::ReadFrame
               : instruction.effects == EIREffect::ReadHeap);
      }

      if (instruction.operandCount == 4) {
         EIROperand& displacement = function.operand(instruction.firstOperand + 2);
         EIROperand& width = function.operand(instruction.firstOperand + 3);
         extendedLoad = source.kind == EIROperandKind::Location
            && source.type == EIRType::Reference
            && displacement.kind == EIROperandKind::Immediate
            && displacement.type == EIRType::Int32
            && width.kind == EIROperandKind::Immediate
            && width.type == EIRType::UInt8
            && (width.value == 1 || width.value == 2 || width.value == 4)
            && instruction.effects == EIREffect::ReadHeap;
      }

      if (instruction.result.isValid()
         || (!staticReference && !locationLoad && !extendedLoad))
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::Store
      && instruction.operandCount != 0)
   {
      if (instruction.operandCount < 2 || instruction.operandCount > 3)
         return EIRVerifyError::InvalidMemory;

      EIROperand& destination = function.operand(instruction.firstOperand);
      EIROperand& source = function.operand(instruction.firstOperand + 1);
      bool objectImmediate = instruction.operandCount == 2
         && destination.kind == EIROperandKind::Location
         && destination.type == EIRType::Reference
         && source.kind == EIROperandKind::Immediate
         && source.type == EIRType::Int32
         && instruction.effects == EIREffect::WriteHeap;
      bool staticReference = instruction.operandCount == 2
         && destination.kind == EIROperandKind::Reference
         && destination.type == EIRType::Pointer
         && source.kind == EIROperandKind::Location
         && source.type == EIRType::Reference
         && instruction.effects == EIREffect::WriteGlobal;
      bool locationStore = false;

      if (instruction.operandCount == 3) {
         EIROperand& displacement = function.operand(instruction.firstOperand + 2);
         bool frameStore = destination.type == EIRType::Pointer
            && instruction.effects == EIREffect::WriteFrame;
         bool heapStore = destination.type == EIRType::Reference
            && instruction.effects == EIREffect::WriteHeap;
         locationStore = destination.kind == EIROperandKind::Location
            && (frameStore || heapStore)
            && source.kind == EIROperandKind::Location
            && displacement.kind == EIROperandKind::Immediate
            && displacement.type == EIRType::Int32;
      }

      if (instruction.result.isValid()
         || (!objectImmediate && !staticReference && !locationStore))
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if ((instruction.opcode == EIROpcode::FrameAddress
      || instruction.opcode == EIROpcode::StackAddress)
      && instruction.operandCount != 0)
   {
      if (instruction.operandCount != 3)
         return EIRVerifyError::InvalidMemory;

      EIROperand& destination = function.operand(instruction.firstOperand);
      EIROperand& address = function.operand(instruction.firstOperand + 1);
      EIROperand& displacement = function.operand(instruction.firstOperand + 2);
      EIRLocation expectedAddress = instruction.opcode == EIROpcode::FrameAddress
         ? EIRLocation::FramePointer
         : EIRLocation::StackPointer;

      if (instruction.result.isValid()
         || destination.kind != EIROperandKind::Location
         || destination.type != EIRType::Reference
         || destination.value != (pos64_t)EIRLocation::ManagedObject
         || address.kind != EIROperandKind::Location
         || address.type != EIRType::Pointer
         || address.value != (pos64_t)expectedAddress
         || displacement.kind != EIROperandKind::Immediate
         || displacement.type != EIRType::Int32
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::IndexedFrameAddress) {
      if (instruction.operandCount != 4)
         return EIRVerifyError::InvalidMemory;

      EIROperand& destination = function.operand(instruction.firstOperand);
      EIROperand& frame = function.operand(instruction.firstOperand + 1);
      EIROperand& index = function.operand(instruction.firstOperand + 2);
      EIROperand& displacement = function.operand(instruction.firstOperand + 3);

      if (instruction.result.isValid()
         || destination.kind != EIROperandKind::Location
         || destination.type != EIRType::Reference
         || destination.value != (pos64_t)EIRLocation::ManagedObject
         || frame.kind != EIROperandKind::Location
         || frame.type != EIRType::Pointer
         || frame.value != (pos64_t)EIRLocation::FramePointer
         || index.kind != EIROperandKind::Location
         || index.type != EIRType::Word
         || index.value != (pos64_t)EIRLocation::ManagedValue
         || displacement.kind != EIROperandKind::Immediate
         || displacement.type != EIRType::Int32
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::MemoryFill) {
      if (instruction.operandCount != 3)
         return EIRVerifyError::InvalidMemory;

      EIROperand& destination = function.operand(instruction.firstOperand);
      EIROperand& count = function.operand(instruction.firstOperand + 1);
      EIROperand& fill = function.operand(instruction.firstOperand + 2);

      if (instruction.result.isValid()
         || destination.kind != EIROperandKind::Location
         || destination.type != EIRType::Reference
         || count.kind != EIROperandKind::Immediate
         || count.type != EIRType::UInt32
         || (fill.kind != EIROperandKind::Immediate
            && fill.kind != EIROperandKind::Reference)
         || fill.type != EIRType::Reference
         || instruction.effects != EIREffect::WriteHeap)
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::ObjectSize) {
      if (instruction.operandCount != 5)
         return EIRVerifyError::InvalidMemory;

      EIROperand& destination = function.operand(instruction.firstOperand);
      EIROperand& object = function.operand(instruction.firstOperand + 1);
      EIROperand& divisor = function.operand(instruction.firstOperand + 2);
      EIROperand& mask = function.operand(instruction.firstOperand + 3);
      EIROperand& offset = function.operand(instruction.firstOperand + 4);

      if (instruction.result.isValid()
         || destination.kind != EIROperandKind::Location
         || destination.type != EIRType::UInt32
         || object.kind != EIROperandKind::Location
         || object.type != EIRType::Reference
         || divisor.kind != EIROperandKind::Immediate
         || divisor.type != EIRType::UInt32
         || divisor.value == 0
         || mask.kind != EIROperandKind::Immediate
         || mask.type != EIRType::UInt32
         || offset.kind != EIROperandKind::Immediate
         || offset.type != EIRType::Int32
         || instruction.effects != EIREffect::ReadHeap)
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::ObjectVMT) {
      if (instruction.operandCount != 0
         || instruction.result.type != EIRType::VMT
         || instruction.effects != EIREffect::ReadHeap)
      {
         return EIRVerifyError::InvalidVirtualMethod;
      }
   }
   else if (instruction.opcode == EIROpcode::SelectAlternativeVMT) {
      if (instruction.operandCount != 1
         || instruction.result.type != EIRType::VMT
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Value
         || function.operand(instruction.firstOperand).type != EIRType::VMT
         || instruction.effects != EIREffect::ReadHeap)
      {
         return EIRVerifyError::InvalidVirtualMethod;
      }
   }
   else if (instruction.opcode == EIROpcode::MethodOffset) {
      if (instruction.operandCount != 3
         || instruction.result.type != EIRType::Word
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Reference
         || function.operand(instruction.firstOperand).type != EIRType::VMT
         || function.operand(instruction.firstOperand + 1).kind
            != EIROperandKind::Reference
         || function.operand(instruction.firstOperand + 1).type
            != EIRType::Message
         || function.operand(instruction.firstOperand + 2).kind
            != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand + 2).type
            != EIRType::UInt32
         || function.operand(instruction.firstOperand + 2).value
            > (unsigned int)MethodLookupOption::AlternativeVMT
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidVirtualMethod;
      }
   }
   else if (instruction.opcode == EIROpcode::ResolveVirtualMethod) {
      if (instruction.operandCount != 2
         || instruction.result.type != EIRType::Pointer
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Value
         || function.operand(instruction.firstOperand).type != EIRType::VMT
         || function.operand(instruction.firstOperand + 1).kind
            != EIROperandKind::Value
         || function.operand(instruction.firstOperand + 1).type != EIRType::Word
         || instruction.effects != EIREffect::ReadHeap)
      {
         return EIRVerifyError::InvalidVirtualMethod;
      }
   }
   else if (instruction.opcode == EIROpcode::ResolveVirtualIndex) {
      if (instruction.operandCount != 2
         || instruction.result.type != EIRType::Pointer
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Value
         || function.operand(instruction.firstOperand).type != EIRType::VMT
         || function.operand(instruction.firstOperand + 1).kind
            != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand + 1).type
            != EIRType::UInt32
         || instruction.effects != EIREffect::ReadHeap)
      {
         return EIRVerifyError::InvalidManagedMethod;
      }
   }
   else if (instruction.opcode == EIROpcode::MethodAddress) {
      if (instruction.operandCount != 3
         || instruction.result.type != EIRType::Pointer
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Reference
         || function.operand(instruction.firstOperand).type != EIRType::VMT
         || function.operand(instruction.firstOperand + 1).kind
            != EIROperandKind::Reference
         || function.operand(instruction.firstOperand + 1).type
            != EIRType::Message
         || function.operand(instruction.firstOperand + 2).kind
            != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand + 2).type
            != EIRType::UInt32
         || function.operand(instruction.firstOperand + 2).value
            > (unsigned int)MethodLookupOption::AlternativeVMT
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidManagedMethod;
      }
   }
   else if (instruction.opcode == EIROpcode::CallDirect) {
      EIREffect effects = EIREffect::ReadHeap | EIREffect::WriteHeap
         | EIREffect::Call | EIREffect::Safepoint | EIREffect::Throw;
      if (instruction.operandCount != 1
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Reference
         || function.operand(instruction.firstOperand).type != EIRType::Pointer
         || instruction.effects != effects)
      {
         return EIRVerifyError::InvalidManagedMethod;
      }
   }
   else if (instruction.opcode == EIROpcode::CallIndirect) {
      EIREffect effects = EIREffect::ReadHeap | EIREffect::WriteHeap
         | EIREffect::Call | EIREffect::Safepoint | EIREffect::Throw;
      if (instruction.operandCount != 1
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || (function.operand(instruction.firstOperand).kind
               != EIROperandKind::Value
            && function.operand(instruction.firstOperand).kind
               != EIROperandKind::Location)
         || function.operand(instruction.firstOperand).type != EIRType::Pointer
         || instruction.effects != effects)
      {
         return EIRVerifyError::InvalidVirtualMethod;
      }
   }
   else if (instruction.opcode == EIROpcode::ExceptionHook) {
      EIREffect effects = EIREffect::ReadFrame | EIREffect::WriteFrame
         | EIREffect::ReadGlobal | EIREffect::WriteGlobal;

      if (instruction.operandCount != 3
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand).type != EIRType::Int32
         || function.operand(instruction.firstOperand + 1).kind
            != EIROperandKind::Reference
         || function.operand(instruction.firstOperand + 1).type
            != EIRType::Pointer
         || function.operand(instruction.firstOperand + 2).kind
            != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand + 2).type != EIRType::UInt8
         || function.operand(instruction.firstOperand + 2).value
            > (unsigned int)ExceptionTargetKind::Reference
         || (instruction.effects != effects
            && instruction.effects != (effects | EIREffect::ReadTLS
               | EIREffect::WriteTLS)))
      {
         return EIRVerifyError::InvalidException;
      }
   }
   else if (instruction.opcode == EIROpcode::IsCurrentStackReference) {
      bool validEffects = instruction.effects == EIREffect::ReadGlobal
         || instruction.effects == EIREffect::ReadTLS;

      if (instruction.operandCount != 0
         || instruction.result.type != EIRType::Boolean
         || !validEffects)
      {
         return EIRVerifyError::InvalidStackReference;
      }
   }
   else if (instruction.opcode == EIROpcode::FrameOpen) {
      if (instruction.operandCount != 0
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || instruction.effects != EIREffect::WriteFrame)
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::FrameLink) {
      if (instruction.operandCount != 1
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || function.operand(instruction.firstOperand).kind != EIROperandKind::Value
         || function.operand(instruction.firstOperand).type != EIRType::Word
         || instruction.effects != EIREffect::WriteFrame)
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::FrameClear) {
      if (instruction.operandCount != 2
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || function.operand(instruction.firstOperand).kind != EIROperandKind::Value
         || function.operand(instruction.firstOperand).type != EIRType::Word
         || function.operand(instruction.firstOperand + 1).kind != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand + 1).type != EIRType::UInt32
         || instruction.effects != EIREffect::WriteFrame)
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::FrameClose) {
      if (instruction.operandCount != 1
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand).type
            != EIRType::UInt32
         || instruction.effects != EIREffect::WriteFrame)
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::ExternalFrameOpen) {
      EIREffect effects = EIREffect::ReadFrame | EIREffect::WriteFrame
         | EIREffect::ReadGlobal | EIREffect::WriteGlobal;
      if (instruction.operandCount != 0 || instruction.result.isValid()
         || (instruction.effects != effects
            && instruction.effects != (effects | EIREffect::ReadTLS | EIREffect::WriteTLS)))
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::ExternalFrameClose) {
      EIREffect effects = EIREffect::ReadFrame | EIREffect::WriteFrame
         | EIREffect::ReadGlobal | EIREffect::WriteGlobal;
      if (instruction.operandCount != 1 || instruction.result.isValid()
         || function.operand(instruction.firstOperand).kind != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand).type != EIRType::UInt32
         || (instruction.effects != effects
            && instruction.effects != (effects | EIREffect::ReadTLS | EIREffect::WriteTLS)))
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::StackAllocate) {
      if (instruction.operandCount != 0
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || instruction.effects != EIREffect::WriteFrame)
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::StackReserve) {
      if (instruction.operandCount != 1
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || function.operand(instruction.firstOperand).kind != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand).type != EIRType::UInt32
         || instruction.effects != EIREffect::WriteFrame)
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::MemoryCopy) {
      if (instruction.operandCount != 1
         || instruction.result.id != INVALID_POS
         || instruction.result.type != EIRType::None
         || function.operand(instruction.firstOperand).kind
            != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand).type
            != EIRType::UInt32
         || instruction.effects != (EIREffect::ReadHeap | EIREffect::WriteHeap))
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::NoOperation) {
      if (instruction.operandCount != 0 || instruction.result.isValid()
         || instruction.effects != EIREffect::None)
      {
         return EIRVerifyError::InvalidInstructionRange;
      }
   }
   else if (instruction.opcode == EIROpcode::Collect) {
      EIREffect required = EIREffect::ReadHeap | EIREffect::WriteHeap | EIREffect::ReadGlobal
         | EIREffect::WriteGlobal | EIREffect::Call | EIREffect::Allocate
         | EIREffect::Safepoint | EIREffect::Throw;

      if (instruction.operandCount != 1 || instruction.result.isValid()
         || function.operand(instruction.firstOperand).kind != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand).type != EIRType::Boolean
         || (instruction.effects != required
            && instruction.effects != (required | EIREffect::ReadTLS | EIREffect::Synchronize)))
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::ThreadPublish) {
      if (instruction.operandCount != 0 || instruction.result.isValid()
         || instruction.effects != (EIREffect::ReadTLS | EIREffect::WriteTLS | EIREffect::WriteFrame))
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::ThreadLocalLoad
      || instruction.opcode == EIROpcode::ThreadLocalStore)
   {
      EIREffect expectedEffects = instruction.opcode
         == EIROpcode::ThreadLocalLoad
         ? EIREffect::ReadTLS
         : EIREffect::ReadTLS | EIREffect::WriteTLS;

      if (instruction.operandCount != 1
         || instruction.result.isValid()
         || instruction.effects != expectedEffects)
      {
         return EIRVerifyError::InvalidMemory;
      }

      EIROperand& reference = function.operand(instruction.firstOperand);
      if (reference.kind != EIROperandKind::Reference
         || reference.type != EIRType::Pointer
         || ((ref_t)reference.value & mskAnyRef) != mskTLSVariable)
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::SystemStartup) {
      EIREffect effects = EIREffect::ReadGlobal | EIREffect::WriteGlobal | EIREffect::Call;
      if (instruction.operandCount != 1 || instruction.result.isValid()
         || function.operand(instruction.firstOperand).kind != EIROperandKind::Immediate
         || function.operand(instruction.firstOperand).type != EIRType::Boolean
         || instruction.effects != effects)
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::SafeRegionEnter
      || instruction.opcode == EIROpcode::SafeRegionLeave)
   {
      EIREffect required = EIREffect::ReadGlobal | EIREffect::WriteGlobal | EIREffect::Synchronize;
      EIREffect multiThread = required | EIREffect::ReadTLS | EIREffect::WriteTLS
         | EIREffect::Call | EIREffect::Safepoint;

      if (instruction.operandCount != 0 || instruction.result.isValid()
         || (instruction.effects != EIREffect::None && instruction.effects != multiThread))
      {
         return EIRVerifyError::InvalidFrame;
      }
   }
   else if (instruction.opcode == EIROpcode::GCLockAcquire
      || instruction.opcode == EIROpcode::GCLockRelease)
   {
      EIREffect effects = EIREffect::ReadGlobal
         | EIREffect::WriteGlobal
         | EIREffect::Synchronize;

      if (instruction.operandCount != 0
         || instruction.result.isValid()
         || instruction.effects != effects)
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::ObjectLockTry
      || instruction.opcode == EIROpcode::ObjectLockRelease)
   {
      EIREffect effects = EIREffect::ReadHeap
         | EIREffect::WriteHeap
         | EIREffect::Synchronize;

      if (instruction.operandCount != 0
         || instruction.result.isValid()
         || (instruction.effects != EIREffect::None
            && instruction.effects != effects))
      {
         return EIRVerifyError::InvalidMemory;
      }
   }
   else if (instruction.opcode == EIROpcode::ExceptionRaise) {
      if (instruction.operandCount != 0 || instruction.result.isValid()
         || !test(instruction.effects, EIREffect::Throw | EIREffect::Terminator))
      {
         return EIRVerifyError::InvalidException;
      }
   }
   else if (instruction.opcode == EIROpcode::ExceptionUnhook
      || instruction.opcode == EIROpcode::ThreadExclude
      || instruction.opcode == EIROpcode::ThreadInclude)
   {
      if (instruction.operandCount != 0 || instruction.result.isValid())
         return EIRVerifyError::InvalidException;
   }

   return EIRVerifyError::None;
}

EIRVerifyError ExceptionEIRProvider :: lower(
   const ExceptionHookSpec& spec,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Immediate,
      EIRType::Int32,
      (pos64_t)(long long)spec.frameOffset);

   function.addOperand(
      EIROperandKind::Reference,
      EIRType::Pointer,
      spec.target);

   function.addOperand(
      EIROperandKind::Immediate,
      EIRType::UInt8,
      (pos64_t)spec.targetKind);

   EIREffect effects = EIREffect::ReadFrame | EIREffect::WriteFrame
      | EIREffect::ReadGlobal | EIREffect::WriteGlobal;

   if (spec.threadLocal)
      effects = effects | EIREffect::ReadTLS | EIREffect::WriteTLS;

   function.addInstruction(
      EIROpcode::ExceptionHook,
      effects,
      noEIRValue(),
      firstOperand,
      3,
      0);

   function.addInstruction(
      EIROpcode::Fallthrough,
      EIREffect::Terminator,
      noEIRValue(),
      function.operandCount(),
      0,
      0);

   function.addBlock(0, 0, 2, 0);

   return EIRVerifier::verify(function);
}

EIRVerifyError StackReferenceEIRProvider :: lower(
   const StackReferenceSpec& spec,
   EIRFunction& function)
{
   function.clear();

   EIREffect effects = spec.threadingMode == ThreadingMode::MultiThread
      ? EIREffect::ReadTLS
      : EIREffect::ReadGlobal;

   function.addInstruction(
      EIROpcode::IsCurrentStackReference,
      effects,
      eirValue(0, EIRType::Boolean),
      0,
      0,
      0);

   function.addInstruction(
      EIROpcode::Fallthrough,
      EIREffect::Terminator,
      noEIRValue(),
      0,
      0,
      0);

   function.addBlock(0, 0, 2, 0);

   return EIRVerifier::verify(function);
}

bool FrameEIRProvider :: layout(
   const FrameOpenSpec& spec, const TargetSpec& target, FrameOpenLayout& layout)
{
   if (!target.isValid())
      return false;

   unsigned int managedAlignment = target.managedABI.stackAlignment;
   unsigned int unmanagedAlignment = target.managedABI.rawStackAlignment;
   unsigned int managedMask = managedAlignment - 1;
   unsigned int unmanagedMask = unmanagedAlignment - 1;

   if (spec.managedSlots > 0xFFFFFFFFu - managedMask
      || spec.unmanagedSize > 0xFFFFFFFFu - unmanagedMask)
   {
      return false;
   }

   layout = {
      .managedSlots = (spec.managedSlots + managedMask) & ~managedMask,
      .unmanagedSize = (spec.unmanagedSize + unmanagedMask) & ~unmanagedMask
   };

   return true;
}

EIRVerifyError FrameEIRProvider :: lower(
   const FrameOpenSpec& spec,
   const TargetSpec& target,
   EIRFunction& function)
{
   FrameOpenLayout frame = {};
   if (!layout(spec, target, frame))
      return EIRVerifyError::InvalidFrame;

   function.clear();

   function.addInstruction(EIROpcode::FrameOpen, EIREffect::WriteFrame,
      noEIRValue(), function.operandCount(), 0, 0);

   bool initializesFrame = frame.unmanagedSize != 0 || frame.managedSlots != 0;
   if (initializesFrame) {
      pos_t zeroOperand = function.addOperand(EIROperandKind::Immediate, EIRType::Word, 0);
      function.addInstruction(EIROpcode::Constant, EIREffect::None,
         eirValue(0, EIRType::Word), zeroOperand, 1, 0);
   }

   if (frame.unmanagedSize != 0) {
      pos_t reserveOperand = function.addOperand(
         EIROperandKind::Immediate, EIRType::UInt32, frame.unmanagedSize);
      function.addInstruction(EIROpcode::StackReserve, EIREffect::WriteFrame,
         noEIRValue(), reserveOperand, 1, 0);

      pos_t linkOperand = function.addOperand(EIROperandKind::Value, EIRType::Word, 0);
      function.addInstruction(EIROpcode::FrameLink, EIREffect::WriteFrame,
         noEIRValue(), linkOperand, 1, 0);
   }

   if (frame.managedSlots != 0) {
      pos_t clearOperand = function.addOperand(EIROperandKind::Value, EIRType::Word, 0);
      function.addOperand(EIROperandKind::Immediate, EIRType::UInt32, frame.managedSlots);
      function.addInstruction(EIROpcode::FrameClear, EIREffect::WriteFrame,
         noEIRValue(), clearOperand, 2, 0);
   }

   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);

   function.addBlock(0, 0, function.instructionCount(), 0);

   return EIRVerifier::verify(function);
}

EIRVerifyError StackEIRProvider :: lowerRootAllocation(EIRFunction& function)
{
   function.clear();

   function.addInstruction(EIROpcode::StackAllocate, EIREffect::WriteFrame, noEIRValue(), 0, 0, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator, noEIRValue(), 0, 0, 0);

   function.addBlock(0, 0, 2, 0);

   return EIRVerifier::verify(function);
}

EIRVerifyError MemoryEIRProvider :: lower(const MemoryCopySpec& spec, EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(EIROperandKind::Immediate, EIRType::UInt32, spec.byteCount);

   function.addInstruction(EIROpcode::MemoryCopy, EIREffect::ReadHeap | EIREffect::WriteHeap,
      noEIRValue(), firstOperand, 1, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);

   function.addBlock(0, 0, 2, 0);

   return EIRVerifier::verify(function);
}

static ECodeEIRLowerError checkedEIR(EIRVerifyError error)
{
   return error == EIRVerifyError::None ? ECodeEIRLowerError::None : ECodeEIRLowerError::InvalidEIR;
}

static EIREffect runtimeEffects(RuntimeCallEffect effects)
{
   EIREffect result = EIREffect::None;

   if (test(effects, RuntimeCallEffect::ReadHeap))
      result = result | EIREffect::ReadHeap;
   if (test(effects, RuntimeCallEffect::WriteHeap))
      result = result | EIREffect::WriteHeap;
   if (test(effects, RuntimeCallEffect::ReadGlobal))
      result = result | EIREffect::ReadGlobal;
   if (test(effects, RuntimeCallEffect::WriteGlobal))
      result = result | EIREffect::WriteGlobal;
   if (test(effects, RuntimeCallEffect::Call))
      result = result | EIREffect::Call;
   if (test(effects, RuntimeCallEffect::Allocate))
      result = result | EIREffect::Allocate;
   if (test(effects, RuntimeCallEffect::Safepoint))
      result = result | EIREffect::Safepoint;
   if (test(effects, RuntimeCallEffect::MayThrow))
      result = result | EIREffect::Throw;
   if (test(effects, RuntimeCallEffect::Synchronize))
      result = result | EIREffect::Synchronize;
   if (test(effects, RuntimeCallEffect::ReadTLS))
      result = result | EIREffect::ReadTLS;

   return result;
}

static ECodeEIRLowerError singleOperation(EIROpcode opcode, EIREffect effects, EIRFunction& function)
{
   function.clear();

   function.addInstruction(opcode, effects, noEIRValue(), 0, 0, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError lowerManagedState(
   const ByteCommand& command,
   EIRFunction& function)
{
   EIROpcode opcode = EIROpcode::Undef;
   EIREffect effects = EIREffect::None;
   EIRLocation location = EIRLocation::ManagedValue;
   EIRType locationType = EIRType::None;
   EIROperandKind argumentKind = EIROperandKind::Immediate;
   EIRType argumentType = EIRType::None;
   pos64_t argument = (pos64_t)(long long)command.arg1;
   bool locationFirst = true;

   switch (command.code) {
      // Shl: shift the managed integer value left by an immediate count.
      case ByteCode::Shl:
         opcode = EIROpcode::ShiftLeft;
         locationType = EIRType::Int32;
         argumentType = EIRType::UInt8;
         argument = (unsigned int)command.arg1 & 0x1F;
         break;

      // Shr: shift the managed integer value right without sign extension.
      case ByteCode::Shr:
         opcode = EIROpcode::ShiftRightUnsigned;
         locationType = EIRType::Int32;
         argumentType = EIRType::UInt8;
         argument = (unsigned int)command.arg1 & 0x1F;
         break;

      // MovN: replace the managed integer value with an immediate value.
      case ByteCode::MovN:
         opcode = EIROpcode::Copy;
         locationType = EIRType::Int32;
         argumentType = EIRType::Int32;
         break;

      // AddN: add an immediate native-word value to the managed value.
      case ByteCode::AddN:
         opcode = EIROpcode::Add;
         locationType = EIRType::Word;
         argumentType = EIRType::Word;
         break;

      // SubN: subtract an immediate native-word value from the managed value.
      case ByteCode::SubN:
         opcode = EIROpcode::Subtract;
         locationType = EIRType::Word;
         argumentType = EIRType::Word;
         break;

      // AndN: mask the managed integer value with an immediate value.
      case ByteCode::AndN:
         opcode = EIROpcode::BitAnd;
         locationType = EIRType::Int32;
         argumentType = EIRType::Int32;
         break;

      // OrN: combine the managed integer value with an immediate bit mask.
      case ByteCode::OrN:
         opcode = EIROpcode::BitOr;
         locationType = EIRType::Int32;
         argumentType = EIRType::Int32;
         break;

      // MulN: multiply the managed integer value by an immediate value.
      case ByteCode::MulN:
         opcode = EIROpcode::Multiply;
         locationType = EIRType::Int32;
         argumentType = EIRType::Int32;
         break;

      // CmpN: compare the managed integer value with an immediate value.
      case ByteCode::CmpN:
         opcode = EIROpcode::Compare;
         locationType = EIRType::Int32;
         argumentType = EIRType::Int32;
         break;

      // MovM: load a symbolic message into the managed value location.
      case ByteCode::MovM:
         opcode = EIROpcode::Copy;
         locationType = EIRType::Message;
         argumentKind = EIROperandKind::Reference;
         argumentType = EIRType::Message;
         break;

      // SetR: load a symbolic or sentinel object reference.
      case ByteCode::SetR:
         opcode = EIROpcode::Copy;
         location = EIRLocation::ManagedObject;
         locationType = EIRType::Reference;
         argumentType = EIRType::Reference;
         argumentKind = command.arg1 == 0 || command.arg1 == -1
            ? EIROperandKind::Immediate
            : EIROperandKind::Reference;
         break;

      // XSaveN: store an immediate 32-bit value at the object payload start.
      case ByteCode::XSaveN:
         opcode = EIROpcode::Store;
         effects = EIREffect::WriteHeap;
         location = EIRLocation::ManagedObject;
         locationType = EIRType::Reference;
         argumentType = EIRType::Int32;
         break;

      // PeekR: load an object reference from a symbolic global slot.
      case ByteCode::PeekR:
         opcode = EIROpcode::Load;
         effects = EIREffect::ReadGlobal;
         location = EIRLocation::ManagedObject;
         locationType = EIRType::Reference;
         argumentKind = EIROperandKind::Reference;
         argumentType = EIRType::Pointer;
         break;

      // StoreR: store the managed object reference into a symbolic global slot.
      case ByteCode::StoreR:
         opcode = EIROpcode::Store;
         effects = EIREffect::WriteGlobal;
         location = EIRLocation::ManagedObject;
         locationType = EIRType::Reference;
         argumentKind = EIROperandKind::Reference;
         argumentType = EIRType::Pointer;
         locationFirst = false;
         break;

      default:
         return ECodeEIRLowerError::UnsupportedOpcode;
   }

   function.clear();

   pos_t firstOperand;
   if (locationFirst) {
      firstOperand = function.addOperand(
         EIROperandKind::Location, locationType, (pos64_t)location);
      function.addOperand(argumentKind, argumentType, argument);
   }
   else {
      firstOperand = function.addOperand(argumentKind, argumentType, argument);
      function.addOperand(EIROperandKind::Location, locationType, (pos64_t)location);
   }

   function.addInstruction(opcode, effects, noEIRValue(), firstOperand, 2, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError locationCopy(
   EIRLocation destination,
   EIRLocation source,
   EIRType type,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location, type, (pos64_t)destination);
   function.addOperand(EIROperandKind::Location, type, (pos64_t)source);

   function.addInstruction(
      EIROpcode::Copy, EIREffect::None, noEIRValue(), firstOperand, 2, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError locationStore(
   EIRLocation address,
   EIRLocation source,
   EIRType sourceType,
   int displacement,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location, EIRType::Pointer, (pos64_t)address);
   function.addOperand(EIROperandKind::Location, sourceType, (pos64_t)source);
   function.addOperand(
      EIROperandKind::Immediate,
      EIRType::Int32,
      (pos64_t)(long long)displacement);

   function.addInstruction(
      EIROpcode::Store, EIREffect::WriteFrame, noEIRValue(), firstOperand, 3, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError locationLoad(
   EIRLocation destination,
   EIRType destinationType,
   EIRLocation address,
   EIRType addressType,
   int displacement,
   EIRFunction& function,
   bool signExtend = false)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location, destinationType, (pos64_t)destination);
   function.addOperand(EIROperandKind::Location, addressType, (pos64_t)address);
   function.addOperand(
      EIROperandKind::Immediate,
      EIRType::Int32,
      (pos64_t)(long long)displacement);

   EIREffect effects = addressType == EIRType::Pointer
      ? EIREffect::ReadFrame
      : EIREffect::ReadHeap;
   function.addInstruction(
      signExtend ? EIROpcode::LoadSignExtend : EIROpcode::Load,
      effects,
      noEIRValue(),
      firstOperand,
      3,
      0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError locationAddress(
   EIROpcode opcode,
   EIRLocation address,
   int displacement,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location,
      EIRType::Reference,
      (pos64_t)EIRLocation::ManagedObject);
   function.addOperand(EIROperandKind::Location, EIRType::Pointer, (pos64_t)address);
   function.addOperand(
      EIROperandKind::Immediate,
      EIRType::Int32,
      (pos64_t)(long long)displacement);

   function.addInstruction(
      opcode, EIREffect::None, noEIRValue(), firstOperand, 3, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError locationCompare(
   EIRLocation value,
   EIRLocation address,
   int displacement,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location, EIRType::Int32, (pos64_t)value);
   function.addOperand(EIROperandKind::Location, EIRType::Pointer, (pos64_t)address);
   function.addOperand(
      EIROperandKind::Immediate,
      EIRType::Int32,
      (pos64_t)(long long)displacement);

   function.addInstruction(
      EIROpcode::Compare, EIREffect::ReadFrame, noEIRValue(), firstOperand, 3, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError locationBinaryMemory(
   EIROpcode opcode,
   EIRLocation value,
   EIRLocation address,
   int displacement,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location, EIRType::Int32, (pos64_t)value);
   function.addOperand(EIROperandKind::Location, EIRType::Pointer, (pos64_t)address);
   function.addOperand(
      EIROperandKind::Immediate,
      EIRType::Int32,
      (pos64_t)(long long)displacement);

   function.addInstruction(
      opcode, EIREffect::ReadFrame, noEIRValue(), firstOperand, 3, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError locationHeapStore(
   EIRLocation address,
   EIRLocation source,
   int displacement,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location, EIRType::Reference, (pos64_t)address);
   function.addOperand(
      EIROperandKind::Location, EIRType::Reference, (pos64_t)source);
   function.addOperand(
      EIROperandKind::Immediate,
      EIRType::Int32,
      (pos64_t)(long long)displacement);

   function.addInstruction(
      EIROpcode::Store, EIREffect::WriteHeap, noEIRValue(), firstOperand, 3, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError locationHeapValueStore(
   EIRLocation source,
   EIRType sourceType,
   int displacement,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location,
      EIRType::Reference,
      (pos64_t)EIRLocation::ManagedObject);
   function.addOperand(EIROperandKind::Location, sourceType, (pos64_t)source);
   function.addOperand(
      EIROperandKind::Immediate,
      EIRType::Int32,
      (pos64_t)(long long)displacement);

   function.addInstruction(
      EIROpcode::Store, EIREffect::WriteHeap, noEIRValue(), firstOperand, 3, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError extendedHeapLoad(
   EIROpcode opcode,
   EIRType destinationType,
   unsigned int width,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location,
      destinationType,
      (pos64_t)EIRLocation::ManagedValue);
   function.addOperand(
      EIROperandKind::Location,
      EIRType::Reference,
      (pos64_t)EIRLocation::ManagedObject);
   function.addOperand(EIROperandKind::Immediate, EIRType::Int32, 0);
   function.addOperand(EIROperandKind::Immediate, EIRType::UInt8, width);

   function.addInstruction(
      opcode, EIREffect::ReadHeap, noEIRValue(), firstOperand, 4, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError indexedHeapOperation(
   EIROpcode opcode,
   EIRLocation destination,
   EIRType valueType,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Location,
      valueType,
      (pos64_t)destination);
   function.addOperand(
      EIROperandKind::Location,
      EIRType::Reference,
      (pos64_t)EIRLocation::ManagedObject);
   function.addOperand(
      EIROperandKind::Location,
      EIRType::Word,
      (pos64_t)EIRLocation::ManagedValue);

   function.addInstruction(
      opcode,
      opcode == EIROpcode::StoreIndexed ? EIREffect::WriteHeap : EIREffect::ReadHeap,
      noEIRValue(),
      firstOperand,
      3,
      0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static bool cachedArgumentLocation(int index, const TargetSpec& target, EIRLocation& location)
{
   if (index < 0 || index >= target.managedABI.cachedArgumentCount)
      return false;

   if (index == 0) {
      location = EIRLocation::CachedArgument0;

      return true;
   }

   if (index == 1) {
      location = EIRLocation::CachedArgument1;

      return true;
   }

   return false;
}

static ECodeEIRLowerError finishLocationOperation(
   EIROpcode opcode,
   EIREffect effects,
   pos_t firstOperand,
   pos_t operandCount,
   bool terminal,
   EIRFunction& function)
{
   function.addInstruction(
      opcode,
      terminal ? effects | EIREffect::Terminator : effects,
      noEIRValue(),
      firstOperand,
      operandCount,
      0);

   if (!terminal) {
      function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
         noEIRValue(), function.operandCount(), 0, 0);
   }

   function.addBlock(0, 0, terminal ? 1 : 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError lowerManagedSlots(
   const ByteCommand& command,
   const RuntimeSpec& runtime,
   const TargetSpec& target,
   const ECodeLoweringContext& context,
   EIRFunction& function)
{
   switch (command.code) {
      // SaveDP: store the managed 32-bit value in a frame data slot.
      case ByteCode::SaveDP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationStore(
            EIRLocation::FramePointer,
            EIRLocation::ManagedValue,
            EIRType::Int32,
            resolved.arg1,
            function);
      }

      // StoreFI: store the managed object reference in a frame slot.
      case ByteCode::StoreFI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationStore(
            EIRLocation::FramePointer,
            EIRLocation::ManagedObject,
            EIRType::Reference,
            resolved.arg1,
            function);
      }

      // SaveSI: store the managed integer in a cached or physical stack slot.
      case ByteCode::SaveSI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         EIRLocation cached;
         if (cachedArgumentLocation(resolved.arg2, target, cached)) {
            return locationCopy(
               cached, EIRLocation::ManagedValue, EIRType::Word, function);
         }

         return locationStore(
            EIRLocation::StackPointer,
            EIRLocation::ManagedValue,
            target.pointerSize == 8 ? EIRType::UInt32 : EIRType::Word,
            resolved.arg1,
            function);
      }

      // StoreSI: store the managed object in a cached or physical stack slot.
      case ByteCode::StoreSI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         EIRLocation cached;
         if (cachedArgumentLocation(resolved.arg2, target, cached)) {
            return locationCopy(
               cached, EIRLocation::ManagedObject, EIRType::Reference, function);
         }

         return locationStore(
            EIRLocation::StackPointer,
            EIRLocation::ManagedObject,
            EIRType::Reference,
            resolved.arg1,
            function);
      }

      // XFlushSI: materialize a cached object argument in its physical stack slot.
      case ByteCode::XFlushSI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         EIRLocation cached;
         if (!cachedArgumentLocation(resolved.arg2, target, cached)) {
            return singleOperation(EIROpcode::NoOperation, EIREffect::None, function);
         }

         return locationStore(
            EIRLocation::StackPointer,
            cached,
            EIRType::Reference,
            resolved.arg1,
            function);
      }

      // XRefreshSI: reload a cached object argument from its physical stack slot.
      case ByteCode::XRefreshSI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         EIRLocation cached;
         if (!cachedArgumentLocation(resolved.arg2, target, cached)) {
            return singleOperation(EIROpcode::NoOperation, EIREffect::None, function);
         }

         return locationLoad(
            cached,
            EIRType::Reference,
            EIRLocation::StackPointer,
            EIRType::Pointer,
            resolved.arg1,
            function);
      }

      // PeekFI: load an object reference from a frame slot.
      case ByteCode::PeekFI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationLoad(
            EIRLocation::ManagedObject,
            EIRType::Reference,
            EIRLocation::FramePointer,
            EIRType::Pointer,
            resolved.arg1,
            function);
      }

      // PeekSI: load an object reference from a cached or physical stack slot.
      case ByteCode::PeekSI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         EIRLocation cached;
         if (cachedArgumentLocation(resolved.arg2, target, cached)) {
            return locationCopy(
               EIRLocation::ManagedObject, cached, EIRType::Reference, function);
         }

         return locationLoad(
            EIRLocation::ManagedObject,
            EIRType::Reference,
            EIRLocation::StackPointer,
            EIRType::Pointer,
            resolved.arg1,
            function);
      }

      // GetI: load an object reference from a field of the managed object.
      case ByteCode::GetI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationLoad(
            EIRLocation::ManagedObject,
            EIRType::Reference,
            EIRLocation::ManagedObject,
            EIRType::Reference,
            resolved.arg1,
            function);
      }

      // XStoreI: load the first cached argument from a managed object field.
      case ByteCode::XStoreI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationLoad(
            EIRLocation::CachedArgument0,
            EIRType::Reference,
            EIRLocation::ManagedObject,
            EIRType::Reference,
            resolved.arg1,
            function);
      }

      // LSaveDP: store the managed 64-bit integer in a frame data slot.
      case ByteCode::LSaveDP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None
            || (target.pointerSize == 4 && resolved.arg1 > 0x7FFFFFFB))
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationStore(
            EIRLocation::FramePointer,
            EIRLocation::ManagedValue,
            EIRType::Int64,
            resolved.arg1,
            function);
      }

      // LLoadDP: load a managed 64-bit integer from a frame data slot.
      case ByteCode::LLoadDP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None
            || (target.pointerSize == 4 && resolved.arg1 > 0x7FFFFFFB))
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationLoad(
            EIRLocation::ManagedValue,
            EIRType::Int64,
            EIRLocation::FramePointer,
            EIRType::Pointer,
            resolved.arg1,
            function);
      }

      // LLoadSI: load an unsigned 64-bit integer from a cached or stack slot.
      case ByteCode::LLoadSI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None
            || (target.pointerSize == 4 && resolved.arg1 > 0x7FFFFFFB))
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         EIRLocation cached;
         if (cachedArgumentLocation(resolved.arg2, target, cached)) {
            return locationCopy(
               EIRLocation::ManagedValue, cached, EIRType::UInt64, function);
         }

         return locationLoad(
            EIRLocation::ManagedValue,
            EIRType::UInt64,
            EIRLocation::StackPointer,
            EIRType::Pointer,
            resolved.arg1,
            function);
      }

      // LoadSI: load a signed native integer from a cached or stack slot.
      case ByteCode::LoadSI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         EIRLocation cached;
         if (cachedArgumentLocation(resolved.arg2, target, cached)) {
            return locationCopy(
               EIRLocation::ManagedValue, cached, EIRType::Word, function);
         }

         return locationLoad(
            EIRLocation::ManagedValue,
            EIRType::Word,
            EIRLocation::StackPointer,
            EIRType::Pointer,
            resolved.arg1,
            function,
            true);
      }

      // XLoadArgFI: load a native value from a frame argument slot.
      case ByteCode::XLoadArgFI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationLoad(
            EIRLocation::ManagedValue,
            EIRType::Word,
            EIRLocation::FramePointer,
            EIRType::Pointer,
            resolved.arg1,
            function);
      }

      // SetDP: form an object reference from a frame data displacement.
      case ByteCode::SetDP:
      // SetFP: form an object reference from a scaled frame slot.
      case ByteCode::SetFP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationAddress(
            EIROpcode::FrameAddress,
            EIRLocation::FramePointer,
            resolved.arg1,
            function);
      }

      // SetSP: form an object reference from a managed stack slot.
      case ByteCode::SetSP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationAddress(
            EIROpcode::StackAddress,
            EIRLocation::StackPointer,
            resolved.arg1,
            function);
      }

      // LoadDP: load a signed native integer from a frame data slot.
      case ByteCode::LoadDP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationLoad(
            EIRLocation::ManagedValue,
            EIRType::Word,
            EIRLocation::FramePointer,
            EIRType::Pointer,
            resolved.arg1,
            function,
            true);
      }

      // XCmpDP: compare the managed integer with a frame data slot.
      case ByteCode::XCmpDP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationCompare(
            EIRLocation::ManagedValue,
            EIRLocation::FramePointer,
            resolved.arg1,
            function);
      }

      // XAddDP: add a frame data value to the managed integer.
      case ByteCode::XAddDP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationBinaryMemory(
            EIROpcode::Add,
            EIRLocation::ManagedValue,
            EIRLocation::FramePointer,
            resolved.arg1,
            function);
      }

      // XSetFP: form a frame address indexed by the managed native value.
      case ByteCode::XSetFP:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Pointer,
            (pos64_t)EIRLocation::FramePointer);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Word,
            (pos64_t)EIRLocation::ManagedValue);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::Int32,
            (pos64_t)(long long)resolved.arg1);
         function.addInstruction(EIROpcode::IndexedFrameAddress, EIREffect::None,
            noEIRValue(), firstOperand, 4, 0);
         function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
            noEIRValue(), function.operandCount(), 0, 0);
         function.addBlock(0, 0, 2, 0);

         return checkedEIR(EIRVerifier::verify(function));
      }

      // XAssignI: store the first cached object argument into an object field.
      case ByteCode::XAssignI:
      {
         ByteCommand resolved;
         if (ECodeOperandResolver::resolve(command, runtime, context, resolved)
            != ECodeResolveError::None)
         {
            return ECodeEIRLowerError::InvalidArgument;
         }

         return locationHeapStore(
            EIRLocation::ManagedObject,
            EIRLocation::CachedArgument0,
            resolved.arg1,
            function);
      }

      // FillIR: fill object reference slots with a symbolic or nil reference.
      case ByteCode::FillIR:
      {
         if (command.arg1 < 0)
            return ECodeEIRLowerError::InvalidArgument;

         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::UInt32,
            (unsigned int)command.arg1);
         function.addOperand(
            command.arg2 == 0 ? EIROperandKind::Immediate : EIROperandKind::Reference,
            EIRType::Reference,
            (pos64_t)(ref_t)command.arg2);
         function.addInstruction(EIROpcode::MemoryFill, EIREffect::WriteHeap,
            noEIRValue(), firstOperand, 3, 0);
         function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
            noEIRValue(), function.operandCount(), 0, 0);
         function.addBlock(0, 0, 2, 0);

         return checkedEIR(EIRVerifier::verify(function));
      }

      // NLen: derive an element count from the encoded object payload size.
      case ByteCode::NLen:
      {
         if (command.arg1 <= 0)
            return ECodeEIRLowerError::InvalidArgument;

         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::UInt32,
            (pos64_t)EIRLocation::ManagedValue);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::UInt32,
            (unsigned int)command.arg1);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::UInt32,
            runtime.objectLayout.objectSizeMask & ~runtime.objectLayout.structMask);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::Int32,
            (pos64_t)(long long)-(int)runtime.objectLayout.sizeOffset);
         function.addInstruction(EIROpcode::ObjectSize, EIREffect::ReadHeap,
            noEIRValue(), firstOperand, 5, 0);
         function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
            noEIRValue(), function.operandCount(), 0, 0);
         function.addBlock(0, 0, 2, 0);

         return checkedEIR(EIRVerifier::verify(function));
      }

      default:
         return ECodeEIRLowerError::UnsupportedOpcode;
   }
}

static ECodeEIRLowerError lowerImplicitState(
   const ByteCommand& command,
   const RuntimeSpec& runtime,
   EIRFunction& function)
{
   EIREffect managedCall = EIREffect::ReadHeap | EIREffect::WriteHeap
      | EIREffect::Call | EIREffect::Safepoint | EIREffect::Throw;

   switch (command.code) {
      // SNop: preserve an explicit neutral instruction.
      case ByteCode::SNop:
         return singleOperation(EIROpcode::NoOperation, EIREffect::None, function);

      // Quit: return from the current managed procedure.
      case ByteCode::Quit:
         function.clear();
         return finishLocationOperation(
            EIROpcode::Return, EIREffect::None, 0, 0, true, function);

      // XJump: transfer to the address held by the managed object location.
      case ByteCode::XJump:
      // XCall: call the address held by the managed object location.
      case ByteCode::XCall:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Pointer,
            (pos64_t)EIRLocation::ManagedObject);

         return finishLocationOperation(
            command.code == ByteCode::XCall
               ? EIROpcode::CallIndirect
               : EIROpcode::IndirectBranch,
            command.code == ByteCode::XCall ? managedCall : EIREffect::None,
            firstOperand,
            1,
            command.code == ByteCode::XJump,
            function);
      }

      // XQuit: return the native managed value to the external caller.
      case ByteCode::XQuit:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Word,
            (pos64_t)EIRLocation::ManagedValue);

         return finishLocationOperation(
            EIROpcode::Return, EIREffect::None, firstOperand, 1, true, function);
      }

      // Not: invert every bit of the managed native value.
      case ByteCode::Not:
      // Neg: negate the managed native value.
      case ByteCode::Neg:
      // LNeg: negate the managed 64-bit value.
      case ByteCode::LNeg:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            command.code == ByteCode::LNeg ? EIRType::Int64 : EIRType::Word,
            (pos64_t)EIRLocation::ManagedValue);

         return finishLocationOperation(
            command.code == ByteCode::Not ? EIROpcode::BitNot : EIROpcode::Negate,
            EIREffect::None,
            firstOperand,
            1,
            false,
            function);
      }

      // ConvL: sign-extend the managed 32-bit value to 64 bits.
      case ByteCode::ConvL:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Int64,
            (pos64_t)EIRLocation::ManagedValue);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Int32,
            (pos64_t)EIRLocation::ManagedValue);

         return finishLocationOperation(
            EIROpcode::Convert, EIREffect::None, firstOperand, 2, false, function);
      }

      // Coalesce: select cached argument zero when the object is nil.
      case ByteCode::Coalesce:
      // XPeekEq: select cached argument zero when the current flags are equal.
      case ByteCode::XPeekEq:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::CachedArgument0);

         return finishLocationOperation(
            command.code == ByteCode::Coalesce
               ? EIROpcode::Coalesce
               : EIROpcode::SelectEqual,
            EIREffect::None,
            firstOperand,
            2,
            false,
            function);
      }

      // MovEnv: materialize the typed system-environment address.
      case ByteCode::MovEnv:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Pointer,
            (pos64_t)EIRLocation::ManagedValue);

         return finishLocationOperation(
            EIROpcode::SystemEnvironment,
            EIREffect::ReadGlobal,
            firstOperand,
            1,
            false,
            function);
      }

      // LoadV: combine argument bits with the action stored in the receiver.
      case ByteCode::LoadV:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Message,
            (pos64_t)EIRLocation::ManagedValue);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Immediate, EIRType::UInt32, ARG_MASK);

         return finishLocationOperation(
            EIROpcode::ComposeMessage,
            EIREffect::ReadHeap,
            firstOperand,
            3,
            false,
            function);
      }

      // XCmp: compare the managed value with the receiver payload.
      case ByteCode::XCmp:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Int32,
            (pos64_t)EIRLocation::ManagedValue);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(EIROperandKind::Immediate, EIRType::Int32, 0);

         return finishLocationOperation(
            EIROpcode::Compare,
            EIREffect::ReadHeap,
            firstOperand,
            3,
            false,
            function);
      }

      // Class: replace the object location with its VMT.
      case ByteCode::Class:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::VMT,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::Int32,
            (pos64_t)(long long)-(int)runtime.objectLayout.vmtOffset);

         return finishLocationOperation(
            EIROpcode::LoadObjectVMT,
            EIREffect::ReadHeap,
            firstOperand,
            3,
            false,
            function);
      }

      // Save: store the managed 32-bit value in the receiver payload.
      case ByteCode::Save:
         return locationHeapValueStore(
            EIRLocation::ManagedValue, EIRType::Int32, 0, function);

      // Load: load a signed native integer from the receiver payload.
      case ByteCode::Load:
         return locationLoad(
            EIRLocation::ManagedValue,
            EIRType::Word,
            EIRLocation::ManagedObject,
            EIRType::Reference,
            0,
            function,
            true);

      // Len: derive the reference-slot count from the object payload size.
      case ByteCode::Len:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::UInt32,
            (pos64_t)EIRLocation::ManagedValue);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::UInt32,
            runtime.objectLayout.fieldSize);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::UInt32,
            runtime.objectLayout.objectSizeMask & ~runtime.objectLayout.structMask);
         function.addOperand(
            EIROperandKind::Immediate,
            EIRType::Int32,
            (pos64_t)(long long)-(int)runtime.objectLayout.sizeOffset);

         return finishLocationOperation(
            EIROpcode::ObjectSize,
            EIREffect::ReadHeap,
            firstOperand,
            5,
            false,
            function);
      }

      // BLoad: load and zero-extend an unsigned byte from the receiver.
      case ByteCode::BLoad:
         return extendedHeapLoad(EIROpcode::LoadZeroExtend, EIRType::UInt32, 1, function);

      // WLoad: load and sign-extend a 16-bit integer from the receiver.
      case ByteCode::WLoad:
         return extendedHeapLoad(EIROpcode::LoadSignExtend, EIRType::Int32, 2, function);

      // MovFrm: copy the managed frame pointer into the value location.
      case ByteCode::MovFrm:
         return locationCopy(
            EIRLocation::ManagedValue, EIRLocation::FramePointer, EIRType::Pointer, function);

      // MLen: isolate the argument-count bits of the current message.
      case ByteCode::MLen:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::UInt32,
            (pos64_t)EIRLocation::ManagedValue);
         function.addOperand(EIROperandKind::Immediate, EIRType::UInt32, 0x1F);

         return finishLocationOperation(
            EIROpcode::BitAnd, EIREffect::None, firstOperand, 2, false, function);
      }

      // XAssign: store cached argument zero at the receiver's managed index.
      case ByteCode::XAssign:
      {
         function.clear();
         pos_t firstOperand = function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::ManagedObject);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Reference,
            (pos64_t)EIRLocation::CachedArgument0);
         function.addOperand(
            EIROperandKind::Location,
            EIRType::Word,
            (pos64_t)EIRLocation::ManagedValue);

         return finishLocationOperation(
            EIROpcode::StoreIndexed,
            EIREffect::WriteHeap,
            firstOperand,
            3,
            false,
            function);
      }

      // LLoad: load a 64-bit integer from the receiver payload.
      case ByteCode::LLoad:
         return locationLoad(
            EIRLocation::ManagedValue,
            EIRType::Int64,
            EIRLocation::ManagedObject,
            EIRType::Reference,
            0,
            function);

      // XLoad: load a 32-bit integer using the managed index.
      case ByteCode::XLoad:
         return indexedHeapOperation(
            EIROpcode::LoadIndexed,
            EIRLocation::ManagedValue,
            EIRType::Int32,
            function);

      // XLLoad: load a 64-bit integer using the managed index.
      case ByteCode::XLLoad:
         return indexedHeapOperation(
            EIROpcode::LoadIndexed,
            EIRLocation::ManagedValue,
            EIRType::Int64,
            function);

      // LSave: store the managed 64-bit integer in the receiver payload.
      case ByteCode::LSave:
         return locationHeapValueStore(
            EIRLocation::ManagedValue, EIRType::Int64, 0, function);

      // Parent: replace the receiver with its parent VMT reference.
      case ByteCode::Parent:
         return locationLoad(
            EIRLocation::ManagedObject,
            EIRType::Reference,
            EIRLocation::ManagedObject,
            EIRType::Reference,
            -(int)runtime.vmtLayout.parentOffset,
            function);

      // XGet: load an object reference using the managed index.
      case ByteCode::XGet:
         return indexedHeapOperation(
            EIROpcode::LoadIndexed,
            EIRLocation::ManagedObject,
            EIRType::Reference,
            function);

      // LoadZ: load and zero-extend a 32-bit integer from the receiver.
      case ByteCode::LoadZ:
         return extendedHeapLoad(EIROpcode::LoadZeroExtend, EIRType::UInt64, 4, function);

      // WLoadZ: load and zero-extend a 16-bit integer from the receiver.
      case ByteCode::WLoadZ:
         return extendedHeapLoad(EIROpcode::LoadZeroExtend, EIRType::UInt64, 2, function);

      default:
         return ECodeEIRLowerError::UnsupportedOpcode;
   }
}

static ECodeEIRLowerError lowerAllocation(
   const ByteCommand& command,
   const RuntimeSpec& runtime,
   EIRFunction& function,
   ECodeEIRMetadata* metadata)
{
   AllocationSpec allocation = {};

   switch (command.code) {
      // NewIR: allocate a fixed-size reference object.
      case ByteCode::NewIR:
         allocation.kind = AllocationKind::FixedReference;
         allocation.vmtReference = (ref_t)command.arg2;
         if (!runtime.objectLayout.payloadSize(command.arg1, allocation.payloadSize)
            || !runtime.objectLayout.allocationSize(command.arg1, allocation.allocationSize))
         {
            return ECodeEIRLowerError::InvalidArgument;
         }
         break;

      // NewNR: allocate a fixed-size binary object.
      case ByteCode::NewNR:
         allocation.kind = AllocationKind::FixedBinary;
         allocation.vmtReference = (ref_t)command.arg2;
         if (!runtime.objectLayout.binarySize(command.arg1, allocation.payloadSize)
            || !runtime.objectLayout.binaryAllocationSize(
               command.arg1, allocation.allocationSize))
         {
            return ECodeEIRLowerError::InvalidArgument;
         }
         break;

      // XNewNR: initialize a binary object in preallocated storage.
      case ByteCode::XNewNR:
         allocation.kind = AllocationKind::InlineBinary;
         allocation.vmtReference = (ref_t)command.arg2;
         if (!runtime.objectLayout.binarySize(command.arg1, allocation.payloadSize))
            return ECodeEIRLowerError::InvalidArgument;
         break;

      // CreateR: allocate a runtime-sized reference array.
      case ByteCode::CreateR:
         allocation.kind = AllocationKind::DynamicReference;
         allocation.vmtReference = (ref_t)command.arg1;
         allocation.elementSize = runtime.objectLayout.fieldSize;
         allocation.payloadMask = runtime.objectLayout.objectSizeMask;
         break;

      // CreateNR: allocate a runtime-sized binary array.
      case ByteCode::CreateNR:
         allocation.kind = AllocationKind::DynamicBinary;
         allocation.vmtReference = (ref_t)command.arg2;
         allocation.elementSize = (unsigned int)command.arg1;
         allocation.payloadMask = runtime.objectLayout.objectSizeMask
            & ~runtime.objectLayout.structMask;
         if (command.arg1 <= 0)
            return ECodeEIRLowerError::InvalidArgument;
         break;

      // XCreateR: allocate a runtime-sized permanent reference array.
      case ByteCode::XCreateR:
         allocation.kind = AllocationKind::Permanent;
         allocation.vmtReference = (ref_t)command.arg1;
         allocation.elementSize = runtime.objectLayout.fieldSize;
         allocation.payloadMask = runtime.objectLayout.objectSizeMask;
         break;

      default:
         return ECodeEIRLowerError::UnsupportedOpcode;
   }

   if (allocation.elementSize > allocation.payloadMask
      && (allocation.kind == AllocationKind::DynamicReference
         || allocation.kind == AllocationKind::DynamicBinary
         || allocation.kind == AllocationKind::Permanent))
   {
      return ECodeEIRLowerError::InvalidArgument;
   }

   if (allocation.payloadSize > 0x7FFFFFFFu
      || allocation.allocationSize > 0x7FFFFFFFu)
   {
      return ECodeEIRLowerError::InvalidArgument;
   }

   if (metadata) {
      metadata->kind = ECodeEIRKind::Allocation;
      metadata->allocation = allocation;
   }

   function.clear();
   pos_t firstOperand = function.addOperand(
      EIROperandKind::Immediate, EIRType::UInt8, (pos64_t)allocation.kind);
   function.addOperand(
      EIROperandKind::Reference, EIRType::VMT, allocation.vmtReference);

   EIROpcode opcode = allocation.kind == AllocationKind::Permanent
      ? EIROpcode::AllocatePermanent
      : allocation.kind == AllocationKind::DynamicReference
         || allocation.kind == AllocationKind::DynamicBinary
         ? EIROpcode::AllocateArray
         : EIROpcode::Allocate;
   EIREffect effects = allocation.kind == AllocationKind::InlineBinary
      ? EIREffect::WriteHeap
      : EIREffect::ReadHeap | EIREffect::WriteHeap | EIREffect::Call
         | EIREffect::Allocate | EIREffect::Safepoint | EIREffect::Throw;

   function.addInstruction(opcode, effects, noEIRValue(), firstOperand, 2, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return checkedEIR(EIRVerifier::verify(function));
}

static ECodeEIRLowerError lowerExceptionControl(
   const ByteCommand& command, const RuntimeSpec& runtime, EIRFunction& function)
{
   EIREffect threadEffects = runtime.threadingMode == ThreadingMode::MultiThread
      ? EIREffect::ReadTLS | EIREffect::WriteTLS
      : EIREffect::ReadGlobal | EIREffect::WriteGlobal;

   switch (command.code) {
      case ByteCode::Throw:
         function.clear();
         function.addInstruction(EIROpcode::ExceptionRaise,
            threadEffects | EIREffect::ReadFrame | EIREffect::Throw | EIREffect::Terminator,
            noEIRValue(), 0, 0, 0);
         function.addBlock(0, 0, 1, 0);

         return checkedEIR(EIRVerifier::verify(function));

      case ByteCode::Unhook:
         return singleOperation(EIROpcode::ExceptionUnhook,
            threadEffects | EIREffect::ReadFrame | EIREffect::WriteFrame, function);

      case ByteCode::Exclude:
      {
         EIREffect effects = threadEffects | EIREffect::ReadFrame | EIREffect::WriteFrame;
         if (runtime.threadingMode == ThreadingMode::MultiThread)
            effects = effects | EIREffect::Call | EIREffect::Safepoint | EIREffect::Synchronize;

         return singleOperation(EIROpcode::ThreadExclude, effects, function);
      }

      case ByteCode::Include:
         return singleOperation(EIROpcode::ThreadInclude,
            threadEffects | EIREffect::ReadFrame | EIREffect::WriteFrame | EIREffect::Synchronize,
            function);

      default:
         return ECodeEIRLowerError::UnsupportedOpcode;
   }
}

ECodeEIRLowerError ECodeEIRProvider :: lower(const ByteCommand& command, const RuntimeSpec& runtime,
   const TargetSpec& target, const ECodeLoweringContext& context, EIRFunction& function,
   ECodeEIRMetadata* metadata)
{
   if (!runtime.isValid(target))
      return ECodeEIRLowerError::InvalidRuntime;

   if (metadata)
      *metadata = {};

   ECodeEIRLowerError managedState = lowerManagedState(command, function);
   if (managedState != ECodeEIRLowerError::UnsupportedOpcode)
      return managedState;

   ECodeEIRLowerError managedSlots = lowerManagedSlots(
      command, runtime, target, context, function);
   if (managedSlots != ECodeEIRLowerError::UnsupportedOpcode)
      return managedSlots;

   ECodeEIRLowerError allocation = lowerAllocation(command, runtime, function, metadata);
   if (allocation != ECodeEIRLowerError::UnsupportedOpcode)
      return allocation;

   ECodeEIRLowerError implicitState = lowerImplicitState(command, runtime, function);
   if (implicitState != ECodeEIRLowerError::UnsupportedOpcode)
      return implicitState;

   if (command.code == ByteCode::CallR || command.code == ByteCode::CallVI
      || command.code == ByteCode::JumpVI || command.code == ByteCode::CallMR
      || command.code == ByteCode::JumpMR)
   {
      ManagedMethodSpec method = {};
      if (!ManagedMethodProvider::get(command, context.alternativeMode, method))
         return ECodeEIRLowerError::InvalidArgument;

      if (metadata) {
         metadata->kind = ECodeEIRKind::ManagedMethod;
         metadata->managedMethod = method;
      }

      return checkedEIR(ManagedMethodEIRProvider::lower(method, function));
   }

   if (command.code == ByteCode::VCallMR || command.code == ByteCode::VJumpMR) {
      VirtualMethodSpec method = {};
      if (!VirtualMethodProvider::get(command, context.alternativeMode, method))
         return ECodeEIRLowerError::InvalidArgument;

      if (metadata) {
         metadata->kind = ECodeEIRKind::VirtualMethod;
         metadata->virtualMethod = method;
      }

      return checkedEIR(VirtualMethodEIRProvider::lower(method, function));
   }

   if (command.code == ByteCode::XDispatchMR || command.code == ByteCode::DispatchMR) {
      DispatchSpec dispatch = {};
      if (!DispatchProvider::get(command, context.alternativeMode, dispatch))
         return ECodeEIRLowerError::InvalidArgument;

      DispatchOption xdispatchOptions = DispatchOption::Variadic | DispatchOption::ReceiverLists;
      DispatchOption dispatchOptions = DispatchOption::VirtualTarget
         | DispatchOption::Variadic | DispatchOption::AlternativeVMT;
      DispatchOption supported = command.code == ByteCode::XDispatchMR
         ? xdispatchOptions : dispatchOptions;

      if (((unsigned char)dispatch.options & ~(unsigned char)supported) != 0
         || (command.code == ByteCode::DispatchMR
            && (!dispatch.has(DispatchOption::VirtualTarget)
               || dispatch.has(DispatchOption::ReceiverLists))))
      {
         return ECodeEIRLowerError::UnsupportedOpcode;
      }

      if (metadata) {
         metadata->kind = ECodeEIRKind::Dispatch;
         metadata->dispatch = dispatch;
      }

      return checkedEIR(DispatchEIRProvider::lower(dispatch, function));
   }

   ECodeEIRLowerError exceptionControl = lowerExceptionControl(command, runtime, function);
   if (exceptionControl != ECodeEIRLowerError::UnsupportedOpcode)
      return exceptionControl;

   if (command.code == ByteCode::XHookDPR) {
      if (command.arg2 == 0)
         return ECodeEIRLowerError::InvalidArgument;

      long long frameOffset = -((long long)command.arg1
         - (command.arg1 < 0 ? context.dataOffset : 0));
      if (frameOffset < -0x80000000LL || frameOffset > 0x7FFFFFFFLL)
         return ECodeEIRLowerError::InvalidArgument;

      ref_t targetMask = command.arg2 & mskAnyRef;
      ExceptionHookSpec hook = {
         .frameOffset = (int)frameOffset,
         .target = targetMask == mskLabelRef
            ? (pos64_t)(command.arg2 & ~mskAnyRef)
            : (pos64_t)(ref_t)command.arg2,
         .targetKind = targetMask == mskLabelRef
            ? ExceptionTargetKind::ProcedureLabel
            : ExceptionTargetKind::Reference,
         .threadLocal = runtime.threadingMode == ThreadingMode::MultiThread
      };

      return checkedEIR(ExceptionEIRProvider::lower(hook, function));
   }

   switch (command.code) {
      case ByteCode::OpenIN:
      {
         if (command.arg1 < 0 || command.arg2 < 0)
            return ECodeEIRLowerError::InvalidArgument;

         FrameOpenSpec spec = {
            .managedSlots = (unsigned int)command.arg1,
            .unmanagedSize = (unsigned int)command.arg2
         };

         return checkedEIR(FrameEIRProvider::lower(spec, target, function));
      }

      case ByteCode::CloseN:
      {
         if (command.arg1 < 0)
            return ECodeEIRLowerError::InvalidArgument;

         long long argumentSize = command.arg1 > 0
            ? (long long)command.arg1 + context.dataHeader : 0;
         if (argumentSize < 0 || argumentSize > 0xFFFFFFFFLL)
            return ECodeEIRLowerError::InvalidArgument;

         FrameCloseSpec spec = {
            .argumentSize = (unsigned int)argumentSize
         };

         return checkedEIR(FrameEIRProvider::lower(spec, function));
      }

      case ByteCode::ExtOpenIN:
      {
         if (command.arg1 < 0 || command.arg2 < 0)
            return ECodeEIRLowerError::InvalidArgument;

         FrameOpenSpec spec = {
            .managedSlots = (unsigned int)command.arg1,
            .unmanagedSize = (unsigned int)command.arg2
         };

         return checkedEIR(FrameEIRProvider::lowerExternal(
            spec, target, runtime.threadingMode, function));
      }

      case ByteCode::ExtCloseN:
      {
         if (command.arg1 < 0)
            return ECodeEIRLowerError::InvalidArgument;

         long long argumentSize = command.arg1 > 0
            ? (long long)command.arg1 + context.dataHeader : 0;
         if (argumentSize < 0 || argumentSize > 0xFFFFFFFFLL)
            return ECodeEIRLowerError::InvalidArgument;

         FrameCloseSpec spec = {
            .argumentSize = (unsigned int)argumentSize
         };

         return checkedEIR(FrameEIRProvider::lowerExternal(
            spec, runtime.threadingMode, function));
      }

      case ByteCode::Copy:
      {
         if (command.arg1 < 0)
            return ECodeEIRLowerError::InvalidArgument;

         MemoryCopySpec spec = {
            .byteCount = (unsigned int)command.arg1
         };

         return checkedEIR(MemoryEIRProvider::lower(spec, function));
      }

      case ByteCode::TstStck:
      {
         StackReferenceSpec spec = {
            .threadingMode = runtime.threadingMode
         };

         return checkedEIR(StackReferenceEIRProvider::lower(spec, function));
      }

      case ByteCode::TryLock:
      case ByteCode::FreeLock:
      {
         bool tryLock = command.code == ByteCode::TryLock;
         EIREffect effects = EIREffect::None;
         if (runtime.threadingMode == ThreadingMode::MultiThread) {
            effects = EIREffect::ReadHeap
               | EIREffect::WriteHeap
               | EIREffect::Synchronize;
         }

         return singleOperation(
            tryLock ? EIROpcode::ObjectLockTry : EIROpcode::ObjectLockRelease,
            effects,
            function);
      }

      case ByteCode::PeekTLS:
      case ByteCode::StoreTLS:
      {
         if (((ref_t)command.arg1 & mskAnyRef) != mskTLSVariable)
            return ECodeEIRLowerError::InvalidArgument;

         bool load = command.code == ByteCode::PeekTLS;

         function.clear();
         pos_t reference = function.addOperand(
            EIROperandKind::Reference,
            EIRType::Pointer,
            (pos64_t)(ref_t)command.arg1);
         function.addInstruction(
            load ? EIROpcode::ThreadLocalLoad : EIROpcode::ThreadLocalStore,
            load ? EIREffect::ReadTLS : EIREffect::ReadTLS | EIREffect::WriteTLS,
            noEIRValue(),
            reference,
            1,
            0);
         function.addInstruction(
            EIROpcode::Fallthrough,
            EIREffect::Terminator,
            noEIRValue(),
            function.operandCount(),
            0,
            0);
         function.addBlock(0, 0, 2, 0);

         return checkedEIR(EIRVerifier::verify(function));
      }

      case ByteCode::System:
         switch (command.arg1) {
            case 0:
               return singleOperation(EIROpcode::NoOperation, EIREffect::None, function);

            case 1:
            case 2:
            {
               RuntimeCallSpec collect = {};
               if (!RuntimeCallProvider::get(RuntimeOperation::Collect, runtime, collect))
                  return ECodeEIRLowerError::InvalidRuntime;

               function.clear();
               pos_t fullCollection = function.addOperand(
                  EIROperandKind::Immediate, EIRType::Boolean, command.arg1 - 1);
               function.addInstruction(EIROpcode::Collect, runtimeEffects(collect.effects),
                  noEIRValue(), fullCollection, 1, 0);
               function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
                  noEIRValue(), function.operandCount(), 0, 0);
               function.addBlock(0, 0, 2, 0);

               return checkedEIR(EIRVerifier::verify(function));
            }

            case 3:
               if (runtime.threadingMode != ThreadingMode::MultiThread)
                  return ECodeEIRLowerError::InvalidRuntime;

               return singleOperation(EIROpcode::ThreadPublish,
                  EIREffect::ReadTLS | EIREffect::WriteTLS | EIREffect::WriteFrame, function);

            case 4:
            {
               function.clear();
               pos_t publishStackRoot = function.addOperand(EIROperandKind::Immediate,
                  EIRType::Boolean, runtime.threadingMode == ThreadingMode::SingleThread ? 1 : 0);
               function.addInstruction(EIROpcode::SystemStartup,
                  EIREffect::ReadGlobal | EIREffect::WriteGlobal | EIREffect::Call,
                  noEIRValue(), publishStackRoot, 1, 0);
               function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
                  noEIRValue(), function.operandCount(), 0, 0);
               function.addBlock(0, 0, 2, 0);

               return checkedEIR(EIRVerifier::verify(function));
            }

            case 5:
               return checkedEIR(StackEIRProvider::lowerRootAllocation(function));

            case 6:
            case 7:
            {
               if (runtime.threadingMode != ThreadingMode::MultiThread)
                  return ECodeEIRLowerError::InvalidRuntime;

               EIROpcode opcode = command.arg1 == 6
                  ? EIROpcode::GCLockAcquire
                  : EIROpcode::GCLockRelease;
               EIREffect effects = EIREffect::ReadGlobal
                  | EIREffect::WriteGlobal
                  | EIREffect::Synchronize;

               return singleOperation(opcode, effects, function);
            }

            case 8:
            case 9:
            {
               EIROpcode opcode = command.arg1 == 8
                  ? EIROpcode::SafeRegionEnter : EIROpcode::SafeRegionLeave;
               EIREffect effects = EIREffect::None;

               if (runtime.threadingMode == ThreadingMode::MultiThread) {
                  effects = EIREffect::ReadGlobal | EIREffect::WriteGlobal
                     | EIREffect::ReadTLS | EIREffect::WriteTLS | EIREffect::Call
                     | EIREffect::Safepoint | EIREffect::Synchronize;
               }

               return singleOperation(opcode, effects, function);
            }

            default:
               return ECodeEIRLowerError::UnsupportedOpcode;
         }

      default:
         return ECodeEIRLowerError::UnsupportedOpcode;
   }
}

EIRVerifyError FrameEIRProvider :: lower(
   const FrameCloseSpec& spec,
   EIRFunction& function)
{
   function.clear();

   pos_t firstOperand = function.addOperand(
      EIROperandKind::Immediate,
      EIRType::UInt32,
      spec.argumentSize);

   function.addInstruction(
      EIROpcode::FrameClose,
      EIREffect::WriteFrame,
      noEIRValue(),
      firstOperand,
      1,
      0);

   function.addInstruction(
      EIROpcode::Fallthrough,
      EIREffect::Terminator,
      noEIRValue(),
      function.operandCount(),
      0,
      0);

   function.addBlock(0, 0, 2, 0);

   return EIRVerifier::verify(function);
}

EIRVerifyError FrameEIRProvider :: lowerExternal(
   const FrameOpenSpec& spec, const TargetSpec& target,
   ThreadingMode threadingMode, EIRFunction& function)
{
   FrameOpenLayout frame = {};
   if (!layout(spec, target, frame))
      return EIRVerifyError::InvalidFrame;

   function.clear();

   EIREffect effects = EIREffect::ReadFrame | EIREffect::WriteFrame
      | EIREffect::ReadGlobal | EIREffect::WriteGlobal;
   if (threadingMode == ThreadingMode::MultiThread)
      effects = effects | EIREffect::ReadTLS | EIREffect::WriteTLS;
   function.addInstruction(EIROpcode::ExternalFrameOpen, effects,
      noEIRValue(), function.operandCount(), 0, 0);

   pos_t zero = function.addOperand(EIROperandKind::Immediate, EIRType::Word, 0);
   function.addInstruction(EIROpcode::Constant, EIREffect::None,
      eirValue(0, EIRType::Word), zero, 1, 0);

   if (frame.unmanagedSize != 0) {
      pos_t size = function.addOperand(
         EIROperandKind::Immediate, EIRType::UInt32, frame.unmanagedSize);
      function.addInstruction(EIROpcode::StackReserve, EIREffect::WriteFrame,
         noEIRValue(), size, 1, 0);
   }

   pos_t link = function.addOperand(EIROperandKind::Value, EIRType::Word, 0);
   function.addInstruction(EIROpcode::FrameLink, EIREffect::WriteFrame,
      noEIRValue(), link, 1, 0);

   if (frame.managedSlots != 0) {
      pos_t clear = function.addOperand(EIROperandKind::Value, EIRType::Word, 0);
      function.addOperand(EIROperandKind::Immediate, EIRType::UInt32, frame.managedSlots);
      function.addInstruction(EIROpcode::FrameClear, EIREffect::WriteFrame,
         noEIRValue(), clear, 2, 0);
   }

   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, function.instructionCount(), 0);

   return EIRVerifier::verify(function);
}

EIRVerifyError FrameEIRProvider :: lowerExternal(
   const FrameCloseSpec& spec, ThreadingMode threadingMode, EIRFunction& function)
{
   function.clear();

   pos_t argumentSize = function.addOperand(
      EIROperandKind::Immediate, EIRType::UInt32, spec.argumentSize);
   EIREffect effects = EIREffect::ReadFrame | EIREffect::WriteFrame
      | EIREffect::ReadGlobal | EIREffect::WriteGlobal;
   if (threadingMode == ThreadingMode::MultiThread)
      effects = effects | EIREffect::ReadTLS | EIREffect::WriteTLS;

   function.addInstruction(EIROpcode::ExternalFrameClose, effects,
      noEIRValue(), argumentSize, 1, 0);
   function.addInstruction(EIROpcode::Fallthrough, EIREffect::Terminator,
      noEIRValue(), function.operandCount(), 0, 0);
   function.addBlock(0, 0, 2, 0);

   return EIRVerifier::verify(function);
}

bool StackReferenceSpec :: contains(
   pos64_t reference,
   pos64_t stackPointer,
   pos64_t stackRoot) const
{
   return stackPointer <= stackRoot
      && reference >= stackPointer
      && reference <= stackRoot;
}

EIRVerifyError EIRVerifier :: verifyPhi(EIRFunction& function,
   EIRInstruction& instruction, pos_t blockId)
{
   if (!instruction.result.isValid() || instruction.operandCount < 2
      || (instruction.operandCount & 1) != 0)
   {
      return EIRVerifyError::InvalidPhi;
   }

   for (pos_t i = 0; i < instruction.operandCount; i += 2) {
      EIROperand& block = function.operand(instruction.firstOperand + i);
      EIROperand& value = function.operand(instruction.firstOperand + i + 1);
      if (block.kind != EIROperandKind::Block
         || value.kind != EIROperandKind::Value
         || value.type != instruction.result.type)
      {
         return EIRVerifyError::InvalidPhi;
      }
      if (!isPredecessor(function, (pos_t)block.value, blockId))
         return EIRVerifyError::InvalidPhiPredecessor;
      for (pos_t j = 0; j < i; j += 2) {
         if (function.operand(instruction.firstOperand + j).value == block.value)
            return EIRVerifyError::InvalidPhiPredecessor;
      }
   }

   if (instruction.operandCount / 2 != predecessorCount(function, blockId))
      return EIRVerifyError::InvalidPhiPredecessor;

   return EIRVerifyError::None;
}

bool EIRVerifier :: targetsBlock(EIRFunction& function,
   EIRInstruction& instruction, pos_t blockId)
{
   if (!test(instruction.effects, EIREffect::Terminator))
      return false;
   if (instruction.firstOperand > function.operandCount()
      || instruction.operandCount > function.operandCount() - instruction.firstOperand)
   {
      return false;
   }

   for (pos_t i = 0; i < instruction.operandCount; i++) {
      EIROperand& operand = function.operand(instruction.firstOperand + i);
      if (operand.kind == EIROperandKind::Block && operand.value == blockId)
         return true;
   }

   return false;
}

bool EIRVerifier :: isPredecessor(EIRFunction& function,
   pos_t predecessor, pos_t blockId)
{
   if (predecessor >= function.blockCount())
      return false;

   EIRBlock& block = function.block(predecessor);
   if (block.instructionCount == 0
      || block.firstInstruction >= function.instructionCount()
      || block.instructionCount > function.instructionCount() - block.firstInstruction)
   {
      return false;
   }
   EIRInstruction& terminator = function.instruction(
      block.firstInstruction + block.instructionCount - 1);

   return targetsBlock(function, terminator, blockId);
}

pos_t EIRVerifier :: predecessorCount(EIRFunction& function, pos_t blockId)
{
   pos_t count = 0;
   for (pos_t i = 0; i < function.blockCount(); i++) {
      if (isPredecessor(function, i, blockId))
         count++;
   }

   return count;
}

EIRVerifyError EIRVerifier :: verifyTerminator(EIRFunction& function,
   EIRInstruction& instruction)
{
   pos_t first = instruction.firstOperand;
   switch (instruction.opcode) {
      case EIROpcode::Branch:
         if (instruction.operandCount != 1
            || function.operand(first).kind != EIROperandKind::Block)
         {
            return EIRVerifyError::InvalidTerminator;
         }
         break;
      case EIROpcode::ConditionalBranch:
         if (instruction.operandCount != 3
            || function.operand(first).kind != EIROperandKind::Value
            || function.operand(first + 1).kind != EIROperandKind::Block
            || function.operand(first + 2).kind != EIROperandKind::Block)
         {
            return EIRVerifyError::InvalidTerminator;
         }
         break;
      case EIROpcode::IndirectBranch:
         if (instruction.operandCount == 0
            || (function.operand(first).kind != EIROperandKind::Value
               && function.operand(first).kind != EIROperandKind::Location))
         {
            return EIRVerifyError::InvalidTerminator;
         }
         break;
      case EIROpcode::Switch:
         if (instruction.operandCount < 2
            || ((instruction.operandCount - 2) & 1) != 0
            || function.operand(first).kind != EIROperandKind::Value
            || function.operand(first + 1).kind != EIROperandKind::Block)
         {
            return EIRVerifyError::InvalidTerminator;
         }
         for (pos_t i = 2; i < instruction.operandCount; i += 2) {
            EIROperand& selector = function.operand(first);
            EIROperand& value = function.operand(first + i);
            EIROperand& target = function.operand(first + i + 1);
            if (value.kind != EIROperandKind::Immediate
               || value.type != selector.type
               || target.kind != EIROperandKind::Block)
            {
               return EIRVerifyError::InvalidTerminator;
            }
         }
         break;
      case EIROpcode::Return:
         if (instruction.operandCount > 1
            || (instruction.operandCount == 1
               && function.operand(first).kind != EIROperandKind::Value
               && function.operand(first).kind != EIROperandKind::Location))
         {
            return EIRVerifyError::InvalidTerminator;
         }
         break;
      case EIROpcode::Fallthrough:
         if (instruction.operandCount != 0)
            return EIRVerifyError::InvalidTerminator;
         break;
      case EIROpcode::Throw:
         if (instruction.operandCount != 1
            || function.operand(first).kind != EIROperandKind::Value)
         {
            return EIRVerifyError::InvalidTerminator;
         }
         break;
      case EIROpcode::ExceptionRaise:
         if (instruction.operandCount != 0)
            return EIRVerifyError::InvalidTerminator;
         break;
      case EIROpcode::Trap:
         if (instruction.operandCount != 0)
            return EIRVerifyError::InvalidTerminator;
         break;
      default:
         return EIRVerifyError::InvalidTerminator;
   }

   return EIRVerifyError::None;
}

EIRVerifyError EIRVerifier :: verify(EIRFunction& function)
{
   if (function.blockCount() == 0 || function.instructionCount() == 0)
      return EIRVerifyError::EmptyFunction;

   pos_t covered = 0;
   for (pos_t i = 0; i < function.blockCount(); i++) {
      EIRBlock& block = function.block(i);
      if (block.id != i)
         return EIRVerifyError::InvalidBlockId;
      if (block.firstInstruction != covered || block.instructionCount == 0
         || block.firstInstruction > function.instructionCount()
         || block.instructionCount > function.instructionCount() - block.firstInstruction)
      {
         return EIRVerifyError::InvalidInstructionRange;
      }

      bool phiAllowed = true;
      for (pos_t j = 0; j < block.instructionCount; j++) {
         EIRInstruction& instruction = function.instruction(block.firstInstruction + j);
         EIRVerifyError error = verifyInstruction(function, instruction);
         if (error != EIRVerifyError::None)
            return error;

         if (instruction.result.id == INVALID_POS) {
            if (instruction.result.type != EIRType::None)
               return EIRVerifyError::InvalidResult;
         }
         else {
            if (!instruction.result.isValid())
               return EIRVerifyError::InvalidResult;
            for (pos_t k = 0; k < block.firstInstruction + j; k++) {
               EIRInstruction& previous = function.instruction(k);
               if (previous.result.isValid()
                  && previous.result.id == instruction.result.id)
               {
                  return EIRVerifyError::DuplicateValue;
               }
            }
         }

         if (instruction.opcode == EIROpcode::Phi) {
            if (!phiAllowed)
               return EIRVerifyError::PhiAfterInstruction;

            error = verifyPhi(function, instruction, block.id);
            if (error != EIRVerifyError::None)
               return error;
         }
         else {
            phiAllowed = false;
         }

         bool last = j + 1 == block.instructionCount;
         if (test(instruction.effects, EIREffect::Terminator)) {
            if (!last)
               return EIRVerifyError::EarlyTerminator;

            error = verifyTerminator(function, instruction);
            if (error != EIRVerifyError::None)
               return error;
         }
         else if (last) {
            return EIRVerifyError::MissingTerminator;
         }
      }

      covered += block.instructionCount;
   }

   return covered == function.instructionCount()
      ? EIRVerifyError::None : EIRVerifyError::UncoveredInstruction;
}

static EIREffect dispatchEffects(DispatchPhase phase)
{
   switch (phase) {
      case DispatchPhase::PreserveState:
      case DispatchPhase::RestoreSuccess:
      case DispatchPhase::RestoreFailure:
         return EIREffect::WriteFrame;
      case DispatchPhase::LocateArguments:
      case DispatchPhase::CountArguments:
      case DispatchPhase::AdvanceArgumentCount:
      case DispatchPhase::TestArgumentSentinel:
         return EIREffect::ReadFrame;
      case DispatchPhase::SelectList:
      case DispatchPhase::SelectOverload:
      case DispatchPhase::LoadSignature:
      case DispatchPhase::LoadArgumentTypes:
      case DispatchPhase::TestArgumentType:
      case DispatchPhase::LoadParent:
      case DispatchPhase::TestParent:
      case DispatchPhase::ResolveTarget:
      case DispatchPhase::AdvanceOverload:
      case DispatchPhase::LoadReceiverVMT:
      case DispatchPhase::SelectAlternativeVMT:
      case DispatchPhase::ResolveVirtualTarget:
         return EIREffect::ReadHeap;
      default:
         return EIREffect::None;
   }
}

bool DispatchEIRProvider :: getPhase(EIRFunction& function, EIRBlock& block, DispatchPhase& phase)
{
   if (block.instructionCount < 2)
      return false;

   EIRInstruction& instruction = function.instruction(block.firstInstruction);
   if (instruction.opcode != EIROpcode::Dispatch
      || instruction.operandCount == 0)
   {
      return false;
   }

   EIROperand& operand = function.operand(instruction.firstOperand);
   if (operand.kind != EIROperandKind::Immediate
      || operand.type != EIRType::UInt32
      || operand.value >= (unsigned int)DispatchPhase::Count)
   {
      return false;
   }

   phase = (DispatchPhase)operand.value;

   return true;
}

EIRVerifyError DispatchEIRProvider :: lower(const DispatchSpec& spec, EIRFunction& function)
{
   DispatchControlFlow controlFlow = {};
   if (!DispatchProvider::buildControlFlow(spec, controlFlow))
      return EIRVerifyError::InvalidDispatch;

   function.clear();

   pos_t nextValue = 1;
   pos_t methodValue = INVALID_POS;
   pos_t vmtValue = INVALID_POS;
   pos_t targetValue = INVALID_POS;
   bool virtualTarget = spec.has(DispatchOption::VirtualTarget);

   for (unsigned char i = 0; i < controlFlow.blockCount; i++) {
      DispatchBlock& block = controlFlow.blocks[i];
      pos_t firstInstruction = function.instructionCount();

      pos_t phaseOperand = function.addOperand(EIROperandKind::Immediate, EIRType::UInt32, (pos64_t)block.phase);

      EIRValue result = noEIRValue();
      pos_t operandCount = 1;

      if (block.has(DispatchBlockProperty::Conditional)) {
         result = eirValue(nextValue++, EIRType::Boolean);
      }
      else if (block.phase == DispatchPhase::ResolveTarget) {
         if (virtualTarget) {
            methodValue = nextValue++;
            result = eirValue(methodValue, EIRType::Word);
         }
         else {
            targetValue = nextValue++;
            result = eirValue(targetValue, EIRType::Pointer);
         }
      }
      else if (block.phase == DispatchPhase::LoadReceiverVMT) {
         vmtValue = nextValue++;
         result = eirValue(vmtValue, EIRType::VMT);
      }
      else if (block.phase == DispatchPhase::SelectAlternativeVMT) {
         if (vmtValue == INVALID_POS)
            return EIRVerifyError::InvalidDispatch;

         function.addOperand(EIROperandKind::Value, EIRType::VMT, vmtValue);

         operandCount++;
         vmtValue = nextValue++;
         result = eirValue(vmtValue, EIRType::VMT);
      }
      else if (block.phase == DispatchPhase::ResolveVirtualTarget) {
         if (vmtValue == INVALID_POS || methodValue == INVALID_POS)
            return EIRVerifyError::InvalidDispatch;

         function.addOperand(EIROperandKind::Value, EIRType::VMT, vmtValue);
         function.addOperand(EIROperandKind::Value, EIRType::Word, methodValue);

         operandCount += 2;
         targetValue = nextValue++;
         result = eirValue(targetValue, EIRType::Pointer);
      }

      function.addInstruction(
         EIROpcode::Dispatch,
         dispatchEffects(block.phase),
         result,
         phaseOperand,
         operandCount,
         0);

      if (block.phase == DispatchPhase::BranchTarget) {
         if (targetValue == INVALID_POS)
            return EIRVerifyError::InvalidDispatch;

         pos_t targetOperand = function.addOperand(EIROperandKind::Value, EIRType::Pointer, targetValue);

         function.addInstruction(
            EIROpcode::IndirectBranch,
            EIREffect::Terminator,
            noEIRValue(),
            targetOperand,
            1,
            0);
      }
      else if (block.phase == DispatchPhase::Fallthrough) {
         function.addInstruction(
            EIROpcode::Fallthrough,
            EIREffect::Terminator,
            noEIRValue(),
            function.operandCount(),
            0,
            0);
      }
      else if (block.has(DispatchBlockProperty::Conditional)) {
         pos_t firstOperand = function.addOperand(EIROperandKind::Value, EIRType::Boolean, result.id);
         function.addOperand(EIROperandKind::Block, EIRType::None, block.next);
         function.addOperand(EIROperandKind::Block, EIRType::None, block.alternate);

         function.addInstruction(
            EIROpcode::ConditionalBranch,
            EIREffect::Terminator,
            noEIRValue(),
            firstOperand,
            3,
            0);
      }
      else {
         pos_t targetOperand = function.addOperand(EIROperandKind::Block, EIRType::None, block.next);

         function.addInstruction(
            EIROpcode::Branch,
            EIREffect::Terminator,
            noEIRValue(),
            targetOperand,
            1,
            0);
      }

      function.addBlock(
         i,
         firstInstruction,
         function.instructionCount() - firstInstruction,
         0);
   }

   return EIRVerifier::verify(function);
}

EIRVerifyError VirtualMethodEIRProvider :: lower(const VirtualMethodSpec& spec, EIRFunction& function)
{
   function.clear();

   pos_t vmtValue = 1;
   pos_t methodOffsetValue = 2;
   pos_t targetValue = 3;

   function.addInstruction(
      EIROpcode::ObjectVMT,
      EIREffect::ReadHeap,
      eirValue(vmtValue, EIRType::VMT),
      function.operandCount(),
      0,
      0);

   pos_t methodOperands = function.addOperand(EIROperandKind::Reference, EIRType::VMT, spec.classReference);
   function.addOperand(EIROperandKind::Reference, EIRType::Message, spec.message);
   function.addOperand(EIROperandKind::Immediate, EIRType::UInt32, (pos64_t)spec.options);

   function.addInstruction(
      EIROpcode::MethodOffset,
      EIREffect::None,
      eirValue(methodOffsetValue, EIRType::Word),
      methodOperands,
      3,
      0);

   if (spec.has(MethodLookupOption::AlternativeVMT)) {
      pos_t sourceVMT = function.addOperand(EIROperandKind::Value, EIRType::VMT, vmtValue);

      vmtValue = 4;
      targetValue = 5;

      function.addInstruction(
         EIROpcode::SelectAlternativeVMT,
         EIREffect::ReadHeap,
         eirValue(vmtValue, EIRType::VMT),
         sourceVMT,
         1,
         0);
   }

   pos_t resolveOperands = function.addOperand(EIROperandKind::Value, EIRType::VMT, vmtValue);
   function.addOperand(EIROperandKind::Value, EIRType::Word, methodOffsetValue);

   function.addInstruction(
      EIROpcode::ResolveVirtualMethod,
      EIREffect::ReadHeap,
      eirValue(targetValue, EIRType::Pointer),
      resolveOperands,
      2,
      0);

   pos_t transferOperand = function.addOperand(EIROperandKind::Value, EIRType::Pointer, targetValue);

   if (spec.transfer == MethodTransferKind::Call) {
      function.addInstruction(
         EIROpcode::CallIndirect,
         EIREffect::ReadHeap | EIREffect::WriteHeap | EIREffect::Call
            | EIREffect::Safepoint | EIREffect::Throw,
         noEIRValue(),
         transferOperand,
         1,
         0);

      function.addInstruction(
         EIROpcode::Fallthrough,
         EIREffect::Terminator,
         noEIRValue(),
         function.operandCount(),
         0,
         0);
   }
   else {
      function.addInstruction(
         EIROpcode::IndirectBranch,
         EIREffect::Terminator,
         noEIRValue(),
         transferOperand,
         1,
         0);
   }

   function.addBlock(0, 0, function.instructionCount());

   return EIRVerifier::verify(function);
}

EIRVerifyError ManagedMethodEIRProvider :: lower(const ManagedMethodSpec& spec, EIRFunction& function)
{
   function.clear();

   EIREffect callEffects = EIREffect::ReadHeap | EIREffect::WriteHeap
      | EIREffect::Call | EIREffect::Safepoint | EIREffect::Throw;
   pos_t targetOperand = 0;

   if (spec.target == ManagedMethodTarget::Symbol) {
      targetOperand = function.addOperand(EIROperandKind::Reference, EIRType::Pointer, spec.reference);

      function.addInstruction(
         EIROpcode::CallDirect,
         callEffects,
         noEIRValue(),
         targetOperand,
         1,
         0);
   }
   else {
      pos_t targetValue = 1;

      if (spec.target == ManagedMethodTarget::VMTIndex) {
         pos_t vmtValue = 1;
         targetValue = 2;

         function.addInstruction(
            EIROpcode::ObjectVMT,
            EIREffect::ReadHeap,
            eirValue(vmtValue, EIRType::VMT),
            function.operandCount(),
            0,
            0);

         pos_t resolveOperands = function.addOperand(EIROperandKind::Value, EIRType::VMT, vmtValue);
         function.addOperand(EIROperandKind::Immediate, EIRType::UInt32, (pos64_t)spec.index);

         function.addInstruction(
            EIROpcode::ResolveVirtualIndex,
            EIREffect::ReadHeap,
            eirValue(targetValue, EIRType::Pointer),
            resolveOperands,
            2,
            0);
      }
      else if (spec.target == ManagedMethodTarget::VMTMethod) {
         pos_t methodOperands = function.addOperand(EIROperandKind::Reference, EIRType::VMT, spec.reference);
         function.addOperand(EIROperandKind::Reference, EIRType::Message, spec.message);
         function.addOperand(EIROperandKind::Immediate, EIRType::UInt32, (pos64_t)spec.options);

         function.addInstruction(
            EIROpcode::MethodAddress,
            EIREffect::None,
            eirValue(targetValue, EIRType::Pointer),
            methodOperands,
            3,
            0);
      }
      else {
         return EIRVerifyError::InvalidManagedMethod;
      }

      targetOperand = function.addOperand(EIROperandKind::Value, EIRType::Pointer, targetValue);

      function.addInstruction(
         spec.transfer == MethodTransferKind::Call
            ? EIROpcode::CallIndirect
            : EIROpcode::IndirectBranch,
         spec.transfer == MethodTransferKind::Call
            ? callEffects
            : EIREffect::Terminator,
         noEIRValue(),
         targetOperand,
         1,
         0);
   }

   if (spec.transfer == MethodTransferKind::Call) {
      function.addInstruction(
         EIROpcode::Fallthrough,
         EIREffect::Terminator,
         noEIRValue(),
         function.operandCount(),
         0,
         0);
   }

   function.addBlock(0, 0, function.instructionCount());

   return EIRVerifier::verify(function);
}
