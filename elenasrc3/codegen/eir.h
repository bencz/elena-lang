#ifndef CODEGEN_EIR_H
#define CODEGEN_EIR_H

#include "common.h"
#include "dispatch.h"
#include "ecode.h"
#include "method.h"
#include "runtime.h"

namespace elena_lang::codegen
{
   enum class EIRType : unsigned char
   {
      None,
      Void,
      Reference,
      Pointer,
      Boolean,
      Int8,
      UInt8,
      Int16,
      UInt16,
      Int32,
      UInt32,
      Int64,
      UInt64,
      Word,
      Float64,
      Message,
      VMT
   };

   enum class EIREffect : unsigned int
   {
      None        = 0x000000,
      ReadHeap    = 0x000001,
      WriteHeap   = 0x000002,
      ReadFrame   = 0x000004,
      WriteFrame  = 0x000008,
      ReadTLS     = 0x000010,
      WriteTLS    = 0x000020,
      ReadGlobal  = 0x000040,
      WriteGlobal = 0x000080,
      Call        = 0x000100,
      Allocate    = 0x000200,
      Safepoint   = 0x000400,
      Throw       = 0x000800,
      Synchronize = 0x001000,
      Volatile    = 0x002000,
      Terminator  = 0x004000
   };

   inline EIREffect operator | (EIREffect left, EIREffect right)
   {
      return (EIREffect)((unsigned int)left | (unsigned int)right);
   }

   inline bool test(EIREffect value, EIREffect mask)
   {
      return ((unsigned int)value & (unsigned int)mask) == (unsigned int)mask;
   }

   enum class EIROpcode : unsigned short
   {
      Undef,
      NoOperation,
      Phi,
      Constant,
      Copy,
      Add,
      Subtract,
      Multiply,
      DivideSigned,
      DivideUnsigned,
      RemainderSigned,
      RemainderUnsigned,
      Negate,
      BitAnd,
      BitOr,
      BitXor,
      BitNot,
      ShiftLeft,
      ShiftRightSigned,
      ShiftRightUnsigned,
      Convert,
      Compare,
      Select,
      FloatAdd,
      FloatSubtract,
      FloatMultiply,
      FloatDivide,
      FloatAbs,
      FloatSqrt,
      FloatExp,
      FloatLog,
      FloatSin,
      FloatCos,
      FloatArcTan,
      FloatRound,
      FloatTruncate,
      Load,
      LoadSignExtend,
      LoadZeroExtend,
      LoadIndexed,
      Store,
      StoreIndexed,
      MemoryCopy,
      MemoryFill,
      FrameAddress,
      StackAddress,
      IndexedFrameAddress,
      FieldAddress,
      ObjectVMT,
      LoadObjectVMT,
      SelectAlternativeVMT,
      MethodOffset,
      ResolveVirtualMethod,
      ResolveVirtualIndex,
      MethodAddress,
      ObjectSize,
      ObjectParent,
      CallDirect,
      CallVirtual,
      CallIndirect,
      CallExternal,
      CallRuntime,
      Dispatch,
      Collect,
      Allocate,
      AllocateArray,
      AllocatePermanent,
      WriteBarrier,
      IsCurrentStackReference,
      ThreadCurrent,
      ThreadPublish,
      ThreadLocalLoad,
      ThreadLocalStore,
      SystemStartup,
      SystemEnvironment,
      ComposeMessage,
      Coalesce,
      SelectEqual,
      ThreadInclude,
      ThreadExclude,
      SafeRegionEnter,
      SafeRegionLeave,
      Safepoint,
      ObjectLockTry,
      ObjectLockRelease,
      GCLockAcquire,
      GCLockRelease,
      AtomicCompareExchange,
      AtomicReadModifyWrite,
      Fence,
      FrameOpen,
      FrameLink,
      FrameClear,
      FrameClose,
      ExternalFrameOpen,
      ExternalFrameClose,
      StackReserve,
      StackAllocate,
      StackFree,
      ExceptionHook,
      ExceptionUnhook,
      ExceptionRaise,
      Branch,
      ConditionalBranch,
      IndirectBranch,
      Switch,
      Fallthrough,
      Return,
      Throw,
      Trap
   };

