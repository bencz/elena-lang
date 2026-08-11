#include "machine.h"

using namespace elena_lang;
using namespace elena_lang::codegen;
using namespace elena_lang::codegen::x86;

bool Operand :: isValid(Architecture architecture) const
{
   unsigned char value = (unsigned char)reg;
   if (reg == Register::None || size == OperandSize::None || kind == ValueKind::None)
      return false;
   if (value > (unsigned char)Register::R15)
      return false;
   if (architecture == Architecture::X86)
      return value < 8 && size != OperandSize::QWord;

   return architecture == Architecture::AMD64;
}

void Sequence :: clear()
{
   _instructions.clear();
}

void Sequence :: add(Instruction instruction)
{
   _instructions.add(instruction);
}

pos_t Sequence :: count() const
{
   return _instructions.count_pos();
}

Instruction& Sequence :: instruction(pos_t index)
{
   return _instructions.get(index);
}

static bool isEmpty(const Operand& operand)
{
   return operand.reg == Register::None
      && operand.size == OperandSize::None
      && operand.kind == ValueKind::None;
}

static MIREffect requiredEffects(Instruction& instruction, const RuntimeCallABI* runtimeABI)
{
   switch (instruction.opcode) {
      case Opcode::InitializeFPU:
         return MIREffect::WriteFPUState;
      case Opcode::Clear:
         return MIREffect::WriteFlags;
      case Opcode::Push:
         return MIREffect::WriteMemory;
      case Opcode::Pop:
         return MIREffect::ReadMemory;
      case Opcode::Negate:
      case Opcode::AddImmediate:
      case Opcode::SubtractImmediate:
      case Opcode::Test:
      case Opcode::ShiftLeftImmediate:
      case Opcode::AndImmediate:
      case Opcode::OrImmediate:
      case Opcode::CompareImmediate:
      case Opcode::MultiplyImmediate:
      case Opcode::DivideUnsigned:
      case Opcode::Add:
      case Opcode::Subtract:
      case Opcode::Compare:
      case Opcode::And:
      case Opcode::Or:
      case Opcode::ShiftRightImmediate:
         return MIREffect::WriteFlags;
      case Opcode::AddCarryImmediate:
         return MIREffect::ReadFlags | MIREffect::WriteFlags;
      case Opcode::ConditionalMove:
         return MIREffect::ReadFlags;
      case Opcode::CallRuntime:
         return runtimeABI ? machineEffects(runtimeABI->effects) : MIREffect::None;
      case Opcode::CallCodeReference:
      case Opcode::CallVMTMethod:
      case Opcode::CallHMTMethod:
      case Opcode::CallRegister:
         return MIREffect::ReadMemory | MIREffect::WriteMemory
            | MIREffect::Call | MIREffect::Safepoint | MIREffect::MayThrow;
      case Opcode::Return:
         return MIREffect::ReadMemory;
      case Opcode::StoreOffset:
         return MIREffect::WriteMemory;
      case Opcode::LoadOffset:
      case Opcode::LoadZeroExtendByteOffset:
      case Opcode::LoadSignExtendWordOffset:
      case Opcode::LoadDWordOffset:
      case Opcode::LoadSignExtendDWordOffset:
      case Opcode::LoadZeroExtendWordOffset:
      case Opcode::LoadIndexedOffset:
      case Opcode::LoadMemory:
      case Opcode::LoadReferenceIndex:
         return MIREffect::ReadMemory;
      case Opcode::CompareMemory:
         return MIREffect::ReadMemory | MIREffect::WriteFlags;
      case Opcode::StoreDWordOffset:
      case Opcode::StoreReferenceIndex:
      case Opcode::StoreImmediateDWord:
      case Opcode::RepeatStore:
         return MIREffect::WriteMemory;
      case Opcode::RepeatMoveBytes:
         return MIREffect::ReadMemory | MIREffect::WriteMemory;
      case Opcode::LoadCurrentThread:
         return MIREffect::ReadMemory | MIREffect::ReadTLS;
      case Opcode::LoadThreadLocal:
         return MIREffect::ReadMemory | MIREffect::ReadTLS;
      case Opcode::StoreThreadLocal:
         return MIREffect::WriteMemory | MIREffect::ReadTLS | MIREffect::WriteTLS;
      case Opcode::StoreScaledIndex:
         return instruction.immediate == 4
            ? MIREffect::WriteMemory | MIREffect::WriteFlags
            : MIREffect::WriteMemory;
      case Opcode::JumpNotEqual:
      case Opcode::JumpZero:
         return MIREffect::ReadFlags;
      case Opcode::AtomicCompareExchangeDWord:
      case Opcode::AtomicExchangeAddDWord:
      case Opcode::AtomicCompareExchangeByte:
      case Opcode::AtomicExchangeAddByte:
         return MIREffect::ReadMemory | MIREffect::WriteMemory
            | MIREffect::WriteFlags | MIREffect::Synchronize;
      default:
         return MIREffect::None;
   }
}

