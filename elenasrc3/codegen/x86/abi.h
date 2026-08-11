#ifndef CODEGEN_X86_ABI_H
#define CODEGEN_X86_ABI_H

#include "../target.h"
#include "../runtime.h"
#include "isa.h"

namespace elena_lang::codegen::x86
{
   struct ManagedABI
   {
      Architecture architecture;
      OperandSize   wordSize;
      Register      value;
      Register      object;
      Register      cachedArgument0;
      Register      cachedArgument1;
      Register      allocationSize;
      Register      wideLow;
      Register      wideHigh;
      Register      dataSource;
      Register      dataDestination;
      Register      scratch;
      Register      stack;
      Register      frame;

      bool isValid() const;
   };

   struct ExternalABI
   {
      PlatformABI  platformABI;
      Architecture architecture;

      Register     integerArguments[6];
      Register     integerReturn;
      RegisterMask volatileRegisters;
      RegisterMask nonvolatileRegisters;

      unsigned char integerArgumentCount;
      unsigned char floatingArgumentCount;
      unsigned char stackAlignment;
      unsigned char stackSlotSize;
      unsigned short shadowSpace;
      unsigned short redZone;
      bool callerCleansStack;
      bool variadicFPCountInA;

      bool isValid(const TargetSpec& target) const;
   };

   enum class ExternalArgumentStorage : unsigned char
   {
      None,
      CallerStack,
      SavedRegister
   };

   struct ExternalFrameLayout
   {
      // ExternalFrameOpen links seven managed-frame slots after saving native state.
      static constexpr unsigned char ScaffoldSlotCount = 7;
      static constexpr unsigned char MaximumSavedRegisterCount = 10;
      static constexpr unsigned char ManagedFrameLinkSlot = 1;
      static constexpr unsigned char NativeFrameSlot = 3;

      Register      savedRegisters[MaximumSavedRegisterCount];
      unsigned char savedRegisterCount;
      ExternalArgumentStorage firstArgumentStorage;

      bool isValid(const TargetSpec& target) const;
      bool resolveArgumentFrameOffset(
         const TargetSpec& target,
         unsigned int unmanagedSize,
         int& frameOffset) const;
   };

   struct RuntimeCallABI
   {
      RuntimeOperation operation;
      Architecture architecture;

      Register argument0;
      Register argument1;
      Register result;

      RegisterMask inputs;
      RegisterMask outputs;
      RegisterMask clobbers;
      RegisterMask preservedRoots;

      RuntimeCallEffect effects;
      bool requiresManagedFrame;

      bool isValid(const ManagedABI& managedABI, const RuntimeCallSpec& call) const;
   };

   class RuntimeABISet
   {
      RuntimeCallABI _calls[(unsigned int)RuntimeOperation::Count];
      unsigned int   _available;

   public:
      bool initialize(const RuntimeSpec& runtime, const ManagedABI& managedABI);
      const RuntimeCallABI* get(RuntimeOperation operation) const;

      RuntimeABISet();
   };

   class ManagedABIProvider
   {
   public:
      static bool get(Architecture architecture, ManagedABI& abi);
   };

   class ExternalABIProvider
   {
   public:
      static bool get(const TargetSpec& target, ExternalABI& abi);
   };

   class ExternalFrameLayoutProvider
   {
   public:
      static bool get(const TargetSpec& target, ExternalFrameLayout& layout);
   };

   class RuntimeABIProvider
   {
   public:
      static bool get(
         RuntimeOperation operation,
         const RuntimeSpec& runtime,
         const ManagedABI& managedABI,
         RuntimeCallABI& abi);
   };
}

#endif
