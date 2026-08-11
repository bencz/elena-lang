#include "runtimecore.h"

using namespace elena_lang;
using namespace elena_lang::codegen;
using namespace elena_lang::codegen::x86;

RuntimeCoreError RuntimeCoreEncoder :: encodeAllocateYoungX86(const RuntimeSpec& runtime)
{
   const int currentOffset = runtime.dataLayout.gc.youngCurrent;
   const int endOffset = runtime.dataLayout.gc.youngEnd;
   const int lockOffset = runtime.dataLayout.gc.lock;
   unsigned char collect = newLabel();
   unsigned char wait = newLabel();

   if (runtime.threadingMode == ThreadingMode::MultiThread) {
      if (!writeByte(0xBF) // mov edi, CORE_GC_TABLE + gc_lock
         || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
            RuntimeCoreSymbol::GCData, lockOffset, 4)
         || !bind(wait))
      {
         return RuntimeCoreError::WriteFailed;
      }

      static const unsigned char lock[] = {
         0xBA, 0x01, 0x00, 0x00, 0x00, // mov edx, 1
         0x31, 0xC0,                   // xor eax, eax
         0xF0, 0x0F, 0xB1, 0x17        // lock cmpxchg dword [edi], edx
      };

      if (!write(lock, sizeof(lock)) || !branch(0x05, wait))
         return RuntimeCoreError::WriteFailed;
   }

   if (!writeByte(0xA1) // mov eax, [CORE_GC_TABLE + gc_yg_current]
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, currentOffset, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char add[] = {
      0x01, 0xC1 // add ecx, eax
   };

   if (!write(add, sizeof(add)) || !branch(0x02, collect))
      return RuntimeCoreError::WriteFailed;

   if (runtime.threadingMode == ThreadingMode::SingleThread) {
      static const unsigned char loadEnd[] = {
         0x8B, 0x3D // mov edi, [CORE_GC_TABLE + gc_yg_end]
      };
      static const unsigned char compare[] = {
         0x39, 0xF9 // cmp ecx, edi
      };

      if (!write(loadEnd, sizeof(loadEnd))
         || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
            RuntimeCoreSymbol::GCData, endOffset, 4)
         || !write(compare, sizeof(compare)))
      {
         return RuntimeCoreError::WriteFailed;
      }
   }
   else {
      static const unsigned char compareEnd[] = {
         0x3B, 0x0D // cmp ecx, [CORE_GC_TABLE + gc_yg_end]
      };

      if (!write(compareEnd, sizeof(compareEnd))
         || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
            RuntimeCoreSymbol::GCData, endOffset, 4))
      {
         return RuntimeCoreError::WriteFailed;
      }
   }

   if (!branch(0x03, collect))
      return RuntimeCoreError::WriteFailed;

   static const unsigned char storeCurrent[] = {
      0x89, 0x0D // mov [CORE_GC_TABLE + gc_yg_current], ecx
   };

   if (!write(storeCurrent, sizeof(storeCurrent))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, currentOffset, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (runtime.threadingMode == ThreadingMode::MultiThread) {
      static const unsigned char success[] = {
         0xBA, 0xFF, 0xFF, 0xFF, 0xFF, // mov edx, -1
         0x8D, 0x58, 0x08,             // lea ebx, [eax + object_header_size]
         0xF0, 0x0F, 0xC1, 0x17,       // lock xadd dword [edi], edx
         0xC3                          // ret
      };

      if (!write(success, sizeof(success)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char success[] = {
         0x8D, 0x58, 0x08, // lea ebx, [eax + object_header_size]
         0xC3              // ret
      };

      if (!write(success, sizeof(success)))
         return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char slow[] = {
      0x29, 0xC1, // sub ecx, eax; restore allocation size
      0x31, 0xD2, // xor edx, edx; minor collection
      0xE8        // call GC_COLLECT rel32
   };

   if (!bind(collect) || !write(slow, sizeof(slow))
      || !addRelocation(RuntimeCoreRelocationKind::Relative32,
         RuntimeCoreSymbol::CollectYoung, 0, 4)
      || !writeByte(0xC3)) // ret
   {
      return RuntimeCoreError::WriteFailed;
   }

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodeAllocatePermanentX86(
   const RuntimeSpec& runtime, const ExternalABI& externalABI,
   const RuntimeCoreProtocol& protocol)
{
   if (!protocol.isValid(runtime))
      return RuntimeCoreError::InvalidRuntime;
   if (runtime.threadingMode == ThreadingMode::MultiThread)
      return encodeAllocatePermanentMTAX86(runtime, externalABI, protocol);

   const int currentOffset = runtime.dataLayout.gc.permanentCurrent;
   const int endOffset = runtime.dataLayout.gc.permanentEnd;
   const int stackFrameOffset = runtime.dataLayout.threadContent.stackFrame;
   unsigned char collect = newLabel();

   if (!writeByte(0xA1) // mov eax, [CORE_GC_TABLE + gc_perm_current]
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, currentOffset, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char add[] = {
      0x01, 0xC1, // add ecx, eax
      0x3B, 0x0D  // cmp ecx, [CORE_GC_TABLE + gc_perm_end]
   };

   if (!write(add, sizeof(add))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, endOffset, 4)
      || !branch(0x03, collect))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char store[] = {
      0x89, 0x0D // mov [CORE_GC_TABLE + gc_perm_current], ecx
   };
   static const unsigned char success[] = {
      0x8D, 0x58, 0x08, // lea ebx, [eax + object_header_size]
      0xC3              // ret
   };

   if (!write(store, sizeof(store))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, currentOffset, 4)
      || !write(success, sizeof(success))
      || !bind(collect))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char slow[] = {
      0x29, 0xC1, // sub ecx, eax; restore allocation size
      0x56,       // push esi
      0x89, 0x25  // mov [CORE_SINGLE_CONTENT + tt_stack_frame], esp
   };

   if (!write(slow, sizeof(slow))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::SingleContent, stackFrameOffset, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.stackAlignment == 16) {
      static const unsigned char alignStack[] = {
         0x89, 0xE6,       // mov esi, esp
         0x83, 0xE4, 0xF0, // and esp, -16
         0x83, 0xEC, 0x0C  // sub esp, 12
      };

      if (!write(alignStack, sizeof(alignStack)))
         return RuntimeCoreError::WriteFailed;
   }

   if (!writeByte(0x51)      // push ecx; allocation size
      || !writeByte(0xFF) || !writeByte(0x15) // call dword [absolute32]
      || !addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
         RuntimeCoreSymbol::CollectPermanent, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char result[] = {
      0x89, 0xC3 // mov ebx, eax
   };

   if (!write(result, sizeof(result)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.stackAlignment == 16) {
      static const unsigned char restore[] = {
         0x89, 0xF4 // mov esp, esi
      };

      if (!write(restore, sizeof(restore)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char releaseArgument[] = {
         0x83, 0xC4, 0x04 // add esp, 4
      };

      if (!write(releaseArgument, sizeof(releaseArgument)))
         return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char end[] = {
      0x5E, // pop esi
      0xC3  // ret
   };
   if (!write(end, sizeof(end)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodeAllocatePermanentMTAX86(
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
   if (_target.tlsModel != TLSModel::Windows && _target.tlsModel != TLSModel::ELF)
      return RuntimeCoreError::InvalidABI;

   unsigned char allocation = newLabel();
   unsigned char wait = newLabel();
   unsigned char collect = newLabel();

   if (!bind(allocation)
      || !writeByte(0xBF) // mov edi, CORE_GC_TABLE + gc_lock
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4)
      || !bind(wait))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char acquireLock[] = {
      0xBA, 0x01, 0x00, 0x00, 0x00, // mov edx, 1
      0x31, 0xC0,                   // xor eax, eax
      0xF0, 0x0F, 0xB1, 0x17        // lock cmpxchg dword [edi], edx
   };

   if (!write(acquireLock, sizeof(acquireLock))
      || !branch(0x05, wait)
      || !writeByte(0xA1) // mov eax, [CORE_GC_TABLE + gc_perm_current]
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.permanentCurrent, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char add[] = {
      0x01, 0xC1 // add ecx, eax
   };
   static const unsigned char compareEnd[] = {
      0x3B, 0x0D // cmp ecx, [CORE_GC_TABLE + gc_perm_end]
   };

   if (!write(add, sizeof(add))
      || !branch(0x02, collect)
      || !write(compareEnd, sizeof(compareEnd))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.permanentEnd, 4)
      || !branch(0x03, collect))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char storeCurrent[] = {
      0x89, 0x0D // mov [CORE_GC_TABLE + gc_perm_current], ecx
   };
   static const unsigned char success[] = {
      0xBA, 0xFF, 0xFF, 0xFF, 0xFF, // mov edx, -1
      0x8D, 0x58, 0x08,             // lea ebx, [eax + object_header_size]
      0xF0, 0x0F, 0xC1, 0x17,       // lock xadd dword [edi], edx
      0xC3                          // ret
   };

   if (!write(storeCurrent, sizeof(storeCurrent))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.permanentCurrent, 4)
      || !write(success, sizeof(success))
      || !bind(collect))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char restoreSize[] = {
      0x29, 0xC1 // sub ecx, eax
   };

   if (!write(restoreSize, sizeof(restoreSize)))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentThread = encodeCurrentThreadX86(runtime);
   if (currentThread != RuntimeCoreError::None)
      return currentThread;

   // RuntimeCoreAction::PublishFrame
   const unsigned char publishFrame[] = {
      0x56,                                                             // push esi
      0x89, 0x60, (unsigned char)runtime.dataLayout.threadContent.stackFrame // mov [eax + tt_stack_frame], esp
   };

   if (!write(publishFrame, sizeof(publishFrame)))
      return RuntimeCoreError::WriteFailed;

   // RuntimeCoreAction::InvokeCollector
   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char callCollector[] = {
         0x89, 0xE6,       // mov esi, esp
         0x83, 0xE4, 0xF0, // and esp, -16
         0x83, 0xEC, 0x0C, // sub esp, 12
         0x51,             // push ecx; allocation size
         0xFF, 0x15        // call dword [absolute32]
      };

      if (!write(callCollector, sizeof(callCollector)))
         return RuntimeCoreError::WriteFailed;
   }
   else if (externalABI.platformABI == PlatformABI::WindowsX86) {
      static const unsigned char callCollector[] = {
         0x51,       // push ecx; allocation size
         0xFF, 0x15  // call dword [absolute32]
      };

      if (!write(callCollector, sizeof(callCollector)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      return RuntimeCoreError::InvalidABI;
   }

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
      RuntimeCoreSymbol::CollectPermanent, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char result[] = {
      0x89, 0xC3 // mov ebx, eax
   };

   if (!write(result, sizeof(result)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char restoreStack[] = {
         0x89, 0xF4 // mov esp, esi
      };

      if (!write(restoreStack, sizeof(restoreStack)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char releaseArgument[] = {
         0x83, 0xC4, 0x04 // add esp, 4
      };

      if (!write(releaseArgument, sizeof(releaseArgument)))
         return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::ReleaseAllocationLock, RuntimeCoreAction::Return
   static const unsigned char epilogue[] = {
      0xBA, 0xFF, 0xFF, 0xFF, 0xFF, // mov edx, -1
      0xF0, 0x0F, 0xC1, 0x17,       // lock xadd dword [edi], edx
      0x5E,                         // pop esi
      0xC3                          // ret
   };

   if (!write(epilogue, sizeof(epilogue)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodeCollectX86(
   const RuntimeSpec& runtime, const ExternalABI& externalABI,
   const RuntimeCoreProtocol& protocol)
{
   if (!protocol.isValid(runtime))
      return RuntimeCoreError::InvalidRuntime;

   if (protocol.contains(RuntimeCoreAction::EnumerateMutators))
      return encodeCollectMTAX86(runtime, externalABI, protocol);

   unsigned char findFrameStart = newLabel();

   // RuntimeCoreAction::PublishFrame
   static const unsigned char prologue[] = {
      0x56,       // push esi
      0x55,       // push ebp
      0x89, 0x25  // mov [CORE_SINGLE_CONTENT + tt_stack_frame], esp
   };

   if (!write(prologue, sizeof(prologue))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::SingleContent,
         runtime.dataLayout.threadContent.stackFrame, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::BeginRoots
   static const unsigned char rootHeader[] = {
      0x52,       // push edx
      0x51,       // push ecx
      0x89, 0xE5, // mov ebp, esp
      0x31, 0xC9, // xor ecx, ecx
      0x51,       // push ecx; root size 3
      0x51,       // push ecx; root address 3
      0x51,       // push ecx; reserved root descriptor word
      0x8B, 0x0D  // mov ecx, [CORE_SYSTEM_ENVIRONMENT]; static root count
   };

   if (!write(rootHeader, sizeof(rootHeader))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::SystemEnvironment, 0, 4)
      || !writeByte(0xBE) // mov esi, STATIC_ROOTS
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::StaticRoots, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendStaticRoots
   static const unsigned char staticRoots[] = {
      0xC1, 0xE1, 0x02, // shl ecx, 2; convert count to bytes
      0x56,             // push esi; static root address
      0x51,             // push ecx; static root size
      0x8B, 0x35        // mov esi, [CORE_GC_TABLE + gc_perm_start]
   };

   if (!write(staticRoots, sizeof(staticRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.permanentStart, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendPermanentRoots
   static const unsigned char permanentRoots[] = {
      0x8B, 0x0D // mov ecx, [CORE_GC_TABLE + gc_perm_current]
   };

   if (!write(permanentRoots, sizeof(permanentRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.permanentCurrent, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendFrameRoots
   static const unsigned char beginFrames[] = {
      0x29, 0xF1, // sub ecx, esi; permanent root size
      0x56,       // push esi; permanent root address
      0x51,       // push ecx; permanent root size
      0xA1        // mov eax, [CORE_SINGLE_CONTENT + tt_stack_frame]
   };

   if (!write(beginFrames, sizeof(beginFrames))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::SingleContent,
         runtime.dataLayout.threadContent.stackFrame, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char frameHead[] = {
      0x89, 0xC1 // mov ecx, eax; upper boundary of the first frame
   };
   static const unsigned char findStart[] = {
      0x89, 0xC6, // mov esi, eax
      0x8B, 0x06, // mov eax, [esi]; previous frame link
      0x85, 0xC0  // test eax, eax
   };

   if (!write(frameHead, sizeof(frameHead))
      || !bind(findFrameStart)
      || !write(findStart, sizeof(findStart))
      || !branch(0x05, findFrameStart))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char frameRange[] = {
      0x51,             // push ecx; frame upper boundary
      0x29, 0xF1,       // sub ecx, esi
      0xF7, 0xD9,       // neg ecx; frame root size
      0x51,             // push ecx; frame root size
      0x8B, 0x46, 0x04, // mov eax, [esi + 4]; next frame descriptor
      0x85, 0xC0,       // test eax, eax
      0x89, 0xC1        // mov ecx, eax
   };

   if (!write(frameRange, sizeof(frameRange))
      || !branch(0x05, findFrameStart))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::InvokeCollector
   static const unsigned char arguments[] = {
      0x89, 0x65, 0xFC, // mov [ebp - 4], esp; root descriptor array
      0x8B, 0x5D, 0x00, // mov ebx, [ebp]; object size
      0x8B, 0x55, 0x04, // mov edx, [ebp + 4]; requested allocation size
      0x89, 0xE0,       // mov eax, esp; root descriptor array
      0x89, 0xE9,       // mov ecx, ebp
      0x8B, 0x69, 0x08  // mov ebp, [ecx + 8]; restore caller frame
   };

   if (!write(arguments, sizeof(arguments)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char align[] = {
         0x83, 0xE4, 0xF0 // and esp, -16
      };

      if (!write(align, sizeof(align)))
         return RuntimeCoreError::WriteFailed;
   }
   else if (externalABI.platformABI != PlatformABI::WindowsX86) {
      return RuntimeCoreError::InvalidABI;
   }

   static const unsigned char call[] = {
      0x51,       // push ecx; collector frame
      0x52,       // push edx; requested allocation size
      0x53,       // push ebx; object size
      0x50,       // push eax; root descriptor array
      0xFF, 0x15  // call dword [absolute32]
   };

   if (!write(call, sizeof(call))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
         RuntimeCoreSymbol::CollectRuntime, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char epilogue[] = {
      0x8B, 0x6C, 0x24, 0x0C, // mov ebp, [esp + 12]; collector frame
      0x89, 0xC3,             // mov ebx, eax; allocation result
      0x89, 0xEC,             // mov esp, ebp
      0x59,                   // pop ecx
      0x5A,                   // pop edx
      0x5D,                   // pop ebp
      0x5E,                   // pop esi
      0xC3                    // ret
   };
   if (!write(epilogue, sizeof(epilogue)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodeCollectMTAX86(
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

   RuntimeCoreError currentThread = encodeCurrentThreadX86(runtime);
   if (currentThread != RuntimeCoreError::None)
      return currentThread;

   // RuntimeCoreAction::PublishFrame
   const unsigned char prologue[] = {
      0x56,                                                              // push esi
      0x55,                                                              // push ebp
      0x8B, 0x70, (unsigned char)runtime.dataLayout.threadContent.syncEvent,  // mov esi, [eax + tt_sync_event]
      0x89, 0x60, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov [eax + tt_stack_frame], esp
      0x52,                                                              // push edx
      0x51,                                                              // push ecx
      0x8B, 0x15                                                         // mov edx, [CORE_GC_TABLE + gc_signal]
   };

   if (!write(prologue, sizeof(prologue))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.signal, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::ObserveCollection
   static const unsigned char checkSignal[] = {
      0x85, 0xD2 // test edx, edx
   };

   if (!write(checkSignal, sizeof(checkSignal)) || !branch(0x04, collect))
      return RuntimeCoreError::WriteFailed;

   // RuntimeCoreAction::ParkMutator
   const unsigned char enterSafeRegion[] = {
      0xFF, 0x70, (unsigned char)runtime.dataLayout.threadContent.flags, // push dword [eax + tt_flags]
      0xC7, 0x40, (unsigned char)runtime.dataLayout.threadContent.flags,
      0x01, 0x00, 0x00, 0x00                                           // mov dword [eax + tt_flags], 1
   };

   if (!write(enterSafeRegion, sizeof(enterSafeRegion)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char callSignal[] = {
         0x89, 0xE7,             // mov edi, esp
         0x83, 0xE4, 0xF0,       // and esp, -16
         0x83, 0xEC, 0x0C,       // sub esp, 12
         0x56,                   // push esi; System V x86 argument 1
         0xFF, 0x15              // call dword [absolute32]
      };

      if (!write(callSignal, sizeof(callSignal)))
         return RuntimeCoreError::WriteFailed;
   }
   else if (externalABI.platformABI == PlatformABI::WindowsX86) {
      static const unsigned char callSignal[] = {
         0x56,       // push esi; Windows x86 argument 1
         0xFF, 0x15  // call dword [absolute32]
      };

      if (!write(callSignal, sizeof(callSignal)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      return RuntimeCoreError::InvalidABI;
   }

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
      RuntimeCoreSymbol::SignalStop, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char restore[] = {
         0x89, 0xFC // mov esp, edi
      };

      if (!write(restore, sizeof(restore)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char release[] = {
         0x83, 0xC4, 0x04 // add esp, 4
      };

      if (!write(release, sizeof(release)))
         return RuntimeCoreError::WriteFailed;
   }

   if (!writeByte(0xBF) // mov edi, CORE_GC_TABLE + gc_lock
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char unlock[] = {
      0xBB, 0xFF, 0xFF, 0xFF, 0xFF, // mov ebx, -1
      0xF0, 0x0F, 0xC1, 0x1F        // lock xadd dword [edi], ebx
   };

   if (!write(unlock, sizeof(unlock)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char callWait[] = {
         0x89, 0xE6,       // mov esi, esp
         0x83, 0xE4, 0xF0, // and esp, -16
         0xFF, 0x15        // call dword [absolute32]
      };

      if (!write(callWait, sizeof(callWait)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char callWait[] = {
         0xFF, 0x15 // call dword [absolute32]
      };

      if (!write(callWait, sizeof(callWait)))
         return RuntimeCoreError::WriteFailed;
   }

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
      RuntimeCoreSymbol::WaitForCollection, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char restore[] = {
         0x89, 0xF4 // mov esp, esi
      };

      if (!write(restore, sizeof(restore)))
         return RuntimeCoreError::WriteFailed;
   }

   RuntimeCoreError currentThreadAfterWait = encodeCurrentThreadX86(runtime);
   if (currentThreadAfterWait != RuntimeCoreError::None)
      return currentThreadAfterWait;

   const unsigned char leaveSafeRegion[] = {
      0x5A,                                                            // pop edx; previous thread flags
      0x89, 0x50, (unsigned char)runtime.dataLayout.threadContent.flags // mov [eax + tt_flags], edx
   };

   if (!write(leaveSafeRegion, sizeof(leaveSafeRegion)))
      return RuntimeCoreError::WriteFailed;

   static const unsigned char restoreState[] = {
      0x59,       // pop ecx
      0x5A,       // pop edx
      0x5D,       // pop ebp
      0x5E,       // pop esi
      0x85, 0xC9  // test ecx, ecx
   };

   // RuntimeCoreAction::RetryOperation
   static const unsigned char acquireLock[] = {
      0xBA, 0x01, 0x00, 0x00, 0x00, // mov edx, 1
      0x31, 0xC0,                   // xor eax, eax
      0xF0, 0x0F, 0xB1, 0x17        // lock cmpxchg dword [edi], edx
   };

   if (!write(restoreState, sizeof(restoreState))
      || !branch(0x05, repeatAllocation)
      || !writeByte(0xBF) // mov edi, CORE_GC_TABLE + gc_lock
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4)
      || !bind(retakeLock)
      || !write(acquireLock, sizeof(acquireLock))
      || !branch(0x05, retakeLock)
      || !jump(start)
      || !bind(repeatAllocation)
      || !writeByte(0xE8) // call GC_ALLOC rel32
      || !addRelocation(RuntimeCoreRelocationKind::Relative32,
         RuntimeCoreSymbol::AllocateYoungRoutine, 0, 4)
      || !writeByte(0xC3) // ret
      || !bind(collect))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::PublishCollector
   static const unsigned char publishCollector[] = {
      0x89, 0x35 // mov [CORE_GC_TABLE + gc_signal], esi
   };

   if (!write(publishCollector, sizeof(publishCollector))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.signal, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::EnumerateMutators
   static const unsigned char beginWaitList[] = {
      0x89, 0xE5, // mov ebp, esp
      0x8B, 0x40  // mov eax, [eax + tt_sync_event]
   };

   if (!write(beginWaitList, sizeof(beginWaitList))
      || !writeByte((unsigned char)runtime.dataLayout.threadContent.syncEvent)
      || !writeByte(0xBE) // mov esi, CORE_THREAD_TABLE + slots
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::ThreadTable, runtime.dataLayout.threadTable.slots, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char waitListCount[] = {
      0x31, 0xC9,                                                       // xor ecx, ecx
      0x8B, 0x7E, (unsigned char)(0 - runtime.dataLayout.threadTable.slots), // mov edi, [esi - slots]
      0x85, 0xFF                                                        // test edi, edi
   };

   if (!write(waitListCount, sizeof(waitListCount))
      || !branch(0x04, threadsReady)
      || !bind(nextThread))
      return RuntimeCoreError::WriteFailed;

   const unsigned char loadThread[] = {
      0x8B, 0x16,                                                       // mov edx, [esi]
      0x83, 0xC6, (unsigned char)runtime.dataLayout.threadTable.slotSize, // add esi, slot_size
      0x85, 0xD2                                                        // test edx, edx
   };

   if (!write(loadThread, sizeof(loadThread)) || !branch(0x04, skipThread))
      return RuntimeCoreError::WriteFailed;

   const unsigned char classifyThread[] = {
      0x31, 0xC9,                                                        // xor ecx, ecx
      0x3B, 0x42, (unsigned char)runtime.dataLayout.threadContent.syncEvent, // cmp eax, [edx + tt_sync_event]
      0x0F, 0x94, 0xC1,                                                  // sete cl
      0x0B, 0x4A, (unsigned char)runtime.dataLayout.threadContent.flags, // or ecx, [edx + tt_flags]
      0xF7, 0xC1, 0x01, 0x00, 0x00, 0x00                                // test ecx, 1
   };

   if (!write(classifyThread, sizeof(classifyThread))
      || !branch(0x05, skipWaitHandle))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char saveWaitHandle[] = {
      0xFF, 0x72, (unsigned char)runtime.dataLayout.threadContent.syncEvent // push dword [edx + tt_sync_event]
   };

   if (!write(saveWaitHandle, sizeof(saveWaitHandle))
      || !bind(skipWaitHandle))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      const unsigned char callClear[] = {
         0x89, 0xE3,                                                        // mov ebx, esp
         0x83, 0xE4, 0xF0,                                                  // and esp, -16
         0x83, 0xEC, 0x0C,                                                  // sub esp, 12
         0xFF, 0x72, (unsigned char)runtime.dataLayout.threadContent.syncEvent, // push dword [edx + tt_sync_event]
         0xFF, 0x15                                                         // call dword [absolute32]
      };

      if (!write(callClear, sizeof(callClear)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      const unsigned char callClear[] = {
         0xFF, 0x72, (unsigned char)runtime.dataLayout.threadContent.syncEvent, // push dword [edx + tt_sync_event]
         0xFF, 0x15                                                         // call dword [absolute32]
      };

      if (!write(callClear, sizeof(callClear)))
         return RuntimeCoreError::WriteFailed;
   }

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
      RuntimeCoreSymbol::SignalClear, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char restore[] = {
         0x89, 0xDC // mov esp, ebx
      };

      if (!write(restore, sizeof(restore)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char release[] = {
         0x83, 0xC4, 0x04 // add esp, 4
      };

      if (!write(release, sizeof(release)))
         return RuntimeCoreError::WriteFailed;
   }

   RuntimeCoreError reloadCurrentThread = encodeCurrentThreadX86(runtime);
   if (reloadCurrentThread != RuntimeCoreError::None)
      return reloadCurrentThread;

   const unsigned char reloadOwnEvent[] = {
      0x8B, 0x40, (unsigned char)runtime.dataLayout.threadContent.syncEvent // mov eax, [eax + tt_sync_event]
   };

   if (!write(reloadOwnEvent, sizeof(reloadOwnEvent))
      || !bind(skipThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char advanceThread[] = {
      0x83, 0xEF, 0x01 // sub edi, 1
   };

   if (!write(advanceThread, sizeof(advanceThread))
      || !branch(0x05, nextThread)
      || !bind(threadsReady)
      || !writeByte(0xBE) // mov esi, CORE_GC_TABLE + gc_lock
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::WaitForMutators
   static const unsigned char releaseForWait[] = {
      0xBA, 0xFF, 0xFF, 0xFF, 0xFF, // mov edx, -1
      0x89, 0xEB,                   // mov ebx, ebp
      0xF0, 0x0F, 0xC1, 0x16,       // lock xadd dword [esi], edx
      0x89, 0xE1,                   // mov ecx, esp; wait handle array
      0x29, 0xE3                    // sub ebx, esp; wait handle bytes
   };

   if (!write(releaseForWait, sizeof(releaseForWait))
      || !branch(0x04, skipWait))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char countWaitHandles[] = {
      0xC1, 0xEB, 0x02 // shr ebx, 2; wait handle count
   };

   if (!write(countWaitHandles, sizeof(countWaitHandles)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char callWaitForSignals[] = {
         0x89, 0xE7,       // mov edi, esp
         0x83, 0xE4, 0xF0, // and esp, -16
         0x83, 0xEC, 0x08, // sub esp, 8
         0x51,             // push ecx; System V x86 argument 2
         0x53,             // push ebx; System V x86 argument 1
         0xFF, 0x15        // call dword [absolute32]
      };

      if (!write(callWaitForSignals, sizeof(callWaitForSignals)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char callWaitForSignals[] = {
         0x51,       // push ecx; Windows x86 argument 2
         0x53,       // push ebx; Windows x86 argument 1
         0xFF, 0x15  // call dword [absolute32]
      };

      if (!write(callWaitForSignals, sizeof(callWaitForSignals)))
         return RuntimeCoreError::WriteFailed;
   }

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
      RuntimeCoreSymbol::WaitForSignals, 0, 4)
      || !bind(skipWait))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char discardWaitList[] = {
      0x89, 0xEC, // mov esp, ebp
      0xBF        // mov edi, CORE_GC_TABLE + gc_lock
   };

   // RuntimeCoreAction::AcquireRootLock
   if (!write(discardWaitList, sizeof(discardWaitList))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4)
      || !bind(rootLock)
      || !write(acquireLock, sizeof(acquireLock))
      || !branch(0x05, rootLock))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::BeginRoots
   static const unsigned char rootHeader[] = {
      0x89, 0xE5, // mov ebp, esp
      0x31, 0xC9, // xor ecx, ecx
      0x51,       // push ecx; root size 3
      0x51,       // push ecx; root address 3
      0x51,       // push ecx; reserved root descriptor word
      0x8B, 0x0D  // mov ecx, [CORE_SYSTEM_ENVIRONMENT]; static root count
   };

   if (!write(rootHeader, sizeof(rootHeader))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::SystemEnvironment, 0, 4)
      || !writeByte(0xBE) // mov esi, STATIC_ROOTS
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::StaticRoots, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendStaticRoots
   static const unsigned char staticRoots[] = {
      0xC1, 0xE1, 0x02, // shl ecx, 2; convert count to bytes
      0x56,             // push esi; static root address
      0x51,             // push ecx; static root size
      0x8B, 0x35        // mov esi, [CORE_GC_TABLE + gc_perm_start]
   };

   if (!write(staticRoots, sizeof(staticRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.permanentStart, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendPermanentRoots
   static const unsigned char permanentRoots[] = {
      0x8B, 0x0D // mov ecx, [CORE_GC_TABLE + gc_perm_current]
   };

   if (!write(permanentRoots, sizeof(permanentRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.permanentCurrent, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char beginThreadRoots[] = {
      0x29, 0xF1, // sub ecx, esi; permanent root size
      0x56,       // push esi; permanent root address
      0x51,       // push ecx; permanent root size
      0xB8        // mov eax, CORE_THREAD_TABLE
   };

   if (!write(beginThreadRoots, sizeof(beginThreadRoots))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::ThreadTable, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendTLSRoots
   static const unsigned char loadThreadCount[] = {
      0x8B, 0x18, // mov ebx, [eax]; thread count
      0x85, 0xDB  // test ebx, ebx
   };

   if (!write(loadThreadCount, sizeof(loadThreadCount))
      || !branch(0x04, rootsReady)
      || !bind(nextRootThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char decrementThread[] = {
      0x83, 0xEB, 0x01, // sub ebx, 1
      0xB8              // mov eax, CORE_THREAD_TABLE + slots
   };

   if (!write(decrementThread, sizeof(decrementThread))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::ThreadTable, runtime.dataLayout.threadTable.slots, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char loadThreadContent[] = {
      0x8B, 0x34, 0xD8, // mov esi, [eax + ebx * 8]
      0x85, 0xF6        // test esi, esi
   };

   if (!write(loadThreadContent, sizeof(loadThreadContent))
      || !branch(0x04, skipRootThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char tlsAddress[] = {
      0x8D, 0x46, (unsigned char)runtime.dataLayout.threadContent.size, // lea eax, [esi + thread_content_size]
      0x8B, 0x0D                                                       // mov ecx, [CORE_SYSTEM_ENVIRONMENT + et_tls_size]
   };

   if (!write(tlsAddress, sizeof(tlsAddress))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::SystemEnvironment,
         runtime.dataLayout.environment.tlsSize, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   const unsigned char tlsRoots[] = {
      0x50,                                                              // push eax; TLS root address
      0x51,                                                              // push ecx; TLS root size
      0x8B, 0x46, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov eax, [esi + tt_stack_frame]
      0x85, 0xC0                                                         // test eax, eax
   };

   if (!write(tlsRoots, sizeof(tlsRoots))
      || !branch(0x04, skipRootThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::AppendFrameRoots
   static const unsigned char startFrame[] = {
      0x89, 0xC1 // mov ecx, eax; upper boundary of the first frame
   };

   static const unsigned char findFrame[] = {
      0x89, 0xC6, // mov esi, eax
      0x8B, 0x06, // mov eax, [esi]; previous frame link
      0x85, 0xC0  // test eax, eax
   };

   if (!write(startFrame, sizeof(startFrame))
      || !bind(nextFrame)
      || !write(findFrame, sizeof(findFrame))
      || !branch(0x05, nextFrame))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char frameRange[] = {
      0x51,             // push ecx; frame upper boundary
      0x29, 0xF1,       // sub ecx, esi
      0xF7, 0xD9,       // neg ecx; frame root size
      0x51,             // push ecx; frame root size
      0x8B, 0x46, 0x04, // mov eax, [esi + 4]; next frame descriptor
      0x85, 0xC0,       // test eax, eax
      0x89, 0xC1        // mov ecx, eax
   };

   if (!write(frameRange, sizeof(frameRange))
      || !branch(0x05, nextFrame)
      || !bind(skipRootThread))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char continueRoots[] = {
      0x85, 0xDB // test ebx, ebx
   };

   if (!write(continueRoots, sizeof(continueRoots))
      || !branch(0x05, nextRootThread)
      || !bind(rootsReady))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::InvokeCollector
   static const unsigned char collectorArguments[] = {
      0x89, 0x65, 0xFC, // mov [ebp - 4], esp; root descriptor array
      0x8B, 0x5D, 0x00, // mov ebx, [ebp]; object size
      0x8B, 0x55, 0x04, // mov edx, [ebp + 4]; requested allocation size
      0x89, 0xE0,       // mov eax, esp; root descriptor array
      0x89, 0xE9,       // mov ecx, ebp
      0x8B, 0x69, 0x08  // mov ebp, [ecx + 8]; restore caller frame
   };

   if (!write(collectorArguments, sizeof(collectorArguments)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char align[] = {
         0x83, 0xE4, 0xF0 // and esp, -16
      };

      if (!write(align, sizeof(align)))
         return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char callCollector[] = {
      0x51,       // push ecx; collector frame
      0x52,       // push edx; requested allocation size
      0x53,       // push ebx; object size
      0x50,       // push eax; root descriptor array
      0xFF, 0x15  // call dword [absolute32]
   };

   if (!write(callCollector, sizeof(callCollector))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
         RuntimeCoreSymbol::CollectRuntime, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char saveResult[] = {
      0x89, 0xC7,             // mov edi, eax; allocated object
      0x8B, 0x6C, 0x24, 0x0C, // mov ebp, [esp + 12]; collector frame
      0x31, 0xC9,             // xor ecx, ecx
      0x89, 0x0D              // mov [CORE_GC_TABLE + gc_signal], ecx
   };

   if (!write(saveResult, sizeof(saveResult))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.signal, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char align[] = {
         0x83, 0xE4, 0xF0 // and esp, -16
      };

      if (!write(align, sizeof(align)))
         return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::ResumeMutators
   static const unsigned char callCollectionEnd[] = {
      0xFF, 0x15 // call dword [absolute32]
   };

   if (!write(callCollectionEnd, sizeof(callCollectionEnd))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
         RuntimeCoreSymbol::SignalCollectionEnd, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char finish[] = {
      0x89, 0xFB, // mov ebx, edi; allocation result
      0xBF        // mov edi, CORE_GC_TABLE + gc_lock
   };

   if (!write(finish, sizeof(finish))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   // RuntimeCoreAction::ReleaseRootLock, RuntimeCoreAction::Return
   static const unsigned char epilogue[] = {
      0xBA, 0xFF, 0xFF, 0xFF, 0xFF, // mov edx, -1
      0xF0, 0x0F, 0xC1, 0x17,       // lock xadd dword [edi], edx
      0x89, 0xEC,                   // mov esp, ebp
      0x59,                         // pop ecx
      0x5A,                         // pop edx
      0x5D,                         // pop ebp
      0x5E,                         // pop esi
      0xC3                          // ret
   };

   if (!write(epilogue, sizeof(epilogue)))
      return RuntimeCoreError::WriteFailed;

   return fixLabels();
}

RuntimeCoreError RuntimeCoreEncoder :: encodePrepareX86(
   const ExternalABI& externalABI)
{
   if (externalABI.platformABI == PlatformABI::WindowsX86)
      return writeByte(0xC3) ? RuntimeCoreError::None // ret; no adapter required
         : RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI != PlatformABI::SystemVX86)
      return RuntimeCoreError::InvalidABI;

   static const unsigned char begin[] = {
      0x56,             // push esi
      0x89, 0xE6,       // mov esi, esp
      0x83, 0xE4, 0xF0, // and esp, -16
      0x83, 0xEC, 0x0C, // sub esp, 12
      0x50,             // push eax; System V x86 argument 1
      0xFF, 0x15        // call dword [absolute32]
   };

   if (!write(begin, sizeof(begin))
      || !addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
         RuntimeCoreSymbol::PrepareRuntime, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char end[] = {
      0x89, 0xF4, // mov esp, esi
      0x5E,       // pop esi
      0xC3        // ret
   };

   return write(end, sizeof(end)) ? RuntimeCoreError::None
      : RuntimeCoreError::WriteFailed;
}

RuntimeCoreError RuntimeCoreEncoder :: encodeCurrentThreadX86(
   const RuntimeSpec& runtime)
{
   if (_target.tlsModel == TLSModel::Windows) {
      static const unsigned char code[] = {
         0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00, // mov eax, [fs:0x2C]; Windows TLS array
         0x8B, 0x00                          // mov eax, [eax]; ELENA thread content
      };

      return write(code, sizeof(code)) ? RuntimeCoreError::None
         : RuntimeCoreError::WriteFailed;
   }
   if (_target.tlsModel == TLSModel::ELF) {
      static const unsigned char code[] = {
         0x65, 0xA1, 0x00, 0x00, 0x00, 0x00, // mov eax, [gs:0]; ELF thread pointer
         0x83, 0xE8                          // sub eax, thread_content_size
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

RuntimeCoreError RuntimeCoreEncoder :: encodeExceptionDispatcherX86(
   const RuntimeSpec& runtime)
{
   static const unsigned char preserveFault[] = {
      0x89, 0xD6, // mov esi, edx; preserve the faulting instruction
      0x89, 0xC2  // mov edx, eax; publish the ELENA exception code
   };

   if (!write(preserveFault, sizeof(preserveFault)))
      return RuntimeCoreError::WriteFailed;

   if (runtime.threadingMode == ThreadingMode::MultiThread) {
      RuntimeCoreError currentThread = encodeCurrentThreadX86(runtime);
      if (currentThread != RuntimeCoreError::None)
         return currentThread;
   }
   else {
      if (!writeByte(0xB8) // mov eax, CORE_SINGLE_CONTENT
         || !addRelocation(
            RuntimeCoreRelocationKind::Absolute32,
            RuntimeCoreSymbol::SingleContent,
            0,
            4))
      {
         return RuntimeCoreError::WriteFailed;
      }
   }

   static const unsigned char dispatch[] = {
      0xFF, 0x20 // jmp dword [eax + eh_critical]
   };

   return write(dispatch, sizeof(dispatch))
      ? RuntimeCoreError::None
      : RuntimeCoreError::WriteFailed;
}

RuntimeCoreError RuntimeCoreEncoder :: encodeWaitForGCX86(
   const RuntimeSpec& runtime, const ExternalABI& externalABI)
{
   if (runtime.threadingMode == ThreadingMode::SingleThread)
      return writeByte(0xC3) ? RuntimeCoreError::None // ret; STA has no collection barrier
         : RuntimeCoreError::WriteFailed;

   unsigned char wait = newLabel();
   unsigned char noCollection = newLabel();
   static const unsigned char save[] = {
      0x53,       // push ebx
      0x56,       // push esi
      0x55,       // push ebp
      0x89, 0xE7, // mov edi, esp
      0x50,       // push eax
      0x52,       // push edx
      0xBB        // mov ebx, CORE_GC_TABLE + gc_lock
   };

   if (!write(save, sizeof(save))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4)
      || !bind(wait))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char lock[] = {
      0xBA, 0x01, 0x00, 0x00, 0x00, // mov edx, 1
      0x31, 0xC0,                   // xor eax, eax
      0xF0, 0x0F, 0xB1, 0x13        // lock cmpxchg dword [ebx], edx
   };

   if (!write(lock, sizeof(lock)) || !branch(0x05, wait))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentThread = encodeCurrentThreadX86(runtime);
   if (currentThread != RuntimeCoreError::None)
      return currentThread;

   const unsigned char thread[] = {
      0x8B, 0x70, (unsigned char)runtime.dataLayout.threadContent.syncEvent,  // mov esi, [eax + tt_sync_event]
      0xFF, 0x70, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // push dword [eax + tt_stack_frame]
      0x89, 0x78, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov [eax + tt_stack_frame], edi
      0x8B, 0x15                                                            // mov edx, [CORE_GC_TABLE + gc_signal]
   };

   if (!write(thread, sizeof(thread))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.signal, 4))
      return RuntimeCoreError::WriteFailed;

   static const unsigned char check[] = {
      0x85, 0xD2 // test edx, edx
   };

   if (!write(check, sizeof(check)) || !branch(0x04, noCollection))
      return RuntimeCoreError::WriteFailed;

   const unsigned char enterSafeRegion[] = {
      0xFF, 0x70, (unsigned char)runtime.dataLayout.threadContent.flags, // push dword [eax + tt_flags]
      0xC7, 0x40, (unsigned char)runtime.dataLayout.threadContent.flags,
      0x01, 0x00, 0x00, 0x00                                           // mov dword [eax + tt_flags], 1
   };

   if (!write(enterSafeRegion, sizeof(enterSafeRegion)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char callSignal[] = {
         0x89, 0xE7,       // mov edi, esp
         0x83, 0xE4, 0xF0, // and esp, -16
         0x83, 0xEC, 0x0C, // sub esp, 12
         0x56,             // push esi; System V x86 argument 1
         0xFF, 0x15        // call dword [absolute32]
      };
      if (!write(callSignal, sizeof(callSignal)))
         return RuntimeCoreError::WriteFailed;
   }
   else if (externalABI.platformABI == PlatformABI::WindowsX86) {
      static const unsigned char callSignal[] = {
         0x56,       // push esi; Windows x86 argument 1
         0xFF, 0x15  // call dword [absolute32]
      };

      if (!write(callSignal, sizeof(callSignal)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      return RuntimeCoreError::InvalidABI;
   }

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
      RuntimeCoreSymbol::SignalStop, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char restore[] = {
         0x89, 0xFC // mov esp, edi
      };

      if (!write(restore, sizeof(restore)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char release[] = {
         0x83, 0xC4, 0x04 // add esp, 4
      };

      if (!write(release, sizeof(release)))
         return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char unlock[] = {
      0xBF // mov edi, CORE_GC_TABLE + gc_lock
   };
   if (!write(unlock, sizeof(unlock))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char releaseLock[] = {
      0xBB, 0xFF, 0xFF, 0xFF, 0xFF, // mov ebx, -1
      0xF0, 0x0F, 0xC1, 0x1F        // lock xadd dword [edi], ebx
   };

   if (!write(releaseLock, sizeof(releaseLock)))
      return RuntimeCoreError::WriteFailed;

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char callWait[] = {
         0x89, 0xE6,       // mov esi, esp
         0x83, 0xE4, 0xF0, // and esp, -16
         0xFF, 0x15        // call dword [absolute32]
      };
      if (!write(callWait, sizeof(callWait)))
         return RuntimeCoreError::WriteFailed;
   }
   else {
      static const unsigned char callWait[] = {
         0xFF, 0x15 // call dword [absolute32]
      };

      if (!write(callWait, sizeof(callWait)))
         return RuntimeCoreError::WriteFailed;
   }

   if (!addRelocation(RuntimeCoreRelocationKind::ExternalAbsolute32,
      RuntimeCoreSymbol::WaitForCollection, 0, 4))
   {
      return RuntimeCoreError::WriteFailed;
   }

   if (externalABI.platformABI == PlatformABI::SystemVX86) {
      static const unsigned char restore[] = {
         0x89, 0xF4 // mov esp, esi
      };

      if (!write(restore, sizeof(restore)))
         return RuntimeCoreError::WriteFailed;
   }

   static const unsigned char restoreFlags[] = {
      0x5A // pop edx; previous thread flags
   };

   if (!write(restoreFlags, sizeof(restoreFlags)))
      return RuntimeCoreError::WriteFailed;

   RuntimeCoreError currentThreadAfterWait = encodeCurrentThreadX86(runtime);
   if (currentThreadAfterWait != RuntimeCoreError::None)
      return currentThreadAfterWait;

   const unsigned char restoreFrame[] = {
      0x89, 0x50, (unsigned char)runtime.dataLayout.threadContent.flags,      // mov [eax + tt_flags], edx
      0x5A,                                                                  // pop edx; previous frame
      0x89, 0x50, (unsigned char)runtime.dataLayout.threadContent.stackFrame // mov [eax + tt_stack_frame], edx
   };
   static const unsigned char end[] = {
      0x5A,             // pop edx
      0x58,             // pop eax
      0x83, 0xC4, 0x04, // add esp, 4; discard saved ebp slot
      0x5E,             // pop esi
      0x5B,             // pop ebx
      0xC3              // ret
   };

   if (!write(restoreFrame, sizeof(restoreFrame))
      || !write(end, sizeof(end))
      || !bind(noCollection))
      return RuntimeCoreError::WriteFailed;

   const unsigned char noCollectionFrame[] = {
      0x5A,                                                                  // pop edx; previous frame
      0x89, 0x50, (unsigned char)runtime.dataLayout.threadContent.stackFrame, // mov [eax + tt_stack_frame], edx
      0xBF                                                                   // mov edi, CORE_GC_TABLE + gc_lock
   };
   if (!write(noCollectionFrame, sizeof(noCollectionFrame))
      || !addRelocation(RuntimeCoreRelocationKind::Absolute32,
         RuntimeCoreSymbol::GCData, runtime.dataLayout.gc.lock, 4)
      || !write(releaseLock, sizeof(releaseLock))
      || !write(end, sizeof(end)))
   {
      return RuntimeCoreError::WriteFailed;
   }

   return fixLabels();
}
