#ifndef CODEGEN_X86_ISA_H
#define CODEGEN_X86_ISA_H

namespace elena_lang::codegen::x86
{
   enum class Register : unsigned char
   {
      A,
      C,
      D,
      B,
      SP,
      BP,
      SI,
      DI,
      R8,
      R9,
      R10,
      R11,
      R12,
      R13,
      R14,
      R15,
      None = 0xFF
   };

   typedef unsigned int RegisterMask;

   inline RegisterMask mask(Register reg)
   {
      return reg == Register::None ? 0 : 1u << (unsigned char)reg;
   }

   enum class OperandSize : unsigned char
   {
      Byte,
      Word,
      DWord,
      QWord,
      None = 0xFF
   };

   enum class Opcode : unsigned char
   {
      Nop,
      InitializeFPU,
      Move,
      Clear,
      Push,
      Pop,
      Add,
      Subtract,
      Compare,
      BitNot,
      Negate,
      AddImmediate,
      SubtractImmediate,
      AddCarryImmediate,
      Test,
      ConditionalMove,
      SignExtend,
      MoveImmediate,
      MoveReference,
      MoveReferenceValue,
      MoveMessage,
      MoveMetadata,
      MoveRuntimeConstant,
      MoveLabelAddress,
      MoveReferenceAddress,
      MoveVMTMethodOffset,
      MoveHMTMethodOffset,
      CallRuntime,
      CallCodeReference,
      CallVMTMethod,
      CallHMTMethod,
      JumpVMTMethod,
      JumpHMTMethod,
      CallRegister,
      StoreOffset,
      AddressOffset,
      LoadOffset,
      ShiftLeftImmediate,
      AndImmediate,
      OrImmediate,
      CompareImmediate,
      MultiplyImmediate,
      And,
      Or,
      ShiftRightImmediate,
      LoadZeroExtendByteOffset,
      LoadSignExtendWordOffset,
      LoadDWordOffset,
      LoadSignExtendDWordOffset,
      StoreDWordOffset,
      SignExtendDWord,
      LoadZeroExtendWordOffset,
      LoadIndexedOffset,
      LoadMemory,
      CompareMemory,
      StoreReferenceIndex,
      LoadReferenceIndex,
      StoreImmediateDWord,
      DivideUnsigned,
      RepeatStore,
      RepeatMoveBytes,
      AddressOffsetFrom,
      AddressScaledIndex,
      LoadCurrentThread,
      LoadThreadLocal,
      StoreThreadLocal,
      MoveRuntimeData,
      StoreScaledIndex,
      Label,
      Jump,
      JumpNotEqual,
      JumpZero,
      JumpRegister,
      Return,
      AtomicCompareExchangeByte,
      AtomicExchangeAddByte,
      AtomicCompareExchangeDWord,
      AtomicExchangeAddDWord
   };

   enum class Condition : unsigned char
   {
      Equal = 4,
      NotEqual = 5,
      BelowEqual = 6,
      None = 0xFF
   };

   enum class EncodeError : unsigned char
   {
      None,
      InvalidArchitecture,
      InvalidRegister,
      InvalidOperandSize,
      InvalidOpcode,
      InvalidOperand,
      InvalidCondition,
      InvalidEffects,
      WriteFailed
   };
}

#endif
