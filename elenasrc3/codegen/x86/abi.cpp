#include "abi.h"

using namespace elena_lang::codegen;
using namespace elena_lang::codegen::x86;

bool ManagedABI :: isValid() const
{
   if (stack != Register::SP || frame != Register::BP)
      return false;

   switch (architecture) {
      case Architecture::X86:
         return wordSize == OperandSize::DWord
            && value == Register::D
            && object == Register::B
            && cachedArgument0 == Register::SI
            && cachedArgument1 == Register::None
            && allocationSize == Register::C
            && wideLow == Register::A
            && wideHigh == Register::D
            && dataSource == Register::SI
            && dataDestination == Register::DI
            && scratch == Register::None;
      case Architecture::AMD64:
         return wordSize == OperandSize::QWord
            && value == Register::D
            && object == Register::B
            && cachedArgument0 == Register::R10
            && cachedArgument1 == Register::R11
            && allocationSize == Register::C
            && wideLow == Register::D
            && wideHigh == Register::None
            && dataSource == Register::SI
            && dataDestination == Register::DI
            && scratch == Register::R8;
      default:
         return false;
   }
}

bool ManagedABIProvider :: get(Architecture architecture, ManagedABI& abi)
{
   switch (architecture) {
      case Architecture::X86:
         abi = {
            .architecture = architecture,
            .wordSize = OperandSize::DWord,
            .value = Register::D,
            .object = Register::B,
            .cachedArgument0 = Register::SI,
            .cachedArgument1 = Register::None,
            .allocationSize = Register::C,
            .wideLow = Register::A,
            .wideHigh = Register::D,
            .dataSource = Register::SI,
            .dataDestination = Register::DI,
            .scratch = Register::None,
            .stack = Register::SP,
            .frame = Register::BP
         };
         break;

      case Architecture::AMD64:
         abi = {
            .architecture = architecture,
            .wordSize = OperandSize::QWord,
            .value = Register::D,
            .object = Register::B,
            .cachedArgument0 = Register::R10,
            .cachedArgument1 = Register::R11,
            .allocationSize = Register::C,
            .wideLow = Register::D,
            .wideHigh = Register::None,
            .dataSource = Register::SI,
            .dataDestination = Register::DI,
            .scratch = Register::R8,
            .stack = Register::SP,
            .frame = Register::BP
         };
         break;

      default:
         return false;
   }

   return abi.isValid();
}

static RegisterMask registerSet(Register r0, Register r1 = Register::None,
   Register r2 = Register::None, Register r3 = Register::None,
   Register r4 = Register::None, Register r5 = Register::None,
   Register r6 = Register::None, Register r7 = Register::None,
   Register r8 = Register::None)
{
   return mask(r0) | mask(r1) | mask(r2) | mask(r3) | mask(r4)
      | mask(r5) | mask(r6) | mask(r7) | mask(r8);
}

bool ExternalABI :: isValid(const TargetSpec& target) const
{
   if (!target.isValid() || platformABI != target.abi
      || architecture != target.architecture
      || integerReturn != Register::A
      || integerArgumentCount != target.externalABI.integerArgumentRegisters
      || floatingArgumentCount != target.externalABI.floatingArgumentRegisters
      || stackAlignment != target.externalABI.stackAlignment
      || stackSlotSize != target.externalABI.stackSlotSize
      || shadowSpace != target.externalABI.shadowSpace
      || redZone != target.externalABI.redZone
      || (volatileRegisters & nonvolatileRegisters) != 0)
   {
      return false;
   }

   for (unsigned int i = 0; i < integerArgumentCount; i++) {
      if (integerArguments[i] == Register::None)
         return false;
   }
   for (unsigned int i = integerArgumentCount; i < 6; i++) {
      if (integerArguments[i] != Register::None)
         return false;
   }

   switch (platformABI) {
      case PlatformABI::WindowsX86:
      case PlatformABI::SystemVX86:
         return architecture == Architecture::X86
            && callerCleansStack && !variadicFPCountInA;
      case PlatformABI::WindowsX64:
         return architecture == Architecture::AMD64
            && !callerCleansStack && !variadicFPCountInA;
      case PlatformABI::SystemVAMD64:
         return architecture == Architecture::AMD64
            && callerCleansStack && variadicFPCountInA;
      default:
         return false;
   }
}

