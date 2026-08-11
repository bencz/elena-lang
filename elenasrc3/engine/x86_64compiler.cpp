//---------------------------------------------------------------------------
//		E L E N A   P r o j e c t:  ELENA Compiler Engine
//
//		This file contains ELENA JIT-X linker class.
//		Supported platforms: x86-64
//                                              (C)2021-2026 by Aleksey Rakov
//---------------------------------------------------------------------------

#include "elena.h"
// --------------------------------------------------------------------------
#include "x86_64compiler.h"
#include "x86helper.h"
#include "langcommon.h"
#include "core.h"
#include "x86runtimecore.h"
#include "../codegen/x86/encoder.h"
#include "../codegen/x86/lowering.h"
#include "../codegen/x86/runtimecore.h"

using namespace elena_lang;

int X86_64JITCompiler :: calcFrameOffset(int argument, bool extMode)
{
   if (!extMode)
      return JITCompiler64::calcFrameOffset(argument, false);

   if (argument < 0)
      throw InternalError(
         errInvalidMachineCode,
         (int)codegen::x86::LowerError::InvalidArgument);

   codegen::x86::ExternalFrameLayout layout = {};
   int frameOffset = 0;
   bool layoutAvailable =
      codegen::x86::ExternalFrameLayoutProvider::get(_target, layout);
   bool offsetResolved = layoutAvailable
      && layout.resolveArgumentFrameOffset(
         _target,
         (unsigned int)argument,
         frameOffset);

   if (!offsetResolved)
   {
      throw InternalError(
         errInvalidMachineCode,
         (int)codegen::x86::LowerError::InvalidArgument);
   }

   return frameOffset;
}

