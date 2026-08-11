   #include "encoder.h"

using namespace elena_lang;
using namespace elena_lang::codegen;
using namespace elena_lang::codegen::x86;

Encoder :: Encoder(Architecture architecture, MemoryWriter& writer)
   : _architecture(architecture), _tlsModel(TLSModel::None), _writer(writer),
     _labelPositions{}, _labels(0)
{
}

Encoder :: Encoder(const TargetSpec& target, MemoryWriter& writer)
   : _architecture(target.architecture), _tlsModel(target.tlsModel), _writer(writer),
     _labelPositions{}, _labels(0)
{
}

EncodeError Encoder :: write(unsigned char value)
{
   return _writer.writeByte(value) ? EncodeError::None : EncodeError::WriteFailed;
}

EncodeError Encoder :: write(const unsigned char* bytes, pos_t length)
{
   return _writer.write(bytes, length) ? EncodeError::None : EncodeError::WriteFailed;
}

EncodeError Encoder :: writeDWord(unsigned int value)
{
   return _writer.writeDWord(value) ? EncodeError::None : EncodeError::WriteFailed;
}

EncodeError Encoder :: writePrefix(Register reg, Register rm, OperandSize size)
{
   unsigned char regValue = (unsigned char)reg;
   unsigned char rmValue = (unsigned char)rm;
   if (_architecture == Architecture::X86) {
      if ((reg != Register::None && regValue >= 8) || rmValue >= 8
         || size == OperandSize::QWord)
      {
         return EncodeError::InvalidArchitecture;
      }
      if (size == OperandSize::Byte
         && ((reg != Register::None && regValue >= 4) || rmValue >= 4))
      {
         return EncodeError::InvalidRegister;
      }
      if (size == OperandSize::Word)
         return write(0x66); // operand-size override

      return EncodeError::None;
   }
   if (_architecture != Architecture::AMD64)
      return EncodeError::InvalidArchitecture;

   if (size == OperandSize::Word) {
      EncodeError error = write(0x66); // operand-size override
      if (error != EncodeError::None)
         return error;
   }

   unsigned char rex = 0x40; // REX
   if (size == OperandSize::QWord)
      rex |= 0x08; // REX.W
   if (reg != Register::None && regValue >= 8)
      rex |= 0x04; // REX.R
   if (rmValue >= 8)
      rex |= 0x01; // REX.B
   if (rex != 0x40 || (size == OperandSize::Byte
      && ((reg != Register::None && regValue >= 4) || rmValue >= 4)))
   {
      return write(rex);
   }

   return EncodeError::None;
}

EncodeError Encoder :: writeMemoryPrefix(Register reg, Register base,
   Register index, OperandSize size)
{
   if (_architecture == Architecture::X86) {
      if ((unsigned char)reg >= 8 || (unsigned char)base >= 8
         || (index != Register::None && (unsigned char)index >= 8)
         || size == OperandSize::QWord)
      {
         return EncodeError::InvalidArchitecture;
      }
      if (size == OperandSize::Word)
         return write(0x66); // operand-size override

      return EncodeError::None;
   }
   if (_architecture != Architecture::AMD64)
      return EncodeError::InvalidArchitecture;

   if (size == OperandSize::Word) {
      EncodeError error = write(0x66); // operand-size override
      if (error != EncodeError::None)
         return error;
   }

   unsigned char rex = 0x40; // REX
   if (size == OperandSize::QWord)
      rex |= 0x08; // REX.W
   if ((unsigned char)reg >= 8)
      rex |= 0x04; // REX.R
   if (index != Register::None && (unsigned char)index >= 8)
      rex |= 0x02; // REX.X
   if ((unsigned char)base >= 8)
      rex |= 0x01; // REX.B

   return rex == 0x40 ? EncodeError::None : write(rex);
}

EncodeError Encoder :: writeModRM(unsigned char mode, unsigned char reg, Register rm)
{
   // ModRM: mode[7:6], register/opcode[5:3], register/memory[2:0]
   return write((mode << 6) | ((reg & 0x07) << 3) | ((unsigned char)rm & 0x07));
}

EncodeError Encoder :: writeSIB(unsigned char scale, Register index, Register base)
{
   // SIB: scale[7:6], index[5:3], base[2:0]
   return write(((scale & 0x03) << 6)
      | (((unsigned char)index & 0x07) << 3)
      | ((unsigned char)base & 0x07));
}