bool ExternalABIProvider :: get(const TargetSpec& target, ExternalABI& abi)
{
   Register none = Register::None;
   switch (target.abi) {
      case PlatformABI::WindowsX86:
      case PlatformABI::SystemVX86:
         abi = {
            .platformABI = target.abi,
            .architecture = Architecture::X86,
            .integerArguments = { none, none, none, none, none, none },
            .integerReturn = Register::A,
            .volatileRegisters = registerSet(Register::A, Register::C, Register::D),
            .nonvolatileRegisters = registerSet(Register::B, Register::BP, Register::SI, Register::DI),
            .integerArgumentCount = 0,
            .floatingArgumentCount = 0,
            .stackAlignment = target.externalABI.stackAlignment,
            .stackSlotSize = 4,
            .shadowSpace = 0,
            .redZone = 0,
            .callerCleansStack = true,
            .variadicFPCountInA = false
         };
         break;

      case PlatformABI::WindowsX64:
         abi = {
            .platformABI = target.abi,
            .architecture = Architecture::AMD64,
            .integerArguments = { Register::C, Register::D, Register::R8, Register::R9, none, none },
            .integerReturn = Register::A,
            .volatileRegisters = registerSet(Register::A, Register::C, Register::D, Register::R8,
               Register::R9, Register::R10, Register::R11),
            .nonvolatileRegisters = registerSet(Register::B, Register::BP, Register::SI, Register::DI,
               Register::R12, Register::R13, Register::R14, Register::R15),
            .integerArgumentCount = 4,
            .floatingArgumentCount = 4,
            .stackAlignment = 16,
            .stackSlotSize = 8,
            .shadowSpace = 32,
            .redZone = 0,
            .callerCleansStack = false,
            .variadicFPCountInA = false
         };
         break;

      case PlatformABI::SystemVAMD64:
         abi = {
            .platformABI = target.abi,
            .architecture = Architecture::AMD64,
            .integerArguments = {
               Register::DI,
               Register::SI,
               Register::D,
               Register::C,
               Register::R8,
               Register::R9
            },
            .integerReturn = Register::A,
            .volatileRegisters = registerSet(Register::A, Register::C, Register::D, Register::SI,
               Register::DI, Register::R8, Register::R9, Register::R10,
               Register::R11),
            .nonvolatileRegisters = registerSet(Register::B, Register::BP, Register::R12, Register::R13,
               Register::R14, Register::R15),
            .integerArgumentCount = 6,
            .floatingArgumentCount = 8,
            .stackAlignment = 16,
            .stackSlotSize = 8,
            .shadowSpace = 0,
            .redZone = 128,
            .callerCleansStack = true,
            .variadicFPCountInA = true
         };
         break;

      default:
         return false;
   }

   return abi.isValid(target);
}

bool ExternalFrameLayout :: isValid(const TargetSpec& target) const
{
   if (!target.isValid()
      || savedRegisterCount == 0
      || savedRegisterCount > MaximumSavedRegisterCount
      || firstArgumentStorage == ExternalArgumentStorage::None)
   {
      return false;
   }

   for (unsigned int i = 0; i < savedRegisterCount; i++) {
      if (savedRegisters[i] == Register::None)
         return false;
   }

   for (unsigned int i = savedRegisterCount; i < MaximumSavedRegisterCount; i++) {
      if (savedRegisters[i] != Register::None)
         return false;
   }

   if (firstArgumentStorage == ExternalArgumentStorage::SavedRegister) {
      ExternalABI externalABI = {};
      if (!ExternalABIProvider::get(target, externalABI)
         || externalABI.integerArgumentCount == 0)
      {
         return false;
      }

      for (unsigned int i = 0; i < savedRegisterCount; i++) {
         if (savedRegisters[i] == externalABI.integerArguments[0])
            return true;
      }

      return false;
   }

   return true;
}