void X86_64JITCompiler :: compileMigrated(JITCompilerScope* scope)
{
   X86_64JITCompiler* compiler = (X86_64JITCompiler*)scope->compiler;
   bool virtualMethodCommand = scope->command.code == ByteCode::VCallMR
      || scope->command.code == ByteCode::VJumpMR;
   bool directMethodCommand = scope->command.code == ByteCode::CallMR
      || scope->command.code == ByteCode::JumpMR;

   if (scope->command.code == ByteCode::OpenIN || scope->command.code == ByteCode::ExtOpenIN) {
      codegen::FrameOpenSpec frame = {
         .managedSlots = (unsigned int)scope->command.arg1,
         .unmanagedSize = (unsigned int)scope->command.arg2
      };
      codegen::FrameOpenLayout layout = {};
      if (scope->command.arg1 < 0 || scope->command.arg2 < 0
         || !codegen::FrameEIRProvider::layout(frame, compiler->_target, layout))
      {
         throw InternalError(errInvalidMachineCode, (int)codegen::x86::LowerError::InvalidArgument);
      }

      scope->command.arg1 = layout.managedSlots;
      scope->command.arg2 = layout.unmanagedSize;
      scope->frameOffset = compiler->calcFrameOffset(
         scope->command.arg2, scope->command.code == ByteCode::ExtOpenIN);
      scope->stackOffset = 0;
   }
   else if (scope->command.code == ByteCode::CloseN
      || scope->command.code == ByteCode::ExtCloseN)
   {
      scope->stackOffset = scope->constants->unframedOffset;
   }

   codegen::ECodeRuntimeCallSelection runtimeCall = codegen::ECodeRuntimeProvider::select(
      scope->command, compiler->_runtime);
   codegen::x86::Sequence sequence;
   const codegen::x86::RuntimeCallABI* callABI = compiler->_runtimeABIs.get(runtimeCall.operation);
   if (!callABI)
      throw InternalError(errInvalidMachineCode,
         (int)codegen::x86::LowerError::InvalidRuntime);
   codegen::x86::LoweringContext context = {
      .frameOffset = (int)scope->frameOffset,
      .stackOffset = (int)scope->stackOffset,
      .vmtSize = scope->constants->vmtSize,
      .platform = compiler->_target.platform,
      .alternativeMode = scope->command.code == ByteCode::DispatchMR
         || virtualMethodCommand || directMethodCommand ? scope->getAltMode() : false,
      .dataOffset = scope->constants->dataOffset,
      .dataHeader = scope->constants->dataHeader
   };
   codegen::x86::LowerError lowerError = codegen::x86::ECodeLowering::lower(
      scope->command, compiler->_runtime, compiler->_managedABI,
      *callABI, context, sequence);
   if (lowerError != codegen::x86::LowerError::None)
      throw InternalError(errInvalidMachineCode,
         encodeLowerError(scope->command.code, scope->command.arg1, lowerError));

   codegen::x86::Encoder encoder(compiler->_target, *scope->codeWriter);
   codegen::x86::EncodeError error = encoder.emit(sequence, compiler->_managedABI,
      runtimeCall.verifyEncoding ? callABI : nullptr);
   if (error != codegen::x86::EncodeError::None)
      throw InternalError(errInvalidMachineCode, (int)error);

   pos_t end = scope->codeWriter->position();
   for (pos_t i = 0; i < encoder.relocationCount(); i++) {
      codegen::x86::Relocation& relocation = encoder.relocation(i);
      if (!scope->codeWriter->seek(relocation.position))
         throw InternalError(errInvalidMachineCode,
            (int)codegen::x86::EncodeError::WriteFailed);
      switch (relocation.kind) {
         case codegen::x86::RelocationKind::RuntimeCall:
         {
            ref_t reference = 0;
            if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::AllocatePermanent) {
               reference = GC_ALLOCPERM;
            }
            else if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::Collect) {
               reference = GC_COLLECT;
            }
            else if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::WaitForGC) {
               reference = THREAD_WAIT;
            }
            else if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::Prepare) {
               reference = PREPARE;
            }
            else {
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);
            }

            unsigned int displacement = 0;
            writeCoreReference(scope, reference | mskCodeRelRef32,
               0, &displacement);
            break;
         }
         case codegen::x86::RelocationKind::ModuleReference:
            if (relocation.value != 1 && relocation.value != 2)
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);

            compiler->writeArgAddress(scope, relocation.value == 1
               ? scope->command.arg1 : scope->command.arg2, 0, mskRef64);
            break;
         case codegen::x86::RelocationKind::ModuleReferenceValue:
            compiler->writeArgAddress(scope, (ref_t)relocation.value, 0, mskRef64);
            break;
         case codegen::x86::RelocationKind::ModuleCode:
            if (scope->command.code != ByteCode::CallR
               || relocation.value != (unsigned int)scope->command.arg1)
            {
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);
            }

            compiler->writeArgAddress(scope, (ref_t)relocation.value,
               0, mskRelRef32);
            break;
         case codegen::x86::RelocationKind::ModuleMessage:
            if (relocation.value != (unsigned int)scope->command.arg1)
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);

            scope->codeWriter->writeDWord(
               scope->helper->importMessage(scope->command.arg1));
            break;
         case codegen::x86::RelocationKind::ThreadLocalOffset:
            if (relocation.value != (unsigned int)scope->command.arg1
               || ((ref_t)relocation.value & mskAnyRef) != mskTLSVariable)
            {
               throw InternalError(
                  errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);
            }

            compiler->writeArgAddress(
               scope,
               (ref_t)relocation.value,
               0,
               mskOffset32);
            break;
         case codegen::x86::RelocationKind::RuntimeData:
         {
            unsigned int offset = 0;
            ref_t reference = 0;
            switch ((codegen::RuntimeDataReference)relocation.value) {
               case codegen::RuntimeDataReference::ThreadTableSlots:
                  reference = CORE_THREAD_TABLE;
                  offset = compiler->_runtime.dataLayout.threadTable.slots;
                  break;
               case codegen::RuntimeDataReference::GCDataLock:
                  reference = CORE_GC_TABLE;
                  offset = compiler->_runtime.dataLayout.gc.lock;
                  break;
               case codegen::RuntimeDataReference::GCDataSignal:
                  reference = CORE_GC_TABLE;
                  offset = compiler->_runtime.dataLayout.gc.signal;
                  break;
               case codegen::RuntimeDataReference::SingleContent:
                  reference = CORE_SINGLE_CONTENT;
                  break;
               case codegen::RuntimeDataReference::SingleContentStackRoot:
                  reference = CORE_SINGLE_CONTENT;
                  offset = compiler->_runtime.dataLayout.threadContent.stackRoot;
                  break;
               case codegen::RuntimeDataReference::SingleContentStackFrame:
                  reference = CORE_SINGLE_CONTENT;
                  offset = compiler->_runtime.dataLayout.threadContent.stackFrame;
                  break;
               case codegen::RuntimeDataReference::SystemEnvironment:
                  reference = SYSTEM_ENV;
                  break;
               default:
                  throw InternalError(errInvalidMachineCode,
                     (int)codegen::x86::EncodeError::InvalidOperand);
            }
            ref_t mask = relocation.value == (unsigned int)
                  codegen::RuntimeDataReference::SystemEnvironment
               ? mskRDataRef64 : mskDataRef64;
            writeCoreReference(scope, reference | mask, 0, &offset);
            break;
         }
         case codegen::x86::RelocationKind::Metadata:
         {
            unsigned int addend = 0;
            writeMDataReference(scope, mskMDataRef64, 0, &addend);
            break;
         }
         case codegen::x86::RelocationKind::RuntimeConstant:
         {
            if (relocation.value != (unsigned int)
               codegen::MachineRuntimeConstant::VoidReference)
            {
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);
            }

            unsigned int addend = compiler->_runtime.objectLayout.headerSize;
            writeCoreReference(scope, VOIDPTR | mskRDataRef64, 0, &addend);
            break;
         }
         case codegen::x86::RelocationKind::ProcedureLabel:
            if (scope->command.code != ByteCode::XHookDPR
               || relocation.value
                  != (unsigned int)(scope->command.arg2 & ~mskAnyRef))
            {
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);
            }

            scope->lh->writeLabelAddress(
               (pos_t)relocation.value,
               *scope->codeWriter,
               mskRef64);
            break;
         case codegen::x86::RelocationKind::VMTMethodOffset:
         case codegen::x86::RelocationKind::HMTMethodOffset:
         {
            if (relocation.value != (unsigned int)scope->command.arg1)
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);

            ref_t classReference = scope->command.arg2 & ~mskAnyRef;
            ref_t methodMask = relocation.kind
                  == codegen::x86::RelocationKind::HMTMethodOffset
               ? mskHMTMethodOffset : mskVMTMethodOffset;
            compiler->writeVMTMethodArg(scope,
               classReference | methodMask,
               0,
               scope->helper->importMessage(scope->command.arg1),
               mskRef32);
            break;
         }
         case codegen::x86::RelocationKind::VMTMethodAddress:
         case codegen::x86::RelocationKind::HMTMethodAddress:
         {
            if ((scope->command.code != ByteCode::CallMR
                  && scope->command.code != ByteCode::JumpMR)
               || relocation.value != (unsigned int)scope->command.arg1)
            {
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::x86::EncodeError::InvalidOperand);
            }

            ref_t classReference = scope->command.arg2 & ~mskAnyRef;
            ref_t methodMask = relocation.kind
                  == codegen::x86::RelocationKind::HMTMethodAddress
               ? mskHMTMethodAddress : mskVMTMethodAddress;
            compiler->writeVMTMethodArg(scope,
               classReference | methodMask,
               0,
               scope->helper->importMessage(scope->command.arg1),
               mskRelRef32);
            break;
         }
         default:
            throw InternalError(errInvalidMachineCode,
               (int)codegen::x86::EncodeError::InvalidOperand);
      }
   }
   if (!scope->codeWriter->seek(end))
      throw InternalError(errInvalidMachineCode,
         (int)codegen::x86::EncodeError::WriteFailed);
}

