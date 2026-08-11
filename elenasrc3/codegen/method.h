#ifndef CODEGEN_METHOD_H
#define CODEGEN_METHOD_H

#include "../engine/bytecode.h"

namespace elena_lang::codegen
{
   enum class MethodLookupOption : unsigned char
   {
      None           = 0x00,
      AlternativeVMT = 0x01
   };

   enum class MethodTransferKind : unsigned char
   {
      Call,
      Jump
   };

   enum class ManagedMethodTarget : unsigned char
   {
      Symbol,
      VMTIndex,
      VMTMethod
   };

   constexpr MethodLookupOption operator | (MethodLookupOption left, MethodLookupOption right)
   {
      return (MethodLookupOption)((unsigned char)left | (unsigned char)right);
   }

   constexpr bool test(MethodLookupOption value, MethodLookupOption mask)
   {
      return ((unsigned char)value & (unsigned char)mask) == (unsigned char)mask;
   }

   struct VirtualMethodSpec
   {
      MethodLookupOption options;
      MethodTransferKind transfer;
      mssg_t             message;
      ref_t              classReference;

      bool has(MethodLookupOption option) const;
   };

   struct ManagedMethodSpec
   {
      ManagedMethodTarget target;
      MethodLookupOption  options;
      MethodTransferKind  transfer;
      ref_t               reference;
      mssg_t              message;
      int                 index;

      bool has(MethodLookupOption option) const;
   };

   class VirtualMethodProvider
   {
   public:
      // VCallMR 0xFC calls and VJumpMR 0xEC tail-transfers through VMT/HMT.
      static bool get(const ByteCommand& command, bool alternativeMode, VirtualMethodSpec& spec);
   };

   class ManagedMethodProvider
   {
   public:
      // CallR 0xB0; CallVI 0xB1; JumpVI 0xB5; JumpMR 0xED; CallMR 0xFD.
      static bool get(const ByteCommand& command, bool alternativeMode, ManagedMethodSpec& spec);
   };
}

#endif