   enum class EIROperandKind : unsigned char
   {
      None,
      Value,
      Block,
      Immediate,
      Reference,
      Location
   };

   enum class EIRLocation : unsigned char
   {
      ManagedValue,
      ManagedObject,
      CachedArgument0,
      CachedArgument1,
      AllocationSize,
      WideLow,
      WideHigh,
      StackPointer,
      FramePointer,
      SystemEnvironment,
      Count
   };

   struct EIRValue
   {
      pos_t   id;
      EIRType type;

      bool isValid() const;
   };

   struct EIROperand
   {
      EIROperandKind kind;
      EIRType        type;
      pos64_t        value;
   };

   struct EIRInstruction
   {
      EIROpcode opcode;
      EIREffect effects;
      EIRValue  result;
      pos_t     firstOperand;
      pos_t     operandCount;
      pos_t     sourceOffset;
   };

   struct EIRBlock
   {
      pos_t id;
      pos_t firstInstruction;
      pos_t instructionCount;
      pos_t sourceOffset;
   };

   enum class EIRVerifyError : unsigned char
   {
      None,
      EmptyFunction,
      InvalidBlockId,
      InvalidInstructionRange,
      UncoveredInstruction,
      MissingTerminator,
      EarlyTerminator,
      InvalidResult,
      DuplicateValue,
      InvalidOperandRange,
      InvalidOperand,
      UndefinedValue,
      ValueTypeMismatch,
      InvalidBlockTarget,
      InvalidPhi,
      InvalidPhiPredecessor,
      PhiAfterInstruction,
      InvalidTerminator,
      InvalidDispatch,
      InvalidVirtualMethod,
      InvalidManagedMethod,
      InvalidException,
      InvalidStackReference,
      InvalidFrame,
      InvalidMemory
   };

   class EIRFunction
   {
      CachedList<EIROperand, 64>     _operands;
      CachedList<EIRInstruction, 64> _instructions;
      CachedList<EIRBlock, 16>       _blocks;

   public:
      void clear();

      pos_t addOperand(EIROperand operand);
      pos_t addOperand(EIROperandKind kind, EIRType type, pos64_t value);

      pos_t addInstruction(EIRInstruction instruction);
      pos_t addInstruction(
         EIROpcode opcode,
         EIREffect effects,
         EIRValue result,
         pos_t firstOperand,
         pos_t operandCount,
         pos_t sourceOffset = 0);

      pos_t addBlock(EIRBlock block);
      pos_t addBlock(pos_t id, pos_t firstInstruction, pos_t instructionCount, pos_t sourceOffset = 0);

      pos_t operandCount() const;
      pos_t instructionCount() const;
      pos_t blockCount() const;

      EIROperand& operand(pos_t index);
      EIRInstruction& instruction(pos_t index);
      EIRBlock& block(pos_t index);

      EIRFunction() = default;
      EIRFunction(const EIRFunction&) = delete;
      EIRFunction& operator =(const EIRFunction&) = delete;
   };

   enum class ExceptionTargetKind : unsigned char
   {
      ProcedureLabel,
      Reference
   };

   struct ExceptionHookSpec
   {
      int                 frameOffset;
      pos64_t             target;
      ExceptionTargetKind targetKind;
      bool                threadLocal;
   };

   class ExceptionEIRProvider
   {
   public:
      static EIRVerifyError lower(const ExceptionHookSpec& spec, EIRFunction& function);
   };

   struct StackReferenceSpec
   {
      ThreadingMode threadingMode;

      bool contains(
         pos64_t reference,
         pos64_t stackPointer,
         pos64_t stackRoot) const;
   };

   class StackReferenceEIRProvider
   {
   public:
      static EIRVerifyError lower(const StackReferenceSpec& spec, EIRFunction& function);
   };

   struct FrameOpenSpec
   {
      unsigned int managedSlots;
      unsigned int unmanagedSize;
   };