X86_64JITCompiler :: X86_64JITCompiler(codegen::TargetPlatform platform)
   : JITCompiler64(), _runtime {}, _target {}, _managedABI {}, _runtimeABIs {}
{
   _constants.dataOffset = 0x0C;
   _constants.unframedOffset = 1;

   if (!codegen::TargetProvider::get(platform, _target)
      || _target.architecture != codegen::Architecture::AMD64
      || !codegen::x86::ManagedABIProvider::get(
         codegen::Architecture::AMD64, _managedABI))
   {
      throw InternalError(errInvalidMachineCode, 0);
   }
   _runtime = codegen::RuntimeProvider::legacy(
      codegen::ThreadingMode::SingleThread, _target);
   if (!_runtimeABIs.initialize(_runtime, _managedABI))
   {
      throw InternalError(errInvalidMachineCode, 0);
   }

   registerMigratedCode(ByteCode::ConvL, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LNeg, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Coalesce, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Not, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Neg, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XPeekEq, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Class, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Save, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Load, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Len, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::BLoad, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::WLoad, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MovFrm, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MLen, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XAssign, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LLoad, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XLoad, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XLLoad, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LSave, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Parent, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XGet, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LoadZ, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::WLoadZ, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::NLen, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::FillIR, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SetR, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MovN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::AddN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SubN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::AndN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CmpN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Shl, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Shr, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XSaveN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MovM, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::OrN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MulN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SaveDP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::StoreFI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SaveSI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::StoreSI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XFlushSI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::GetI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XRefreshSI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::PeekFI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::PeekSI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LSaveDP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LLoadDP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XStoreI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SetDP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SetFP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SetSP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LoadDP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XCmpDP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XAddDP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XSetFP, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XAssignI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XCreateR, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LLoadSI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LoadSI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XLoadArgFI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::ExtOpenIN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::ExtCloseN, X86_64JITCompiler::compileMigrated);

   // OpenIN 0xF0 opens a managed frame; CloseN 0x91 closes it.
   registerMigratedCode(ByteCode::OpenIN, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CloseN, X86_64JITCompiler::compileMigrated);

   // Copy 0x90 copies arg1 bytes from the source register to the object register.
   registerMigratedCode(ByteCode::Copy, X86_64JITCompiler::compileMigrated);

   // TstStck 0x17 classifies the object register against the current thread's stack interval.
   registerMigratedCode(ByteCode::TstStck, X86_64JITCompiler::compileMigrated);

   // TryLock 2B and FreeLock 2C synchronize the object's header lock byte.
   registerMigratedCode(ByteCode::TryLock, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::FreeLock, X86_64JITCompiler::compileMigrated);

   // PeekTLS BB and StoreTLS BC access a module thread-local reference.
   registerMigratedCode(ByteCode::PeekTLS, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::StoreTLS, X86_64JITCompiler::compileMigrated);

   // Throw 0A transfers to the active handler; Unhook 0B restores its frame and predecessor.
   // XHookDPR E6 publishes a handler; Exclude 10 and Include 11 delimit a safe region.
   registerMigratedCode(ByteCode::Throw, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Unhook, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XHookDPR, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Exclude, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Include, X86_64JITCompiler::compileMigrated);

   // SNop 02; Quit 04; XJump 27; XCall 2F; XQuit 34.
   registerMigratedCode(ByteCode::SNop, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Quit, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XJump, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XCall, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XQuit, X86_64JITCompiler::compileMigrated);

   // MovEnv 05; LoadV 0C; XCmp 0D; PeekR 84; StoreR 85.
   registerMigratedCode(ByteCode::MovEnv, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LoadV, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XCmp, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::PeekR, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::StoreR, X86_64JITCompiler::compileMigrated);

   // CallR B0; CallVI B1; JumpVI B5; JumpMR ED; CallMR FD.
   registerMigratedCode(ByteCode::CallR, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CallVI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::JumpVI, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::JumpMR, X86_64JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CallMR, X86_64JITCompiler::compileMigrated);

   // XDispatchMR FA: direct fixed, variadic, receiver-list, and variadic receiver-list dispatch.
   registerMigratedInlines(ByteCode::XDispatchMR,
      (1u << 0) | (1u << 5) | (1u << 9) | (1u << 10), X86_64JITCompiler::compileMigrated);

   // DispatchMR FB: virtual fixed and variadic dispatch through the primary or alternative VMT.
   registerMigratedInlines(ByteCode::DispatchMR,
      (1u << 0) | (1u << 5) | (1u << 6) | (1u << 11), X86_64JITCompiler::compileMigrated);

   // VCallMR FC and VJumpMR EC: primary or alternative VMT method transfer.
   registerMigratedInlines(ByteCode::VCallMR, (1u << 0) | (1u << 6), X86_64JITCompiler::compileMigrated);
   registerMigratedInlines(ByteCode::VJumpMR, (1u << 0) | (1u << 6), X86_64JITCompiler::compileMigrated);

   // System CF: runtime lifecycle, collection, GC locking, root-stack allocation, and safe regions.
   registerMigratedInlines(ByteCode::System,
      (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4)
         | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9),
      X86_64JITCompiler::compileMigrated);
}

