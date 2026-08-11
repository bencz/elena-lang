//---------------------------------------------------------------------------
//		E L E N A   P r o j e c t:  ELENA Compiler Engine
//
//		This file contains ELENA JIT-X linker class.
//		Supported platforms: x86
//                                                 (C)2021-2025, by Aleksey Rakov
//---------------------------------------------------------------------------

#include "elena.h"
// --------------------------------------------------------------------------
#include "x86compiler.h"
#include "x86helper.h"
#include "langcommon.h"
#include "core.h"
#include "x86runtimecore.h"
#include "../codegen/x86/encoder.h"
#include "../codegen/x86/lowering.h"
#include "../codegen/x86/runtimecore.h"

using namespace elena_lang;

int X86JITCompiler :: calcFrameOffset(int argument, bool extMode)
{
   if (!extMode)
      return JITCompiler32::calcFrameOffset(argument, false);

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

bool X86JITCompiler :: compileCoreRoutine(ref_t reference, JITCompilerScope& scope)
{
   return compileX86CoreRoutine(reference, _target,
      _runtime, _managedABI, _runtimeABIs, scope);
}

void X86JITCompiler :: compileMigrated(JITCompilerScope* scope)
{
   X86JITCompiler* compiler = (X86JITCompiler*)scope->compiler;
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
         throw InternalError(errInvalidMachineCode, (int)codegen::x86::EncodeError::WriteFailed);

      switch (relocation.kind) {
         case codegen::x86::RelocationKind::RuntimeCall:
         {
            ref_t reference = 0;
            if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::AllocateYoung)
            {
               reference = GC_ALLOC;
            }
            else if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::AllocatePermanent)
            {
               reference = GC_ALLOCPERM;
            }
            else if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::Collect)
            {
               reference = GC_COLLECT;
            }
            else if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::WaitForGC)
            {
               reference = THREAD_WAIT;
            }
            else if (relocation.value
               == (unsigned int)codegen::RuntimeOperation::Prepare)
            {
               reference = PREPARE;
            }
            else {
               throw InternalError(errInvalidMachineCode, (int)codegen::x86::EncodeError::InvalidOperand);
            }

            unsigned int displacement = 0;
            writeCoreReference(scope, reference | mskCodeRelRef32,
               0, &displacement);
            break;
         }
         case codegen::x86::RelocationKind::ModuleReference:
            if (relocation.value != 1 && relocation.value != 2)
               throw InternalError(errInvalidMachineCode, (int)codegen::x86::EncodeError::InvalidOperand);

            compiler->writeArgAddress(scope, relocation.value == 1
               ? scope->command.arg1 : scope->command.arg2, 0, mskRef32);
            break;
         case codegen::x86::RelocationKind::ModuleReferenceValue:
            compiler->writeArgAddress(scope, (ref_t)relocation.value, 0, mskRef32);
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
               ? mskRDataRef32 : mskDataRef32;
            writeCoreReference(scope, reference | mask, 0, &offset);
            break;
         }
         case codegen::x86::RelocationKind::Metadata:
         {
            unsigned int addend = 0;
            writeMDataReference(scope, mskMDataRef32, 0, &addend);
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
            writeCoreReference(scope, VOIDPTR | mskRDataRef32, 0, &addend);
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
               mskRef32);
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
            throw InternalError(errInvalidMachineCode, (int)codegen::x86::EncodeError::InvalidOperand);
      }
   }
   if (!scope->codeWriter->seek(end))
      throw InternalError(errInvalidMachineCode, (int)codegen::x86::EncodeError::WriteFailed);
}