   struct FrameCloseSpec
   {
      unsigned int argumentSize;
   };

   struct FrameOpenLayout
   {
      unsigned int managedSlots;
      unsigned int unmanagedSize;
   };

   class FrameEIRProvider
   {
   public:
      static bool layout(const FrameOpenSpec& spec, const TargetSpec& target, FrameOpenLayout& layout);
      static EIRVerifyError lower(const FrameOpenSpec& spec, const TargetSpec& target, EIRFunction& function);
      static EIRVerifyError lower(const FrameCloseSpec& spec, EIRFunction& function);
      static EIRVerifyError lowerExternal(const FrameOpenSpec& spec, const TargetSpec& target,
         ThreadingMode threadingMode, EIRFunction& function);
      static EIRVerifyError lowerExternal(
         const FrameCloseSpec& spec, ThreadingMode threadingMode, EIRFunction& function);
   };

   class StackEIRProvider
   {
   public:
      static EIRVerifyError lowerRootAllocation(EIRFunction& function);
   };

   struct MemoryCopySpec
   {
      unsigned int byteCount;
   };

   class MemoryEIRProvider
   {
   public:
      static EIRVerifyError lower(const MemoryCopySpec& spec, EIRFunction& function);
   };

   enum class ECodeEIRLowerError : unsigned char
   {
      None,
      UnsupportedOpcode,
      InvalidArgument,
      InvalidRuntime,
      InvalidEIR
   };

   enum class ECodeEIRKind : unsigned char
   {
      Operation,
      ManagedMethod,
      VirtualMethod,
      Dispatch,
      Allocation
   };

   enum class AllocationKind : unsigned char
   {
      FixedReference,
      FixedBinary,
      InlineBinary,
      DynamicReference,
      DynamicBinary,
      Permanent
   };

   struct AllocationSpec
   {
      AllocationKind kind;
      ref_t           vmtReference;
      unsigned int    elementSize;
      unsigned int    payloadSize;
      unsigned int    allocationSize;
      unsigned int    payloadMask;
   };

   struct ECodeEIRMetadata
   {
      ECodeEIRKind     kind;
      ManagedMethodSpec managedMethod;
      VirtualMethodSpec virtualMethod;
      DispatchSpec      dispatch;
      AllocationSpec    allocation;
   };

   class ECodeEIRProvider
   {
   public:
      static ECodeEIRLowerError lower(const ByteCommand& command, const RuntimeSpec& runtime,
         const TargetSpec& target, const ECodeLoweringContext& context, EIRFunction& function,
         ECodeEIRMetadata* metadata = nullptr);
   };

   class EIRVerifier
   {
      static EIRInstruction* findDefinition(EIRFunction& function, pos_t valueId);
      static EIRVerifyError verifyInstruction(EIRFunction& function, EIRInstruction& instruction);
      static EIRVerifyError verifyPhi(EIRFunction& function, EIRInstruction& instruction, pos_t blockId);
      static EIRVerifyError verifyTerminator(EIRFunction& function, EIRInstruction& instruction);
      static bool targetsBlock(EIRFunction& function, EIRInstruction& instruction, pos_t blockId);
      static bool isPredecessor(EIRFunction& function, pos_t predecessor, pos_t blockId);
      static pos_t predecessorCount(EIRFunction& function, pos_t blockId);

   public:
      static EIRVerifyError verify(EIRFunction& function);
   };

   class DispatchEIRProvider
   {
   public:
      static bool getPhase(EIRFunction& function, EIRBlock& block, DispatchPhase& phase);
      static EIRVerifyError lower(const DispatchSpec& spec, EIRFunction& function);
   };

   class VirtualMethodEIRProvider
   {
   public:
      static EIRVerifyError lower(const VirtualMethodSpec& spec, EIRFunction& function);
   };

   class ManagedMethodEIRProvider
   {
   public:
      static EIRVerifyError lower(const ManagedMethodSpec& spec, EIRFunction& function);
   };
}

#endif