static MIRVerifyError verifyUnary(Instruction& instruction, const ManagedABI& abi)
{
   if (!instruction.destination.isValid(abi.architecture))
      return MIRVerifyError::InvalidDestination;
   if (!isEmpty(instruction.source))
      return MIRVerifyError::InvalidSource;
   if (instruction.destination.kind != ValueKind::Integer)
      return MIRVerifyError::InvalidOperandKind;
   if (instruction.immediate != 0)
      return MIRVerifyError::InvalidImmediate;
   if (instruction.condition != Condition::None)
      return MIRVerifyError::InvalidCondition;

   return MIRVerifyError::None;
}

static MIRVerifyError verifyBinary(Instruction& instruction, const ManagedABI& abi)
{
   if (!instruction.destination.isValid(abi.architecture))
      return MIRVerifyError::InvalidDestination;
   if (!instruction.source.isValid(abi.architecture))
      return MIRVerifyError::InvalidSource;
   if (instruction.destination.size != instruction.source.size
      || instruction.destination.kind != instruction.source.kind)
   {
      return MIRVerifyError::InvalidOperandKind;
   }
   if (instruction.immediate != 0)
      return MIRVerifyError::InvalidImmediate;

   return MIRVerifyError::None;
}

MIRVerifyError MIRVerifier :: verify(
   Sequence& sequence,
   const ManagedABI& abi,
   const RuntimeCallABI* runtimeABI)
{
   if (!abi.isValid())
      return MIRVerifyError::InvalidABI;
   if (sequence.count() == 0)
      return MIRVerifyError::EmptySequence;

   unsigned int declaredLabels = 0;
   unsigned int usedLabels = 0;
   for (pos_t i = 0; i < sequence.count(); i++) {
      Instruction& instruction = sequence.instruction(i);
      MIRVerifyError error = MIRVerifyError::None;

      switch (instruction.opcode) {
         case Opcode::Nop:
         case Opcode::InitializeFPU:
            if (!isEmpty(instruction.destination))
               error = MIRVerifyError::InvalidDestination;
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;

         case Opcode::Move:
         case Opcode::Test:
         case Opcode::And:
         case Opcode::Or:
         case Opcode::Add:
         case Opcode::Subtract:
         case Opcode::Compare:
            error = verifyBinary(instruction, abi);

            if ((instruction.opcode == Opcode::And
               || instruction.opcode == Opcode::Or
               || instruction.opcode == Opcode::Add
               || instruction.opcode == Opcode::Subtract)
               && instruction.destination.kind != ValueKind::Integer)
            {
               error = MIRVerifyError::InvalidOperandKind;
            }

            if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;

         case Opcode::Clear:
            error = verifyUnary(instruction, abi);
            break;

         case Opcode::Push:
         case Opcode::Pop:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::ConditionalMove:
            error = verifyBinary(instruction, abi);
            if (instruction.condition == Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::BitNot:
         case Opcode::Negate:
            error = verifyUnary(instruction, abi);
            break;
         case Opcode::AddImmediate:
         case Opcode::SubtractImmediate:
         case Opcode::AddCarryImmediate:
            if (!instruction.destination.isValid(abi.architecture))
               error = MIRVerifyError::InvalidDestination;
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.destination.kind != ValueKind::Integer
               && ((instruction.opcode != Opcode::AddImmediate
                     && instruction.opcode != Opcode::SubtractImmediate)
                  || (instruction.destination.kind != ValueKind::Reference
                     && instruction.destination.kind != ValueKind::Address)))
            {
               error = MIRVerifyError::InvalidOperandKind;
            }
            else if (instruction.opcode == Opcode::AddCarryImmediate
               && (instruction.immediate < -128 || instruction.immediate > 127))
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::SignExtend:
            error = verifyBinary(instruction, abi);
            if (error == MIRVerifyError::None
               && (instruction.destination.reg != abi.wideHigh
                  || instruction.source.reg != abi.wideLow
                  || instruction.destination.kind != ValueKind::Integer
                  || instruction.condition != Condition::None))
            {
               error = MIRVerifyError::InvalidOperandKind;
            }
            break;
         case Opcode::SignExtendDWord:
            if (abi.architecture != Architecture::AMD64
               || !instruction.destination.isValid(abi.architecture)
               || instruction.destination.kind != ValueKind::Integer
               || instruction.destination.size != OperandSize::QWord)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.kind != ValueKind::Integer
               || instruction.source.size != OperandSize::DWord
               || instruction.source.reg != instruction.destination.reg)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::MoveImmediate:
         case Opcode::MoveReference:
         case Opcode::MoveReferenceValue:
         case Opcode::MoveMessage:
         case Opcode::MoveMetadata:
         case Opcode::MoveRuntimeConstant:
         case Opcode::MoveLabelAddress:
         case Opcode::MoveReferenceAddress:
         case Opcode::MoveVMTMethodOffset:
         case Opcode::MoveHMTMethodOffset:
            if (!instruction.destination.isValid(abi.architecture))
               error = MIRVerifyError::InvalidDestination;
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.opcode == Opcode::MoveMessage
               ? instruction.destination.kind != ValueKind::Integer
                  || instruction.destination.size != OperandSize::DWord
               : instruction.opcode == Opcode::MoveVMTMethodOffset
                  || instruction.opcode == Opcode::MoveHMTMethodOffset
               ? instruction.destination.kind != ValueKind::Integer
                  || instruction.destination.size != OperandSize::DWord
               : instruction.opcode == Opcode::MoveImmediate
               ? instruction.destination.kind == ValueKind::Integer
                  ? instruction.destination.size != OperandSize::DWord
                     && instruction.destination.size != abi.wordSize
                  : instruction.destination.kind != ValueKind::Reference
                     || instruction.destination.size != abi.wordSize
               : instruction.opcode == Opcode::MoveLabelAddress
                  || instruction.opcode == Opcode::MoveReferenceAddress
               ? instruction.destination.kind != ValueKind::Address
                  || instruction.destination.size != abi.wordSize
               : instruction.destination.size != abi.wordSize)
               error = MIRVerifyError::InvalidOperandKind;
            else if (instruction.opcode == Opcode::MoveMessage
               || instruction.opcode == Opcode::MoveVMTMethodOffset
               || instruction.opcode == Opcode::MoveHMTMethodOffset
               ? instruction.destination.kind != ValueKind::Integer
               : instruction.opcode == Opcode::MoveLabelAddress
                  || instruction.opcode == Opcode::MoveReferenceAddress
               ? instruction.destination.kind != ValueKind::Address
               : instruction.opcode == Opcode::MoveImmediate
               ? instruction.destination.kind != ValueKind::Integer
                  && instruction.destination.kind != ValueKind::Reference
               : instruction.destination.kind != ValueKind::VMT
                  && instruction.destination.kind != ValueKind::Reference)
            {
               error = MIRVerifyError::InvalidOperandKind;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            else if ((instruction.opcode == Opcode::MoveVMTMethodOffset
                  || instruction.opcode == Opcode::MoveHMTMethodOffset)
               && instruction.immediate == 0)
            {
               error = MIRVerifyError::InvalidImmediate;
            }
            break;
         case Opcode::LoadMemory:
         case Opcode::CompareMemory:
            if (!instruction.destination.isValid(abi.architecture))
               error = MIRVerifyError::InvalidDestination;
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.size != instruction.destination.size)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.index != Register::None
               && ((unsigned char)instruction.index >= (abi.architecture
                  == Architecture::X86 ? 8 : 16)
                  || instruction.index == Register::SP))
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.scale > 3)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.opcode == Opcode::CompareMemory
               && instruction.destination.kind != ValueKind::Integer)
            {
               error = MIRVerifyError::InvalidOperandKind;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::LoadCurrentThread:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != Register::A
               || (instruction.destination.kind != ValueKind::Reference
                  && instruction.destination.kind != ValueKind::Address)
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate <= 0 || instruction.immediate > 0x7F)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::MoveRuntimeData:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.kind != ValueKind::Address
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate
               < (int)RuntimeDataReference::ThreadTableSlots
               || instruction.immediate > (int)RuntimeDataReference::SystemEnvironment)
            {
               error = MIRVerifyError::InvalidImmediate;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::CallRuntime:
            if (!runtimeABI)
               error = MIRVerifyError::InvalidRuntimeABI;
            else if (runtimeABI->result == Register::None
               ? !isEmpty(instruction.destination)
               : !instruction.destination.isValid(abi.architecture)
                  || instruction.destination.reg != runtimeABI->result
                  || instruction.destination.kind != ValueKind::Reference)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (runtimeABI->argument0 == Register::None
               ? !isEmpty(instruction.source)
               : !instruction.source.isValid(abi.architecture)
                  || instruction.source.reg != runtimeABI->argument0
                  || instruction.source.kind != ValueKind::Integer)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate != (int)runtimeABI->operation)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::StoreOffset:
            if (!instruction.destination.isValid(abi.architecture)
               || (instruction.destination.kind != ValueKind::Reference
                  && instruction.destination.kind != ValueKind::Address))
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.size != instruction.destination.size)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::AddressOffset:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.kind != ValueKind::Reference)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate < -128 || instruction.immediate > 127)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::AddressOffsetFrom:
         case Opcode::AddressScaledIndex:
            if (!instruction.destination.isValid(abi.architecture)
               || (instruction.destination.kind != ValueKind::Reference
                  && instruction.destination.kind != ValueKind::Address)
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.kind != ValueKind::Address
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::LoadOffset:
            if (!instruction.destination.isValid(abi.architecture)
               || (instruction.destination.kind != ValueKind::Integer
                  && instruction.destination.kind != ValueKind::VMT
                  && instruction.destination.kind != ValueKind::Reference
                  && instruction.destination.kind != ValueKind::Address))
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || (instruction.source.kind != ValueKind::Reference
                  && instruction.source.kind != ValueKind::Address
                  && instruction.source.kind != ValueKind::VMT)
               || instruction.source.size != instruction.destination.size)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::LoadZeroExtendByteOffset:
         case Opcode::LoadSignExtendWordOffset:
         case Opcode::LoadZeroExtendWordOffset:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.kind != ValueKind::Integer
               || instruction.destination.size != OperandSize::DWord)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.kind != ValueKind::Reference
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::LoadIndexedOffset:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.kind != ValueKind::Integer)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.kind != ValueKind::Reference
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::StoreReferenceIndex:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.kind != ValueKind::Reference
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.kind != ValueKind::Reference
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::StoreScaledIndex:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != Register::DI
               || instruction.destination.kind != ValueKind::Address
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.kind != ValueKind::Reference
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate < 0 || instruction.immediate > 4)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::Label:
         case Opcode::Jump:
         case Opcode::JumpNotEqual:
         case Opcode::JumpZero:
            if (!isEmpty(instruction.destination))
               error = MIRVerifyError::InvalidDestination;
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate < 0 || instruction.immediate >= 32)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            else {
               unsigned int label = 1u << instruction.immediate;
               if (instruction.opcode == Opcode::Label) {
                  if ((declaredLabels & label) != 0)
                     error = MIRVerifyError::InvalidImmediate;
                  else declaredLabels |= label;
               }
               else usedLabels |= label;
            }
            break;
         case Opcode::JumpRegister:
         case Opcode::CallRegister:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.size != abi.wordSize
               || instruction.destination.kind != ValueKind::Address)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::Return:
            if (!isEmpty(instruction.destination))
               error = MIRVerifyError::InvalidDestination;
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::CallCodeReference:
         case Opcode::CallVMTMethod:
         case Opcode::CallHMTMethod:
         case Opcode::JumpVMTMethod:
         case Opcode::JumpHMTMethod:
            if (!isEmpty(instruction.destination))
               error = MIRVerifyError::InvalidDestination;
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate == 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::AtomicCompareExchangeDWord:
         case Opcode::AtomicExchangeAddDWord:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != Register::DI
               || instruction.destination.kind != ValueKind::Address
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.reg != Register::C
               || instruction.source.kind != ValueKind::Integer
               || instruction.source.size != OperandSize::DWord)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate < -128 || instruction.immediate > 127)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::AtomicCompareExchangeByte:
         case Opcode::AtomicExchangeAddByte:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != abi.object
               || instruction.destination.kind != ValueKind::Address
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.reg != Register::C
               || instruction.source.kind != ValueKind::Integer
               || instruction.source.size != OperandSize::DWord)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate < -128 || instruction.immediate > 127)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::LoadReferenceIndex:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.kind != ValueKind::Reference
               || instruction.destination.size != abi.wordSize
               || !instruction.source.isValid(abi.architecture)
               || instruction.source.kind != ValueKind::Reference
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::LoadThreadLocal:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != abi.object
               || instruction.destination.kind != ValueKind::Reference
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.immediate == 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::StoreThreadLocal:
            if (!isEmpty(instruction.destination))
               error = MIRVerifyError::InvalidDestination;
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.reg != abi.object
               || instruction.source.kind != ValueKind::Reference
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate == 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::StoreImmediateDWord:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != abi.object
               || instruction.destination.kind != ValueKind::Reference
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::LoadDWordOffset:
         case Opcode::LoadSignExtendDWordOffset:
            if (abi.architecture != Architecture::AMD64
               || !instruction.destination.isValid(abi.architecture)
               || instruction.destination.kind != ValueKind::Integer
               || instruction.destination.size != (instruction.opcode
                  == Opcode::LoadDWordOffset ? OperandSize::DWord : OperandSize::QWord))
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || (instruction.source.kind != ValueKind::Reference
                  && instruction.source.kind != ValueKind::Address)
               || instruction.source.size != OperandSize::QWord)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::StoreDWordOffset:
            if (abi.architecture != Architecture::AMD64
               || !instruction.destination.isValid(abi.architecture)
               || (instruction.destination.kind != ValueKind::Reference
                  && instruction.destination.kind != ValueKind::Address)
               || instruction.destination.size != OperandSize::QWord)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.kind != ValueKind::Integer
               || instruction.source.size != OperandSize::DWord)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::RepeatStore:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != Register::DI
               || (instruction.destination.kind != ValueKind::Reference
                  && instruction.destination.kind != ValueKind::Address)
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.reg != Register::A
               || (instruction.source.kind != ValueKind::Reference
                  && instruction.source.kind != ValueKind::Integer)
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::RepeatMoveBytes:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != Register::DI
               || (instruction.destination.kind != ValueKind::Reference
                  && instruction.destination.kind != ValueKind::Address)
               || instruction.destination.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.reg != Register::SI
               || (instruction.source.kind != ValueKind::Reference
                  && instruction.source.kind != ValueKind::Address)
               || instruction.source.size != abi.wordSize)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::ShiftLeftImmediate:
         case Opcode::ShiftRightImmediate:
            if (!instruction.destination.isValid(abi.architecture))
               error = MIRVerifyError::InvalidDestination;
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.destination.kind != ValueKind::Integer)
               error = MIRVerifyError::InvalidOperandKind;
            else if (instruction.immediate < 0 || instruction.immediate > 31)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::AndImmediate:
         case Opcode::OrImmediate:
         case Opcode::CompareImmediate:
            if (!instruction.destination.isValid(abi.architecture))
               error = MIRVerifyError::InvalidDestination;
            else if (!isEmpty(instruction.source))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.destination.kind != ValueKind::Integer)
               error = MIRVerifyError::InvalidOperandKind;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::MultiplyImmediate:
            if (!instruction.destination.isValid(abi.architecture))
               error = MIRVerifyError::InvalidDestination;
            else if (!instruction.source.isValid(abi.architecture))
               error = MIRVerifyError::InvalidSource;
            else if (instruction.destination.size != instruction.source.size
               || instruction.destination.kind != ValueKind::Integer
               || instruction.source.kind != ValueKind::Integer)
            {
               error = MIRVerifyError::InvalidOperandKind;
            }
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         case Opcode::DivideUnsigned:
            if (!instruction.destination.isValid(abi.architecture)
               || instruction.destination.reg != abi.wideLow
               || instruction.destination.size != OperandSize::DWord
               || instruction.destination.kind != ValueKind::Integer)
            {
               error = MIRVerifyError::InvalidDestination;
            }
            else if (!instruction.source.isValid(abi.architecture)
               || instruction.source.size != OperandSize::DWord
               || instruction.source.kind != ValueKind::Integer)
            {
               error = MIRVerifyError::InvalidSource;
            }
            else if (instruction.immediate != 0)
               error = MIRVerifyError::InvalidImmediate;
            else if (instruction.condition != Condition::None)
               error = MIRVerifyError::InvalidCondition;
            break;
         default:
            error = MIRVerifyError::InvalidOpcode;
            break;
      }
      if (error != MIRVerifyError::None)
         return error;
      if (instruction.effects != requiredEffects(instruction, runtimeABI))
         return MIRVerifyError::InvalidEffects;
   }

   return (usedLabels & ~declaredLabels) == 0
      ? MIRVerifyError::None : MIRVerifyError::InvalidImmediate;
}
