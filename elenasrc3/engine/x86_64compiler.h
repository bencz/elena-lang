//---------------------------------------------------------------------------
//		E L E N A   P r o j e c t:  ELENA Compiler Engine
//
//		This file contains ELENA JIT-X linker class.
//		Supported platforms: x86-64
//                                             (C)2021-2026, by Aleksey Rakov
//---------------------------------------------------------------------------

#ifndef X86_64COMPILER_H
#define X86_64COMPILER_H

#include "jitcompiler.h"
#include "../codegen/runtime.h"
#include "../codegen/x86/abi.h"

namespace elena_lang
{
   // --- X86_64JITCompiler --
   class X86_64JITCompiler : public JITCompiler64
   {
      codegen::RuntimeSpec         _runtime;
      codegen::TargetSpec          _target;
      codegen::x86::ManagedABI     _managedABI;
      codegen::x86::RuntimeABISet  _runtimeABIs;

      static void compileMigrated(JITCompilerScope* scope);

      bool compileCoreRoutine(ref_t reference, JITCompilerScope& scope) override;

   protected:
      void prepare(
         LibraryLoaderBase* loader, 
         ImageProviderBase* imageProvider, 
         ReferenceHelperBase* helper,
         LabelHelperBase* lh,
         ProcessSettings& settings,
         bool virtualMode) override;

      friend void x86_64loadCallOp(JITCompilerScope* scope);
      friend void x86_64compileStackOp(JITCompilerScope* scope);
      friend void x86_64compileXOpenIN(JITCompilerScope* scope);

   public:
      static PlatformSettings getSettings()
      {
         return { 2, 2, 16, 32, 8, 8 };
      }

      int calcFrameOffset(int argument, bool extMode) override;

      void writeImm9(MemoryWriter* writer, int value, int type) override;
      void writeImm12(MemoryWriter* writer, int value, int type) override;

      void alignCode(MemoryWriter& writer, pos_t alignment, bool isText) override;
      void alignJumpAddress(MemoryWriter&) override
      {
         // must be implemented
      }

      // NOTE that LabelHelperBase argument should be overridden inside the CPU compiler
      void compileProcedure(ReferenceHelperBase* helper, MemoryReader& bcReader, 
         MemoryWriter& codeWriter, LabelHelperBase*) override;
      void compileSymbol(ReferenceHelperBase* helper, MemoryReader& bcReader, 
         MemoryWriter& codeWriter, LabelHelperBase*) override;

      X86_64JITCompiler(
         codegen::TargetPlatform platform = codegen::TargetPlatform::LinuxAMD64);
   };

   void x86_64loadCallOp(JITCompilerScope* scope);
   void x86_64compileStackOp(JITCompilerScope* scope);
   void x86_64compileXOpenIN(JITCompilerScope* scope);
}

#endif