EncodeError Encoder :: writeDisplacement(int displacement)
{
   return displacement >= -128 && displacement <= 127
      ? write((unsigned char)displacement)
      : writeDWord((unsigned int)displacement);
}

EncodeError Encoder :: emitCurrentThread(Instruction& instruction)
{
   if (_architecture == Architecture::X86) {
      if (_tlsModel == TLSModel::Windows) {
         static const unsigned char code[] = {
            0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00, // mov eax, [fs:0x2C]; Windows TLS array
            0x8B, 0x00                          // mov eax, [eax]; ELENA thread content
         };

         return write(code, sizeof(code));
      }

      if (_tlsModel == TLSModel::ELF) {
         static const unsigned char code[] = {
            0x65, 0xA1, 0x00, 0x00, 0x00, 0x00, // mov eax, [gs:0]; ELF thread pointer
            0x83, 0xE8                          // sub eax, thread_content_size
         };

         EncodeError error = write(code, sizeof(code));
         if (error != EncodeError::None)
            return error;

         return write((unsigned char)instruction.immediate);
      }

      return EncodeError::InvalidArchitecture;
   }

   if (_architecture == Architecture::AMD64) {
      if (_tlsModel == TLSModel::Windows) {
         static const unsigned char code[] = {
            0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, // mov rax, [gs:0x58]; Windows TLS array
            0x48, 0x8B, 0x00                                      // mov rax, [rax]; ELENA thread content
         };

         return write(code, sizeof(code));
      }

      if (_tlsModel == TLSModel::ELF) {
         static const unsigned char code[] = {
            0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov rax, [fs:0]; ELF thread pointer
            0x48, 0x83, 0xE8                                      // sub rax, thread_content_size
         };

         EncodeError error = write(code, sizeof(code));
         if (error != EncodeError::None)
            return error;

         return write((unsigned char)instruction.immediate);
      }
   }

   return EncodeError::InvalidArchitecture;
}

EncodeError Encoder :: emitThreadLocal(Instruction& instruction, bool load)
{
   EncodeError error = EncodeError::None;

   if (_architecture == Architecture::X86) {
      if (_tlsModel == TLSModel::Windows) {
         static const unsigned char tlsBase[] = {
            0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00,
            0x8B, 0x00,
            0x05
         };
         if ((error = write(tlsBase, sizeof(tlsBase))) != EncodeError::None)
            return error;
      }
      else if (_tlsModel == TLSModel::ELF) {
         static const unsigned char tlsBase[] = {
            0x65, 0xA1, 0x00, 0x00, 0x00, 0x00,
            0x2D
         };
         if ((error = write(tlsBase, sizeof(tlsBase))) != EncodeError::None)
            return error;
      }
      else {
         return EncodeError::InvalidArchitecture;
      }
   }
   else if (_architecture == Architecture::AMD64) {
      if (_tlsModel == TLSModel::Windows) {
         static const unsigned char tlsBase[] = {
            0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0x00,
            0x48, 0x05
         };
         if ((error = write(tlsBase, sizeof(tlsBase))) != EncodeError::None)
            return error;
      }
      else if (_tlsModel == TLSModel::ELF) {
         static const unsigned char tlsBase[] = {
            0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x2D
         };
         if ((error = write(tlsBase, sizeof(tlsBase))) != EncodeError::None)
            return error;
      }
      else {
         return EncodeError::InvalidArchitecture;
      }
   }
   else {
      return EncodeError::InvalidArchitecture;
   }

   _relocations.add({
      RelocationKind::ThreadLocalOffset,
      _writer.position(),
      (unsigned int)instruction.immediate
   });
   if ((error = writeDWord(0)) != EncodeError::None)
      return error;

   if (_tlsModel == TLSModel::ELF) {
      if (_architecture == Architecture::X86) {
         static const unsigned char previousSlot[] = { 0x83, 0xE8, 0x04 };
         if ((error = write(previousSlot, sizeof(previousSlot))) != EncodeError::None)
            return error;
      }
      else {
         static const unsigned char previousSlot[] = { 0x48, 0x83, 0xE8, 0x08 };
         if ((error = write(previousSlot, sizeof(previousSlot))) != EncodeError::None)
            return error;
      }
   }

   if (_architecture == Architecture::AMD64
      && (error = write(0x48)) != EncodeError::None)
   {
      return error;
   }
   if ((error = write(load ? 0x8B : 0x89)) != EncodeError::None)
      return error;

   return write(0x18);
}

