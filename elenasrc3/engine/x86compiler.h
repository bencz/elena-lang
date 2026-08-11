//---------------------------------------------------------------------------
//		E L E N A   P r o j e c t:  ELENA Compiler Engine
//
//		This file contains ELENA JIT-X linker class.
//		Supported platforms: x86
//                                             (C)2021-2025, by Aleksey Rakov
//---------------------------------------------------------------------------

#ifndef X86COMPILER_H
#define X86COMPILER_H

#include "jitcompiler.h"
#include "../codegen/runtime.h"
#include "../codegen/x86/abi.h"

namespace elena_lang
{
   // --- X86JITCompiler ---
   class X86JITCompiler : public JITCompiler32
   {
      codegen::RuntimeSpec         _runtime;
      codegen::TargetSpec          _target;
      codegen::x86::ManagedABI     _managedABI;
      codegen::x86::RuntimeABISet  _runtimeABIs;

      static void compileMigrated(JITCompilerScope* scope);

      bool compileCoreRoutine(ref_t reference, JITCompilerScope& scope) override;

      void prepare(
         LibraryLoaderBase* loader, 
         ImageProviderBase* imageProvider, 
         ReferenceHelperBase* helper,
         LabelHelperBase* lh,
         ProcessSettings& settings,
         bool virtualMode) override;

   public:
      static PlatformSettings getSettings()
      {
         return { 1, 1, 4, 16, 4, 4 };
      }

      int calcFrameOffset(int argument, bool extMode) override;

      void writeImm9(MemoryWriter* writer, int value, int type) override;
      void writeImm12(MemoryWriter* writer, int value, int type) override;

      void alignCode(MemoryWriter& writer, pos_t alignment, bool isText) override;
      void alignJumpAddress(MemoryWriter& writer) override;

      // NOTE that LabelHelperBase argument should be overridden inside the CPU compiler
      void compileProcedure(ReferenceHelperBase* helper, MemoryReader& bcReader, 
         MemoryWriter& codeWriter, LabelHelperBase* lh) override;
      void compileSymbol(ReferenceHelperBase* helper, MemoryReader& bcReader, 
         MemoryWriter& codeWriter, LabelHelperBase* lh) override;

      X86JITCompiler(codegen::TargetPlatform platform = codegen::TargetPlatform::LinuxX86);
   };
}

#endif
