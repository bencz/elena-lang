#ifndef X86RUNTIMECORE_H
#define X86RUNTIMECORE_H

#include "jitcompiler.h"
#include "core.h"
#include "langcommon.h"
#include "../codegen/x86/lowering.h"
#include "../codegen/x86/runtimecore.h"

namespace elena_lang
{
   inline int encodeLowerError(
      ByteCode opcode,
      int argument,
      codegen::x86::LowerError error)
   {
      return ((unsigned char)opcode << 24)
         | ((unsigned char)error << 16)
         | (argument & 0xFFFF);
   }

   inline bool compileX86CoreRoutine(ref_t reference, const codegen::TargetSpec& target,
      const codegen::RuntimeSpec& runtime, const codegen::x86::ManagedABI& managedABI,
      const codegen::x86::RuntimeABISet& runtimeABIs,
      JITCompilerScope& scope)
   {
      bool exceptionDispatcher = reference == EXCEPTION_HANDLER;
      codegen::RuntimeOperation operation = codegen::RuntimeOperation::AllocateYoung;
      const codegen::x86::RuntimeCallABI* callABI = nullptr;

      if (!exceptionDispatcher) {
         if (reference == GC_ALLOC) {
            operation = codegen::RuntimeOperation::AllocateYoung;
         }
         else if (reference == GC_ALLOCPERM) {
            operation = codegen::RuntimeOperation::AllocatePermanent;
         }
         else if (reference == PREPARE) {
            operation = codegen::RuntimeOperation::Prepare;
         }
         else if (reference == GC_COLLECT) {
            operation = codegen::RuntimeOperation::Collect;
         }
         else if (reference == THREAD_WAIT) {
            operation = codegen::RuntimeOperation::WaitForGC;
         }
         else {
            return false;
         }

         callABI = runtimeABIs.get(operation);
         if (!callABI)
            return false;
      }

      codegen::x86::RuntimeCoreEncoder encoder(target, *scope.codeWriter);
      codegen::RuntimeCoreError error = exceptionDispatcher
         ? encoder.encode(
            codegen::RuntimeCoreEntry::ExceptionDispatcher,
            runtime,
            managedABI)
         : encoder.encode(operation, runtime, managedABI, *callABI);
      if (error != codegen::RuntimeCoreError::None)
         throw InternalError(errInvalidMachineCode, (int)error);

      if (!scope.helper)
         return true;

      pos_t end = scope.codeWriter->position();
      for (pos_t i = 0; i < encoder.relocationCount(); i++) {
         codegen::RuntimeCoreRelocation& relocation = encoder.relocation(i);
         if (!scope.codeWriter->seek(relocation.position))
            throw InternalError(errInvalidMachineCode, (int)codegen::RuntimeCoreError::WriteFailed);

         unsigned long long value = (unsigned int)relocation.addend;
         switch (relocation.symbol) {
            case codegen::RuntimeCoreSymbol::GCData:
               if (target.architecture == codegen::Architecture::X86) {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute32)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, CORE_GC_TABLE | mskDataRef32, 0, &value);
               }
               else {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute64)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, CORE_GC_TABLE | mskDataRef64, 0, &value);
               }
               break;
            case codegen::RuntimeCoreSymbol::SingleContent:
               if (target.architecture == codegen::Architecture::X86) {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute32)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, CORE_SINGLE_CONTENT | mskDataRef32,
                     0, &value);
               }
               else {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute64)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, CORE_SINGLE_CONTENT | mskDataRef64,
                     0, &value);
               }
               break;
            case codegen::RuntimeCoreSymbol::ThreadTable:
               if (target.architecture == codegen::Architecture::X86) {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute32)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, CORE_THREAD_TABLE | mskDataRef32,
                     0, &value);
               }
               else {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute64)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, CORE_THREAD_TABLE | mskDataRef64,
                     0, &value);
               }
               break;
            case codegen::RuntimeCoreSymbol::SystemEnvironment:
               if (target.architecture == codegen::Architecture::X86) {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute32)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, SYSTEM_ENV | mskRDataRef32, 0, &value);
               }
               else {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute64)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, SYSTEM_ENV | mskRDataRef64, 0, &value);
               }
               break;
            case codegen::RuntimeCoreSymbol::StaticRoots:
               if (target.architecture == codegen::Architecture::X86) {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute32)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, mskStatDataRef32, 0, &value);
               }
               else {
                  if (relocation.kind != codegen::RuntimeCoreRelocationKind::Absolute64)
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);

                  writeCoreReference(&scope, mskStatDataRef64, 0, &value);
               }
               break;
            case codegen::RuntimeCoreSymbol::CollectYoung:
            case codegen::RuntimeCoreSymbol::AllocateYoungRoutine:
               if (relocation.kind != codegen::RuntimeCoreRelocationKind::Relative32)
                  throw InternalError(errInvalidMachineCode,
                     (int)codegen::RuntimeCoreError::InvalidOperation);

               value = 0;
               writeCoreReference(&scope,
                  (relocation.symbol == codegen::RuntimeCoreSymbol::CollectYoung
                     ? GC_COLLECT : GC_ALLOC) | mskCodeRelRef32,
                  0, &value);
               break;
            case codegen::RuntimeCoreSymbol::CollectPermanent:
            case codegen::RuntimeCoreSymbol::CollectRuntime:
               if (target.architecture == codegen::Architecture::X86) {
                  if (relocation.kind
                     != codegen::RuntimeCoreRelocationKind::ExternalAbsolute32)
                  {
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);
                  }

                  scope.helper->writeExternalReference(
                     *scope.codeWriter->Memory(), scope.codeWriter->position(),
                     relocation.symbol == codegen::RuntimeCoreSymbol::CollectRuntime
                        ? "$rt.CollectGCLA" : "$rt.CollectPermGCLA",
                     0, mskRef32);
               }
               else {
                  if (relocation.kind
                     != codegen::RuntimeCoreRelocationKind::ExternalRelative32)
                  {
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);
                  }

                  scope.helper->writeExternalReference(
                     *scope.codeWriter->Memory(), scope.codeWriter->position(),
                     relocation.symbol == codegen::RuntimeCoreSymbol::CollectRuntime
                        ? "$rt.CollectGCLA" : "$rt.CollectPermGCLA",
                     0, mskRelRef32);
               }
               break;
            case codegen::RuntimeCoreSymbol::PrepareRuntime:
               if (target.architecture == codegen::Architecture::X86) {
                  if (relocation.kind
                     != codegen::RuntimeCoreRelocationKind::ExternalAbsolute32)
                  {
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);
                  }

                  scope.helper->writeExternalReference(
                     *scope.codeWriter->Memory(), scope.codeWriter->position(),
                     "$rt.PrepareLA", 0, mskRef32);
               }
               else {
                  if (relocation.kind
                     != codegen::RuntimeCoreRelocationKind::ExternalRelative32)
                  {
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);
                  }

                  scope.helper->writeExternalReference(
                     *scope.codeWriter->Memory(), scope.codeWriter->position(),
                     "$rt.PrepareLA", 0, mskRelRef32);
               }
               break;
            case codegen::RuntimeCoreSymbol::SignalStop:
            case codegen::RuntimeCoreSymbol::SignalClear:
            case codegen::RuntimeCoreSymbol::WaitForSignals:
            case codegen::RuntimeCoreSymbol::WaitForSignal:
            case codegen::RuntimeCoreSymbol::WaitForCollection:
            case codegen::RuntimeCoreSymbol::SignalCollectionEnd:
            {
               const char* external = relocation.symbol
                  == codegen::RuntimeCoreSymbol::SignalStop
                     ? "$rt.SignalStopGCLA"
                     : relocation.symbol == codegen::RuntimeCoreSymbol::SignalClear
                        ? "$rt.SignalClearGCLA"
                        : relocation.symbol == codegen::RuntimeCoreSymbol::WaitForSignals
                           ? "$rt.WaitForSignalsGCLA"
                     : relocation.symbol == codegen::RuntimeCoreSymbol::WaitForSignal
                        ? "$rt.WaitForSignalGCLA"
                        : relocation.symbol == codegen::RuntimeCoreSymbol::WaitForCollection
                           ? "$rt.WaitForCollectionGCLA"
                           : "$rt.SignalCollectionEndGCLA";
               if (target.architecture == codegen::Architecture::X86) {
                  if (relocation.kind
                     != codegen::RuntimeCoreRelocationKind::ExternalAbsolute32)
                  {
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);
                  }

                  scope.helper->writeExternalReference(
                     *scope.codeWriter->Memory(), scope.codeWriter->position(),
                     external, 0, mskRef32);
               }
               else {
                  if (relocation.kind
                     != codegen::RuntimeCoreRelocationKind::ExternalRelative32)
                  {
                     throw InternalError(errInvalidMachineCode,
                        (int)codegen::RuntimeCoreError::InvalidOperation);
                  }

                  scope.helper->writeExternalReference(
                     *scope.codeWriter->Memory(), scope.codeWriter->position(),
                     external, 0, mskRelRef32);
               }
               break;
            }
            default:
               throw InternalError(errInvalidMachineCode,
                  (int)codegen::RuntimeCoreError::InvalidOperation);
         }
      }

      if (!scope.codeWriter->seek(end))
         throw InternalError(errInvalidMachineCode, (int)codegen::RuntimeCoreError::WriteFailed);

      return true;
   }
}

#endif