EncodeError Encoder :: emitStoreScaledIndex(Instruction& instruction)
{
   EncodeError error;
   if (instruction.immediate == 4) {
      if (_architecture != Architecture::AMD64)
         return EncodeError::InvalidOperand;
      if ((error = writePrefix(Register::None, Register::D,
         instruction.destination.size)) != EncodeError::None
         || (error = write(0xC1)) != EncodeError::None // C1 /4 ib: shl r/m, imm8
         || (error = writeModRM(3, 4, Register::D)) != EncodeError::None
         || (error = write(4)) != EncodeError::None)
      {
         return error;
      }
   }

   if ((error = writePrefix(instruction.source.reg,
      instruction.destination.reg, instruction.destination.size)) != EncodeError::None
      || (error = write(0x89)) != EncodeError::None // 89 /r: mov r/m, r
      || (error = writeModRM(0, (unsigned char)instruction.source.reg,
         Register::SP)) != EncodeError::None
      || (error = writeSIB(instruction.immediate == 4 ? 0
         : (unsigned char)instruction.immediate, Register::D,
         instruction.destination.reg)) != EncodeError::None)
   {
      return error;
   }

   if (instruction.immediate != 4)
      return EncodeError::None;

   if ((error = writePrefix(Register::None, Register::D,
      instruction.destination.size)) != EncodeError::None
      || (error = write(0xC1)) != EncodeError::None // C1 /5 ib: shr r/m, imm8
      || (error = writeModRM(3, 5, Register::D)) != EncodeError::None)
   {
      return error;
   }
   return write(4);
}

EncodeError Encoder :: emitLoadMemory(Instruction& instruction)
{
   Register destination = instruction.destination.reg;
   Register base = instruction.source.reg;
   Register index = instruction.index;
   int displacement = instruction.immediate;

   EncodeError error = writeMemoryPrefix(destination, base, index,
      instruction.destination.size);
   if (error != EncodeError::None
      || (error = write(instruction.opcode == Opcode::CompareMemory
         ? 0x3B : 0x8B)) != EncodeError::None) // 3B /r cmp register, memory; 8B /r mov register, memory
   {
      return error;
   }

   bool requiresSIB = index != Register::None
      || base == Register::SP || base == Register::R12;
   bool forcedDisplacement = base == Register::BP || base == Register::R13;
   unsigned char mode = displacement == 0 && !forcedDisplacement
      ? 0 : displacement >= -128 && displacement <= 127 ? 1 : 2;
   Register encodedBase = requiresSIB ? Register::SP : base;
   if ((error = writeModRM(mode, (unsigned char)destination, encodedBase))
      != EncodeError::None)
   {
      return error;
   }
   if (requiresSIB && (error = writeSIB(instruction.scale,
      index == Register::None ? Register::SP : index, base)) != EncodeError::None)
   {
      return error;
   }

   return mode == 0 ? EncodeError::None : writeDisplacement(displacement);
}