bool X86_64JITCompiler :: compileCoreRoutine(ref_t reference, JITCompilerScope& scope)
{
   return compileX86CoreRoutine(reference, _target,
      _runtime, _managedABI, _runtimeABIs, scope);
}

constexpr auto OverloadsCount = 4;
const Pair<ByteCode, CodeGenerator, ByteCode::None, nullptr> Overloads[OverloadsCount] =
{
   { ByteCode::CallExtR, x86_64loadCallOp},
   { ByteCode::FreeI, x86_64compileStackOp},
   { ByteCode::AllocI, x86_64compileStackOp},
   { ByteCode::XOpenIN, x86_64compileXOpenIN},
};

static inline void x86_64AllocStack(int args, MemoryWriter* code)
{
   // sub esp, arg
   if (args < 0x80) {
      code->writeByte(0x48);
      code->writeWord(0xEC83);
      code->writeByte(args << 3);
   }
   else {
      code->writeByte(0x48);
      code->writeWord(0xEC81);
      code->writeDWord(args << 3);
   }
}

static inline void x86_64FreeStack(int args, MemoryWriter* code)
{
   // add rsp, arg
   if (args < 0x80) {
      code->writeByte(0x48);
      code->writeWord(0xC483);
      code->writeByte((char)(args << 3));
   }
   else {
      code->writeByte(0x48);
      code->writeWord(0xC481);
      code->writeDWord(args << 3);
   }
}