X86JITCompiler :: X86JITCompiler(codegen::TargetPlatform platform)
   : JITCompiler32(), _runtime {}, _target {}, _managedABI {}, _runtimeABIs {}
{
   _constants.unframedOffset = 1;

   if (!codegen::TargetProvider::get(platform, _target)
      || _target.architecture != codegen::Architecture::X86
      || !codegen::x86::ManagedABIProvider::get(codegen::Architecture::X86, _managedABI))
   {
      throw InternalError(errInvalidMachineCode, (int)codegen::x86::LowerError::InvalidABI);
   }
   _runtime = codegen::RuntimeProvider::legacy(
      codegen::ThreadingMode::SingleThread, _target);
   if (!_runtimeABIs.initialize(_runtime, _managedABI))
   {
      throw InternalError(errInvalidMachineCode, (int)codegen::x86::LowerError::InvalidRuntime);
   }

   registerMigratedCode(ByteCode::ConvL, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LNeg, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Coalesce, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Not, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Neg, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XPeekEq, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Class, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Save, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Load, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Len, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::BLoad, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::WLoad, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MovFrm, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MLen, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XAssign, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LLoad, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XLoad, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XLLoad, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LSave, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Parent, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XGet, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LoadZ, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::WLoadZ, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::NLen, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::FillIR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SetR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MovN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::AddN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SubN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::AndN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CmpN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Shl, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Shr, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XSaveN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MovM, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::OrN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::MulN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SaveDP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::StoreFI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SaveSI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::StoreSI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XFlushSI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::GetI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XRefreshSI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::PeekFI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::PeekSI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LSaveDP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LLoadDP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XStoreI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SetDP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SetFP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::SetSP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LoadDP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XCmpDP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XAddDP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XSetFP, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XAssignI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::NewIR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::NewNR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XNewNR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CreateR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CreateNR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XCreateR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LLoadSI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LoadSI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XLoadArgFI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::ExtOpenIN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::ExtCloseN, X86JITCompiler::compileMigrated);

   // OpenIN 0xF0 opens a managed frame; CloseN 0x91 closes it.
   registerMigratedCode(ByteCode::OpenIN, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CloseN, X86JITCompiler::compileMigrated);

   // Copy 0x90 copies arg1 bytes from the source register to the object register.
   registerMigratedCode(ByteCode::Copy, X86JITCompiler::compileMigrated);

   // TstStck 0x17 classifies the object register against the current thread's stack interval.
   registerMigratedCode(ByteCode::TstStck, X86JITCompiler::compileMigrated);

   // TryLock 2B and FreeLock 2C synchronize the object's header lock byte.
   registerMigratedCode(ByteCode::TryLock, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::FreeLock, X86JITCompiler::compileMigrated);

   // PeekTLS BB and StoreTLS BC access a module thread-local reference.
   registerMigratedCode(ByteCode::PeekTLS, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::StoreTLS, X86JITCompiler::compileMigrated);

   // Throw 0A transfers to the active handler; Unhook 0B restores its frame and predecessor.
   // XHookDPR E6 publishes a handler; Exclude 10 and Include 11 delimit a safe region.
   registerMigratedCode(ByteCode::Throw, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Unhook, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XHookDPR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Exclude, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Include, X86JITCompiler::compileMigrated);

   // SNop 02; Quit 04; XJump 27; XCall 2F; XQuit 34.
   registerMigratedCode(ByteCode::SNop, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::Quit, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XJump, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XCall, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XQuit, X86JITCompiler::compileMigrated);

   // MovEnv 05; LoadV 0C; XCmp 0D; PeekR 84; StoreR 85.
   registerMigratedCode(ByteCode::MovEnv, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::LoadV, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::XCmp, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::PeekR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::StoreR, X86JITCompiler::compileMigrated);

   // CallR B0; CallVI B1; JumpVI B5; JumpMR ED; CallMR FD.
   registerMigratedCode(ByteCode::CallR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CallVI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::JumpVI, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::JumpMR, X86JITCompiler::compileMigrated);
   registerMigratedCode(ByteCode::CallMR, X86JITCompiler::compileMigrated);

   // XDispatchMR FA: direct fixed, variadic, receiver-list, and variadic receiver-list dispatch.
   registerMigratedInlines(ByteCode::XDispatchMR,
      (1u << 0) | (1u << 5) | (1u << 9) | (1u << 10), X86JITCompiler::compileMigrated);

   // DispatchMR FB: virtual fixed and variadic dispatch through the primary or alternative VMT.
   registerMigratedInlines(ByteCode::DispatchMR,
      (1u << 0) | (1u << 5) | (1u << 6) | (1u << 11), X86JITCompiler::compileMigrated);

   // VCallMR FC and VJumpMR EC: primary or alternative VMT method transfer.
   registerMigratedInlines(ByteCode::VCallMR, (1u << 0) | (1u << 6), X86JITCompiler::compileMigrated);
   registerMigratedInlines(ByteCode::VJumpMR, (1u << 0) | (1u << 6), X86JITCompiler::compileMigrated);

   // System CF: runtime lifecycle, collection, GC locking, root-stack allocation, and safe regions.
   registerMigratedInlines(ByteCode::System,
      (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4)
         | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9),
      X86JITCompiler::compileMigrated);
}