bool ExternalFrameLayout :: resolveArgumentFrameOffset(
   const TargetSpec& target,
   unsigned int unmanagedSize,
   int& frameOffset) const
{
   if (!isValid(target)
      || unmanagedSize % target.managedABI.rawStackAlignment != 0)
   {
      return false;
   }

   unsigned long long scaffoldSize =
      (savedRegisterCount + ScaffoldSlotCount) * target.pointerSize;
   int firstArgumentEntryOffset = target.pointerSize;
   if (firstArgumentStorage == ExternalArgumentStorage::SavedRegister) {
      ExternalABI externalABI = {};
      if (!ExternalABIProvider::get(target, externalABI))
         return false;

      for (unsigned int i = 0; i < savedRegisterCount; i++) {
         if (savedRegisters[i] == externalABI.integerArguments[0]) {
            firstArgumentEntryOffset = -(i + 1) * (int)target.pointerSize;
            break;
         }
      }
   }

   long long firstArgumentOffset = (long long)scaffoldSize
      + unmanagedSize
      + firstArgumentEntryOffset;
   long long resolvedOffset = firstArgumentOffset - target.pointerSize;
   if (resolvedOffset < 0 || resolvedOffset > 0x7FFFFFFF)
      return false;

   frameOffset = (int)resolvedOffset;

   return true;
}

bool ExternalFrameLayoutProvider :: get(
   const TargetSpec& target,
   ExternalFrameLayout& layout)
{
   Register none = Register::None;

   switch (target.abi) {
      case PlatformABI::WindowsX86:
      case PlatformABI::SystemVX86:
         layout = {
            .savedRegisters = {
               Register::SI,
               Register::DI,
               Register::C,
               Register::B,
               none,
               none,
               none,
               none,
               none,
               none
            },
            .savedRegisterCount = 4,
            .firstArgumentStorage = ExternalArgumentStorage::CallerStack
         };
         break;

      case PlatformABI::WindowsX64:
         layout = {
            .savedRegisters = {
               Register::A,
               Register::SI,
               Register::DI,
               Register::B,
               Register::R12,
               Register::R13,
               Register::R14,
               Register::R15,
               none,
               none
            },
            .savedRegisterCount = 8,
            .firstArgumentStorage = ExternalArgumentStorage::CallerStack
         };
         break;

      case PlatformABI::SystemVAMD64:
         layout = {
            .savedRegisters = {
               Register::A,
               Register::C,
               Register::D,
               Register::SI,
               Register::DI,
               Register::B,
               Register::R12,
               Register::R13,
               Register::R14,
               Register::R15
            },
            .savedRegisterCount = 10,
            .firstArgumentStorage = ExternalArgumentStorage::SavedRegister
         };
         break;

      default:
         return false;
   }

   return layout.isValid(target);
}

bool RuntimeCallABI :: isValid(const ManagedABI& managedABI,
   const RuntimeCallSpec& call) const
{
   Register expectedArgument0;
   Register expectedArgument1 = Register::None;
   Register expectedResult = Register::None;
   switch (operation) {
      case RuntimeOperation::AllocateYoung:
      case RuntimeOperation::AllocatePermanent:
         expectedArgument0 = managedABI.allocationSize;
         expectedResult = managedABI.object;
         break;
      case RuntimeOperation::Collect:
         expectedArgument0 = managedABI.allocationSize;
         expectedArgument1 = managedABI.value;
         expectedResult = managedABI.object;
         break;
      case RuntimeOperation::Prepare:
         expectedArgument0 = Register::A;
         break;
      case RuntimeOperation::WaitForGC:
         expectedArgument0 = managedABI.value;
         break;
      default:
         return false;
   }

   if (!managedABI.isValid() || operation != call.operation
      || architecture != managedABI.architecture
      || argument0 != expectedArgument0 || argument1 != expectedArgument1
      || result != expectedResult
      || inputs != (mask(argument0) | mask(argument1))
      || outputs != mask(result)
      || (clobbers & outputs) != outputs
      || effects != call.effects
      || requiresManagedFrame != call.requiresManagedFrame)
   {
      return false;
   }

   RegisterMask roots = operation == RuntimeOperation::Prepare ? 0
      : architecture == Architecture::X86
         ? mask(Register::SI)
         : mask(Register::R10) | mask(Register::R11);

   return preservedRoots == roots && (clobbers & roots) == 0;
}

