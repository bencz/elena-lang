#ifndef CODEGEN_MACHINE_H
#define CODEGEN_MACHINE_H

#include "runtime.h"

namespace elena_lang::codegen
{
   enum class MachineValueKind : unsigned char
   {
      None,
      Integer,
      Reference,
      VMT,
      Address
   };

   enum class MachineEffect : unsigned short
   {
      None          = 0x0000,
      ReadFlags     = 0x0001,
      WriteFlags    = 0x0002,
      ReadMemory    = 0x0004,
      WriteMemory   = 0x0008,
      Call          = 0x0010,
      Allocate      = 0x0020,
      Safepoint     = 0x0040,
      Synchronize   = 0x0080,
      MayThrow      = 0x0100,
      WriteBarrier  = 0x0200,
      RelocateRoots = 0x0400,
      ReadTLS       = 0x0800,
      WriteFPUState = 0x1000,
      WriteTLS      = 0x2000
   };

   enum class MachineRelocationKind : unsigned char
   {
      RuntimeCall,
      ModuleReference,
      ModuleReferenceValue,
      ModuleCode,
      ModuleMessage,
      ThreadLocalOffset,
      RuntimeData,
      Metadata,
      RuntimeConstant,
      ProcedureLabel,
      VMTMethodAddress,
      HMTMethodAddress,
      VMTMethodOffset,
      HMTMethodOffset
   };

   enum class MachineRuntimeConstant : unsigned char
   {
      VoidReference
   };

   struct MachineRelocation
   {
      MachineRelocationKind kind;
      pos_t position;
      unsigned int value;
   };

   constexpr MachineEffect operator | (MachineEffect left, MachineEffect right)
   {
      return (MachineEffect)((unsigned short)left | (unsigned short)right);
   }

   constexpr bool test(MachineEffect value, MachineEffect mask)
   {
      return ((unsigned short)value & (unsigned short)mask) == (unsigned short)mask;
   }

   inline MachineEffect machineEffects(RuntimeCallEffect effects)
   {
      MachineEffect result = MachineEffect::WriteFlags;

      if (test(effects, RuntimeCallEffect::ReadHeap) || test(effects, RuntimeCallEffect::ReadGlobal))
         result = result | MachineEffect::ReadMemory;

      if (test(effects, RuntimeCallEffect::WriteHeap) || test(effects, RuntimeCallEffect::WriteGlobal))
         result = result | MachineEffect::WriteMemory;

      if (test(effects, RuntimeCallEffect::Call))
         result = result | MachineEffect::Call;

      if (test(effects, RuntimeCallEffect::Allocate))
         result = result | MachineEffect::Allocate;

      if (test(effects, RuntimeCallEffect::Safepoint))
         result = result | MachineEffect::Safepoint;

      if (test(effects, RuntimeCallEffect::MayThrow))
         result = result | MachineEffect::MayThrow;

      if (test(effects, RuntimeCallEffect::Synchronize))
         result = result | MachineEffect::Synchronize;

      if (test(effects, RuntimeCallEffect::ReadTLS))
         result = result | MachineEffect::ReadTLS;

      if (test(effects, RuntimeCallEffect::RelocateRoots))
         result = result | MachineEffect::RelocateRoots;

      return result;
   }
}

#endif
