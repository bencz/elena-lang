#ifndef CODEGEN_X86_LOWERING_H
#define CODEGEN_X86_LOWERING_H

#include "../ecode.h"
#include "../eir.h"
#include "machine.h"

namespace elena_lang::codegen::x86
{
   enum class LowerError : unsigned char
   {
      None,
      UnsupportedOpcode,
      InvalidABI,
      InvalidMIR,
      InvalidRuntime,
      InvalidArgument
   };

   using LoweringContext = ECodeLoweringContext;

   class ECodeLowering
   {
   public:
      static LowerError lower(const ByteCommand& command, const RuntimeSpec& runtime,
         const ManagedABI& abi, const RuntimeCallABI& runtimeABI, Sequence& sequence);
      static LowerError lower(const ByteCommand& command, const RuntimeSpec& runtime,
         const ManagedABI& abi, const RuntimeCallABI& runtimeABI,
         const LoweringContext& context, Sequence& sequence);
   };
}

#endif