void elena_lang::x86_64loadCallOp(JITCompilerScope* scope)
{
   MemoryWriter* writer = scope->codeWriter;

   int argsToFree = 0;
   switch (scope->command.arg2) {
      case 0:
         argsToFree = 4;
         x86_64AllocStack(argsToFree, writer);
         break;
      case 1:
      case 2:
         argsToFree = 2;
         x86_64AllocStack(argsToFree, writer);
         break;
      default:
         // to make compiler happy
         break;
   }

   void* code = nullptr;
   switch (scope->command.arg2) {
      case 0:
         code = ((X86_64JITCompiler*)scope->compiler)->_inlines[1][scope->code()];
         break;
      case 1:
         code = ((X86_64JITCompiler*)scope->compiler)->_inlines[2][scope->code()];
         break;
      case 2:
         code = ((X86_64JITCompiler*)scope->compiler)->_inlines[3][scope->code()];
         break;
      case 3:
         code = ((X86_64JITCompiler*)scope->compiler)->_inlines[4][scope->code()];
         break;
      case 4:
         code = ((X86_64JITCompiler*)scope->compiler)->_inlines[5][scope->code()];
         break;
      default:
         code = ((X86_64JITCompiler*)scope->compiler)->_inlines[0][scope->code()];
         break;
   }

   pos_t position = writer->position();
   pos_t length = *(pos_t*)((char*)code - sizeof(pos_t));

   // simply copy correspondent inline code
   writer->write(code, length);

   // resolve section references
   pos_t count = *(pos_t*)((char*)code + length);
   RelocationEntry* entries = (RelocationEntry*)((char*)code + length + sizeof(pos_t));
   while (count > 0) {
      // locate relocation position
      writer->seek(position + entries->offset);
      if (entries->reference == RELPTR32_1) {
         ((X86_64JITCompiler*)scope->compiler)->writeArgAddress(scope, scope->command.arg1, 0, mskRelRef32);
      }
      //else writeCoreReference();

      entries++;
      count--;
   }
   writer->seekEOF();

   if (argsToFree > 0) {
      x86_64FreeStack(argsToFree, writer);
   }
}