EncodeError Encoder :: emit(Instruction& instruction)
{
   Register destination = instruction.destination.reg;
   Register source = instruction.source.reg;
   OperandSize size = instruction.destination.size;
   unsigned char group = 0;
   unsigned char opcode = 0;
   EncodeError error;

   switch (instruction.opcode) {
      case Opcode::Nop:
         return EncodeError::None;
      case Opcode::InitializeFPU:
         if ((error = write(0x9B)) != EncodeError::None // fwait
            || (error = write(0xDB)) != EncodeError::None) // DB E3: fninit
         {
            return error;
         }
         return write(0xE3); // DB E3: fninit
      case Opcode::Move:
      case Opcode::And:
      case Opcode::Or:
      case Opcode::Add:
      case Opcode::Subtract:
      case Opcode::Compare:
         error = writePrefix(source, destination, size);
         if (error != EncodeError::None)
            return error;

         // 21 /r and, 09 /r or, 01 /r add, 29 /r sub, 39 /r cmp, 88/89 /r mov
         if ((error = write(instruction.opcode == Opcode::And ? 0x21
            : instruction.opcode == Opcode::Or ? 0x09
            : instruction.opcode == Opcode::Add ? 0x01
            : instruction.opcode == Opcode::Subtract ? 0x29
            : instruction.opcode == Opcode::Compare ? 0x39
            : size == OperandSize::Byte ? 0x88 : 0x89)) != EncodeError::None)
            return error;
         return writeModRM(3, (unsigned char)source, destination);
      case Opcode::Clear:
         error = writePrefix(destination, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x31)) != EncodeError::None) // 31 /r: xor r/m, r
            return error;
         return writeModRM(3, (unsigned char)destination, destination);
      case Opcode::Push:
      case Opcode::Pop:
         error = writePrefix(Register::None, destination, OperandSize::DWord);
         if (error != EncodeError::None)
            return error;

         // 50+rd push register, 58+rd pop register
         return write((instruction.opcode == Opcode::Push ? 0x50 : 0x58)
            + ((unsigned char)destination & 0x07));
      case Opcode::BitNot:
      case Opcode::Negate:
         group = instruction.opcode == Opcode::BitNot ? 2 : 3;
         error = writePrefix(Register::None, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(size == OperandSize::Byte ? 0xF6 : 0xF7)) != EncodeError::None) // F6/F7 /2 not, /3 neg
            return error;
         return writeModRM(3, group, destination);
      case Opcode::AddImmediate:
      case Opcode::SubtractImmediate:
      case Opcode::AddCarryImmediate:
         group = instruction.opcode == Opcode::AddImmediate ? 0
            : instruction.opcode == Opcode::SubtractImmediate ? 5 : 2;
         error = writePrefix(Register::None, destination, size);
         if (error != EncodeError::None)
            return error;

         // 80/81/83 with group /0 add, /5 sub, /2 adc
         opcode = size == OperandSize::Byte ? 0x80
            : instruction.immediate >= -128 && instruction.immediate <= 127
               ? 0x83 : 0x81;
         if ((error = write(opcode)) != EncodeError::None
            || (error = writeModRM(3, group, destination)) != EncodeError::None)
         {
            return error;
         }
         return opcode == 0x81
            ? writeDWord((unsigned int)instruction.immediate)
            : write((unsigned char)instruction.immediate);
      case Opcode::Test:
         error = writePrefix(source, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(size == OperandSize::Byte ? 0x84 : 0x85)) != EncodeError::None) // 84/85 /r: test r/m, r
            return error;
         return writeModRM(3, (unsigned char)source, destination);
      case Opcode::ConditionalMove:
         if (size == OperandSize::Byte)
            return EncodeError::InvalidOperandSize;
         error = writePrefix(destination, source, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x0F)) != EncodeError::None // 0F 4x: cmovcc
            || (error = write(0x40 + (unsigned char)instruction.condition)) != EncodeError::None) // cmovcc condition opcode
         {
            return error;
         }
         return writeModRM(3, (unsigned char)destination, source);
      case Opcode::SignExtend:
         if (size != OperandSize::DWord && size != OperandSize::QWord)
            return EncodeError::InvalidOperandSize;
         if (size == OperandSize::QWord
            && (error = write(0x48)) != EncodeError::None) // REX.W
         {
            return error;
         }
         return write(0x99); // cdq or cqo
      case Opcode::SignExtendDWord:
         error = writePrefix(destination, source, OperandSize::QWord);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x63)) != EncodeError::None) // 63 /r: movsxd r64, r/m32
            return error;
         return writeModRM(3, (unsigned char)destination, source);
      case Opcode::MoveImmediate:
      case Opcode::MoveReference:
      case Opcode::MoveReferenceValue:
      case Opcode::MoveMessage:
      case Opcode::MoveMetadata:
      case Opcode::MoveRuntimeConstant:
      case Opcode::MoveLabelAddress:
      case Opcode::MoveReferenceAddress:
      case Opcode::MoveRuntimeData:
      case Opcode::MoveVMTMethodOffset:
      case Opcode::MoveHMTMethodOffset:
         error = writePrefix(Register::None, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0xB8 + ((unsigned char)destination & 0x07))) // B8+rd: mov register, immediate
            != EncodeError::None)
         {
            return error;
         }
         if (instruction.opcode == Opcode::MoveReference
            || instruction.opcode == Opcode::MoveReferenceValue
            || instruction.opcode == Opcode::MoveMessage
            || instruction.opcode == Opcode::MoveMetadata
            || instruction.opcode == Opcode::MoveRuntimeConstant
            || instruction.opcode == Opcode::MoveLabelAddress
            || instruction.opcode == Opcode::MoveReferenceAddress
            || instruction.opcode == Opcode::MoveRuntimeData
            || instruction.opcode == Opcode::MoveVMTMethodOffset
            || instruction.opcode == Opcode::MoveHMTMethodOffset)
         {
            _relocations.add({
               instruction.opcode == Opcode::MoveReference
                  ? RelocationKind::ModuleReference
                  : instruction.opcode == Opcode::MoveReferenceValue
                     ? RelocationKind::ModuleReferenceValue
                  : instruction.opcode == Opcode::MoveRuntimeData
                     ? RelocationKind::RuntimeData
                  : instruction.opcode == Opcode::MoveLabelAddress
                     ? RelocationKind::ProcedureLabel
                  : instruction.opcode == Opcode::MoveReferenceAddress
                     ? RelocationKind::ModuleReferenceValue
                  : instruction.opcode == Opcode::MoveMetadata
                     ? RelocationKind::Metadata
                  : instruction.opcode == Opcode::MoveRuntimeConstant
                     ? RelocationKind::RuntimeConstant
                  : instruction.opcode == Opcode::MoveVMTMethodOffset
                     ? RelocationKind::VMTMethodOffset
                  : instruction.opcode == Opcode::MoveHMTMethodOffset
                     ? RelocationKind::HMTMethodOffset
                  : RelocationKind::ModuleMessage,
               _writer.position(),
               (unsigned int)instruction.immediate
            });
            if (instruction.opcode == Opcode::MoveMessage)
               return writeDWord(0);

            return size == OperandSize::QWord
               ? (_writer.writeQWord(0) ? EncodeError::None : EncodeError::WriteFailed)
               : writeDWord(0);
         }
         return size == OperandSize::QWord
            ? (_writer.writeQWord((unsigned long long)(long long)instruction.immediate)
               ? EncodeError::None : EncodeError::WriteFailed)
            : writeDWord((unsigned int)instruction.immediate);
      case Opcode::LoadCurrentThread:
         return emitCurrentThread(instruction);
      case Opcode::LoadThreadLocal:
         return emitThreadLocal(instruction, true);
      case Opcode::StoreThreadLocal:
         return emitThreadLocal(instruction, false);
      case Opcode::Label:
         _labelPositions[instruction.immediate] = _writer.position();
         _labels |= 1u << instruction.immediate;
         return EncodeError::None;
      case Opcode::Jump:
      case Opcode::JumpNotEqual:
      case Opcode::JumpZero:
         if (instruction.opcode == Opcode::Jump) {
         if ((error = write(0xE9)) != EncodeError::None) // jmp rel32
               return error;
         }
         else if ((error = write(0x0F)) != EncodeError::None // 0F 84/85: jz/jnz rel32
            || (error = write(instruction.opcode == Opcode::JumpNotEqual
               ? 0x85 : 0x84)) != EncodeError::None) // 85 jnz, 84 jz
         {
            return error;
         }
         _branches.add({
            _writer.position(), (unsigned char)instruction.immediate
         });
         return writeDWord(0);
      case Opcode::JumpRegister:
      case Opcode::CallRegister:
         error = writePrefix(Register::None, destination, size);
         if (error != EncodeError::None
            || (error = write(0xFF)) != EncodeError::None)
         {
            return error;
         }
         return writeModRM(3,
            instruction.opcode == Opcode::CallRegister ? 2 : 4,
            destination);
      case Opcode::Return:
         return write(0xC3);
      case Opcode::CallRuntime:
         if ((error = write(0xE8)) != EncodeError::None) // call rel32
            return error;
         _relocations.add({
            RelocationKind::RuntimeCall, _writer.position(),
            (unsigned int)instruction.immediate
         });
         return writeDWord(0);
      case Opcode::CallCodeReference:
      case Opcode::CallVMTMethod:
      case Opcode::CallHMTMethod:
      case Opcode::JumpVMTMethod:
      case Opcode::JumpHMTMethod:
         if ((error = write(instruction.opcode == Opcode::CallCodeReference
               || instruction.opcode == Opcode::CallVMTMethod
               || instruction.opcode == Opcode::CallHMTMethod
            ? 0xE8 : 0xE9)) != EncodeError::None)
         {
            return error;
         }
         _relocations.add({
            instruction.opcode == Opcode::CallCodeReference
               ? RelocationKind::ModuleCode
               : instruction.opcode == Opcode::CallHMTMethod
                  || instruction.opcode == Opcode::JumpHMTMethod
               ? RelocationKind::HMTMethodAddress
               : RelocationKind::VMTMethodAddress,
            _writer.position(),
            (unsigned int)instruction.immediate
         });
         return writeDWord(0);
      case Opcode::StoreOffset:
         error = writePrefix(source, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x89)) != EncodeError::None // 89 /r: mov [base + displacement], register
            || (error = writeModRM(instruction.immediate >= -128
               && instruction.immediate <= 127 ? 1 : 2,
               (unsigned char)source, destination))
               != EncodeError::None)
         {
            return error;
         }
         if ((destination == Register::SP || destination == Register::R12)
            && (error = writeSIB(0, Register::SP, destination))
               != EncodeError::None)
         {
            return error;
         }
         return writeDisplacement(instruction.immediate);
      case Opcode::AddressOffset:
         error = writePrefix(destination, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x8D)) != EncodeError::None // 8D /r: lea register, [base + displacement]
            || (error = writeModRM(1, (unsigned char)destination, destination))
               != EncodeError::None)
         {
            return error;
         }
         return write((unsigned char)instruction.immediate);
      case Opcode::LoadOffset:
         error = writePrefix(destination, source, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x8B)) != EncodeError::None // 8B /r: mov register, [base + displacement]
            || (error = writeModRM(instruction.immediate >= -128
               && instruction.immediate <= 127 ? 1 : 2,
               (unsigned char)destination, source))
               != EncodeError::None)
         {
            return error;
         }
         if ((source == Register::SP || source == Register::R12)
            && (error = writeSIB(0, Register::SP, source)) != EncodeError::None)
         {
            return error;
         }
         return writeDisplacement(instruction.immediate);
      case Opcode::AddressOffsetFrom:
         error = writePrefix(destination, source, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x8D)) != EncodeError::None // 8D /r: lea register, [base + displacement]
            || (error = writeModRM(instruction.immediate >= -128
               && instruction.immediate <= 127 ? 1 : 2,
               (unsigned char)destination, source)) != EncodeError::None)
         {
            return error;
         }
         if ((source == Register::SP || source == Register::R12)
            && (error = writeSIB(0, Register::SP, source)) != EncodeError::None)
         {
            return error;
         }
         return writeDisplacement(instruction.immediate);
      case Opcode::AddressScaledIndex:
         error = writePrefix(destination, source, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x8D)) != EncodeError::None // 8D /r: lea register, [base + index * scale + displacement]
            || (error = writeModRM(instruction.immediate >= -128
               && instruction.immediate <= 127 ? 1 : 2,
               (unsigned char)destination, Register::SP)) != EncodeError::None
            || (error = writeSIB(_architecture == Architecture::X86 ? 2 : 3,
               Register::D, source)) != EncodeError::None)
         {
            return error;
         }
         return writeDisplacement(instruction.immediate);
      case Opcode::LoadZeroExtendByteOffset:
      case Opcode::LoadSignExtendWordOffset:
      case Opcode::LoadZeroExtendWordOffset:
         error = writePrefix(destination, source, OperandSize::DWord);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x0F)) != EncodeError::None // 0F B6/B7/BF: movzx/movsx
            || (error = write(instruction.opcode == Opcode::LoadZeroExtendByteOffset
               ? 0xB6 : instruction.opcode == Opcode::LoadZeroExtendWordOffset
                  ? 0xB7 : 0xBF)) != EncodeError::None // B6 byte zx, B7 word zx, BF word sx
            || (error = writeModRM(instruction.immediate >= -128
               && instruction.immediate <= 127 ? 1 : 2,
               (unsigned char)destination, source))
               != EncodeError::None)
         {
            return error;
         }
         if ((source == Register::SP || source == Register::R12)
            && (error = writeSIB(0, Register::SP, source)) != EncodeError::None)
         {
            return error;
         }
         return writeDisplacement(instruction.immediate);
      case Opcode::LoadIndexedOffset:
         error = writePrefix(destination, source, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x8B)) != EncodeError::None // 8B /r: mov register, [base + edx]
            || (error = writeModRM(instruction.immediate == 0 ? 0
               : instruction.immediate >= -128 && instruction.immediate <= 127
                  ? 1 : 2,
               (unsigned char)destination, Register::SP)) != EncodeError::None
            || (error = writeSIB(0, Register::D, source)) != EncodeError::None)
         {
            return error;
         }
         return instruction.immediate == 0 ? EncodeError::None
            : writeDisplacement(instruction.immediate);
      case Opcode::LoadMemory:
      case Opcode::CompareMemory:
         return emitLoadMemory(instruction);
      case Opcode::StoreReferenceIndex:
         error = writePrefix(source, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x89)) != EncodeError::None // 89 /r: mov [base + edx * scale], register
            || (error = writeModRM(0, (unsigned char)source, Register::SP))
               != EncodeError::None
            || (error = writeSIB(_architecture == Architecture::X86 ? 2 : 3,
               Register::D, destination)) != EncodeError::None)
         {
            return error;
         }
         return EncodeError::None;
      case Opcode::StoreScaledIndex:
         return emitStoreScaledIndex(instruction);
      case Opcode::AtomicCompareExchangeDWord:
      case Opcode::AtomicExchangeAddDWord:
         if ((error = write(0xF0)) != EncodeError::None // lock prefix
            || (error = write(0x0F)) != EncodeError::None // 0F B1/C1: cmpxchg/xadd
            || (error = write(instruction.opcode
               == Opcode::AtomicCompareExchangeDWord ? 0xB1 : 0xC1)) // B1 cmpxchg, C1 xadd
               != EncodeError::None
            || (error = writeModRM(instruction.immediate == 0 ? 0 : 1,
               (unsigned char)source, destination))
               != EncodeError::None)
         {
            return error;
         }
         return instruction.immediate == 0
            ? EncodeError::None
            : write((unsigned char)instruction.immediate);
      case Opcode::AtomicCompareExchangeByte:
      case Opcode::AtomicExchangeAddByte:
         if ((error = write(0xF0)) != EncodeError::None
            || (error = write(0x0F)) != EncodeError::None
            || (error = write(instruction.opcode
               == Opcode::AtomicCompareExchangeByte ? 0xB0 : 0xC0))
               != EncodeError::None
            || (error = writeModRM(instruction.immediate == 0 ? 0 : 1,
               (unsigned char)source,
               destination)) != EncodeError::None)
         {
            return error;
         }

         return instruction.immediate == 0
            ? EncodeError::None
            : write((unsigned char)instruction.immediate);
      case Opcode::LoadReferenceIndex:
         error = writePrefix(destination, source, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x8B)) != EncodeError::None // 8B /r: mov register, [base + edx * scale]
            || (error = writeModRM(0, (unsigned char)destination, Register::SP))
               != EncodeError::None
            || (error = writeSIB(_architecture == Architecture::X86 ? 2 : 3,
               Register::D, source)) != EncodeError::None)
         {
            return error;
         }
         return EncodeError::None;
      case Opcode::StoreImmediateDWord:
         error = writePrefix(Register::None, destination, OperandSize::DWord);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0xC7)) != EncodeError::None // C7 /0 id: mov dword [base], immediate
            || (error = writeModRM(0, 0, destination)) != EncodeError::None
            || (error = writeDWord((unsigned int)instruction.immediate))
               != EncodeError::None)
         {
            return error;
         }
         return EncodeError::None;
      case Opcode::LoadDWordOffset:
      case Opcode::LoadSignExtendDWordOffset:
         error = writePrefix(destination, source,
            instruction.opcode == Opcode::LoadDWordOffset
               ? OperandSize::DWord : OperandSize::QWord);
         if (error != EncodeError::None)
            return error;
         if ((error = write(instruction.opcode == Opcode::LoadDWordOffset
            ? 0x8B : 0x63)) != EncodeError::None // 8B mov r32, r/m32; 63 movsxd r64, r/m32
            || (error = writeModRM(instruction.immediate >= -128
               && instruction.immediate <= 127 ? 1 : 2,
               (unsigned char)destination, source))
               != EncodeError::None)
         {
            return error;
         }
         if ((source == Register::SP || source == Register::R12)
            && (error = writeSIB(0, Register::SP, source)) != EncodeError::None)
         {
            return error;
         }
         return writeDisplacement(instruction.immediate);
      case Opcode::StoreDWordOffset:
         error = writePrefix(source, destination, OperandSize::DWord);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x89)) != EncodeError::None // 89 /r: mov dword [base + displacement], register
            || (error = writeModRM(instruction.immediate >= -128
               && instruction.immediate <= 127 ? 1 : 2,
               (unsigned char)source, destination))
               != EncodeError::None)
         {
            return error;
         }
         if ((destination == Register::SP || destination == Register::R12)
            && (error = writeSIB(0, Register::SP, destination))
               != EncodeError::None)
         {
            return error;
         }
         return writeDisplacement(instruction.immediate);
      case Opcode::ShiftLeftImmediate:
      case Opcode::ShiftRightImmediate:
         error = writePrefix(Register::None, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0xC1)) != EncodeError::None // C1 /4 or /5 ib: shl/shr r/m, imm8
            || (error = writeModRM(3,
               instruction.opcode == Opcode::ShiftLeftImmediate ? 4 : 5,
               destination)) != EncodeError::None)
         {
            return error;
         }
         return write((unsigned char)instruction.immediate);
      case Opcode::AndImmediate:
      case Opcode::OrImmediate:
      case Opcode::CompareImmediate:
         group = instruction.opcode == Opcode::AndImmediate ? 4
            : instruction.opcode == Opcode::OrImmediate ? 1 : 7;
         error = writePrefix(Register::None, destination, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x81)) != EncodeError::None // 81 /4, /1, /7 id: and/or/cmp r/m, immediate
            || (error = writeModRM(3, group, destination)) != EncodeError::None
            || (error = writeDWord((unsigned int)instruction.immediate))
               != EncodeError::None)
         {
            return error;
         }
         return EncodeError::None;
      case Opcode::MultiplyImmediate:
         error = writePrefix(destination, source, size);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0x69)) != EncodeError::None // 69 /r id: imul register, register, immediate
            || (error = writeModRM(3, (unsigned char)destination, source))
               != EncodeError::None
            || (error = writeDWord((unsigned int)instruction.immediate))
               != EncodeError::None)
         {
            return error;
         }
         return EncodeError::None;
      case Opcode::DivideUnsigned:
         error = writePrefix(Register::None, source, OperandSize::DWord);
         if (error != EncodeError::None)
            return error;
         if ((error = write(0xF7)) != EncodeError::None) // F7 /6: div r/m
            return error;
         return writeModRM(3, 6, source);
      case Opcode::RepeatStore:
         if ((error = write(0xF3)) != EncodeError::None) // rep prefix
            return error;
         if (size == OperandSize::QWord
            && (error = write(0x48)) != EncodeError::None) // REX.W
         {
            return error;
         }
         return write(0xAB); // stosd or stosq
      case Opcode::RepeatMoveBytes:
         if ((error = write(0xF3)) != EncodeError::None)
            return error;

         return write(0xA4);
      default:
         return EncodeError::InvalidOpcode;
   }
}

