#ifndef CODEGEN_X86_MACHINE_H
#define CODEGEN_X86_MACHINE_H

#include "common.h"
#include "../machine.h"
#include "abi.h"

namespace elena_lang::codegen::x86
{
   typedef MachineValueKind ValueKind;
   typedef MachineEffect MIREffect;

   struct Operand
   {
      Register    reg;
      OperandSize size;
      ValueKind   kind;

      bool isValid(Architecture architecture) const;
   };

   struct Instruction
   {
      Opcode      opcode;
      Operand     destination;
      Operand     source;
      int         immediate;
      Condition   condition;
      MIREffect   effects;
      Register    index = Register::None;
      unsigned char scale = 0;
   };

   enum class MIRVerifyError : unsigned char
   {
      None,
      EmptySequence,
      InvalidABI,
      InvalidOpcode,
      InvalidDestination,
      InvalidSource,
      InvalidImmediate,
      InvalidCondition,
      InvalidEffects,
      InvalidOperandKind,
      InvalidRuntimeABI
   };

   class Sequence
   {
      CachedList<Instruction, 8> _instructions;

   public:
      void clear();
      void add(Instruction instruction);
      pos_t count() const;
      Instruction& instruction(pos_t index);
   };

   class MIRVerifier
   {
   public:
      static MIRVerifyError verify(
         Sequence& sequence,
         const ManagedABI& abi,
         const RuntimeCallABI* runtimeABI = nullptr);
   };
}

#endif
