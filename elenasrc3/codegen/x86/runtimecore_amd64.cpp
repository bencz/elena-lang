#include "runtimecore.h"

using namespace elena_lang;
using namespace elena_lang::codegen;
using namespace elena_lang::codegen::x86;

RuntimeCoreError RuntimeCoreEncoder :: encodeAllocateYoungAMD64(const RuntimeSpec& runtime)
{
   const int currentOffset = runtime.dataLayout.gc.youngCurrent;
   const int endOffset = runtime.dataLayout.gc.youngEnd;
   const int lockOffset = runtime.dataLayout.gc.lock;
   unsigned char collect = newLabel();
   unsigned char wait = newLabel();

   if (runtime.threadingMode == ThreadingMode::MultiThread) {
      static const unsigned char moveTable[] = {
         0x48, 0xBF // mov rdi, CORE_GC_TABLE
      };

      if (!write(moveTable, sizeof(moveTable))
         || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
            RuntimeCoreSymbol::GCData, 0, 8)
         || !bind(wait))
      {
         return RuntimeCoreError::WriteFailed;
      }

      static const unsigned char lock[] = {
         0xBA, 0x01, 0x00, 0x00, 0x00,                           // mov edx, 1
         0x31, 0xC0,                                             // xor eax, eax
         0xF0, 0x0F, 0xB1, 0x57, (unsigned char)lockOffset       // lock cmpxchg dword [rdi + gc_lock], edx
      };

      if (!write(lock, sizeof(lock)) || !branch(0x05, wait))
         return RuntimeCoreError::WriteFailed;

      static const unsigned char loadCurrent[] = {
         0x48, 0x8B, 0x47, (unsigned char)currentOffset, // mov rax, [rdi + gc_yg_current]
         0x31, 0xD2,                                   // xor edx, edx
         0x48, 0x01, 0xC1                              // add rcx, rax
      };

      if (!write(loadCurrent, sizeof(loadCurrent)) || !branch(0x02, collect))
         return RuntimeCoreError::WriteFailed;

      static const unsigned char compareEnd[] = {
         0x48, 0x3B, 0x4F, (unsigned char)endOffset // cmp rcx, [rdi + gc_yg_end]
      };

      if (!write(compareEnd, sizeof(compareEnd)) || !branch(0x03, collect))
         return RuntimeCoreError::WriteFailed;

      static const unsigned char success[] = {
         0x48, 0x89, 0x4F, (unsigned char)currentOffset, // mov [rdi + gc_yg_current], rcx
         0x89, 0x50, 0x08,                              // mov [rax + elSizeOffset], edx
         0xBA, 0xFF, 0xFF, 0xFF, 0xFF,                  // mov edx, -1
         0x48, 0x8D, 0x58, 0x10,                        // lea rbx, [rax + object_header_size]
         0xF0, 0x0F, 0xC1, 0x57, (unsigned char)lockOffset, // lock xadd dword [rdi + gc_lock], edx
         0xC3                                           // ret
      };

      if (!write(success, sizeof(success)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char moveTable[] = {
         0x49, 0xBC // mov r12, CORE_GC_TABLE
      };

      if (!write(moveTable, sizeof(moveTable))
         || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
            RuntimeCoreSymbol::GCData, 0, 8))
      {
         return RuntimeCoreError::WriteFailed;
      }

      static const unsigned char loadCurrent[] = {
         0x49, 0x8B, 0x44, 0x24, (unsigned char)currentOffset, // mov rax, [r12 + gc_yg_current]
         0x48, 0x01, 0xC1                                   // add rcx, rax
      };

      if (!write(loadCurrent, sizeof(loadCurrent)) || !branch(0x02, collect))
         return RuntimeCoreError::WriteFailed;

      static const unsigned char compareEnd[] = {
         0x49, 0x3B, 0x4C, 0x24, (unsigned char)endOffset // cmp rcx, [r12 + gc_yg_end]
      };

      if (!write(compareEnd, sizeof(compareEnd)) || !branch(0x03, collect))
         return RuntimeCoreError::WriteFailed;

      static const unsigned char success[] = {
         0x49, 0x89, 0x4C, 0x24, (unsigned char)currentOffset, // mov [r12 + gc_yg_current], rcx
         0x48, 0x8D, 0x58, 0x10,                              // lea rbx, [rax + object_header_size]
         0xC3                                                 // ret
      };

      if (!write(success, sizeof(success)))
         return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char slow[] = {
      0x48, 0x29, 0xC1, // sub rcx, rax; restore allocation size
      0x31, 0xD2,       // xor edx, edx; minor collection
      0x31, 0xC0,       // xor eax, eax
      0x50,             // push rax; maintain managed stack convention
      0xE8              // call GC_COLLECT rel32
   };

   if (!bind(collect) || !write(slow, sizeof(slow))
      || !addRelocation(RuntimeCoreRelocationKind::Relative32,
         RuntimeCoreSymbol::CollectYoung, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char end[] = {
      0x58, // pop rax
      0xC3  // ret
   };
   if (!write(end, sizeof(end)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodeAllocatePermanentAMD64(
   const RuntimeSpec& runtime, const ExternalABI& externalABI,
   const RuntimeCoreProtocol& protocol)
{
   if (!protocol.isValid(runtime))
      return RuntimeCoreError::InvalidRuntime;
   if (runtime.threadingMode == ThreadingMode::MultiThread)
      return encodeAllocatePermanentMTAAMD64(runtime, externalABI, protocol);

   const int currentOffset = runtime.dataLayout.gc.permanentCurrent;
   const int endOffset = runtime.dataLayout.gc.permanentEnd;
   const int stackFrameOffset = runtime.dataLayout.threadContent.stackFrame;
   unsigned char collect = newLabel();

   static const unsigned char moveTable[] = {
      0x49, 0xBC // mov r12, CORE_GC_TABLE
   };

   if (!write(moveTable, sizeof(moveTable))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char allocate[] = {
      0x49, 0x8B, 0x44, 0x24, (unsigned char)currentOffset, // mov rax, [r12 + gc_perm_current]
      0x48, 0x01, 0xC1,                                   // add rcx, rax
      0x49, 0x3B, 0x4C, 0x24, (unsigned char)endOffset     // cmp rcx, [r12 + gc_perm_end]
   };

   if (!write(allocate, sizeof(allocate)) || !branch(0x03, collect))
      return RuntimeCoreError::WriteFailed;

   static const unsigned char success[] = {
      0x49, 0x89, 0x4C, 0x24, (unsigned char)currentOffset, // mov [r12 + gc_perm_current], rcx
      0x48, 0x8D, 0x58, 0x10,                              // lea rbx, [rax + object_header_size]
      0xC3                                                 // ret
   };

   if (!write(success, sizeof(success)) || !bind(collect))
      return RuntimeCoreError::WriteFailed;

   static const unsigned char save[] = {
      0x48, 0x29, 0xC1, // sub rcx, rax; restore allocation size
      0x55,             // push rbp
      0x41, 0x52,       // push r10
      0x41, 0x53,       // push r11
      0x48, 0xB8        // mov rax, CORE_SINGLE_CONTENT
   };

   if (!write(save, sizeof(save))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::SingleContent, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char lockFrame[] = {
      0x48, 0x89, 0x60, (unsigned char)stackFrameOffset // mov [rax + tt_stack_frame], rsp
   };

   if (!write(lockFrame, sizeof(lockFrame)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.integerArguments[0] == Register::DI) {
      static const unsigned char moveArgument[] = {
         0x48, 0x89, 0xCF // mov rdi, rcx; System V AMD64 argument 1
      };

      if (!write(moveArgument, sizeof(moveArgument)))
         return RuntimeCoreError::WriteFailed;
   }
   else if (externalABI.integerArguments[0] != Register::C) {
      return RuntimeCoreError::InvalidABI;
   }

   if (externalABI.shadowSpace != 0) {
      if (externalABI.shadowSpace > 0x7F
         || !writeByte(0x48) || !writeByte(0x83) || !writeByte(0xEC) // sub rsp, shadow_space
         || !writeByte((unsigned char)externalABI.shadowSpace))
      {
         return RuntimeCoreError::WriteFailed;
      }
   }

   if (!writeByte(0xFF) || !writeByte(0x15) // call qword [rip + rel32]
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::CollectPermanent, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.shadowSpace != 0) {
      if (!writeByte(0x48) || !writeByte(0x83) || !writeByte(0xC4) // add rsp, shadow_space
         || !writeByte((unsigned char)externalABI.shadowSpace))
      {
         return RuntimeCoreError::WriteFailed;
      }
   }

   static const unsigned char end[] = {
      0x48, 0x89, 0xC3, // mov rbx, rax
      0x41, 0x5B,       // pop r11
      0x41, 0x5A,       // pop r10
      0x5D,             // pop rbp
      0xC3              // ret
   };
   if (!write(end, sizeof(end)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodeAllocatePermanentMTAAMD64(
   const RuntimeSpec& runtime, const ExternalABI& externalABI,
   const RuntimeCoreProtocol& protocol)
{
   if (!protocol.isValid(runtime)
      || protocol.operation != RuntimeOperation::AllocatePermanent
      || !protocol.contains(RuntimeCoreAction::AcquireAllocationLock)
      || !protocol.contains(RuntimeCoreAction::ReleaseAllocationLock))
   {
      return RuntimeCoreError::InvalidRuntime;
   }
   if (externalABI.platformABI != PlatformABI::WindowsX64
      && externalABI.platformABI != PlatformABI::SystemVAMD64)
   {
      return RuntimeCoreError::InvalidABI;
   }
   if (_target.tlsModel != TLSModel::Windows && _target.tlsModel != TLSModel::ELF)
      return RuntimeCoreError::InvalidABI;

   unsigned char allocation = newLabel();
   unsigned char wait = newLabel();
   unsigned char collect = newLabel();

   if (!bind(allocation))
      return RuntimeCoreError::WriteFailed;

   static const unsigned char lockAddress[] = {
      0x48, 0xBF // mov rdi, CORE_GC_TABLE + gc_lock
   };

   if (!write(lockAddress, sizeof(lockAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8)
      || !bind(wait))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char acquireLock[] = {
      0xBA, 0x01, 0x00, 0x00, 0x00, // mov edx, 1
      0x31, 0xC0,                   // xor eax, eax
      0xF0, 0x0F, 0xB1, 0x17        // lock cmpxchg dword [rdi], edx
   };

   static const unsigned char tableAddress[] = {
      0x49, 0xBC // mov r12, CORE_GC_TABLE
   };

   if (!write(acquireLock, sizeof(acquireLock))
      || !branch(0x05, wait)
      || !write(tableAddress, sizeof(tableAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char allocate[] = {
      0x49, 0x8B, 0x44, 0x24,
         (unsigned char)runtime.dataLayout.gc.permanentCurrent, // mov rax, [r12 + gc_perm_current]
      0x48, 0x01, 0xC1                                      // add rcx, rax
   };
   const unsigned char compareEnd[] = {
      0x49, 0x3B, 0x4C, 0x24,
         (unsigned char)runtime.dataLayout.gc.permanentEnd // cmp rcx, [r12 + gc_perm_end]
   };

   if (!write(allocate, sizeof(allocate))
      || !branch(0x02, collect)
      || !write(compareEnd, sizeof(compareEnd))
      || !branch(0x03, collect))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char success[] = {
      0x49, 0x89, 0x4C, 0x24,
         (unsigned char)runtime.dataLayout.gc.permanentCurrent, // mov [r12 + gc_perm_current], rcx
      0x31, 0xD2,                                              // xor edx, edx
      0x89, 0x50, 0x08,                                        // mov dword [rax + sync_forward], edx
      0xBA, 0xFF, 0xFF, 0xFF, 0xFF,                            // mov edx, -1
      0x48, 0x8D, 0x58, 0x10,                                  // lea rbx, [rax + object_header_size]
      0xF0, 0x0F, 0xC1, 0x17,                                  // lock xadd dword [rdi], edx
      0xC3                                                     // ret
   };

   if (!write(success, sizeof(success)) || !bind(collect))
      return RuntimeCoreError::WriteFailed;

   static const unsigned char restoreSize[] = {
      0x48, 0x29, 0xC1 // sub rcx, rax
   };

   if (!write(restoreSize, sizeof(restoreSize)))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentThread = encodeCurrentThreadAMD64(runtime);
   if (currentThread != RuntimeCoreError::None)
      return currentThread;

   // RuntimeCoreAction::PublishFrame
   const unsigned char prologue[] = {
      0x55,       // push rbp
      0x41, 0x52, // push r10
      0x41, 0x53  // push r11
   };
   const unsigned char publishFrame[] = {
      0x48, 0x89, 0x60, (unsigned char)runtime.dataLayout.threadContent.stackFrame // mov [rax + tt_stack_frame], rsp
   };

   if (!write(prologue, sizeof(prologue))
      || !write(publishFrame, sizeof(publishFrame)))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::InvokeCollector
   if (externalABI.platformABI == PlatformABI::SystemVAMD64) {
      static const unsigned char argument[] = {
         0x48, 0x89, 0xCF // mov rdi, rcx; System V AMD64 argument 1
      };

      if (!write(argument, sizeof(argument)))
         return RuntimeCoreError::WriteFailed;
   }
   else if (externalABI.platformABI != PlatformABI::WindowsX64) {
      return RuntimeCoreError::InvalidABI;
   }

   if (externalABI.shadowSpace != 0) {
      if (externalABI.shadowSpace > 0x7F
         || !writeByte(0x48) || !writeByte(0x83) || !writeByte(0xEC)
         || !writeByte((unsigned char)externalABI.shadowSpace))
      {
         return RuntimeCoreError::WriteFailed;
      }
   }

   if (!writeByte(0xFF) || !writeByte(0x15)
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::CollectPermanent, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.shadowSpace != 0) {
      if (!writeByte(0x48) || !writeByte(0x83) || !writeByte(0xC4)
         || !writeByte((unsigned char)externalABI.shadowSpace))
      {
         return RuntimeCoreError::WriteFailed;
      }
   }

   // RuntimeCoreAction::ReleaseAllocationLock, RuntimeCoreAction::Return
   const unsigned char epilogue[] = {
      0x48, 0x89, 0xC3,                                           // mov rbx, rax
      0xBA, 0xFF, 0xFF, 0xFF, 0xFF,                               // mov edx, -1
      0xF0, 0x41, 0x0F, 0xC1, 0x54, 0x24,
         (unsigned char)runtime.dataLayout.gc.lock,                 // lock xadd dword [r12 + gc_lock], edx
      0x41, 0x5B,                                                 // pop r11
      0x41, 0x5A,                                                 // pop r10
      0x5D,                                                       // pop rbp
      0xC3                                                        // ret
   };

   if (!write(epilogue, sizeof(epilogue)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodeCollectAMD64(
   const RuntimeSpec& runtime, const ExternalABI& externalABI,
   const RuntimeCoreProtocol& protocol)
{
   if (!protocol.isValid(runtime))
      return RuntimeCoreError::InvalidRuntime;
   if (protocol.contains(RuntimeCoreAction::EnumerateMutators))
      return encodeCollectMTAAMD64(runtime, externalABI, protocol);

   unsigned char findFrameStart = newLabel();

   // RuntimeCoreAction::PublishFrame
   static const unsigned char prologue[] = {
      0x41, 0x52, // push r10
      0x41, 0x53, // push r11
      0x55,       // push rbp
      0x48, 0xB8  // mov rax, CORE_SINGLE_CONTENT
   };

   if (!write(prologue, sizeof(prologue))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::SingleContent, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char publishFrame[] = {
      0x48, 0x89, 0x60, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov [rax + tt_stack_frame], rsp
      0x52,                                                                         // push rdx
      0x51,                                                                         // push rcx
      0x48, 0x89, 0xE5,                                                             // mov rbp, rsp
      0x31, 0xC9,                                                                   // xor ecx, ecx
      0x51,                                                                         // push rcx; root size 4
      0x51,                                                                         // push rcx; root address 4
      0x51,                                                                         // push rcx; root size 3
      0x51,                                                                         // push rcx; root address 3
      0x48, 0xB8                                                                    // mov rax, CORE_SYSTEM_ENVIRONMENT
   };

   if (!write(publishFrame, sizeof(publishFrame))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::SystemEnvironment, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendStaticRoots
   static const unsigned char staticRootAddress[] = {
      0x48, 0xBE // mov rsi, STATIC_ROOTS
   };

   if (!write(staticRootAddress, sizeof(staticRootAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::StaticRoots, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char staticRoots[] = {
      0x8B, 0x08,       // mov ecx, [rax]; static root count
      0xC1, 0xE1, 0x03, // shl ecx, 3; convert count to bytes
      0x56,             // push rsi; static root address
      0x51,             // push rcx; static root size
      0x48, 0xB8        // mov rax, CORE_GC_TABLE
   };

   if (!write(staticRoots, sizeof(staticRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendPermanentRoots
   const unsigned char permanentRoots[] = {
      0x48, 0x8B, 0x70, (unsigned char)runtime.dataLayout.gc.permanentStart,   // mov rsi, [rax + gc_perm_start]
      0x48, 0x8B, 0x48, (unsigned char)runtime.dataLayout.gc.permanentCurrent, // mov rcx, [rax + gc_perm_current]
      0x48, 0x29, 0xF1,                                                       // sub rcx, rsi
      0x56,                                                                   // push rsi; permanent root address
      0x51,                                                                   // push rcx; permanent root size
      0x48, 0xB8                                                             // mov rax, CORE_SINGLE_CONTENT
   };

   if (!write(permanentRoots, sizeof(permanentRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::SingleContent, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendFrameRoots
   const unsigned char beginFrames[] = {
      0x48, 0x8B, 0x40, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov rax, [rax + tt_stack_frame]
      0x48, 0x89, 0xC1                                                            // mov rcx, rax; first upper boundary
   };
   static const unsigned char findStart[] = {
      0x48, 0x89, 0xC6, // mov rsi, rax
      0x48, 0x8B, 0x06, // mov rax, [rsi]; previous frame link
      0x48, 0x85, 0xC0  // test rax, rax
   };

   if (!write(beginFrames, sizeof(beginFrames))
      || !bind(findFrameStart)
      || !write(findStart, sizeof(findStart))
      || !branch(0x05, findFrameStart))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char frameRange[] = {
      0x51,                   // push rcx; frame upper boundary
      0x48, 0x29, 0xF1,       // sub rcx, rsi
      0x48, 0xF7, 0xD9,       // neg rcx; frame root size
      0x51,                   // push rcx; frame root size
      0x48, 0x8B, 0x46, 0x08, // mov rax, [rsi + 8]; next frame descriptor
      0x48, 0x85, 0xC0,       // test rax, rax
      0x48, 0x89, 0xC1        // mov rcx, rax
   };

   if (!write(frameRange, sizeof(frameRange))
      || !branch(0x05, findFrameStart))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::InvokeCollector
   static const unsigned char saveRoots[] = {
      0x48, 0x89, 0x65, 0xF8 // mov [rbp - 8], rsp; root descriptor array
   };

   if (!write(saveRoots, sizeof(saveRoots)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::WindowsX64) {
      static const unsigned char arguments[] = {
         0x4C, 0x8B, 0x45, 0x08, // mov r8, [rbp + 8]; requested allocation size
         0x48, 0x8B, 0x55, 0x00, // mov rdx, [rbp]; object size
         0x48, 0x89, 0xE1        // mov rcx, rsp; root descriptor array
      };

      if (!write(arguments, sizeof(arguments)))
         return RuntimeCoreError::WriteFailed;
   }
   else if (externalABI.platformABI == PlatformABI::SystemVAMD64) {
      static const unsigned char arguments[] = {
         0x48, 0x8B, 0x55, 0x08, // mov rdx, [rbp + 8]; requested allocation size
         0x48, 0x8B, 0x75, 0x00, // mov rsi, [rbp]; object size
         0x48, 0x89, 0xE7        // mov rdi, rsp; root descriptor array
      };

      if (!write(arguments, sizeof(arguments)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      return RuntimeCoreError::InvalidABI;
   }

   static const unsigned char call[] = {
      0x48, 0x89, 0xE8,             // mov rax, rbp
      0x48, 0x8B, 0x68, 0x10,       // mov rbp, [rax + 16]; restore caller frame
      0x48, 0x83, 0xEC, 0x30,       // sub rsp, 48; shadow space and alignment
      0x48, 0x89, 0x44, 0x24, 0x28, // mov [rsp + 40], rax; preserve collector frame
      0xFF, 0x15                    // call qword [rip + rel32]
   };

   if (!write(call, sizeof(call))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::CollectRuntime, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char epilogue[] = {
      0x48, 0x8B, 0x6C, 0x24, 0x28, // mov rbp, [rsp + 40]; collector frame
      0x48, 0x83, 0xC4, 0x30,       // add rsp, 48
      0x48, 0x89, 0xC3,             // mov rbx, rax; allocation result
      0x48, 0x89, 0xEC,             // mov rsp, rbp
      0x59,                         // pop rcx
      0x5A,                         // pop rdx
      0x5D,                         // pop rbp
      0x41, 0x5B,                   // pop r11
      0x41, 0x5A,                   // pop r10
      0xC3                          // ret
   };
   if (!write(epilogue, sizeof(epilogue)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodeCollectMTAAMD64(
   const RuntimeSpec& runtime, const ExternalABI& externalABI,
   const RuntimeCoreProtocol& protocol)
{
   if (!protocol.isValid(runtime)
      || !protocol.contains(RuntimeCoreAction::AppendTLSRoots)
      || !protocol.contains(RuntimeCoreAction::EnterSafeRegion)
      || !protocol.contains(RuntimeCoreAction::AwaitCollection)
      || !protocol.contains(RuntimeCoreAction::LeaveSafeRegion))
   {
      return RuntimeCoreError::InvalidRuntime;
   }
   if (externalABI.platformABI != PlatformABI::WindowsX64
      && externalABI.platformABI != PlatformABI::SystemVAMD64)
   {
      return RuntimeCoreError::InvalidABI;
   }
   if (_target.tlsModel != TLSModel::Windows && _target.tlsModel != TLSModel::ELF)
      return RuntimeCoreError::InvalidABI;

   unsigned char start = newLabel();
   unsigned char collect = newLabel();
   unsigned char retakeLock = newLabel();
   unsigned char repeatAllocation = newLabel();
   unsigned char nextThread = newLabel();
   unsigned char threadsReady = newLabel();
   unsigned char skipWaitHandle = newLabel();
   unsigned char skipThread = newLabel();
   unsigned char skipWait = newLabel();
   unsigned char rootLock = newLabel();
   unsigned char nextRootThread = newLabel();
   unsigned char rootsReady = newLabel();
   unsigned char skipRootThread = newLabel();
   unsigned char nextFrame = newLabel();

   if (!bind(start))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentThread = encodeCurrentThreadAMD64(runtime);
   if (currentThread != RuntimeCoreError::None)
      return currentThread;

   // RuntimeCoreAction::PublishFrame
   const unsigned char prologue[] = {
      0x41, 0x52,                                                        // push r10
      0x41, 0x53,                                                        // push r11
      0x55,                                                              // push rbp
      0x48, 0x8B, 0x70, (unsigned char)runtime.dataLayout.threadContent.syncEvent,  // mov rsi, [rax + tt_sync_event]
      0x48, 0x89, 0x60, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov [rax + tt_stack_frame], rsp
      0x52,                                                              // push rdx
      0x51,                                                              // push rcx
      0x48, 0xBA                                                         // mov rdx, CORE_GC_TABLE + gc_signal
   };

   if (!write(prologue, sizeof(prologue))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.signal, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::ObserveCollection
   static const unsigned char loadSignal[] = {
      0x48, 0x8B, 0x12, // mov rdx, [rdx]
      0x48, 0x85, 0xD2  // test rdx, rdx
   };

   if (!write(loadSignal, sizeof(loadSignal)) || !branch(0x04, collect))
      return RuntimeCoreError::WriteFailed;

   // RuntimeCoreAction::ParkMutator
   const unsigned char enterSafeRegion[] = {
      0x8B, 0x48, (unsigned char)runtime.dataLayout.threadContent.flags,   // mov ecx, [rax + tt_flags]
      0x49, 0x89, 0xCD,                                                  // mov r13, rcx
      0xC7, 0x40, (unsigned char)runtime.dataLayout.threadContent.flags,
      0x01, 0x00, 0x00, 0x00,                                            // mov dword [rax + tt_flags], 1
      0x48, 0x83, 0xEC, 0x30                                             // sub rsp, 48
   };

   if (!write(enterSafeRegion, sizeof(enterSafeRegion)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::WindowsX64) {
      static const unsigned char argument[] = {
         0x48, 0x89, 0xF1 // mov rcx, rsi; Windows x64 argument 1
      };

      if (!write(argument, sizeof(argument)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char argument[] = {
         0x48, 0x89, 0xF7 // mov rdi, rsi; System V AMD64 argument 1
      };

      if (!write(argument, sizeof(argument)))
         return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char callExternal[] = {
      0xFF, 0x15 // call qword [rip + rel32]
   };

   if (!write(callExternal, sizeof(callExternal))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::SignalStop, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char unlockAddress[] = {
      0x48, 0xBF // mov rdi, CORE_GC_TABLE + gc_lock
   };

   if (!write(unlockAddress, sizeof(unlockAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char unlockAndWait[] = {
      0xBB, 0xFF, 0xFF, 0xFF, 0xFF, // mov ebx, -1
      0xF0, 0x0F, 0xC1, 0x1F,       // lock xadd dword [rdi], ebx
      0xFF, 0x15                    // call qword [rip + rel32]
   };

   if (!write(unlockAndWait, sizeof(unlockAndWait))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::WaitForCollection, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char releaseCallFrame[] = {
      0x48, 0x83, 0xC4, 0x30 // add rsp, 48
   };

   if (!write(releaseCallFrame, sizeof(releaseCallFrame)))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentThreadAfterWait = encodeCurrentThreadAMD64(runtime);
   if (currentThreadAfterWait != RuntimeCoreError::None)
      return currentThreadAfterWait;

   const unsigned char restoreSafeRegion[] = {
      0x4C, 0x89, 0xE9,                                                // mov rcx, r13
      0x89, 0x48, (unsigned char)runtime.dataLayout.threadContent.flags, // mov [rax + tt_flags], ecx
      0x59,                                                            // pop rcx
      0x5A,                                                            // pop rdx
      0x5D,                                                            // pop rbp
      0x41, 0x5B,                                                      // pop r11
      0x41, 0x5A,                                                      // pop r10
      0x48, 0x85, 0xC9                                                 // test rcx, rcx
   };

   if (!write(restoreSafeRegion, sizeof(restoreSafeRegion)))
      return RuntimeCoreError::WriteFailed;

   // RuntimeCoreAction::RetryOperation
   static const unsigned char acquireLock[] = {
      0xBA, 0x01, 0x00, 0x00, 0x00, // mov edx, 1
      0x31, 0xC0,                   // xor eax, eax
      0xF0, 0x0F, 0xB1, 0x17        // lock cmpxchg dword [rdi], edx
   };

   if (!branch(0x05, repeatAllocation)
      || !write(unlockAddress, sizeof(unlockAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8)
      || !bind(retakeLock)
      || !write(acquireLock, sizeof(acquireLock))
      || !branch(0x05, retakeLock)
      || !jump(start)
      || !bind(repeatAllocation))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char repeat[] = {
      0x31, 0xC0, // xor eax, eax
      0x50,       // push rax
      0xE8        // call rel32
   };

   if (!write(repeat, sizeof(repeat))
      || !addRelocation(RuntimeCoreRelocationKind::Relative32,
         RuntimeCoreSymbol::AllocateYoungRoutine, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char repeatEnd[] = {
      0x58, // pop rax
      0xC3  // ret
   };

   if (!write(repeatEnd, sizeof(repeatEnd)) || !bind(collect))
      return RuntimeCoreError::WriteFailed;

   // RuntimeCoreAction::PublishCollector
   static const unsigned char signalAddress[] = {
      0x48, 0xB8 // mov rax, CORE_GC_TABLE + gc_signal
   };

   if (!write(signalAddress, sizeof(signalAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.signal, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char publishCollector[] = {
      0x48, 0x89, 0x30, // mov [rax], rsi
      0x48, 0x89, 0xE5  // mov rbp, rsp
   };

   if (!write(publishCollector, sizeof(publishCollector)))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentCollector = encodeCurrentThreadAMD64(runtime);
   if (currentCollector != RuntimeCoreError::None)
      return currentCollector;

   // RuntimeCoreAction::EnumerateMutators
   const unsigned char ownEvent[] = {
      0x48, 0x8B, 0x40, (unsigned char)runtime.dataLayout.threadContent.syncEvent, // mov rax, [rax + tt_sync_event]
      0x49, 0xBD                                                                // mov r13, CORE_THREAD_TABLE + slots
   };

   if (!write(ownEvent, sizeof(ownEvent))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::ThreadTable, runtime.dataLayout.threadTable.slots, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char waitListCount[] = {
      0x49, 0x8B, 0x5D, (unsigned char)(0 - runtime.dataLayout.threadTable.slots), // mov rbx, [r13 - slots]
      0x48, 0x85, 0xDB                                                           // test rbx, rbx
   };

   if (!write(waitListCount, sizeof(waitListCount))
      || !branch(0x04, threadsReady)
      || !bind(nextThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char loadThread[] = {
      0x49, 0x8B, 0x55, 0x00,                                           // mov rdx, [r13]
      0x4D, 0x8D, 0x6D, (unsigned char)runtime.dataLayout.threadTable.slotSize, // lea r13, [r13 + slot_size]
      0x48, 0x85, 0xD2                                                  // test rdx, rdx
   };

   if (!write(loadThread, sizeof(loadThread)) || !branch(0x04, skipThread))
      return RuntimeCoreError::WriteFailed;

   const unsigned char classifyThread[] = {
      0x31, 0xC9,                                                        // xor ecx, ecx
      0x48, 0x3B, 0x42, (unsigned char)runtime.dataLayout.threadContent.syncEvent, // cmp rax, [rdx + tt_sync_event]
      0x0F, 0x94, 0xC1,                                                  // sete cl
      0x0B, 0x4A, (unsigned char)runtime.dataLayout.threadContent.flags, // or ecx, [rdx + tt_flags]
      0xF7, 0xC1, 0x01, 0x00, 0x00, 0x00                                // test ecx, 1
   };

   if (!write(classifyThread, sizeof(classifyThread))
      || !branch(0x05, skipWaitHandle))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char saveWaitHandle[] = {
      0xFF, 0x72, (unsigned char)runtime.dataLayout.threadContent.syncEvent // push qword [rdx + tt_sync_event]
   };

   if (!write(saveWaitHandle, sizeof(saveWaitHandle))
      || !bind(skipWaitHandle))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char alignClearCall[] = {
      0x49, 0x89, 0xE4,       // mov r12, rsp
      0x48, 0x83, 0xEC, 0x30, // sub rsp, 48; includes Windows x64 shadow space
      0x48, 0x83, 0xE4, 0xF0  // and rsp, -16
   };

   if (!write(alignClearCall, sizeof(alignClearCall)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::WindowsX64) {
      const unsigned char argument[] = {
         0x48, 0x8B, 0x4A, (unsigned char)runtime.dataLayout.threadContent.syncEvent // mov rcx, [rdx + tt_sync_event]
      };

      if (!write(argument, sizeof(argument)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      const unsigned char argument[] = {
         0x48, 0x8B, 0x7A, (unsigned char)runtime.dataLayout.threadContent.syncEvent // mov rdi, [rdx + tt_sync_event]
      };

      if (!write(argument, sizeof(argument)))
         return RuntimeCoreError::WriteFailed;
   }

   if (!write(callExternal, sizeof(callExternal))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::SignalClear, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char restoreClearCall[] = {
      0x4C, 0x89, 0xE4 // mov rsp, r12
   };

   if (!write(restoreClearCall, sizeof(restoreClearCall)))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError reloadCollector = encodeCurrentThreadAMD64(runtime);
   if (reloadCollector != RuntimeCoreError::None)
      return reloadCollector;

   const unsigned char reloadOwnEvent[] = {
      0x48, 0x8B, 0x40, (unsigned char)runtime.dataLayout.threadContent.syncEvent // mov rax, [rax + tt_sync_event]
   };

   static const unsigned char advanceThread[] = {
      0x48, 0x83, 0xEB, 0x01 // sub rbx, 1
   };

   if (!write(reloadOwnEvent, sizeof(reloadOwnEvent))
      || !bind(skipThread)
      || !write(advanceThread, sizeof(advanceThread))
      || !branch(0x05, nextThread)
      || !bind(threadsReady))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::WaitForMutators
   static const unsigned char releaseLockAddress[] = {
      0x48, 0xBE // mov rsi, CORE_GC_TABLE + gc_lock
   };

   if (!write(releaseLockAddress, sizeof(releaseLockAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char releaseForWait[] = {
      0xBA, 0xFF, 0xFF, 0xFF, 0xFF, // mov edx, -1
      0x48, 0x89, 0xEB,             // mov rbx, rbp
      0xF0, 0x0F, 0xC1, 0x16,       // lock xadd dword [rsi], edx
      0x48, 0x89, 0xE2,             // mov rdx, rsp; wait handle array
      0x48, 0x29, 0xE3              // sub rbx, rsp; wait handle bytes
   };

   if (!write(releaseForWait, sizeof(releaseForWait))
      || !branch(0x04, skipWait))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char prepareWait[] = {
      0xC1, 0xEB, 0x03,       // shr ebx, 3; wait handle count
      0x48, 0x83, 0xEC, 0x30, // sub rsp, 48; includes Windows x64 shadow space
      0x48, 0x83, 0xE4, 0xF0  // and rsp, -16
   };

   if (!write(prepareWait, sizeof(prepareWait)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::WindowsX64) {
      static const unsigned char arguments[] = {
         0x89, 0xD9 // mov ecx, ebx; Windows x64 argument 1, rdx already holds argument 2
      };

      if (!write(arguments, sizeof(arguments)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char arguments[] = {
         0x89, 0xDF,       // mov edi, ebx; System V AMD64 argument 1
         0x48, 0x89, 0xD6  // mov rsi, rdx; System V AMD64 argument 2
      };

      if (!write(arguments, sizeof(arguments)))
         return RuntimeCoreError::WriteFailed;
   }

   if (!write(callExternal, sizeof(callExternal))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::WaitForSignals, 0, 4)
      || !bind(skipWait))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char discardWaitList[] = {
      0x48, 0x89, 0xEC // mov rsp, rbp
   };

   // RuntimeCoreAction::AcquireRootLock
   if (!write(discardWaitList, sizeof(discardWaitList))
      || !write(unlockAddress, sizeof(unlockAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8)
      || !bind(rootLock)
      || !write(acquireLock, sizeof(acquireLock))
      || !branch(0x05, rootLock))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::BeginRoots
   static const unsigned char beginRoots[] = {
      0x48, 0x89, 0xE5, // mov rbp, rsp
      0x31, 0xC9,       // xor ecx, ecx
      0x51,             // push rcx; root size 4
      0x51,             // push rcx; root address 4
      0x51,             // push rcx; root size 3
      0x51,             // push rcx; root address 3
      0x48, 0xB8        // mov rax, CORE_SYSTEM_ENVIRONMENT
   };

   if (!write(beginRoots, sizeof(beginRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::SystemEnvironment, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendStaticRoots
   static const unsigned char staticRootAddress[] = {
      0x48, 0xBE // mov rsi, STATIC_ROOTS
   };

   if (!write(staticRootAddress, sizeof(staticRootAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::StaticRoots, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char staticRoots[] = {
      0x8B, 0x08,       // mov ecx, [rax]; static root count
      0xC1, 0xE1, 0x03, // shl ecx, 3; convert count to bytes
      0x56,             // push rsi; static root address
      0x51,             // push rcx; static root size
      0x48, 0xB8        // mov rax, CORE_GC_TABLE
   };

   if (!write(staticRoots, sizeof(staticRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendPermanentRoots
   const unsigned char permanentRoots[] = {
      0x48, 0x8B, 0x70, (unsigned char)runtime.dataLayout.gc.permanentStart,   // mov rsi, [rax + gc_perm_start]
      0x48, 0x8B, 0x48, (unsigned char)runtime.dataLayout.gc.permanentCurrent, // mov rcx, [rax + gc_perm_current]
      0x48, 0x29, 0xF1,                                                       // sub rcx, rsi
      0x56,                                                                   // push rsi; permanent root address
      0x51,                                                                   // push rcx; permanent root size
      0x48, 0xB8                                                             // mov rax, CORE_THREAD_TABLE
   };

   if (!write(permanentRoots, sizeof(permanentRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::ThreadTable, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendTLSRoots
   static const unsigned char rootThreadCount[] = {
      0x48, 0x8B, 0x18, // mov rbx, [rax]; thread count
      0x48, 0x85, 0xDB  // test rbx, rbx
   };

   if (!write(rootThreadCount, sizeof(rootThreadCount))
      || !branch(0x04, rootsReady)
      || !bind(nextRootThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char rootThreadIndex[] = {
      0x48, 0x83, 0xEB, 0x01, // sub rbx, 1
      0x48, 0xB8              // mov rax, CORE_THREAD_TABLE + slots
   };

   if (!write(rootThreadIndex, sizeof(rootThreadIndex))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::ThreadTable, runtime.dataLayout.threadTable.slots, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char loadRootThread[] = {
      0x49, 0x89, 0xD8,       // mov r8, rbx
      0x49, 0xC1, 0xE0, 0x04, // shl r8, 4; multiply by slot size
      0x49, 0x01, 0xC0,       // add r8, rax
      0x49, 0x8B, 0x30,       // mov rsi, [r8]
      0x48, 0x85, 0xF6        // test rsi, rsi
   };

   if (!write(loadRootThread, sizeof(loadRootThread))
      || !branch(0x04, skipRootThread)
      || !write(signalAddress, sizeof(signalAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::SystemEnvironment, 0, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char tlsRoots[] = {
      0x48, 0x8B, 0x48, (unsigned char)runtime.dataLayout.environment.tlsSize, // mov rcx, [rax + et_tls_size]
      0x48, 0x8D, 0x46, (unsigned char)runtime.dataLayout.threadContent.size,  // lea rax, [rsi + thread_content_size]
      0x50,                                                                   // push rax; TLS root address
      0x51,                                                                   // push rcx; TLS root size
      0x48, 0x8B, 0x46, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov rax, [rsi + tt_stack_frame]
      0x48, 0x85, 0xC0                                                        // test rax, rax
   };

   if (!write(tlsRoots, sizeof(tlsRoots))
      || !branch(0x04, skipRootThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendFrameRoots
   static const unsigned char beginFrames[] = {
      0x48, 0x89, 0xC1 // mov rcx, rax; upper boundary of the first frame
   };

   static const unsigned char findFrame[] = {
      0x48, 0x89, 0xC6, // mov rsi, rax
      0x48, 0x8B, 0x06, // mov rax, [rsi]; previous frame link
      0x48, 0x85, 0xC0  // test rax, rax
   };

   if (!write(beginFrames, sizeof(beginFrames))
      || !bind(nextFrame)
      || !write(findFrame, sizeof(findFrame))
      || !branch(0x05, nextFrame))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char frameRange[] = {
      0x51,                   // push rcx; frame upper boundary
      0x48, 0x29, 0xF1,       // sub rcx, rsi
      0x48, 0xF7, 0xD9,       // neg rcx; frame root size
      0x51,                   // push rcx; frame root size
      0x48, 0x8B, 0x46, 0x08, // mov rax, [rsi + 8]; next frame descriptor
      0x48, 0x85, 0xC0,       // test rax, rax
      0x48, 0x89, 0xC1        // mov rcx, rax
   };

   if (!write(frameRange, sizeof(frameRange))
      || !branch(0x05, nextFrame)
      || !bind(skipRootThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char continueRoots[] = {
      0x48, 0x85, 0xDB // test rbx, rbx
   };

   if (!write(continueRoots, sizeof(continueRoots))
      || !branch(0x05, nextRootThread)
      || !bind(rootsReady))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::InvokeCollector
   static const unsigned char saveRoots[] = {
      0x48, 0x89, 0x65, 0xF8 // mov [rbp - 8], rsp; root descriptor array
   };

   if (!write(saveRoots, sizeof(saveRoots)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::WindowsX64) {
      static const unsigned char arguments[] = {
         0x4C, 0x8B, 0x45, 0x08, // mov r8, [rbp + 8]; requested allocation size
         0x48, 0x8B, 0x55, 0x00, // mov rdx, [rbp]; object size
         0x48, 0x89, 0xE1        // mov rcx, rsp; root descriptor array
      };

      if (!write(arguments, sizeof(arguments)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char arguments[] = {
         0x48, 0x8B, 0x55, 0x08, // mov rdx, [rbp + 8]; requested allocation size
         0x48, 0x8B, 0x75, 0x00, // mov rsi, [rbp]; object size
         0x48, 0x89, 0xE7        // mov rdi, rsp; root descriptor array
      };

      if (!write(arguments, sizeof(arguments)))
         return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char prepareCollect[] = {
      0x48, 0x89, 0xE8,             // mov rax, rbp
      0x48, 0x8B, 0x68, 0x10,       // mov rbp, [rax + 16]; restore caller frame
      0x48, 0x83, 0xEC, 0x30,       // sub rsp, 48; Windows x64 shadow space and alignment
      0x48, 0x89, 0x44, 0x24, 0x28, // mov [rsp + 40], rax; preserve collector frame
      0xFF, 0x15                    // call qword [rip + rel32]
   };

   if (!write(prepareCollect, sizeof(prepareCollect))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::CollectRuntime, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char saveResult[] = {
      0x49, 0x89, 0xC4,             // mov r12, rax; allocated object
      0x48, 0x8B, 0x6C, 0x24, 0x28, // mov rbp, [rsp + 40]; collector frame
      0x31, 0xDB,                   // xor ebx, ebx
      0x48, 0xB8                    // mov rax, CORE_GC_TABLE + gc_signal
   };

   if (!write(saveResult, sizeof(saveResult))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.signal, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::ResumeMutators
   static const unsigned char clearCollector[] = {
      0x48, 0x89, 0x18, // mov [rax], rbx
      0xFF, 0x15        // call qword [rip + rel32]
   };

   if (!write(clearCollector, sizeof(clearCollector))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::SignalCollectionEnd, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char finishCall[] = {
      0x48, 0x83, 0xC4, 0x30, // add rsp, 48
      0x4C, 0x89, 0xE3,       // mov rbx, r12; allocation result
      0x48, 0xBF              // mov rdi, CORE_GC_TABLE + gc_lock
   };

   if (!write(finishCall, sizeof(finishCall))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::ReleaseRootLock, RuntimeCoreAction::Return
   static const unsigned char epilogue[] = {
      0xBA, 0xFF, 0xFF, 0xFF, 0xFF, // mov edx, -1
      0xF0, 0x0F, 0xC1, 0x17,       // lock xadd dword [rdi], edx
      0x48, 0x89, 0xEC,             // mov rsp, rbp
      0x59,                         // pop rcx
      0x5A,                         // pop rdx
      0x5D,                         // pop rbp
      0x41, 0x5B,                   // pop r11
      0x41, 0x5A,                   // pop r10
      0xC3                          // ret
   };

   if (!write(epilogue, sizeof(epilogue)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodePrepareAMD64(
   const ExternalABI& externalABI)
{
   if (externalABI.platformABI == PlatformABI::WindowsX64)
      return writeByte(0xC3) ? RuntimeCoreError::None // ret; no adapter required
         : RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI != PlatformABI::SystemVAMD64)
      return RuntimeCoreError::InvalidABI;

   static const unsigned char begin[] = {
      0x48, 0x89, 0xC7,       // mov rdi, rax; System V AMD64 argument 1
      0x48, 0x83, 0xEC, 0x08, // sub rsp, 8; align call frame
      0xFF, 0x15              // call qword [rip + rel32]
   };

   if (!write(begin, sizeof(begin))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
         RuntimeCoreSymbol::PrepareRuntime, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char end[] = {
      0x48, 0x83, 0xC4, 0x08, // add rsp, 8
      0xC3                    // ret
   };

   return write(end, sizeof(end)) ? RuntimeCoreError::None
      : RuntimeCoreError::WriteFailed;
}

RuntimeCoreError RuntimeCoreEncoder :: encodeCurrentThreadAMD64(
   const RuntimeSpec& runtime)
{
   if (_target.tlsModel == TLSModel::Windows) {
      static const unsigned char code[] = {
         0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, // mov rax, [gs:0x58]; Windows TLS array
         0x48, 0x8B, 0x00                                      // mov rax, [rax]; ELENA thread content
      };

      return write(code, sizeof(code)) ? RuntimeCoreError::None
         : RuntimeCoreError::WriteFailed;
   }
   if (_target.tlsModel == TLSModel::ELF) {
      static const unsigned char code[] = {
         0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov rax, [fs:0]; ELF thread pointer
         0x48, 0x83, 0xE8                                      // sub rax, thread_content_size
      };
      if (runtime.dataLayout.threadContent.size > 0x7F
         || !write(code, sizeof(code))
         || !writeByte((unsigned char)runtime.dataLayout.threadContent.size))
      {
         return RuntimeCoreError::WriteFailed;
      }

      return RuntimeCoreError::None;
   }

   return RuntimeCoreError::InvalidABI;
}

RuntimeCoreError RuntimeCoreEncoder :: encodeExceptionDispatcherAMD64(
   const RuntimeSpec& runtime)
{
   static const unsigned char preserveFault[] = {
      0x49, 0x89, 0xD2, // mov r10, rdx; preserve the faulting instruction
      0x48, 0x89, 0xC2  // mov rdx, rax; publish the ELENA exception code
   };

   if (!write(preserveFault, sizeof(preserveFault)))
      return RuntimeCoreError::WriteFailed;

   if (runtime.threadingMode == ThreadingMode::MultiThread) {
      RuntimeCoreError currentThread = encodeCurrentThreadAMD64(runtime);
      if (currentThread != RuntimeCoreError::None)
         return currentThread;
   }
   else {
      static const unsigned char contentAddress[] = {
         0x48, 0xB8 // mov rax, CORE_SINGLE_CONTENT
      };

      if (!write(contentAddress, sizeof(contentAddress))
         || !addRelocation(
            RuntimeCoreRelocationKind::Absolute64,
            RuntimeCoreSymbol::SingleContent,
            0,
            8))
      {
         return RuntimeCoreError::WriteFailed;
      }
   }

   static const unsigned char dispatch[] = {
      0xFF, 0x20 // jmp qword [rax + eh_critical]
   };

   return write(dispatch, sizeof(dispatch))
      ? RuntimeCoreError::None
      : RuntimeCoreError::WriteFailed;
}

RuntimeCoreError RuntimeCoreEncoder :: encodeWaitForGCAMD64(
   const RuntimeSpec& runtime, const ExternalABI& externalABI)
{
   if (runtime.threadingMode == ThreadingMode::SingleThread)
      return writeByte(0xC3) ? RuntimeCoreError::None // ret; STA has no collection barrier
         : RuntimeCoreError::WriteFailed;

   unsigned char wait = newLabel();
   unsigned char noCollection = newLabel();
   static const unsigned char save[] = {
      0x53,             // push rbx
      0x41, 0x52,       // push r10
      0x41, 0x53,       // push r11
      0x55,             // push rbp
      0x48, 0x89, 0xE7, // mov rdi, rsp
      0x52,             // push rdx
      0x56,             // push rsi
      0x50,             // push rax
      0x48, 0xBB        // mov rbx, CORE_GC_TABLE + gc_lock
   };

   if (!write(save, sizeof(save))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8)
      || !bind(wait))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char lock[] = {
      0xBA, 0x01, 0x00, 0x00, 0x00, // mov edx, 1
      0x31, 0xC0,                   // xor eax, eax
      0xF0, 0x0F, 0xB1, 0x13        // lock cmpxchg dword [rbx], edx
   };

   if (!write(lock, sizeof(lock)) || !branch(0x05, wait))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentThread = encodeCurrentThreadAMD64(runtime);
   if (currentThread != RuntimeCoreError::None)
      return currentThread;

   const unsigned char thread[] = {
      0x48, 0x8B, 0x70, (unsigned char)runtime.dataLayout.threadContent.syncEvent,  // mov rsi, [rax + tt_sync_event]
      0xFF, 0x70, (unsigned char)runtime.dataLayout.threadContent.stackFrame,       // push qword [rax + tt_stack_frame]
      0x48, 0x89, 0x78, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov [rax + tt_stack_frame], rdi
      0x48, 0xBA                                                                    // mov rdx, CORE_GC_TABLE + gc_signal
   };

   if (!write(thread, sizeof(thread))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.signal, 8))
      return RuntimeCoreError::WriteFailed;

   static const unsigned char loadSignal[] = {
      0x48, 0x8B, 0x12, // mov rdx, [rdx]
      0x48, 0x85, 0xD2  // test rdx, rdx
   };

   if (!write(loadSignal, sizeof(loadSignal)) || !branch(0x04, noCollection))
      return RuntimeCoreError::WriteFailed;

   const unsigned char enterSafeRegion[] = {
      0xFF, 0x70, (unsigned char)runtime.dataLayout.threadContent.flags, // push qword [rax + tt_flags]
      0xC7, 0x40, (unsigned char)runtime.dataLayout.threadContent.flags,
      0x01, 0x00, 0x00, 0x00,                                           // mov dword [rax + tt_flags], 1
      0x48, 0x83, 0xEC, 0x30                                            // sub rsp, 48; shadow space and alignment
   };

   if (!write(enterSafeRegion, sizeof(enterSafeRegion)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::WindowsX64) {
      static const unsigned char callSignal[] = {
         0x48, 0x89, 0xF1, // mov rcx, rsi; Windows x64 argument 1
         0xFF, 0x15        // call qword [rip + rel32]
      };
      if (!write(callSignal, sizeof(callSignal)))
         return RuntimeCoreError::WriteFailed;
   }
   else if (externalABI.platformABI == PlatformABI::SystemVAMD64) {
      static const unsigned char callSignal[] = {
         0x48, 0x89, 0xF7, // mov rdi, rsi; System V AMD64 argument 1
         0xFF, 0x15        // call qword [rip + rel32]
      };
      if (!write(callSignal, sizeof(callSignal)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      return RuntimeCoreError::InvalidABI;
   }

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
      RuntimeCoreSymbol::SignalStop, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char unlockAddress[] = {
      0x48, 0xBF // mov rdi, CORE_GC_TABLE + gc_lock
   };

   if (!write(unlockAddress, sizeof(unlockAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char unlock[] = {
      0xB8, 0xFF, 0xFF, 0xFF, 0xFF, // mov eax, -1
      0xF0, 0x0F, 0xC1, 0x07,       // lock xadd dword [rdi], eax
      0xFF, 0x15                    // call qword [rip + rel32]
   };

   if (!write(unlock, sizeof(unlock)))
      return RuntimeCoreError::WriteFailed;

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalRelative32,
      RuntimeCoreSymbol::WaitForCollection, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char releaseCallFrame[] = {
      0x48, 0x83, 0xC4, 0x30, // add rsp, 48
      0x5A                    // pop rdx; previous thread flags
   };

   if (!write(releaseCallFrame, sizeof(releaseCallFrame)))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentThreadAfterWait = encodeCurrentThreadAMD64(runtime);
   if (currentThreadAfterWait != RuntimeCoreError::None)
      return currentThreadAfterWait;

   const unsigned char restoreFrame[] = {
      0x89, 0x50, (unsigned char)runtime.dataLayout.threadContent.flags,      // mov [rax + tt_flags], edx
      0x5A,                                                                  // pop rdx; previous frame
      0x48, 0x89, 0x50, (unsigned char)runtime.dataLayout.threadContent.stackFrame // mov [rax + tt_stack_frame], rdx
   };
   static const unsigned char end[] = {
      0x58,                   // pop rax
      0x5E,                   // pop rsi
      0x5A,                   // pop rdx
      0x48, 0x83, 0xC4, 0x08, // add rsp, 8; discard saved rbp slot
      0x41, 0x5B,             // pop r11
      0x41, 0x5A,             // pop r10
      0x5B,                   // pop rbx
      0xC3                    // ret
   };

   if (!write(restoreFrame, sizeof(restoreFrame))
      || !write(end, sizeof(end))
      || !bind(noCollection))
      return RuntimeCoreError::WriteFailed;

   const unsigned char noCollectionFrame[] = {
      0x5A,                                                                       // pop rdx; previous frame
      0x48, 0x89, 0x50, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov [rax + tt_stack_frame], rdx
      0x48, 0xBF                                                                 // mov rdi, CORE_GC_TABLE + gc_lock
   };

   if (!write(noCollectionFrame, sizeof(noCollectionFrame))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute64,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 8))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char noCollectionUnlock[] = {
      0xBB, 0xFF, 0xFF, 0xFF, 0xFF, // mov ebx, -1
      0xF0, 0x0F, 0xC1, 0x1F        // lock xadd dword [rdi], ebx
   };
   if (!write(noCollectionUnlock, sizeof(noCollectionUnlock))
      || !write(end, sizeof(end)))
   {
      return RuntimeCoreError::WriteFailed;
   }

   return fixLabels();
}