void elena_lang::x86_64compileStackOp(JITCompilerScope* scope)
{
   // NOTE : stack should be aligned to 16 bytes
   scope->command.arg1 = align(scope->command.arg1, 2);

   elena_lang::loadIndexOp(scope);
}

void elena_lang::x86_64compileXOpenIN(JITCompilerScope* scope)
{
   // NOTE : stack should be aligned to 16 bytes
   scope->command.arg1 = align(scope->command.arg1, 2);
   scope->command.arg2 = align(scope->command.arg2, 16);

   elena_lang::compileXOpen(scope);
}

// --- X86_64JITCompiler ---

void X86_64JITCompiler :: prepare(
   LibraryLoaderBase* loader, 
   ImageProviderBase* imageProvider, 
   ReferenceHelperBase* helper,
   LabelHelperBase*,
   ProcessSettings& settings,
   bool virtualMode)
{
   _runtime = codegen::RuntimeProvider::legacy(
      settings.threadCounter > 1 ? codegen::ThreadingMode::MultiThread
         : codegen::ThreadingMode::SingleThread,
      _target);
   if (!_runtimeABIs.initialize(_runtime, _managedABI))
   {
      throw InternalError(errInvalidMachineCode, 0);
   }

   if (!supportsMigrationProfile(
         _runtime.threadingMode == codegen::ThreadingMode::MultiThread))
   {
      throw InternalError(
         errInvalidMachineCode,
         (int)codegen::x86::LowerError::InvalidRuntime);
   }

   _constants.inlineMask = mskCodeRelRef32;
   if (_runtime.threadingMode == codegen::ThreadingMode::SingleThread) {
      _migratedInlineMasks[(unsigned char)ByteCode::System]
         &= ~(1u << 3);
   }

   // override code generators
   auto commands = codeGenerators();

   for (size_t i = 0; i < OverloadsCount; i++)
      commands[(int)Overloads[i].value1] = Overloads[i].value2;

   X86LabelHelper lh;
   JITCompiler64::prepare(loader, imageProvider, helper, &lh, settings, virtualMode);
}

void X86_64JITCompiler :: writeImm9(MemoryWriter*/* writer*/, int, int)
{
   throw InternalError(errNotImplemented);
}

void X86_64JITCompiler :: writeImm12(MemoryWriter*/* writer*/, int, int)
{
   throw InternalError(errNotImplemented);
}

void X86_64JITCompiler :: alignCode(MemoryWriter& writer, pos_t alignment, bool isText)
{
   writer.align(alignment, isText ? 0x90 : 0x00);
}

void X86_64JITCompiler :: compileProcedure(ReferenceHelperBase* helper, MemoryReader& bcReader, 
   MemoryWriter& codeWriter, LabelHelperBase*)
{
   X86LabelHelper lh;

   JITCompiler::compileProcedure(helper, bcReader, codeWriter, &lh);

   alignCode(codeWriter, 0x08, true);
}

void X86_64JITCompiler :: compileSymbol(ReferenceHelperBase* helper, MemoryReader& bcReader, 
   MemoryWriter& codeWriter, LabelHelperBase*)
{
   X86LabelHelper lh;

   JITCompiler64::compileSymbol(helper, bcReader, codeWriter, &lh);

   alignCode(codeWriter, 0x08, true);
}