static bool getRuntimeRegisterUsage(
   RuntimeOperation operation,
   Architecture architecture,
   RegisterMask& clobbers,
   RegisterMask& roots)
{
   switch (architecture) {
      case Architecture::X86:
         if (operation == RuntimeOperation::Prepare) {
            clobbers = registerSet(Register::A, Register::C, Register::D);
            roots = 0;
         }
         else {
            clobbers = registerSet(Register::A, Register::C, Register::D, Register::B, Register::DI);
            roots = mask(Register::SI);
         }
         return true;

      case Architecture::AMD64:
         if (operation == RuntimeOperation::Prepare) {
            clobbers = registerSet(
               Register::A,
               Register::C,
               Register::D,
               Register::SI,
               Register::DI,
               Register::R8,
               Register::R9,
               Register::R10,
               Register::R11);
            roots = 0;
         }
         else {
            clobbers = registerSet(
               Register::A,
               Register::C,
               Register::D,
               Register::B,
               Register::SI,
               Register::DI,
               Register::R8,
               Register::R9,
               Register::R12) | mask(Register::R13);
            roots = mask(Register::R10) | mask(Register::R11);
         }
         return true;

      default:
         return false;
   }
}

static bool getRuntimeCallOperands(
   RuntimeOperation operation,
   const ManagedABI& managedABI,
   Register& argument0,
   Register& argument1,
   Register& result)
{
   argument0 = Register::None;
   argument1 = Register::None;
   result = Register::None;

   switch (operation) {
      case RuntimeOperation::AllocateYoung:
      case RuntimeOperation::AllocatePermanent:
         argument0 = managedABI.allocationSize;
         result = managedABI.object;
         return true;

      case RuntimeOperation::Collect:
         argument0 = managedABI.allocationSize;
         argument1 = managedABI.value;
         result = managedABI.object;
         return true;

      case RuntimeOperation::Prepare:
         argument0 = Register::A;
         return true;

      case RuntimeOperation::WaitForGC:
         argument0 = managedABI.value;
         return true;

      default:
         return false;
   }
}

bool RuntimeABIProvider :: get(RuntimeOperation operation, const RuntimeSpec& runtime,
   const ManagedABI& managedABI, RuntimeCallABI& abi)
{
   RuntimeCallSpec call = {};
   if (!RuntimeCallProvider::get(operation, runtime, call) || !managedABI.isValid())
      return false;

   RegisterMask clobbers = 0;
   RegisterMask roots = 0;
   Register argument0 = Register::None;
   Register argument1 = Register::None;
   Register result = Register::None;

   if (!getRuntimeRegisterUsage(operation, managedABI.architecture, clobbers, roots)
      || !getRuntimeCallOperands(operation, managedABI, argument0, argument1, result))
   {
      return false;
   }

   abi = {
      .operation = operation,
      .architecture = managedABI.architecture,
      .argument0 = argument0,
      .argument1 = argument1,
      .result = result,
      .inputs = mask(argument0) | mask(argument1),
      .outputs = mask(result),
      .clobbers = clobbers,
      .preservedRoots = roots,
      .effects = call.effects,
      .requiresManagedFrame = call.requiresManagedFrame
   };

   return abi.isValid(managedABI, call);
}

RuntimeABISet :: RuntimeABISet()
   : _calls {}, _available(0)
{
}

bool RuntimeABISet :: initialize(const RuntimeSpec& runtime,
   const ManagedABI& managedABI)
{
   _available = 0;
   for (unsigned int i = 0; i < (unsigned int)RuntimeOperation::Count; i++) {
      RuntimeOperation operation = (RuntimeOperation)i;
      RuntimeCallABI call = {};
      if (RuntimeABIProvider::get(operation, runtime, managedABI, call)) {
         _calls[i] = call;
         _available |= 1u << i;
      }
   }

   unsigned int required = (1u << (unsigned int)RuntimeOperation::AllocateYoung)
      | (1u << (unsigned int)RuntimeOperation::AllocatePermanent)
      | (1u << (unsigned int)RuntimeOperation::Collect)
      | (1u << (unsigned int)RuntimeOperation::Prepare);
   if (runtime.threadingMode == ThreadingMode::MultiThread)
      required |= 1u << (unsigned int)RuntimeOperation::WaitForGC;

   return (_available & required) == required;
}

const RuntimeCallABI* RuntimeABISet :: get(RuntimeOperation operation) const
{
   unsigned int index = (unsigned int)operation;
   if (index >= (unsigned int)RuntimeOperation::Count
      || (_available & (1u << index)) == 0)
   {
      return nullptr;
   }

   return _calls + index;
}
