#include "method.h"

using namespace elena_lang;
using namespace elena_lang::codegen;

bool VirtualMethodSpec :: has(MethodLookupOption option) const
{
   return test(options, option);
}

bool ManagedMethodSpec :: has(MethodLookupOption option) const
{
   return test(options, option);
}

bool VirtualMethodProvider :: get(const ByteCommand& command, bool alternativeMode, VirtualMethodSpec& spec)
{
   if ((command.code != ByteCode::VCallMR
         && command.code != ByteCode::VJumpMR)
      || command.arg1 == 0 || command.arg2 == 0)
   {
      return false;
   }

   spec = {
      .options = alternativeMode
         ? MethodLookupOption::AlternativeVMT
         : MethodLookupOption::None,
      .transfer = command.code == ByteCode::VCallMR
         ? MethodTransferKind::Call
         : MethodTransferKind::Jump,
      .message = (mssg_t)command.arg1,
      .classReference = (ref_t)command.arg2
   };

   return true;
}

bool ManagedMethodProvider :: get(const ByteCommand& command, bool alternativeMode, ManagedMethodSpec& spec)
{
   spec = {};

   switch (command.code) {
      case ByteCode::CallR:
         if (command.arg1 == 0)
            return false;

         spec = {
            .target = ManagedMethodTarget::Symbol,
            .options = MethodLookupOption::None,
            .transfer = MethodTransferKind::Call,
            .reference = (ref_t)command.arg1,
            .message = 0,
            .index = 0
         };
         break;
      case ByteCode::CallVI:
      case ByteCode::JumpVI:
         if (command.arg1 < 0)
            return false;

         spec = {
            .target = ManagedMethodTarget::VMTIndex,
            .options = MethodLookupOption::None,
            .transfer = command.code == ByteCode::CallVI
               ? MethodTransferKind::Call
               : MethodTransferKind::Jump,
            .reference = 0,
            .message = 0,
            .index = command.arg1
         };
         break;
      case ByteCode::CallMR:
      case ByteCode::JumpMR:
         if (command.arg1 == 0 || command.arg2 == 0)
            return false;

         spec = {
            .target = ManagedMethodTarget::VMTMethod,
            .options = alternativeMode
               ? MethodLookupOption::AlternativeVMT
               : MethodLookupOption::None,
            .transfer = command.code == ByteCode::CallMR
               ? MethodTransferKind::Call
               : MethodTransferKind::Jump,
            .reference = (ref_t)command.arg2,
            .message = (mssg_t)command.arg1,
            .index = 0
         };
         break;
      default:
         return false;
   }

   return true;
}
