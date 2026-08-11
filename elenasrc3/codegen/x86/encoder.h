#ifndef CODEGEN_X86_ENCODER_H
#define CODEGEN_X86_ENCODER_H

#include "machine.h"

namespace elena_lang::codegen::x86
{
   typedef MachineRelocationKind RelocationKind;
   typedef MachineRelocation Relocation;

   struct LocalBranch
   {
      pos_t position;
      unsigned char label;
   };

   class Encoder
   {
      Architecture _architecture;
      TLSModel _tlsModel;
      MemoryWriter& _writer;

      CachedList<Relocation, 4> _relocations;
      CachedList<LocalBranch, 8> _branches;
      pos_t _labelPositions[32];
      unsigned int _labels;

      EncodeError write(unsigned char value);
      EncodeError write(const unsigned char* bytes, pos_t length);
      EncodeError writeDWord(unsigned int value);
      EncodeError writePrefix(Register reg, Register rm, OperandSize size);
      EncodeError writeMemoryPrefix(Register reg, Register base, Register index, OperandSize size);
      EncodeError writeModRM(unsigned char mode, unsigned char reg, Register rm);
      EncodeError writeSIB(unsigned char scale, Register index, Register base);
      EncodeError writeDisplacement(int displacement);
      EncodeError emit(Instruction& instruction);
      EncodeError emitCurrentThread(Instruction& instruction);
      EncodeError emitThreadLocal(Instruction& instruction, bool load);
      EncodeError emitStoreScaledIndex(Instruction& instruction);
      EncodeError emitLoadMemory(Instruction& instruction);

   public:
      EncodeError emit(
         Sequence& sequence,
         const ManagedABI& abi,
         const RuntimeCallABI* runtimeABI = nullptr);
      pos_t relocationCount() const;
      Relocation& relocation(pos_t index);

      Encoder(Architecture architecture, MemoryWriter& writer);
      Encoder(const TargetSpec& target, MemoryWriter& writer);
   };
}

#endif