void X86JITCompiler :: prepare(
   LibraryLoaderBase* loader, 
   ImageProviderBase* imageProvider, 
   ReferenceHelperBase* helper,
   LabelHelperBase*,
   ProcessSettings& settings,
   bool virtualMode)
{
   X86LabelHelper lh;

   _runtime = codegen::RuntimeProvider::legacy(
      settings.threadCounter > 1 ? codegen::ThreadingMode::MultiThread
         : codegen::ThreadingMode::SingleThread,
      _target);
   if (!_runtimeABIs.initialize(_runtime, _managedABI))
   {
      throw InternalError(errInvalidMachineCode, (int)codegen::x86::LowerError::InvalidRuntime);
   }

   if (!supportsMigrationProfile(
         _runtime.threadingMode == codegen::ThreadingMode::MultiThread))
   {
      throw InternalError(
         errInvalidMachineCode,
         (int)codegen::x86::LowerError::InvalidRuntime);
   }

   _constants.inlineMask = mskCodeRef32;
   if (_runtime.threadingMode == codegen::ThreadingMode::SingleThread) {
      _migratedInlineMasks[(unsigned char)ByteCode::System]
         &= ~(1u << 3);
   }

   JITCompiler32::prepare(loader, imageProvider, helper, &lh, settings, virtualMode);
}

void X86JITCompiler :: writeImm9(MemoryWriter*/* writer*/, int/* value*/, int/* type*/)
{
   throw InternalError(errNotImplemented);
}

void X86JITCompiler :: writeImm12(MemoryWriter*/* writer*/, int/* value*/, int/* type*/)
{
   throw InternalError(errNotImplemented);
}

void X86JITCompiler :: alignCode(MemoryWriter& writer, pos_t alignment, bool isText)
{
   writer.align(alignment, isText ? 0x90 : 0x00);
}

void X86JITCompiler :: alignJumpAddress(MemoryWriter& writer)
{
   int bytesToFill = 0x10 - (writer.position() & 0xF);
   switch (bytesToFill) {
      case 1:
         writer.writeByte(0x90);
         break;
      case 2:
         writer.writeWord(0x9066);
         break;
      case 3:
         writer.writeByte(0x0F);
         writer.writeWord(0x1F);
         break;
      case 4:
         writer.writeByte(0x0F);
         writer.writeWord(0x401F);
         writer.writeByte(0);
         break;
      case 5:
         writer.writeByte(0x0F);
         writer.writeWord(0x441F);
         writer.writeWord(0);
         break;
      case 6:
         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x441F);
         writer.writeWord(0);
         break;
      case 7:
         writer.writeByte(0x0F);
         writer.writeWord(0x801F);
         writer.writeDWord(0);
         break;
      case 8:
         writer.writeByte(0x0F);
         writer.writeWord(0x841F);
         writer.writeDWord(0);
         writer.writeByte(0);
         break;
      case 9:
         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x841F);
         writer.writeDWord(0);
         writer.writeByte(0);
         break;
      case 10:
         writer.writeByte(0x90);

         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x841F);
         writer.writeDWord(0);
         writer.writeByte(0);
         break;
      case 11:
         writer.writeWord(0x9066);

         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x841F);
         writer.writeDWord(0);
         writer.writeByte(0);
         break;
      case 12:
         writer.writeByte(0x0F);
         writer.writeWord(0x1F);

         writer.writeByte(0x0F);
         writer.writeWord(0x1F);
         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x841F);
         writer.writeDWord(0);
         writer.writeByte(0);
         break;
      case 13:
         writer.writeByte(0x0F);
         writer.writeWord(0x401F);
         writer.writeByte(0);

         writer.writeByte(0x0F);
         writer.writeWord(0x1F);
         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x841F);
         writer.writeDWord(0);
         writer.writeByte(0);
         break;
      case 14:
         writer.writeByte(0x0F);
         writer.writeWord(0x441F);
         writer.writeWord(0);

         writer.writeByte(0x0F);
         writer.writeWord(0x1F);
         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x841F);
         writer.writeDWord(0);
         writer.writeByte(0);
         break;
      case 15:
         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x441F);
         writer.writeWord(0);

         writer.writeByte(0x0F);
         writer.writeWord(0x1F);
         writer.writeByte(0x66);
         writer.writeByte(0x0F);
         writer.writeWord(0x841F);
         writer.writeDWord(0);
         writer.writeByte(0);
         break;
      default:
         break;
   }
}

void X86JITCompiler :: compileProcedure(ReferenceHelperBase* helper, MemoryReader& bcReader, 
   MemoryWriter& codeWriter, LabelHelperBase*)
{
   X86LabelHelper lh;

   JITCompiler::compileProcedure(helper, bcReader, codeWriter, &lh);

   alignCode(codeWriter, 0x04, true);
}

void X86JITCompiler :: compileSymbol(ReferenceHelperBase* helper, MemoryReader& bcReader, 
   MemoryWriter& codeWriter, LabelHelperBase*)
{
   X86LabelHelper lh;

   JITCompiler32::compileSymbol(helper, bcReader, codeWriter, &lh);

   alignCode(codeWriter, 0x04, true);
}