EncodeError Encoder :: emit(Sequence& sequence, const ManagedABI& abi,
   const RuntimeCallABI* runtimeABI)
{
   if (_architecture != abi.architecture)
      return EncodeError::InvalidArchitecture;

   MIRVerifyError verifyError = MIRVerifier::verify(sequence, abi, runtimeABI);
   if (verifyError != MIRVerifyError::None)
      return verifyError == MIRVerifyError::InvalidEffects
         ? EncodeError::InvalidEffects : EncodeError::InvalidOperand;

   _relocations.clear();
   _branches.clear();
   _labels = 0;
   for (pos_t i = 0; i < sequence.count(); i++) {
      EncodeError error = emit(sequence.instruction(i));
      if (error != EncodeError::None)
         return error;
   }

   pos_t end = _writer.position();
   for (pos_t i = 0; i < _branches.count_pos(); i++) {
      LocalBranch& branch = _branches.get(i);
      unsigned int label = 1u << branch.label;
      if ((_labels & label) == 0 || !_writer.seek(branch.position))
         return EncodeError::InvalidOperand;

      int displacement = (int)((long long)_labelPositions[branch.label]
         - (long long)branch.position - 4);
      EncodeError error = writeDWord((unsigned int)displacement);
      if (error != EncodeError::None)
         return error;
   }
   if (!_writer.seek(end))
      return EncodeError::WriteFailed;

   return EncodeError::None;
}

pos_t Encoder :: relocationCount() const
{
   return _relocations.count_pos();
}

Relocation& Encoder :: relocation(pos_t index)
{
   return _relocations.get(index);
}
