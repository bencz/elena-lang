#include "runtime.h"
#include "eir.h"
#include "ecode.h"
#include "x86/encoder.h"
#include "x86/lowering.h"
#include "x86/runtimecore.h"

using namespace elena_lang;
using namespace elena_lang::codegen;

class TestMemory : public MemoryBase
{
   char  _buffer[4096];
   pos_t _length;

public:
   pos_t length() const override
   {
      return _length;
   }

   void* get(pos_t position) const override
   {
      return position < _length ? (void*)(_buffer + position) : nullptr;
   }

   bool write(pos_t position, const void* source, pos_t length) override
   {
      if (position > _length || length > sizeof(_buffer) - position)
         return false;

      memcpy(_buffer + position, source, length);
      if (position + length > _length)
         _length = position + length;

      return true;
   }

   bool insert(pos_t position, const void* source, pos_t length) override
   {
      if (position > _length || length > sizeof(_buffer) - _length)
         return false;

      memmove(_buffer + position + length, _buffer + position, _length - position);
      if (source)
         memcpy(_buffer + position, source, length);
      else memset(_buffer + position, 0, length);

      _length += length;

      return true;
   }

   bool read(pos_t position, void* target, pos_t length) const override
   {
      if (position > _length || length > _length - position)
         return false;

      memcpy(target, _buffer + position, length);

      return true;
   }

   void trim(pos_t position) override
   {
      if (position < _length)
         _length = position;
   }

   TestMemory()
      : _buffer {}, _length(0)
   {
   }
};

static bool beginProcedure(TestMemory& memory, pos_t offset)
{
   char bytes[32] = {};

   return offset + sizeof(pos_t) <= sizeof(bytes)
      && memory.write(0, bytes, offset + sizeof(pos_t));
}

static bool endProcedure(TestMemory& memory, pos_t offset)
{
   pos_t bodyLength = memory.length() - offset - sizeof(pos_t);

   return memory.write(offset, &bodyLength, sizeof(bodyLength));
}

static bool writeCommand(TestMemory& memory, ByteCode code, arg_t arg1 = 0, arg_t arg2 = 0)
{
   ECodeInfo info = {};
   if (!ECodeProvider::get(code, info))
      return false;

   unsigned char rawCode = (unsigned char)code;
   if (!memory.write(memory.length(), &rawCode, sizeof(rawCode)))
      return false;
   if (info.operandCount > 0
      && !memory.write(memory.length(), &arg1, sizeof(arg1)))
   {
      return false;
   }
   if (info.operandCount > 1
      && !memory.write(memory.length(), &arg2, sizeof(arg2)))
   {
      return false;
   }

   return true;
}

static bool testTargets()
{
   int count = 0;
   for (int i = (int)TargetPlatform::None + 1; i < (int)TargetPlatform::Count; i++) {
      TargetSpec target = {};
      if (!TargetProvider::get((TargetPlatform)i, target) || !target.isValid())
         return false;

      if (target.is64Bit() != (target.pointerSize == 8))
         return false;

      count++;
   }

   return count == 9;
}

static bool testInvalidTarget()
{
   TargetSpec target = {};

   return !TargetProvider::get(TargetPlatform::None, target) && !target.isValid();
}

static bool testRuntime()
{
   for (int i = (int)TargetPlatform::None + 1; i < (int)TargetPlatform::Count; i++) {
      TargetSpec target = {};
      if (!TargetProvider::get((TargetPlatform)i, target))
         return false;

      RuntimeSpec staLegacy = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
      RuntimeSpec mtaLegacy = RuntimeProvider::legacy(ThreadingMode::MultiThread, target);

      if (!staLegacy.isValid(target) || !mtaLegacy.isValid(target))
         return false;

      if (staLegacy.dataLayout.gc.permanentCurrent
            != RuntimeLayout::offsetOf(target.pointerSize,
               GCDataField::PermanentCurrent)
         || staLegacy.dataLayout.gc.lock
            != RuntimeLayout::offsetOf(target.pointerSize, GCDataField::Lock)
         || staLegacy.dataLayout.gc.signal
            != RuntimeLayout::offsetOf(target.pointerSize, GCDataField::Signal)
         || staLegacy.dataLayout.gc.queueSemaphore
            != RuntimeLayout::offsetOf(
               target.pointerSize,
               GCDataField::QueueSemaphore)
         || staLegacy.dataLayout.threadContent.stackFrame
            != RuntimeLayout::offsetOf(target.pointerSize,
               ThreadContentField::StackFrame)
         || staLegacy.dataLayout.threadTable.slotSize
            != RuntimeLayout::offsetOf(target.pointerSize,
               ThreadSlotField::SlotEnd)
         || staLegacy.dataLayout.environment.threadCount
            != RuntimeLayout::offsetOf(target.pointerSize,
               SystemEnvironmentField::ThreadCount))
      {
         return false;
      }

      if (staLegacy.vmtLayout.sizeOffset
            != RuntimeLayout::offsetOf(target.pointerSize,
               VMTHeaderField::Size)
         || staLegacy.vmtLayout.flagsOffset
            != RuntimeLayout::offsetOf(target.pointerSize,
               VMTHeaderField::Flags)
         || staLegacy.vmtLayout.parentOffset
            != RuntimeLayout::offsetOf(target.pointerSize,
               VMTHeaderField::Parent)
         || staLegacy.vmtLayout.methodOffset
            != RuntimeLayout::offsetOf(target.pointerSize,
               VMTTableField::FirstMethod)
         || staLegacy.vmtLayout.entrySize
            != RuntimeLayout::offsetOf(target.pointerSize,
               VMTTableField::EntryEnd))
      {
         return false;
      }

      unsigned int firstMessageEntry = RuntimeLayout::entryOffset(
         target.pointerSize, 1, MessageTableField::Action);
      unsigned int messagePayload = RuntimeLayout::entryOffset(
         target.pointerSize, 1, MessageTableField::Payload);
      if (firstMessageEntry != RuntimeLayout::offsetOf(target.pointerSize,
            MessageTableField::EntryEnd)
         || messagePayload != firstMessageEntry
            + RuntimeLayout::offsetOf(target.pointerSize,
               MessageTableField::Payload))
      {
         return false;
      }

      if (mtaLegacy.safepointStrategy != SafepointStrategy::Cooperative)
         return false;

      unsigned int payloadSize = 0;
      unsigned int allocationSize = 0;
      if (!staLegacy.objectLayout.payloadSize(3, payloadSize)
         || !staLegacy.objectLayout.allocationSize(3, allocationSize)
         || payloadSize != target.pointerSize * 3
         || allocationSize != (target.pointerSize == 4 ? 32u : 64u))
      {
         return false;
      }

      RuntimeCallSpec staAllocate = {};
      RuntimeCallSpec mtaAllocate = {};
      RuntimeCallSpec staAllocatePermanent = {};
      RuntimeCallSpec mtaAllocatePermanent = {};
      if (!RuntimeCallProvider::get(RuntimeOperation::AllocateYoung,
            staLegacy, staAllocate)
         || !RuntimeCallProvider::get(RuntimeOperation::AllocateYoung,
            mtaLegacy, mtaAllocate)
         || !RuntimeCallProvider::get(RuntimeOperation::AllocatePermanent,
            staLegacy, staAllocatePermanent)
         || !RuntimeCallProvider::get(RuntimeOperation::AllocatePermanent,
            mtaLegacy, mtaAllocatePermanent)
         || test(staAllocate.effects, RuntimeCallEffect::Synchronize)
         || !test(mtaAllocate.effects, RuntimeCallEffect::Synchronize
            | RuntimeCallEffect::ReadTLS)
         || test(staAllocatePermanent.effects, RuntimeCallEffect::Synchronize)
         || !test(mtaAllocatePermanent.effects, RuntimeCallEffect::Synchronize
            | RuntimeCallEffect::ReadTLS)
         || !test(mtaAllocate.effects, RuntimeCallEffect::RelocateRoots))
      {
         return false;
      }
   }

   return true;
}

static bool testTargetValidation()
{
   TargetSpec target = {};
   if (!TargetProvider::get(TargetPlatform::WindowsAMD64, target))
      return false;

   target.externalABI.shadowSpace = 0;
   if (target.isValid())
      return false;

   if (!TargetProvider::get(TargetPlatform::LinuxAMD64, target))
      return false;

   target.operatingSystem = OperatingSystem::Windows;

   return !target.isValid();
}

static bool testRuntimeValidation()
{
   TargetSpec target = {};
   if (!TargetProvider::get(TargetPlatform::LinuxAMD64, target))
      return false;

   RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::MultiThread, target);
   runtime.safepointStrategy = SafepointStrategy::AllocationOnly;
   if (runtime.isValid(target))
      return false;

   runtime = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
   runtime.writeBarrierMode = WriteBarrierMode::None;

   if (runtime.isValid(target))
      return false;

   runtime = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
   runtime.dataLayout.gc.permanentCurrent++;

   return !runtime.isValid(target);
}

static bool contains(const TestMemory& memory, unsigned char first,
   unsigned char second)
{
   for (pos_t i = 0; i + 1 < memory.length(); i++) {
      unsigned char bytes[2] = {};
      if (!memory.read(i, bytes, sizeof(bytes)))
         return false;
      if (bytes[0] == first && bytes[1] == second)
         return true;
   }

   return false;
}

static bool contains(const TestMemory& memory, const unsigned char* bytes,
   pos_t length)
{
   if (length == 0 || length > memory.length())
      return false;

   for (pos_t i = 0; i + length <= memory.length(); i++) {
      bool equal = true;
      for (pos_t j = 0; j < length; j++) {
         unsigned char value = 0;
         if (!memory.read(i + j, &value, 1) || value != bytes[j]) {
            equal = false;
            break;
         }
      }
      if (equal)
         return true;
   }

   return false;
}

static bool equals(const TestMemory& memory, const unsigned char* expected,
   pos_t length)
{
   if (memory.length() != length)
      return false;

   unsigned char actual[256] = {};
   if (length > sizeof(actual) || !memory.read(0, actual, length))
      return false;

   for (pos_t i = 0; i < length; i++) {
      if (actual[i] != expected[i])
         return false;
   }

   return true;
}

static bool testRuntimeCore(TargetPlatform platform, ThreadingMode threadingMode)
{
   TargetSpec target = {};
   x86::ManagedABI managedABI = {};
   x86::RuntimeCallABI callABI = {};
   if (!TargetProvider::get(platform, target)
      || !x86::ManagedABIProvider::get(target.architecture, managedABI))
   {
      return false;
   }

   RuntimeSpec runtime = RuntimeProvider::legacy(threadingMode, target);
   if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
      runtime, managedABI, callABI))
   {
      return false;
   }

   RuntimeSpec invalidRuntime = runtime;
   invalidRuntime.objectLayout.fieldSize++;
   TestMemory invalidMemory;
   MemoryWriter invalidWriter(&invalidMemory);
   x86::RuntimeCoreEncoder invalidEncoder(target, invalidWriter);
   if (invalidEncoder.encode(RuntimeOperation::AllocateYoung, invalidRuntime,
      managedABI, callABI) != RuntimeCoreError::InvalidRuntime
      || invalidMemory.length() != 0)
   {
      return false;
   }

   TestMemory memory;
   MemoryWriter writer(&memory);
   x86::RuntimeCoreEncoder encoder(target, writer);
   if (encoder.encode(RuntimeOperation::AllocateYoung, runtime,
      managedABI, callABI) != RuntimeCoreError::None)
   {
      return false;
   }

   pos_t expectedRelocations = target.architecture == Architecture::X86
      ? (threadingMode == ThreadingMode::SingleThread ? 4 : 5) : 2;
   if (encoder.relocationCount() != expectedRelocations
      || memory.length() == 0 || !contains(memory, 0x0F, 0x82)
      || !contains(memory, 0x0F, 0x83))
   {
      return false;
   }

   RuntimeCoreRelocation& call = encoder.relocation(expectedRelocations - 1);
   if (call.kind != RuntimeCoreRelocationKind::Relative32
      || call.symbol != RuntimeCoreSymbol::CollectYoung)
   {
      return false;
   }

   RuntimeCoreRelocation& table = encoder.relocation(0);
   return table.symbol == RuntimeCoreSymbol::GCData
      && table.kind == (target.architecture == Architecture::X86
         ? RuntimeCoreRelocationKind::Absolute32
         : RuntimeCoreRelocationKind::Absolute64);
}

static bool testPermanentRuntimeCore(TargetPlatform platform)
{
   TargetSpec target = {};
   x86::ManagedABI managedABI = {};
   x86::RuntimeCallABI callABI = {};
   if (!TargetProvider::get(platform, target)
      || !x86::ManagedABIProvider::get(target.architecture, managedABI))
   {
      return false;
   }

   RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::SingleThread,
      target);
   if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocatePermanent,
      runtime, managedABI, callABI))
   {
      return false;
   }

   RuntimeSpec mtaRuntime = RuntimeProvider::legacy(ThreadingMode::MultiThread,
      target);
   x86::RuntimeCallABI mtaCallABI = {};
   TestMemory mtaMemory;
   MemoryWriter mtaWriter(&mtaMemory);
   x86::RuntimeCoreEncoder mtaEncoder(target, mtaWriter);
   if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocatePermanent,
      mtaRuntime, managedABI, mtaCallABI))
      return false;

   RuntimeCoreError mtaError = mtaEncoder.encode(
      RuntimeOperation::AllocatePermanent, mtaRuntime, managedABI, mtaCallABI);
   if (target.tlsModel == TLSModel::MachO) {
      if (mtaError != RuntimeCoreError::InvalidABI || mtaMemory.length() != 0)
         return false;
   }
   else {
      pos_t expectedMTARelocations = target.architecture == Architecture::X86
         ? 5 : 3;
      if (mtaError != RuntimeCoreError::None || mtaMemory.length() == 0
         || mtaEncoder.relocationCount() != expectedMTARelocations
         || mtaEncoder.relocation(0).symbol != RuntimeCoreSymbol::GCData
         || mtaEncoder.relocation(expectedMTARelocations - 1).symbol
            != RuntimeCoreSymbol::CollectPermanent)
      {
         return false;
      }
   }

   TestMemory memory;
   MemoryWriter writer(&memory);
   x86::RuntimeCoreEncoder encoder(target, writer);
   if (encoder.encode(RuntimeOperation::AllocatePermanent, runtime,
      managedABI, callABI) != RuntimeCoreError::None)
   {
      return false;
   }

   pos_t expectedRelocations = target.architecture == Architecture::X86 ? 5 : 3;
   if (encoder.relocationCount() != expectedRelocations
      || memory.length() == 0 || !contains(memory, 0x0F, 0x83))
   {
      return false;
   }

   RuntimeCoreRelocation& frame = encoder.relocation(expectedRelocations - 2);
   RuntimeCoreRelocation& call = encoder.relocation(expectedRelocations - 1);
   if (frame.symbol != RuntimeCoreSymbol::SingleContent
      || call.symbol != RuntimeCoreSymbol::CollectPermanent)
   {
      return false;
   }

   if (target.architecture == Architecture::X86) {
      bool systemVStack = contains(memory, 0x89, 0xE6)
         && contains(memory, 0x83, 0xE4);

      return frame.kind == RuntimeCoreRelocationKind::Absolute32
         && call.kind == RuntimeCoreRelocationKind::ExternalAbsolute32
         && encoder.relocation(0).addend == runtime.dataLayout.gc.permanentCurrent
         && encoder.relocation(1).addend == runtime.dataLayout.gc.permanentEnd
         && encoder.relocation(2).addend == runtime.dataLayout.gc.permanentCurrent
         && systemVStack == (target.abi == PlatformABI::SystemVX86);
   }

   const unsigned char loadCurrent[] = {
      0x49, 0x8B, 0x44, 0x24,
      (unsigned char)runtime.dataLayout.gc.permanentCurrent
   };
   const unsigned char compareEnd[] = {
      0x49, 0x3B, 0x4C, 0x24,
      (unsigned char)runtime.dataLayout.gc.permanentEnd
   };
   bool systemVArgument = contains(memory, 0x48, 0x89)
      && contains(memory, 0x89, 0xCF);
   bool windowsShadow = contains(memory, 0x83, 0xEC);

   return frame.kind == RuntimeCoreRelocationKind::Absolute64
      && call.kind == RuntimeCoreRelocationKind::ExternalRelative32
      && contains(memory, loadCurrent, sizeof(loadCurrent))
      && contains(memory, compareEnd, sizeof(compareEnd))
      && systemVArgument == (target.abi == PlatformABI::SystemVAMD64)
      && windowsShadow == (target.abi == PlatformABI::WindowsX64);
}

static bool testPrepareRuntimeCore(TargetPlatform platform)
{
   TargetSpec target = {};
   x86::ManagedABI managedABI = {};
   if (!TargetProvider::get(platform, target)
      || !x86::ManagedABIProvider::get(target.architecture, managedABI))
   {
      return false;
   }

   RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::SingleThread,
      target);
   x86::RuntimeABISet runtimeABIs;
   if (!runtimeABIs.initialize(runtime, managedABI))
      return false;

   const x86::RuntimeCallABI* callABI = runtimeABIs.get(
      RuntimeOperation::Prepare);
   if (!callABI || callABI->argument0 != x86::Register::A
      || callABI->argument1 != x86::Register::None
      || callABI->result != x86::Register::None)
   {
      return false;
   }

   TestMemory memory;
   MemoryWriter writer(&memory);
   x86::RuntimeCoreEncoder encoder(target, writer);
   if (encoder.encode(RuntimeOperation::Prepare, runtime, managedABI,
      *callABI) != RuntimeCoreError::None)
   {
      return false;
   }

   if (target.operatingSystem == OperatingSystem::Windows) {
      const unsigned char expected[] = { 0xC3 };

      return encoder.relocationCount() == 0
         && equals(memory, expected, sizeof(expected));
   }

   if (encoder.relocationCount() != 1
      || encoder.relocation(0).symbol != RuntimeCoreSymbol::PrepareRuntime)
   {
      return false;
   }

   return encoder.relocation(0).kind
      == (target.architecture == Architecture::X86
         ? RuntimeCoreRelocationKind::ExternalAbsolute32
         : RuntimeCoreRelocationKind::ExternalRelative32);
}

static bool testCollectRuntimeCore(TargetPlatform platform)
{
   TargetSpec target = {};
   x86::ManagedABI managedABI = {};
   if (!TargetProvider::get(platform, target)
      || !x86::ManagedABIProvider::get(target.architecture, managedABI))
   {
      return false;
   }

   RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::SingleThread,
      target);
   x86::RuntimeABISet runtimeABIs;
   if (!runtimeABIs.initialize(runtime, managedABI))
      return false;

   const x86::RuntimeCallABI* callABI = runtimeABIs.get(
      RuntimeOperation::Collect);
   if (!callABI)
      return false;

   TestMemory memory;
   MemoryWriter writer(&memory);
   x86::RuntimeCoreEncoder encoder(target, writer);
   pos_t expectedRelocations = target.architecture == Architecture::X86 ? 7 : 6;
   if (encoder.encode(RuntimeOperation::Collect, runtime, managedABI,
      *callABI) != RuntimeCoreError::None
      || encoder.relocationCount() != expectedRelocations)
   {
      return false;
   }

   pos_t singleContent = 0;
   pos_t systemEnvironment = 0;
   pos_t staticRoots = 0;
   pos_t gcData = 0;
   pos_t collector = 0;
   for (pos_t i = 0; i < encoder.relocationCount(); i++) {
      switch (encoder.relocation(i).symbol) {
         case RuntimeCoreSymbol::SingleContent:
            singleContent++;
            break;
         case RuntimeCoreSymbol::SystemEnvironment:
            systemEnvironment++;
            break;
         case RuntimeCoreSymbol::StaticRoots:
            staticRoots++;
            break;
         case RuntimeCoreSymbol::GCData:
            gcData++;
            break;
         case RuntimeCoreSymbol::CollectRuntime:
            collector++;
            break;
         default:
            return false;
      }
   }

   if (singleContent != 2 || systemEnvironment != 1 || staticRoots != 1
      || gcData != (target.architecture == Architecture::X86 ? 2u : 1u)
      || collector != 1)
   {
      return false;
   }

   RuntimeSpec mtaRuntime = RuntimeProvider::legacy(
      ThreadingMode::MultiThread, target);
   x86::RuntimeCallABI mtaCallABI = {};
   TestMemory mtaMemory;
   MemoryWriter mtaWriter(&mtaMemory);
   x86::RuntimeCoreEncoder mtaEncoder(target, mtaWriter);
   if (!x86::RuntimeABIProvider::get(RuntimeOperation::Collect,
      mtaRuntime, managedABI, mtaCallABI))
   {
      return false;
   }

   RuntimeCoreError mtaError = mtaEncoder.encode(RuntimeOperation::Collect,
      mtaRuntime, managedABI, mtaCallABI);
   if (target.architecture == Architecture::X86) {
      if (mtaError != RuntimeCoreError::None
         || mtaEncoder.relocationCount() != 23 || mtaMemory.length() == 0)
      {
         return false;
      }
   }
   else {
      bool supported = target.tlsModel != TLSModel::MachO;
      if ((supported && (mtaError != RuntimeCoreError::None
            || mtaEncoder.relocationCount() != 22 || mtaMemory.length() == 0))
         || (!supported && (mtaError != RuntimeCoreError::InvalidABI
            || mtaMemory.length() != 0)))
      {
         return false;
      }
   }

   if (mtaError == RuntimeCoreError::None) {
      pos_t signalStop = 0;
      pos_t awaitCollection = 0;
      pos_t signalCollectionEnd = 0;

      for (pos_t i = 0; i < mtaEncoder.relocationCount(); i++) {
         RuntimeCoreSymbol symbol = mtaEncoder.relocation(i).symbol;
         signalStop += symbol == RuntimeCoreSymbol::SignalStop ? 1 : 0;
         awaitCollection += symbol == RuntimeCoreSymbol::WaitForCollection ? 1 : 0;
         signalCollectionEnd += symbol == RuntimeCoreSymbol::SignalCollectionEnd
            ? 1 : 0;
      }

      if (signalStop != 1 || awaitCollection != 1
         || signalCollectionEnd != 1)
      {
         return false;
      }
   }

   RuntimeCoreRelocation& call = encoder.relocation(expectedRelocations - 1);
   return call.kind == (target.architecture == Architecture::X86
      ? RuntimeCoreRelocationKind::ExternalAbsolute32
      : RuntimeCoreRelocationKind::ExternalRelative32);
}

static bool testRuntimeCoreProtocol()
{
   const TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64,
      TargetPlatform::LinuxARM64,
      TargetPlatform::LinuxPPC64le
   };
   unsigned int staMask = 0;
   unsigned int mtaMask = 0;

   for (unsigned int i = 0; i < sizeof(platforms) / sizeof(platforms[0]); i++) {
      TargetSpec target = {};
      if (!TargetProvider::get(platforms[i], target))
         return false;

      RuntimeSpec staRuntime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      RuntimeSpec mtaRuntime = RuntimeProvider::legacy(
         ThreadingMode::MultiThread, target);
      RuntimeCoreProtocol sta = {};
      RuntimeCoreProtocol mta = {};
      RuntimeCoreProtocol staPermanent = {};
      RuntimeCoreProtocol mtaPermanent = {};
      if (!RuntimeCoreProtocolProvider::get(RuntimeOperation::Collect,
            staRuntime, sta)
         || !RuntimeCoreProtocolProvider::get(RuntimeOperation::Collect,
            mtaRuntime, mta)
         || !RuntimeCoreProtocolProvider::get(
            RuntimeOperation::AllocatePermanent, staRuntime, staPermanent)
         || !RuntimeCoreProtocolProvider::get(
            RuntimeOperation::AllocatePermanent, mtaRuntime, mtaPermanent)
         || !sta.isValid(staRuntime) || !mta.isValid(mtaRuntime)
         || !staPermanent.isValid(staRuntime)
         || !mtaPermanent.isValid(mtaRuntime)
         || sta.count != 7 || mta.count != 20
         || staPermanent.count != 3 || mtaPermanent.count != 5
         || sta.contains(RuntimeCoreAction::EnumerateMutators)
         || sta.contains(RuntimeCoreAction::AppendTLSRoots)
         || !mta.contains(RuntimeCoreAction::EnumerateMutators)
         || !mta.contains(RuntimeCoreAction::AppendTLSRoots)
         || !mta.contains(RuntimeCoreAction::EnterSafeRegion)
         || !mta.contains(RuntimeCoreAction::AwaitCollection)
         || !mta.contains(RuntimeCoreAction::LeaveSafeRegion)
         || staPermanent.contains(RuntimeCoreAction::EnumerateMutators)
         || mtaPermanent.contains(RuntimeCoreAction::AppendTLSRoots)
         || mtaPermanent.contains(RuntimeCoreAction::EnumerateMutators)
         || !mtaPermanent.contains(RuntimeCoreAction::AcquireAllocationLock)
         || !mtaPermanent.contains(RuntimeCoreAction::ReleaseAllocationLock)
         || sta.steps[0].action != RuntimeCoreAction::PublishFrame
         || sta.steps[5].action != RuntimeCoreAction::InvokeCollector
         || sta.steps[6].action != RuntimeCoreAction::Return
         || mta.steps[8].action != RuntimeCoreAction::EnumerateMutators
         || mta.steps[14].action != RuntimeCoreAction::AppendTLSRoots
         || mta.steps[16].action != RuntimeCoreAction::InvokeCollector
         || mta.steps[19].action != RuntimeCoreAction::Return
         || !test(mta.steps[8].effects, RuntimeCallEffect::Synchronize)
         || !test(mta.steps[14].effects, RuntimeCallEffect::ReadTLS)
         || !test(mta.steps[16].effects, RuntimeCallEffect::RelocateRoots)
         || mtaPermanent.steps[2].action
            != RuntimeCoreAction::InvokeCollector
         || mtaPermanent.steps[4].action != RuntimeCoreAction::Return)
      {
         return false;
      }

      if (i == 0) {
         staMask = sta.actionMask;
         mtaMask = mta.actionMask;

         RuntimeCoreProtocol reordered = mta;
         RuntimeCoreStep step = reordered.steps[8];
         reordered.steps[8] = reordered.steps[9];
         reordered.steps[9] = step;
         if (reordered.isValid(mtaRuntime))
            return false;
      }
      else if (staMask != sta.actionMask || mtaMask != mta.actionMask) {
         return false;
      }
   }

   return staMask != mtaMask;
}

static bool testWaitRuntimeCore(TargetPlatform platform, bool supported)
{
   TargetSpec target = {};
   x86::ManagedABI managedABI = {};
   if (!TargetProvider::get(platform, target)
      || !x86::ManagedABIProvider::get(target.architecture, managedABI))
   {
      return false;
   }

   RuntimeSpec staRuntime = RuntimeProvider::legacy(
      ThreadingMode::SingleThread, target);
   x86::RuntimeABISet staRuntimeABIs;
   if (!staRuntimeABIs.initialize(staRuntime, managedABI))
      return false;

   const x86::RuntimeCallABI* staCallABI = staRuntimeABIs.get(
      RuntimeOperation::WaitForGC);
   TestMemory staMemory;
   MemoryWriter staWriter(&staMemory);
   x86::RuntimeCoreEncoder staEncoder(target, staWriter);
   if (!staCallABI || staEncoder.encode(RuntimeOperation::WaitForGC,
      staRuntime, managedABI, *staCallABI) != RuntimeCoreError::None)
   {
      return false;
   }

   const unsigned char staWait[] = { 0xC3 };
   if (staEncoder.relocationCount() != 0
      || !equals(staMemory, staWait, sizeof(staWait)))
   {
      return false;
   }

   RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::MultiThread,
      target);
   x86::RuntimeABISet runtimeABIs;
   if (!runtimeABIs.initialize(runtime, managedABI))
      return false;

   const x86::RuntimeCallABI* callABI = runtimeABIs.get(
      RuntimeOperation::WaitForGC);
   if (!callABI || callABI->argument0 != x86::Register::D
      || callABI->result != x86::Register::None)
   {
      return false;
   }

   TestMemory memory;
   MemoryWriter writer(&memory);
   x86::RuntimeCoreEncoder encoder(target, writer);
   RuntimeCoreError error = encoder.encode(RuntimeOperation::WaitForGC,
      runtime, managedABI, *callABI);
   if (!supported)
      return error == RuntimeCoreError::InvalidABI;

   if (error != RuntimeCoreError::None || encoder.relocationCount() != 6)
      return false;

   pos_t gcRelocations = 0;
   pos_t signalRelocations = 0;
   pos_t waitRelocations = 0;
   for (pos_t i = 0; i < encoder.relocationCount(); i++) {
      RuntimeCoreRelocation& relocation = encoder.relocation(i);
      if (relocation.symbol == RuntimeCoreSymbol::GCData) {
         gcRelocations++;
      }
      else if (relocation.symbol == RuntimeCoreSymbol::SignalStop) {
         signalRelocations++;
      }
      else if (relocation.symbol == RuntimeCoreSymbol::WaitForCollection) {
         waitRelocations++;
      }
   }
   if (gcRelocations != 4 || signalRelocations != 1 || waitRelocations != 1
      || encoder.relocation(0).addend != runtime.dataLayout.gc.lock
      || encoder.relocation(1).addend != runtime.dataLayout.gc.signal)
   {
      return false;
   }

   if (target.architecture == Architecture::X86) {
      return encoder.relocation(0).kind == RuntimeCoreRelocationKind::Absolute32
         && encoder.relocation(2).kind
            == RuntimeCoreRelocationKind::ExternalAbsolute32
         && encoder.relocation(4).kind
            == RuntimeCoreRelocationKind::ExternalAbsolute32
         && contains(memory,
            target.tlsModel == TLSModel::Windows ? 0x64 : 0x65, 0xA1);
   }

   return encoder.relocation(0).kind == RuntimeCoreRelocationKind::Absolute64
      && encoder.relocation(2).kind
         == RuntimeCoreRelocationKind::ExternalRelative32
      && encoder.relocation(4).kind
         == RuntimeCoreRelocationKind::ExternalRelative32
      && contains(memory,
         target.tlsModel == TLSModel::Windows ? 0x65 : 0x64, 0x48);
}

static bool testExceptionRuntimeCore(
   TargetPlatform platform,
   ThreadingMode threadingMode,
   bool supported)
{
   TargetSpec target = {};
   x86::ManagedABI managedABI = {};
   if (!TargetProvider::get(platform, target)
      || !x86::ManagedABIProvider::get(target.architecture, managedABI))
   {
      return false;
   }

   RuntimeSpec runtime = RuntimeProvider::legacy(threadingMode, target);
   TestMemory memory;
   MemoryWriter writer(&memory);
   x86::RuntimeCoreEncoder encoder(target, writer);

   // VEH_HANDLER / core reference 0x10003: OS fault context to ELENA handler.
   RuntimeCoreError error = encoder.encode(
      RuntimeCoreEntry::ExceptionDispatcher,
      runtime,
      managedABI);

   if (!supported)
      return error == RuntimeCoreError::InvalidABI;

   if (error != RuntimeCoreError::None)
      return false;

   const unsigned char x86Prefix[] = {
      0x89, 0xD6, // mov esi, edx; managed cached argument 0
      0x89, 0xC2  // mov edx, eax; ELENA exception code
   };
   const unsigned char amd64Prefix[] = {
      0x49, 0x89, 0xD2, // mov r10, rdx; managed cached argument 0
      0x48, 0x89, 0xC2  // mov rdx, rax; ELENA exception code
   };
   const unsigned char dispatch[] = {
      0xFF, 0x20 // jmp [current_thread + eh_critical]
   };

   const unsigned char* prefix = target.architecture == Architecture::X86
      ? x86Prefix
      : amd64Prefix;
   pos_t prefixLength = target.architecture == Architecture::X86
      ? sizeof(x86Prefix)
      : sizeof(amd64Prefix);

   unsigned char actualPrefix[sizeof(amd64Prefix)] = {};
   unsigned char actualDispatch[sizeof(dispatch)] = {};
   if (memory.length() < prefixLength + sizeof(dispatch)
      || !memory.read(0, actualPrefix, prefixLength)
      || !memory.read(
         memory.length() - sizeof(dispatch),
         actualDispatch,
         sizeof(actualDispatch))
      || memcmp(actualPrefix, prefix, prefixLength) != 0
      || memcmp(actualDispatch, dispatch, sizeof(dispatch)) != 0)
   {
      return false;
   }

   if (threadingMode == ThreadingMode::MultiThread) {
      if (target.architecture == Architecture::X86) {
         unsigned char segment = target.tlsModel == TLSModel::Windows
            ? 0x64
            : 0x65;

         return encoder.relocationCount() == 0
            && contains(memory, segment, 0xA1);
      }

      unsigned char segment = target.tlsModel == TLSModel::Windows
         ? 0x65
         : 0x64;

      return encoder.relocationCount() == 0
         && contains(memory, segment, 0x48);
   }

   if (encoder.relocationCount() != 1)
      return false;

   RuntimeCoreRelocation& content = encoder.relocation(0);
   RuntimeCoreRelocationKind expectedKind = target.architecture
      == Architecture::X86
         ? RuntimeCoreRelocationKind::Absolute32
         : RuntimeCoreRelocationKind::Absolute64;

   return content.symbol == RuntimeCoreSymbol::SingleContent
      && content.kind == expectedKind
      && content.addend == 0;
}

static bool testX86ABIs()
{
   for (int i = (int)TargetPlatform::None + 1; i < (int)TargetPlatform::Count; i++) {
      TargetSpec target = {};
      if (!TargetProvider::get((TargetPlatform)i, target))
         return false;

      if (target.architecture != Architecture::X86
         && target.architecture != Architecture::AMD64)
      {
         continue;
      }

      x86::ManagedABI managedABI = {};
      x86::ExternalABI externalABI = {};
      if (!x86::ManagedABIProvider::get(target.architecture, managedABI)
         || !x86::ExternalABIProvider::get(target, externalABI))
      {
         return false;
      }

      RuntimeSpec sta = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
      RuntimeSpec mta = RuntimeProvider::legacy(ThreadingMode::MultiThread, target);
      x86::RuntimeCallABI staAllocate = {};
      x86::RuntimeCallABI mtaAllocate = {};
      x86::RuntimeCallABI staAllocatePermanent = {};
      x86::RuntimeCallABI collect = {};
      x86::RuntimeCallABI prepare = {};
      x86::RuntimeABISet runtimeABIs;
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
            sta, managedABI, staAllocate)
         || !x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
            mta, managedABI, mtaAllocate)
         || !x86::RuntimeABIProvider::get(RuntimeOperation::AllocatePermanent,
            sta, managedABI, staAllocatePermanent)
         || !x86::RuntimeABIProvider::get(RuntimeOperation::Collect,
            sta, managedABI, collect)
         || !x86::RuntimeABIProvider::get(RuntimeOperation::Prepare,
            sta, managedABI, prepare)
         || !runtimeABIs.initialize(mta, managedABI)
         || !runtimeABIs.get(RuntimeOperation::WaitForGC)
         || staAllocate.argument0 != x86::Register::C
         || staAllocate.result != x86::Register::B
         || staAllocatePermanent.argument0 != x86::Register::C
         || staAllocatePermanent.result != x86::Register::B
         || collect.argument0 != x86::Register::C
         || collect.argument1 != x86::Register::D
         || prepare.argument0 != x86::Register::A
         || managedABI.dataSource != x86::Register::SI
         || managedABI.dataDestination != x86::Register::DI
         || (target.architecture == Architecture::X86
            ? managedABI.scratch != x86::Register::None
            : managedABI.scratch != x86::Register::R8)
         || !test(mtaAllocate.effects, RuntimeCallEffect::Synchronize))
      {
         return false;
      }
   }

   return true;
}

static bool testEIR()
{
   EIRFunction function;
   function.addOperand({ EIROperandKind::Immediate, EIRType::Word, 32 });
   function.addInstruction({
      EIROpcode::Allocate,
      EIREffect::WriteHeap | EIREffect::Allocate | EIREffect::Safepoint,
      { 1, EIRType::Reference },
      0,
      1,
      0
   });
   function.addOperand({ EIROperandKind::Value, EIRType::Reference, 1 });
   function.addInstruction({
      EIROpcode::Return,
      EIREffect::Terminator,
      { INVALID_POS, EIRType::None },
      1,
      1,
      1
   });
   function.addBlock({ 0, 0, 2, 0 });

   return EIRVerifier::verify(function) == EIRVerifyError::None
      && function.instruction(0).result.isValid()
      && test(function.instruction(0).effects, EIREffect::WriteHeap)
      && test(function.instruction(0).effects, EIREffect::Allocate)
      && test(function.instruction(0).effects, EIREffect::Safepoint);
}

static bool testEIRPhi()
{
   EIRFunction function;

   function.addOperand({ EIROperandKind::Immediate, EIRType::Boolean, 1 });
   function.addInstruction({
      EIROpcode::Constant, EIREffect::None, { 1, EIRType::Boolean }, 0, 1, 0
   });
   function.addOperand({ EIROperandKind::Value, EIRType::Boolean, 1 });
   function.addOperand({ EIROperandKind::Block, EIRType::None, 1 });
   function.addOperand({ EIROperandKind::Block, EIRType::None, 2 });
   function.addInstruction({
      EIROpcode::ConditionalBranch, EIREffect::Terminator,
      { INVALID_POS, EIRType::None }, 1, 3, 1
   });
   function.addBlock({ 0, 0, 2, 0 });

   function.addOperand({ EIROperandKind::Immediate, EIRType::Int32, 10 });
   function.addInstruction({
      EIROpcode::Constant, EIREffect::None, { 2, EIRType::Int32 }, 4, 1, 2
   });
   function.addOperand({ EIROperandKind::Block, EIRType::None, 3 });
   function.addInstruction({
      EIROpcode::Branch, EIREffect::Terminator,
      { INVALID_POS, EIRType::None }, 5, 1, 3
   });
   function.addBlock({ 1, 2, 2, 2 });

   function.addOperand({ EIROperandKind::Immediate, EIRType::Int32, 20 });
   function.addInstruction({
      EIROpcode::Constant, EIREffect::None, { 3, EIRType::Int32 }, 6, 1, 4
   });
   function.addOperand({ EIROperandKind::Block, EIRType::None, 3 });
   function.addInstruction({
      EIROpcode::Branch, EIREffect::Terminator,
      { INVALID_POS, EIRType::None }, 7, 1, 5
   });
   function.addBlock({ 2, 4, 2, 4 });

   function.addOperand({ EIROperandKind::Block, EIRType::None, 1 });
   function.addOperand({ EIROperandKind::Value, EIRType::Int32, 2 });
   function.addOperand({ EIROperandKind::Block, EIRType::None, 2 });
   function.addOperand({ EIROperandKind::Value, EIRType::Int32, 3 });
   function.addInstruction({
      EIROpcode::Phi, EIREffect::None, { 4, EIRType::Int32 }, 8, 4, 6
   });
   function.addOperand({ EIROperandKind::Value, EIRType::Int32, 4 });
   function.addInstruction({
      EIROpcode::Return, EIREffect::Terminator,
      { INVALID_POS, EIRType::None }, 12, 1, 7
   });
   function.addBlock({ 3, 6, 2, 6 });

   return EIRVerifier::verify(function) == EIRVerifyError::None;
}

static bool testEIRVerificationErrors()
{
   EIRFunction missingTerminator;
   missingTerminator.addOperand({ EIROperandKind::Immediate, EIRType::Int32, 1 });
   missingTerminator.addInstruction({
      EIROpcode::Constant, EIREffect::None, { 1, EIRType::Int32 }, 0, 1, 0
   });
   missingTerminator.addBlock({ 0, 0, 1, 0 });

   EIRFunction undefinedValue;
   undefinedValue.addOperand({ EIROperandKind::Value, EIRType::Int32, 7 });
   undefinedValue.addInstruction({
      EIROpcode::Return, EIREffect::Terminator,
      { INVALID_POS, EIRType::None }, 0, 1, 0
   });
   undefinedValue.addBlock({ 0, 0, 1, 0 });

   return EIRVerifier::verify(missingTerminator) == EIRVerifyError::MissingTerminator
      && EIRVerifier::verify(undefinedValue) == EIRVerifyError::UndefinedValue;
}

static bool testEIRSwitch()
{
   EIRFunction function;
   function.addOperand({ EIROperandKind::Immediate, EIRType::Int32, 1 });
   function.addInstruction({
      EIROpcode::Constant, EIREffect::None, { 1, EIRType::Int32 }, 0, 1, 0
   });
   function.addOperand({ EIROperandKind::Value, EIRType::Int32, 1 });
   function.addOperand({ EIROperandKind::Block, EIRType::None, 1 });
   function.addOperand({ EIROperandKind::Immediate, EIRType::Int32, 1 });
   function.addOperand({ EIROperandKind::Block, EIRType::None, 1 });
   function.addInstruction({
      EIROpcode::Switch, EIREffect::Terminator,
      { INVALID_POS, EIRType::None }, 1, 4, 1
   });
   function.addBlock({ 0, 0, 2, 0 });
   function.addInstruction({
      EIROpcode::Return, EIREffect::Terminator,
      { INVALID_POS, EIRType::None }, 5, 0, 2
   });
   function.addBlock({ 1, 2, 1, 2 });

   return EIRVerifier::verify(function) == EIRVerifyError::None;
}

static bool testX86Encoder()
{
   TargetSpec x86Target = {};
   x86::ManagedABI x86ABI = {};
   if (!TargetProvider::get(TargetPlatform::LinuxX86, x86Target)
      || !x86::ManagedABIProvider::get(Architecture::X86, x86ABI))
   {
      return false;
   }
   RuntimeSpec x86Runtime = RuntimeProvider::legacy(
      ThreadingMode::SingleThread, x86Target);
   x86::RuntimeCallABI x86RuntimeABI = {};
   if (!x86::RuntimeABIProvider::get(
      RuntimeOperation::AllocateYoung, x86Runtime, x86ABI, x86RuntimeABI))
   {
      return false;
   }

   TestMemory x86Code;
   MemoryWriter x86Writer(&x86Code);
   x86::Encoder x86Encoder(Architecture::X86, x86Writer);
   ByteCode migrated[] = {
      ByteCode::ConvL, ByteCode::LNeg, ByteCode::Coalesce,
      ByteCode::Not, ByteCode::Neg, ByteCode::XPeekEq,
      ByteCode::Class, ByteCode::Save, ByteCode::Load, ByteCode::Len,
      ByteCode::BLoad, ByteCode::WLoad, ByteCode::MovFrm, ByteCode::MLen,
      ByteCode::XAssign, ByteCode::LLoad, ByteCode::XLoad, ByteCode::XLLoad,
      ByteCode::LSave, ByteCode::Parent, ByteCode::XGet,
      ByteCode::LoadZ, ByteCode::WLoadZ
   };
   for (size_t i = 0; i < sizeof(migrated) / sizeof(ByteCode); i++) {
      x86::Sequence sequence;
      x86::LowerError lowerError = x86::ECodeLowering::lower(
         ByteCommand(migrated[i]), x86Runtime, x86ABI, x86RuntimeABI, sequence);
      x86::EncodeError encodeError = lowerError == x86::LowerError::None
         ? x86Encoder.emit(sequence, x86ABI) : x86::EncodeError::None;
      if (lowerError != x86::LowerError::None
         || encodeError != x86::EncodeError::None)
      {
         return false;
      }
   }

   unsigned char expectedX86[] = {
      0x89, 0xD0, 0x99,
      0xF7, 0xD2, 0xF7, 0xD0, 0x83, 0xC0, 0x01, 0x83, 0xD2, 0x00,
      0x85, 0xDB, 0x0F, 0x44, 0xDE,
      0xF7, 0xD2, 0xF7, 0xDA,
      0x0F, 0x44, 0xDE,
      0x8B, 0x5B, 0xF8,
      0x89, 0x53, 0x00,
      0x8B, 0x53, 0x00,
      0xBA, 0xFF, 0xFF, 0x7F, 0x00,
      0x8B, 0x4B, 0xFC,
      0x21, 0xCA,
      0xC1, 0xEA, 0x02,
      0x0F, 0xB6, 0x53, 0x00,
      0x0F, 0xBF, 0x53, 0x00,
      0x89, 0xEA,
      0x81, 0xE2, 0x1F, 0x00, 0x00, 0x00,
      0x89, 0x34, 0x93,
      0x8B, 0x43, 0x00, 0x8B, 0x53, 0x04,
      0x8B, 0x14, 0x13,
      0x8B, 0x04, 0x13, 0x8B, 0x54, 0x13, 0x04,
      0x89, 0x53, 0x04, 0x89, 0x43, 0x00,
      0x8B, 0x5B, 0xF0,
      0x8B, 0x1C, 0x93,
      0x8B, 0x43, 0x00, 0xBA, 0x00, 0x00, 0x00, 0x00,
      0x0F, 0xB7, 0x43, 0x00, 0xBA, 0x00, 0x00, 0x00, 0x00
   };
   unsigned char actualX86[sizeof(expectedX86)] = {};
   if (x86Code.length() != sizeof(expectedX86)
      || !x86Code.read(0, actualX86, sizeof(actualX86)))
   {
      return false;
   }
   for (pos_t i = 0; i < sizeof(expectedX86); i++) {
      if (actualX86[i] != expectedX86[i])
         return false;
   }

   TestMemory amd64Code;
   MemoryWriter amd64Writer(&amd64Code);
   x86::Encoder amd64Encoder(Architecture::AMD64, amd64Writer);
   x86::ManagedABI amd64ABI = {};
   TargetSpec amd64Target = {};
   if (!TargetProvider::get(TargetPlatform::LinuxAMD64, amd64Target)
      || !x86::ManagedABIProvider::get(Architecture::AMD64, amd64ABI))
      return false;
   RuntimeSpec amd64Runtime = RuntimeProvider::legacy(
      ThreadingMode::SingleThread, amd64Target);
   x86::RuntimeCallABI amd64RuntimeABI = {};
   if (!x86::RuntimeABIProvider::get(
      RuntimeOperation::AllocateYoung,
      amd64Runtime,
      amd64ABI,
      amd64RuntimeABI))
   {
      return false;
   }

   TestMemory amd64MigratedCode;
   MemoryWriter amd64MigratedWriter(&amd64MigratedCode);
   x86::Encoder amd64MigratedEncoder(
      Architecture::AMD64, amd64MigratedWriter);
   for (size_t i = 0; i < sizeof(migrated) / sizeof(ByteCode); i++) {
      x86::Sequence sequence;
      x86::LowerError lowerError = x86::ECodeLowering::lower(
         ByteCommand(migrated[i]),
         amd64Runtime,
         amd64ABI,
         amd64RuntimeABI,
         sequence);
      x86::EncodeError encodeError = lowerError == x86::LowerError::None
         ? amd64MigratedEncoder.emit(sequence, amd64ABI) : x86::EncodeError::None;
      if (lowerError != x86::LowerError::None
         || encodeError != x86::EncodeError::None)
      {
         return false;
      }
   }
   unsigned char expectedAMD64Migrated[] = {
      0x48, 0x63, 0xD2,
      0x48, 0xF7, 0xDA,
      0x48, 0x85, 0xDB, 0x49, 0x0F, 0x44, 0xDA,
      0x48, 0xF7, 0xD2,
      0x48, 0xF7, 0xDA,
      0x49, 0x0F, 0x44, 0xDA,
      0x48, 0x8B, 0x5B, 0xF0,
      0x89, 0x53, 0x00,
      0x48, 0x63, 0x53, 0x00,
      0xBA, 0xFF, 0xFF, 0xFF, 0x3F,
      0x8B, 0x4B, 0xFC,
      0x21, 0xCA,
      0xC1, 0xEA, 0x03,
      0x0F, 0xB6, 0x53, 0x00,
      0x0F, 0xBF, 0x53, 0x00,
      0x48, 0x89, 0xEA,
      0x81, 0xE2, 0x1F, 0x00, 0x00, 0x00,
      0x4C, 0x89, 0x14, 0xD3,
      0x48, 0x8B, 0x53, 0x00,
      0x8B, 0x14, 0x13,
      0x48, 0x8B, 0x14, 0x13,
      0x48, 0x89, 0x53, 0x00,
      0x48, 0x8B, 0x5B, 0xE0,
      0x48, 0x8B, 0x1C, 0xD3,
      0x8B, 0x53, 0x00,
      0x0F, 0xB7, 0x53, 0x00
   };
   unsigned char actualAMD64Migrated[sizeof(expectedAMD64Migrated)] = {};
   if (amd64MigratedCode.length() != sizeof(expectedAMD64Migrated)
      || !amd64MigratedCode.read(0, actualAMD64Migrated,
         sizeof(actualAMD64Migrated)))
   {
      return false;
   }
   for (pos_t i = 0; i < sizeof(expectedAMD64Migrated); i++) {
      if (actualAMD64Migrated[i] != expectedAMD64Migrated[i])
         return false;
   }

   x86::Sequence amd64Sequence;
   x86::Operand r9 = {
      x86::Register::R9, x86::OperandSize::QWord, x86::ValueKind::Integer
   };
   x86::Operand empty = {
      x86::Register::None, x86::OperandSize::None, x86::ValueKind::None
   };
   amd64Sequence.add({
      x86::Opcode::Negate, r9, empty, 0, x86::Condition::None,
      x86::MIREffect::WriteFlags
   });
   if (amd64Encoder.emit(amd64Sequence, amd64ABI) != x86::EncodeError::None)
      return false;

   unsigned char actualAMD64[3] = {};
   unsigned char expectedAMD64[] = { 0x49, 0xF7, 0xD9 };
   if (amd64Code.length() != sizeof(expectedAMD64)
      || !amd64Code.read(0, actualAMD64, sizeof(actualAMD64)))
   {
      return false;
   }
   for (pos_t i = 0; i < sizeof(expectedAMD64); i++) {
      if (actualAMD64[i] != expectedAMD64[i])
         return false;
   }

   x86::Sequence invalidSequence;
   invalidSequence.add({
      x86::Opcode::Negate,
      { x86::Register::D, x86::OperandSize::DWord, x86::ValueKind::Integer },
      empty, 0, x86::Condition::None, x86::MIREffect::None
   });

   if (x86Encoder.emit(invalidSequence, x86ABI)
      != x86::EncodeError::InvalidEffects
      || x86::ECodeLowering::lower(
         ByteCommand(ByteCode::Quit),
         x86Runtime,
         x86ABI,
         x86RuntimeABI,
         invalidSequence) != x86::LowerError::None)
   {
      return false;
   }

   return invalidSequence.count() == 1
      && invalidSequence.instruction(0).opcode == x86::Opcode::Return;
}

static bool testX86NewIR()
{
   TargetSpec target = {};
   x86::ManagedABI managedABI = {};
   if (!TargetProvider::get(TargetPlatform::LinuxX86, target)
      || !x86::ManagedABIProvider::get(Architecture::X86, managedABI))
   {
      return false;
   }

   RuntimeSpec sta = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
   RuntimeSpec mta = RuntimeProvider::legacy(ThreadingMode::MultiThread, target);
   x86::RuntimeCallABI staABI = {};
   x86::RuntimeCallABI mtaABI = {};
   if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         sta, managedABI, staABI)
      || !x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         mta, managedABI, mtaABI))
   {
      return false;
   }

   ByteCommand command(ByteCode::NewIR, 3, (arg_t)mskVMTRef);
   x86::Sequence staSequence;
   x86::Sequence mtaSequence;
   if (x86::ECodeLowering::lower(command, sta, managedABI, staABI, staSequence)
         != x86::LowerError::None
      || x86::ECodeLowering::lower(command, mta, managedABI, mtaABI, mtaSequence)
         != x86::LowerError::None
      || test(staSequence.instruction(1).effects, x86::MIREffect::Synchronize)
      || !test(mtaSequence.instruction(1).effects, x86::MIREffect::Synchronize
         | x86::MIREffect::Safepoint | x86::MIREffect::RelocateRoots)
      || x86::MIRVerifier::verify(mtaSequence, managedABI, &staABI)
         != x86::MIRVerifyError::InvalidEffects)
   {
      return false;
   }

   TestMemory code;
   MemoryWriter writer(&code);
   x86::Encoder encoder(Architecture::X86, writer);
   if (encoder.emit(staSequence, managedABI, &staABI) != x86::EncodeError::None
      || encoder.relocationCount() != 2)
   {
      return false;
   }

   x86::Relocation& runtimeRelocation = encoder.relocation(0);
   x86::Relocation& vmtRelocation = encoder.relocation(1);
   if (runtimeRelocation.kind != x86::RelocationKind::RuntimeCall
      || runtimeRelocation.position != 6
      || runtimeRelocation.value != (unsigned int)RuntimeOperation::AllocateYoung
      || vmtRelocation.kind != x86::RelocationKind::ModuleReferenceValue
      || vmtRelocation.position != 16
      || vmtRelocation.value != (unsigned int)mskVMTRef)
   {
      return false;
   }

   unsigned char expected[] = {
      0xB9, 0x20, 0x00, 0x00, 0x00,
      0xE8, 0x00, 0x00, 0x00, 0x00,
      0xB9, 0x0C, 0x00, 0x00, 0x00,
      0xB8, 0x00, 0x00, 0x00, 0x00,
      0x89, 0x4B, 0xFC,
      0x89, 0x43, 0xF8
   };
   unsigned char actual[sizeof(expected)] = {};
   if (code.length() != sizeof(expected) || !code.read(0, actual, sizeof(actual)))
      return false;
   for (pos_t i = 0; i < sizeof(expected); i++) {
      if (actual[i] != expected[i])
         return false;
   }

   ByteCommand invalid(ByteCode::NewIR, -1, 0);
   if (x86::ECodeLowering::lower(invalid, sta, managedABI, staABI, staSequence)
      != x86::LowerError::InvalidArgument)
   {
      return false;
   }

   ByteCommand newBinary(ByteCode::NewNR, 12, (arg_t)mskVMTRef);
   x86::Sequence binarySequence;
   if (x86::ECodeLowering::lower(newBinary, sta, managedABI, staABI,
      binarySequence) != x86::LowerError::None)
   {
      return false;
   }

   TestMemory binaryCode;
   MemoryWriter binaryWriter(&binaryCode);
   x86::Encoder binaryEncoder(Architecture::X86, binaryWriter);
   if (binaryEncoder.emit(binarySequence, managedABI, &staABI)
      != x86::EncodeError::None || binaryEncoder.relocationCount() != 2)
   {
      return false;
   }

   unsigned char expectedBinary[] = {
      0xB9, 0x20, 0x00, 0x00, 0x00,
      0xE8, 0x00, 0x00, 0x00, 0x00,
      0xB9, 0x0C, 0x00, 0x80, 0x00,
      0xB8, 0x00, 0x00, 0x00, 0x00,
      0x89, 0x4B, 0xFC,
      0x89, 0x43, 0xF8
   };
   unsigned char actualBinary[sizeof(expectedBinary)] = {};
   if (binaryCode.length() != sizeof(expectedBinary)
      || !binaryCode.read(0, actualBinary, sizeof(actualBinary)))
   {
      return false;
   }
   for (pos_t i = 0; i < sizeof(expectedBinary); i++) {
      if (actualBinary[i] != expectedBinary[i])
         return false;
   }

   ByteCommand oversizedBinary(ByteCode::NewNR, 0x00800000, 0);
   if (x86::ECodeLowering::lower(oversizedBinary, sta, managedABI, staABI,
      binarySequence) != x86::LowerError::InvalidArgument)
   {
      return false;
   }

   ByteCommand inlineBinary(ByteCode::XNewNR, 12, (arg_t)mskVMTRef);
   x86::Sequence inlineSequence;
   if (x86::ECodeLowering::lower(inlineBinary, sta, managedABI, staABI,
      inlineSequence) != x86::LowerError::None)
   {
      return false;
   }

   TestMemory inlineCode;
   MemoryWriter inlineWriter(&inlineCode);
   x86::Encoder inlineEncoder(Architecture::X86, inlineWriter);
   if (inlineEncoder.emit(inlineSequence, managedABI) != x86::EncodeError::None
      || inlineEncoder.relocationCount() != 1
      || inlineEncoder.relocation(0).kind
         != x86::RelocationKind::ModuleReferenceValue
      || inlineEncoder.relocation(0).position != 9
      || inlineEncoder.relocation(0).value != (unsigned int)mskVMTRef)
   {
      return false;
   }

   unsigned char expectedInline[] = {
      0x8D, 0x5B, 0x08,
      0xB9, 0x0C, 0x00, 0x80, 0x00,
      0xB8, 0x00, 0x00, 0x00, 0x00,
      0x89, 0x4B, 0xFC,
      0x89, 0x43, 0xF8
   };
   unsigned char actualInline[sizeof(expectedInline)] = {};
   if (inlineCode.length() != sizeof(expectedInline)
      || !inlineCode.read(0, actualInline, sizeof(actualInline)))
   {
      return false;
   }
   for (pos_t i = 0; i < sizeof(expectedInline); i++) {
      if (actualInline[i] != expectedInline[i])
         return false;
   }

   return true;
}

static bool testX86DynamicAllocation()
{
   TargetSpec target = {};
   x86::ManagedABI managedABI = {};
   if (!TargetProvider::get(TargetPlatform::LinuxX86, target)
      || !x86::ManagedABIProvider::get(Architecture::X86, managedABI))
   {
      return false;
   }

   RuntimeSpec sta = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
   RuntimeSpec mta = RuntimeProvider::legacy(ThreadingMode::MultiThread, target);
   x86::RuntimeCallABI staABI = {};
   x86::RuntimeCallABI mtaABI = {};
   if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         sta, managedABI, staABI)
      || !x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         mta, managedABI, mtaABI))
   {
      return false;
   }

   ByteCommand createReferences(ByteCode::CreateR, (arg_t)mskVMTRef);
   x86::Sequence referenceSequence;
   if (x86::ECodeLowering::lower(createReferences, sta, managedABI, staABI,
      referenceSequence) != x86::LowerError::None)
   {
      return false;
   }

   TestMemory referenceCode;
   MemoryWriter referenceWriter(&referenceCode);
   x86::Encoder referenceEncoder(Architecture::X86, referenceWriter);
   if (referenceEncoder.emit(referenceSequence, managedABI, &staABI)
      != x86::EncodeError::None || referenceEncoder.relocationCount() != 2
      || referenceEncoder.relocation(0).position != 35
      || referenceEncoder.relocation(1).position != 49
      || referenceEncoder.relocation(1).kind
         != x86::RelocationKind::ModuleReferenceValue
      || referenceEncoder.relocation(1).value != (unsigned int)mskVMTRef)
   {
      return false;
   }

   unsigned char expectedReferences[] = {
      0x8B, 0x46, 0x00,
      0x89, 0xC2,
      0x69, 0xD2, 0x04, 0x00, 0x00, 0x00,
      0x83, 0xC2, 0x17,
      0x81, 0xE2, 0xF0, 0xFF, 0xFF, 0xFF,
      0xB9, 0xFF, 0xFF, 0xFF, 0xFF,
      0x81, 0xF8, 0xFF, 0xFF, 0x3F, 0x00,
      0x0F, 0x46, 0xCA,
      0xE8, 0x00, 0x00, 0x00, 0x00,
      0x8B, 0x4E, 0x00,
      0x69, 0xC9, 0x04, 0x00, 0x00, 0x00,
      0xB8, 0x00, 0x00, 0x00, 0x00,
      0x89, 0x4B, 0xFC,
      0x89, 0x43, 0xF8
   };
   unsigned char actualReferences[sizeof(expectedReferences)] = {};
   if (referenceCode.length() != sizeof(expectedReferences)
      || !referenceCode.read(0, actualReferences, sizeof(actualReferences)))
   {
      return false;
   }
   for (pos_t i = 0; i < sizeof(expectedReferences); i++) {
      if (actualReferences[i] != expectedReferences[i])
         return false;
   }

   ByteCommand createBinary(ByteCode::CreateNR, 2, (arg_t)mskVMTRef);
   x86::Sequence binarySequence;
   x86::Sequence mtaSequence;
   if (x86::ECodeLowering::lower(createBinary, sta, managedABI, staABI,
         binarySequence) != x86::LowerError::None
      || x86::ECodeLowering::lower(createBinary, mta, managedABI, mtaABI,
         mtaSequence) != x86::LowerError::None
      || !test(mtaSequence.instruction(8).effects,
         x86::MIREffect::Synchronize | x86::MIREffect::Safepoint)
      || binarySequence.count() != 15
      || binarySequence.instruction(11).opcode != x86::Opcode::OrImmediate
      || binarySequence.instruction(11).immediate != 0x00800000)
   {
      return false;
   }

   ByteCommand invalid(ByteCode::CreateNR, 0, 0);

   return x86::ECodeLowering::lower(invalid, sta, managedABI, staABI,
      binarySequence) == x86::LowerError::InvalidArgument;
}

static bool testX86NLen()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86, TargetPlatform::LinuxAMD64
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }
      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, runtimeABI))
      {
         return false;
      }
      x86::Sequence powerSequence;
      x86::Sequence genericSequence;
      x86::Sequence invalidSequence;
      ByteCommand power(ByteCode::NLen, 8);
      ByteCommand generic(ByteCode::NLen, 3);
      ByteCommand invalid(ByteCode::NLen, 0);
      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platforms[i]
      };
      if (x86::ECodeLowering::lower(power, runtime, abi, runtimeABI,
            context, powerSequence) != x86::LowerError::None
         || powerSequence.instruction(powerSequence.count() - 1).opcode
            != x86::Opcode::ShiftRightImmediate
         || x86::ECodeLowering::lower(generic, runtime, abi, runtimeABI,
            context, genericSequence) != x86::LowerError::None
         || genericSequence.instruction(genericSequence.count() - 2).opcode
            != x86::Opcode::DivideUnsigned
         || x86::ECodeLowering::lower(invalid, runtime, abi, runtimeABI,
            context, invalidSequence) != x86::LowerError::InvalidArgument)
      {
         return false;
      }

      TestMemory code;
      MemoryWriter writer(&code);
      x86::Encoder encoder(target.architecture, writer);
      if (encoder.emit(genericSequence, abi) != x86::EncodeError::None
         || code.length() == 0)
      {
         return false;
      }
   }

   return true;
}

static bool testX86ThreadStartup()
{
   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86, TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64, TargetPlatform::LinuxAMD64
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::MultiThread, target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, runtimeABI))
      {
         return false;
      }

      ByteCommand startup(ByteCode::System, 3);
      x86::LoweringContext context = { 0, 0, 0, target.platform };
      x86::Sequence sequence;
      if (x86::ECodeLowering::lower(startup, runtime, abi, runtimeABI, context, sequence)
            != x86::LowerError::None
         || sequence.count() != 5
         || sequence.instruction(0).opcode != x86::Opcode::LoadCurrentThread
         || sequence.instruction(1).opcode != x86::Opcode::MoveRuntimeData
         || sequence.instruction(2).opcode != x86::Opcode::StoreScaledIndex)
      {
         return false;
      }

      TestMemory code;
      MemoryWriter writer(&code);
      x86::Encoder encoder(target, writer);
      if (encoder.emit(sequence, abi) != x86::EncodeError::None
         || encoder.relocationCount() != 1
         || encoder.relocation(0).kind != x86::RelocationKind::RuntimeData
         || encoder.relocation(0).value != (unsigned int)
            RuntimeDataReference::ThreadTableSlots)
      {
         return false;
      }

      unsigned char windowsX86[] = {
         0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00, 0x8B, 0x00,
         0xBF, 0x00, 0x00, 0x00, 0x00,
         0x89, 0x04, 0xD7,
         0x89, 0x60, 0x14,
         0x89, 0x68, 0x08
      };
      unsigned char linuxX86[] = {
         0x65, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x83, 0xE8, 0x18,
         0xBF, 0x00, 0x00, 0x00, 0x00,
         0x89, 0x04, 0xD7,
         0x89, 0x60, 0x14,
         0x89, 0x68, 0x08
      };
      unsigned char windowsAMD64[] = {
         0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,
         0x48, 0x8B, 0x00,
         0x48, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x48, 0xC1, 0xE2, 0x04,
         0x48, 0x89, 0x04, 0x17,
         0x48, 0xC1, 0xEA, 0x04,
         0x48, 0x89, 0x60, 0x28,
         0x48, 0x89, 0x68, 0x10
      };
      unsigned char linuxAMD64[] = {
         0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
         0x48, 0x83, 0xE8, 0x30,
         0x48, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x48, 0xC1, 0xE2, 0x04,
         0x48, 0x89, 0x04, 0x17,
         0x48, 0xC1, 0xEA, 0x04,
         0x48, 0x89, 0x60, 0x28,
         0x48, 0x89, 0x68, 0x10
      };
      bool valid = platforms[i] == TargetPlatform::WindowsX86
         ? equals(code, windowsX86, sizeof(windowsX86))
         : platforms[i] == TargetPlatform::LinuxX86
            ? equals(code, linuxX86, sizeof(linuxX86))
            : platforms[i] == TargetPlatform::WindowsAMD64
               ? equals(code, windowsAMD64, sizeof(windowsAMD64))
               : equals(code, linuxAMD64, sizeof(linuxAMD64));
      if (!valid)
         return false;
   }

   return true;
}

static bool testX86SystemStartup()
{
   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86, TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64, TargetPlatform::LinuxAMD64,
      TargetPlatform::FreeBSDAMD64, TargetPlatform::MacOSAMD64
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      for (int mode = 0; mode < 2; mode++) {
         RuntimeSpec runtime = RuntimeProvider::legacy(mode == 0
            ? ThreadingMode::SingleThread : ThreadingMode::MultiThread, target);
         x86::RuntimeCallABI prepareABI = {};
         if (!x86::RuntimeABIProvider::get(RuntimeOperation::Prepare,
            runtime, abi, prepareABI))
         {
            return false;
         }

         x86::LoweringContext context = { 0, 0, 0, target.platform };
         ByteCommand startup(ByteCode::System, 4);
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(startup, runtime, abi, prepareABI,
               context, sequence) != x86::LowerError::None
            || sequence.instruction(0).opcode != x86::Opcode::InitializeFPU)
         {
            return false;
         }

         pos_t calls = 0;
         pos_t pushes = 0;
         for (pos_t j = 0; j < sequence.count(); j++) {
            if (sequence.instruction(j).opcode == x86::Opcode::CallRuntime)
               calls++;
            else if (sequence.instruction(j).opcode == x86::Opcode::Push)
               pushes++;
         }
         pos_t expectedPushes = target.abi == PlatformABI::WindowsX86
               || target.abi == PlatformABI::WindowsX64 ? 0
            : target.operatingSystem == OperatingSystem::FreeBSD ? 2 : 1;
         if (calls != 1 || pushes != expectedPushes)
            return false;

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target, writer);
         if (encoder.emit(sequence, abi, &prepareABI) != x86::EncodeError::None
            || encoder.relocationCount() != (mode == 0 ? 2 : 1))
         {
            return false;
         }
         pos_t callRelocation = mode == 0 ? 1 : 0;
         if (encoder.relocation(callRelocation).kind
               != x86::RelocationKind::RuntimeCall
            || encoder.relocation(callRelocation).value
               != (unsigned int)RuntimeOperation::Prepare)
         {
            return false;
         }
         if (mode == 0
            && (encoder.relocation(0).kind != x86::RelocationKind::RuntimeData
               || encoder.relocation(0).value != (unsigned int)
                  RuntimeDataReference::SingleContentStackRoot))
         {
            return false;
         }
      }
   }

   return true;
}

static bool testStackReference()
{
   StackReferenceSpec singleThread = {
      .threadingMode = ThreadingMode::SingleThread
   };
   StackReferenceSpec multiThread = {
      .threadingMode = ThreadingMode::MultiThread
   };

   if (singleThread.contains(0x0FFF, 0x1000, 0x2000)
      || !singleThread.contains(0x1000, 0x1000, 0x2000)
      || !singleThread.contains(0x1800, 0x1000, 0x2000)
      || !singleThread.contains(0x2000, 0x1000, 0x2000)
      || singleThread.contains(0x2001, 0x1000, 0x2000)
      || singleThread.contains(0x1800, 0x2000, 0x1000))
   {
      return false;
   }

   StackReferenceSpec specs[] = {
      singleThread,
      multiThread
   };

   for (StackReferenceSpec& spec : specs) {
      EIRFunction function;

      if (StackReferenceEIRProvider::lower(spec, function)
            != EIRVerifyError::None
         || function.blockCount() != 1
         || function.instructionCount() != 2)
      {
         return false;
      }

      EIRInstruction& stackReference = function.instruction(0);
      EIRInstruction& fallthrough = function.instruction(1);
      EIREffect expectedEffects = spec.threadingMode
            == ThreadingMode::MultiThread
         ? EIREffect::ReadTLS
         : EIREffect::ReadGlobal;

      if (stackReference.opcode
            != EIROpcode::IsCurrentStackReference
         || stackReference.result.type != EIRType::Boolean
         || stackReference.effects != expectedEffects
         || fallthrough.opcode != EIROpcode::Fallthrough
         || fallthrough.effects != EIREffect::Terminator)
      {
         return false;
      }
   }

   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86,
      TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64,
      TargetPlatform::LinuxAMD64,
      TargetPlatform::FreeBSDAMD64
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};

      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(
            target.architecture,
            abi))
      {
         return false;
      }

      for (StackReferenceSpec& spec : specs) {
         RuntimeSpec runtime = RuntimeProvider::legacy(
            spec.threadingMode,
            target);
         x86::RuntimeCallABI runtimeABI = {};

         if (!x86::RuntimeABIProvider::get(
            RuntimeOperation::AllocateYoung,
            runtime,
            abi,
            runtimeABI))
         {
            return false;
         }

         x86::LoweringContext context = {
            .frameOffset = 0,
            .stackOffset = 0,
            .vmtSize = 0,
            .platform = platform
         };
         x86::Sequence sequence;

         if (x86::ECodeLowering::lower(
            ByteCommand(ByteCode::TstStck),
            runtime,
            abi,
            runtimeABI,
            context,
            sequence) != x86::LowerError::None)
         {
            return false;
         }

         unsigned int conditionalMoves = 0;
         unsigned int currentThreadLoads = 0;
         bool loadsStackRoot = false;
         bool finalStackComparison = false;

         for (pos_t i = 0; i < sequence.count(); i++) {
            x86::Instruction& instruction = sequence.instruction(i);

            if (instruction.opcode == x86::Opcode::ConditionalMove) {
               conditionalMoves++;

               if (instruction.condition
                  != x86::Condition::BelowEqual)
               {
                  return false;
               }
            }
            else if (instruction.opcode
               == x86::Opcode::LoadCurrentThread)
            {
               currentThreadLoads++;
            }
            else if (instruction.opcode == x86::Opcode::LoadOffset
               && instruction.immediate
                  == (spec.threadingMode == ThreadingMode::MultiThread
                     ? runtime.dataLayout.threadContent.stackRoot
                     : 0))
            {
               loadsStackRoot = true;
            }
            else if (instruction.opcode
                  == x86::Opcode::CompareImmediate
               && instruction.destination.reg == x86::Register::A
               && instruction.immediate == 1)
            {
               finalStackComparison = true;
            }
         }

         if (conditionalMoves != 2
            || currentThreadLoads
               != (spec.threadingMode == ThreadingMode::MultiThread ? 1 : 0)
            || !loadsStackRoot
            || !finalStackComparison)
         {
            return false;
         }

         TestMemory memory;
         MemoryWriter writer(&memory);
         x86::Encoder encoder(target, writer);

         if (encoder.emit(sequence, abi) != x86::EncodeError::None
            || encoder.relocationCount()
               != (spec.threadingMode == ThreadingMode::SingleThread ? 1 : 0))
         {
            return false;
         }

         if (spec.threadingMode == ThreadingMode::SingleThread
            && (encoder.relocation(0).kind
                  != x86::RelocationKind::RuntimeData
               || encoder.relocation(0).value
                  != (unsigned int)
                     RuntimeDataReference::SingleContentStackRoot))
         {
            return false;
         }
      }
   }

   TargetSpec macOSTarget = {};
   x86::ManagedABI macOSABI = {};

   if (!TargetProvider::get(
         TargetPlatform::MacOSAMD64,
         macOSTarget)
      || !x86::ManagedABIProvider::get(
         Architecture::AMD64,
         macOSABI))
   {
      return false;
   }

   RuntimeSpec macOSRuntime = RuntimeProvider::legacy(
      ThreadingMode::MultiThread,
      macOSTarget);
   x86::RuntimeCallABI macOSRuntimeABI = {};
   x86::RuntimeABIProvider::get(
      RuntimeOperation::AllocateYoung,
      macOSRuntime,
      macOSABI,
      macOSRuntimeABI);

   x86::LoweringContext macOSContext = {
      .frameOffset = 0,
      .stackOffset = 0,
      .vmtSize = 0,
      .platform = TargetPlatform::MacOSAMD64
   };
   x86::Sequence rejectedSequence;

   return x86::ECodeLowering::lower(
      ByteCommand(ByteCode::TstStck),
      macOSRuntime,
      macOSABI,
      macOSRuntimeABI,
      macOSContext,
      rejectedSequence) == x86::LowerError::InvalidRuntime;
}

static bool testX86ExternalFrames()
{
   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86, TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64, TargetPlatform::LinuxAMD64,
      TargetPlatform::FreeBSDAMD64, TargetPlatform::MacOSAMD64
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      x86::ExternalFrameLayout frameLayout = {};
      int argumentFrameOffset = 0;
      if (!x86::ExternalFrameLayoutProvider::get(target, frameLayout)
         || !frameLayout.resolveArgumentFrameOffset(
            target,
            16,
            argumentFrameOffset))
      {
         return false;
      }

      int expectedArgumentFrameOffset = 0;
      switch (target.abi) {
         case PlatformABI::WindowsX86:
         case PlatformABI::SystemVX86:
            expectedArgumentFrameOffset = 60;
            break;
         case PlatformABI::WindowsX64:
            expectedArgumentFrameOffset = 136;
            break;
         case PlatformABI::SystemVAMD64:
            expectedArgumentFrameOffset = 104;
            break;
         default:
            return false;
      }
      if (argumentFrameOffset != expectedArgumentFrameOffset)
         return false;

      int modeCount = target.tlsModel == TLSModel::MachO ? 1 : 2;
      for (int mode = 0; mode < modeCount; mode++) {
         RuntimeSpec runtime = RuntimeProvider::legacy(mode == 0
            ? ThreadingMode::SingleThread : ThreadingMode::MultiThread, target);
         x86::RuntimeCallABI runtimeABI = {};
         if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
            runtime, abi, runtimeABI))
         {
            return false;
         }

         x86::LoweringContext context = { 0, 0, 0, target.platform };
         ByteCommand open(ByteCode::ExtOpenIN, 2, 16);
         ByteCommand close(ByteCode::ExtCloseN,
            runtime.objectLayout.headerSize + 16);
         x86::Sequence openSequence;
         x86::Sequence closeSequence;
         if (x86::ECodeLowering::lower(open, runtime, abi, runtimeABI,
               context, openSequence) != x86::LowerError::None
            || x86::ECodeLowering::lower(close, runtime, abi, runtimeABI,
               context, closeSequence) != x86::LowerError::None)
         {
            return false;
         }

         bool openTLS = false;
         bool closeTLS = false;
         unsigned int unwindLoad = 0;
         bool nativeFrameSelected = false;
         bool registerRestoreStarted = false;
         for (pos_t j = 0; j < openSequence.count(); j++) {
            if (openSequence.instruction(j).opcode
               == x86::Opcode::LoadCurrentThread)
            {
               openTLS = true;
            }
         }
         for (pos_t j = 0; j < closeSequence.count(); j++) {
            x86::Instruction& instruction = closeSequence.instruction(j);

            if (instruction.opcode == x86::Opcode::LoadCurrentThread)
            {
               closeTLS = true;
            }

            if (instruction.opcode == x86::Opcode::LoadOffset
               && unwindLoad < 3)
            {
               x86::Register expectedDestination[] = {
                  x86::Register::C,
                  x86::Register::C,
                  x86::Register::B
               };
               x86::Register expectedSource[] = {
                  x86::Register::BP,
                  x86::Register::C,
                  x86::Register::C
               };
               int expectedOffset[] = {
                  runtime.objectLayout.fieldSize,
                  0,
                  runtime.objectLayout.fieldSize
               };

               if (instruction.destination.reg
                     != expectedDestination[unwindLoad]
                  || instruction.source.reg != expectedSource[unwindLoad]
                  || instruction.immediate != expectedOffset[unwindLoad])
               {
                  return false;
               }

               unwindLoad++;
            }

            if (instruction.opcode == x86::Opcode::AddImmediate
               && instruction.destination.reg == x86::Register::SP
               && instruction.immediate
                  == runtime.objectLayout.fieldSize
                     * x86::ExternalFrameLayout::NativeFrameSlot)
            {
               nativeFrameSelected = !registerRestoreStarted;
            }

            if (instruction.opcode == x86::Opcode::Pop)
               registerRestoreStarted = true;
         }
         if (openTLS != (mode != 0)
            || closeTLS != (mode != 0)
            || unwindLoad != 3
            || !nativeFrameSelected)
         {
            return false;
         }

         TestMemory openCode;
         TestMemory closeCode;
         MemoryWriter openWriter(&openCode);
         MemoryWriter closeWriter(&closeCode);
         x86::Encoder openEncoder(target, openWriter);
         x86::Encoder closeEncoder(target, closeWriter);
         if (openEncoder.emit(openSequence, abi) != x86::EncodeError::None
            || closeEncoder.emit(closeSequence, abi) != x86::EncodeError::None
            || openEncoder.relocationCount() != (mode == 0 ? 1 : 0)
            || closeEncoder.relocationCount() != (mode == 0 ? 1 : 0))
         {
            return false;
         }
         if (mode == 0
            && (openEncoder.relocation(0).value != (unsigned int)
                  RuntimeDataReference::SingleContentStackFrame
               || closeEncoder.relocation(0).value != (unsigned int)
                  RuntimeDataReference::SingleContentStackFrame))
         {
            return false;
         }

         ByteCommand exactOpen(ByteCode::ExtOpenIN, 1, 12);
         x86::Sequence exactSequence;
         if (x86::ECodeLowering::lower(exactOpen, runtime, abi, runtimeABI,
               context, exactSequence) != x86::LowerError::None)
         {
            return false;
         }
         bool localSize = false;
         bool argumentSize = false;
         int expectedLocalSize = target.is64Bit() ? 16 : 12;
         int expectedArgumentSize = target.managedABI.stackAlignment * runtime.objectLayout.fieldSize;
         for (pos_t j = 0; j < exactSequence.count(); j++) {
            x86::Instruction& instruction = exactSequence.instruction(j);
            if (instruction.opcode == x86::Opcode::SubtractImmediate
               && instruction.destination.reg == x86::Register::SP)
            {
               localSize |= instruction.immediate == expectedLocalSize;
               argumentSize |= instruction.immediate == expectedArgumentSize;
            }
         }
         if (!localSize || !argumentSize)
            return false;
      }
   }

   TargetSpec target = {};
   x86::ManagedABI abi = {};
   TargetProvider::get(TargetPlatform::LinuxAMD64, target);
   x86::ManagedABIProvider::get(Architecture::AMD64, abi);
   RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::MultiThread,
      target);
   x86::RuntimeCallABI runtimeABI = {};
   x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung, runtime, abi,
      runtimeABI);
   x86::LoweringContext context = { 0, 0, 0, target.platform };
   x86::Sequence invalid;

   return x86::ECodeLowering::lower(
      ByteCommand(ByteCode::ExtOpenIN, -1, 0), runtime, abi, runtimeABI,
      context, invalid) == x86::LowerError::InvalidArgument;
}

static bool testX86Copy()
{
   MemoryCopySpec copySpec = {
      .byteCount = 13
   };
   EIRFunction copyFunction;

   if (MemoryEIRProvider::lower(copySpec, copyFunction) != EIRVerifyError::None
      || copyFunction.instructionCount() != 2
      || copyFunction.instruction(0).opcode != EIROpcode::MemoryCopy
      || copyFunction.instruction(0).effects != (EIREffect::ReadHeap | EIREffect::WriteHeap)
      || copyFunction.instruction(1).opcode != EIROpcode::Fallthrough)
   {
      return false;
   }

   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};

      if (!TargetProvider::get(platform, target) || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI runtimeABI = {};

      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung, runtime, abi, runtimeABI))
      {
         return false;
      }

      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platform
      };
      x86::Sequence sequence;

      if (x86::ECodeLowering::lower(ByteCommand(ByteCode::Copy, 13), runtime, abi,
         runtimeABI, context, sequence) != x86::LowerError::None)
      {
         return false;
      }

      bool loadsByteCount = false;
      bool copiesBytes = false;
      bool savesSource = false;
      bool restoresSource = false;
      bool loadsAMD64Source = false;

      for (pos_t i = 0; i < sequence.count(); i++) {
         x86::Instruction& instruction = sequence.instruction(i);

         if (instruction.opcode == x86::Opcode::MoveImmediate
            && instruction.destination.reg == x86::Register::C
            && instruction.immediate == 13)
         {
            loadsByteCount = true;
         }
         else if (instruction.opcode == x86::Opcode::RepeatMoveBytes) {
            copiesBytes = instruction.destination.reg == abi.dataDestination
               && instruction.source.reg == abi.dataSource;
         }
         else if (instruction.opcode == x86::Opcode::Push
            && instruction.destination.reg == abi.dataSource
            && instruction.destination.size == abi.wordSize)
         {
            savesSource = true;
         }
         else if (instruction.opcode == x86::Opcode::Pop
            && instruction.destination.reg == abi.dataSource
            && instruction.destination.size == abi.wordSize)
         {
            restoresSource = true;
         }
         else if (instruction.opcode == x86::Opcode::Move
            && instruction.destination.reg == abi.dataSource
            && instruction.source.reg == abi.cachedArgument0)
         {
            loadsAMD64Source = true;
         }
      }

      bool amd64 = target.architecture == Architecture::AMD64;
      if (!loadsByteCount || !copiesBytes || !savesSource || !restoresSource || loadsAMD64Source != amd64)
      {
         return false;
      }

      TestMemory memory;
      MemoryWriter writer(&memory);
      x86::Encoder encoder(target, writer);
      const unsigned char repeatMove[] = { 0xF3, 0xA4 };

      if (encoder.emit(sequence, abi) != x86::EncodeError::None
         || encoder.relocationCount() != 0
         || !contains(memory, repeatMove, sizeof(repeatMove)))
      {
         return false;
      }

      x86::Sequence rejected;
      if (x86::ECodeLowering::lower(ByteCommand(ByteCode::Copy, -1), runtime, abi,
         runtimeABI, context, rejected) != x86::LowerError::InvalidArgument)
      {
         return false;
      }
   }

   return true;
}

static bool testGenericECodeLowering()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64,
      TargetPlatform::LinuxARM64,
      TargetPlatform::LinuxPPC64le
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      if (!TargetProvider::get(platform, target))
         return false;

      RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::MultiThread, target);
      EIRFunction function;

      ECodeLoweringContext context = { 0, 0, 0, target.platform };
      if (ECodeEIRProvider::lower(
            ByteCommand(ByteCode::OpenIN, 3, 12), runtime, target, context, function)
            != ECodeEIRLowerError::None
         || function.instructionCount() != 6
         || function.instruction(0).opcode != EIROpcode::FrameOpen
         || function.instruction(2).opcode != EIROpcode::StackReserve
         || function.instruction(3).opcode != EIROpcode::FrameLink
         || function.instruction(4).opcode != EIROpcode::FrameClear)
      {
         return false;
      }

      EIROperand& reserveSize = function.operand(function.instruction(2).firstOperand);
      EIROperand& managedSlots = function.operand(function.instruction(4).firstOperand + 1);
      unsigned int expectedReserveSize = target.is64Bit() ? 16 : 12;
      unsigned int expectedManagedSlots = target.is64Bit() ? 4 : 3;

      if (reserveSize.value != expectedReserveSize || managedSlots.value != expectedManagedSlots)
         return false;

      if (ECodeEIRProvider::lower(
            ByteCommand(ByteCode::System, 5), runtime, target, context, function)
            != ECodeEIRLowerError::None
         || function.instruction(0).opcode != EIROpcode::StackAllocate)
      {
         return false;
      }

      if (ECodeEIRProvider::lower(
            ByteCommand(ByteCode::MovN, 17), runtime, target, context, function)
            != ECodeEIRLowerError::None
         || function.instruction(0).opcode != EIROpcode::Copy
         || function.operand(function.instruction(0).firstOperand).kind
            != EIROperandKind::Location
         || function.operand(function.instruction(0).firstOperand).value
            != (pos64_t)EIRLocation::ManagedValue)
      {
         return false;
      }

      if (ECodeEIRProvider::lower(
            ByteCommand(ByteCode::SaveSI, 2), runtime, target, context, function)
            != ECodeEIRLowerError::None
         || function.instruction(0).opcode != EIROpcode::Store
         || function.operand(function.instruction(0).firstOperand).value
            != (pos64_t)EIRLocation::StackPointer)
      {
         return false;
      }

      if (ECodeEIRProvider::lower(
            ByteCommand(ByteCode::LLoadDP, -4), runtime, target, context, function)
            != ECodeEIRLowerError::None
         || function.instruction(0).opcode != EIROpcode::Load
         || function.operand(function.instruction(0).firstOperand).type
            != EIRType::Int64)
      {
         return false;
      }

      if (ECodeEIRProvider::lower(
            ByteCommand(ByteCode::LoadDP, -4), runtime, target, context, function)
            != ECodeEIRLowerError::None
         || function.instruction(0).opcode != EIROpcode::LoadSignExtend
         || function.operand(function.instruction(0).firstOperand).type
            != EIRType::Word)
      {
         return false;
      }

      ECodeEIRMetadata metadata = {};
      if (ECodeEIRProvider::lower(ByteCommand(ByteCode::CallR, 0x01001234),
            runtime, target, context, function, &metadata) != ECodeEIRLowerError::None
         || metadata.kind != ECodeEIRKind::ManagedMethod
         || function.instruction(0).opcode != EIROpcode::CallDirect)
      {
         return false;
      }
   }

   TargetSpec target = {};
   if (!TargetProvider::get(TargetPlatform::LinuxAMD64, target))
      return false;

   RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
   ECodeLoweringContext context = {
      .frameOffset = 40,
      .stackOffset = 3,
      .vmtSize = 24,
      .platform = target.platform
   };
   ByteCommand resolved;

   if (ECodeOperandResolver::resolve(ByteCommand(ByteCode::SetFP, 2), runtime, context, resolved)
         != ECodeResolveError::None
      || resolved.arg1 != -16)
   {
      return false;
   }

   if (ECodeOperandResolver::resolve(ByteCommand(ByteCode::SaveSI, 1), runtime, context, resolved)
         != ECodeResolveError::None
      || resolved.arg1 != 32
      || resolved.arg2 != 1)
   {
      return false;
   }

   return true;
}

static bool testX86ManagedFrames()
{
   FrameOpenSpec frameSpec = {
      .managedSlots = 3,
      .unmanagedSize = 12
   };
   EIRFunction function;

   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};

      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      function.clear();
      if (FrameEIRProvider::lower(frameSpec, target, function) != EIRVerifyError::None
         || function.blockCount() != 1
         || function.instructionCount() != 6
         || function.instruction(0).opcode != EIROpcode::FrameOpen
         || function.instruction(1).opcode != EIROpcode::Constant
         || function.instruction(2).opcode != EIROpcode::StackReserve
         || function.instruction(3).opcode != EIROpcode::FrameLink
         || function.instruction(4).opcode != EIROpcode::FrameClear
         || function.instruction(5).opcode != EIROpcode::Fallthrough)
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread,
         target);
      x86::RuntimeCallABI runtimeABI = {};

      if (!x86::RuntimeABIProvider::get(
         RuntimeOperation::AllocateYoung,
         runtime,
         abi,
         runtimeABI))
      {
         return false;
      }

      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platform
      };
      x86::Sequence sequence;

      if (x86::ECodeLowering::lower(
         ByteCommand(ByteCode::OpenIN, 5, 12),
         runtime,
         abi,
         runtimeABI,
         context,
         sequence) != x86::LowerError::None)
      {
         return false;
      }

      bool amd64 = target.architecture == Architecture::AMD64;
      int expectedManagedSlots = amd64 ? 6 : 5;
      int expectedUnmanagedSize = amd64 ? 16 : 12;
      bool allocatesUnmanagedArea = false;
      unsigned int zeroPushes = 0;
      bool usesBlockFill = false;

      for (pos_t i = 0; i < sequence.count(); i++) {
         x86::Instruction& instruction = sequence.instruction(i);

         if (instruction.opcode == x86::Opcode::SubtractImmediate
            && instruction.destination.reg == x86::Register::SP)
         {
            allocatesUnmanagedArea |= instruction.immediate
               == expectedUnmanagedSize;
         }
         else if (instruction.opcode == x86::Opcode::Push
            && instruction.destination.reg == x86::Register::A)
         {
            zeroPushes++;
         }
         else if (instruction.opcode == x86::Opcode::RepeatStore) {
            usesBlockFill = true;
         }
      }

      if (!allocatesUnmanagedArea
         || zeroPushes != (unsigned int)expectedManagedSlots + 1
         || usesBlockFill)
      {
         return false;
      }

      x86::Sequence compactSequence;
      if (x86::ECodeLowering::lower(
         ByteCommand(ByteCode::OpenIN, 3, 0),
         runtime,
         abi,
         runtimeABI,
         context,
         compactSequence) != x86::LowerError::None)
      {
         return false;
      }

      unsigned int compactZeroPushes = 0;
      bool compactUsesBlockFill = false;

      for (pos_t i = 0; i < compactSequence.count(); i++) {
         x86::Instruction& instruction = compactSequence.instruction(i);

         if (instruction.opcode == x86::Opcode::Push
            && instruction.destination.reg == x86::Register::A)
         {
            compactZeroPushes++;
         }
         else if (instruction.opcode == x86::Opcode::RepeatStore) {
            compactUsesBlockFill = true;
         }
      }

      unsigned int expectedZeroPushes = amd64 ? 4 : 3;
      if (compactZeroPushes != expectedZeroPushes || compactUsesBlockFill)
         return false;

      FrameCloseSpec closeSpec = {
         .argumentSize = 24
      };
      EIRFunction closeFunction;

      if (FrameEIRProvider::lower(closeSpec, closeFunction)
            != EIRVerifyError::None
         || closeFunction.instruction(0).opcode != EIROpcode::FrameClose)
      {
         return false;
      }

      x86::Sequence closeSequence;
      if (x86::ECodeLowering::lower(
         ByteCommand(ByteCode::CloseN, 24),
         runtime,
         abi,
         runtimeABI,
         context,
         closeSequence) != x86::LowerError::None
         || closeSequence.count() != 3
         || closeSequence.instruction(0).opcode
            != x86::Opcode::AddImmediate
         || closeSequence.instruction(0).destination.reg
            != x86::Register::BP
         || closeSequence.instruction(0).immediate != 24
         || closeSequence.instruction(1).opcode != x86::Opcode::Move
         || closeSequence.instruction(1).destination.reg
            != x86::Register::SP
         || closeSequence.instruction(2).opcode != x86::Opcode::Pop
         || closeSequence.instruction(2).destination.reg
            != x86::Register::BP)
      {
         return false;
      }

      TestMemory memory;
      MemoryWriter writer(&memory);
      x86::Encoder encoder(target, writer);

      if (encoder.emit(sequence, abi) != x86::EncodeError::None
         || encoder.relocationCount() != 0)
      {
         return false;
      }

      x86::Sequence rejected;
      if (x86::ECodeLowering::lower(
         ByteCommand(ByteCode::OpenIN, -1, 0),
         runtime,
         abi,
         runtimeABI,
         context,
         rejected) != x86::LowerError::InvalidArgument)
      {
         return false;
      }
   }

   return true;
}

static bool testX86RootStackAllocation()
{
   EIRFunction function;
   if (StackEIRProvider::lowerRootAllocation(function) != EIRVerifyError::None
      || function.instructionCount() != 2
      || function.instruction(0).opcode != EIROpcode::StackAllocate
      || function.instruction(0).effects != EIREffect::WriteFrame
      || function.instruction(1).opcode != EIROpcode::Fallthrough)
   {
      return false;
   }

   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};

      if (!TargetProvider::get(platform, target) || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI runtimeABI = {};

      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung, runtime, abi, runtimeABI))
      {
         return false;
      }

      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platform
      };
      x86::Sequence sequence;

      if (x86::ECodeLowering::lower(ByteCommand(ByteCode::System, 5), runtime, abi,
         runtimeABI, context, sequence) != x86::LowerError::None)
      {
         return false;
      }

      bool subtractsDynamicSize = false;
      bool clearsRoots = false;
      bool preservesStackTop = false;
      bool preservesCachedArgument = target.architecture == Architecture::AMD64;
      bool savesDataSource = target.architecture == Architecture::X86;
      bool restoresDataSource = target.architecture == Architecture::X86;
      bool alignsStack = target.architecture == Architecture::X86;

      for (pos_t i = 0; i < sequence.count(); i++) {
         x86::Instruction& instruction = sequence.instruction(i);

         if (instruction.opcode == x86::Opcode::Subtract
            && instruction.destination.reg == x86::Register::SP
            && instruction.source.reg == x86::Register::A)
         {
            subtractsDynamicSize = true;
         }
         else if (instruction.opcode == x86::Opcode::RepeatStore) {
            clearsRoots = instruction.destination.reg == x86::Register::DI
               && instruction.source.reg == x86::Register::A;
         }
         else if (instruction.opcode == x86::Opcode::Push
            && instruction.destination.reg == x86::Register::SI)
         {
            preservesStackTop = true;
         }
         else if (target.architecture == Architecture::X86
            && instruction.opcode == x86::Opcode::LoadOffset
            && instruction.destination.reg == abi.cachedArgument0
            && instruction.source.reg == x86::Register::SP)
         {
            preservesCachedArgument = instruction.immediate == runtime.objectLayout.fieldSize;
         }
         else if (target.architecture == Architecture::AMD64
            && instruction.opcode == x86::Opcode::AndImmediate
            && instruction.destination.reg == abi.value)
         {
            alignsStack = instruction.immediate == -2;
         }
         else if (target.architecture == Architecture::AMD64
            && instruction.opcode == x86::Opcode::Move
            && instruction.destination.reg == abi.scratch
            && instruction.source.reg == abi.dataSource)
         {
            savesDataSource = true;
         }
         else if (target.architecture == Architecture::AMD64
            && instruction.opcode == x86::Opcode::Move
            && instruction.destination.reg == abi.dataSource
            && instruction.source.reg == abi.scratch)
         {
            restoresDataSource = true;
         }
      }

      if (!subtractsDynamicSize || !clearsRoots || !preservesStackTop
         || !preservesCachedArgument || !savesDataSource || !restoresDataSource || !alignsStack)
      {
         return false;
      }

      TestMemory memory;
      MemoryWriter writer(&memory);
      x86::Encoder encoder(target, writer);

      if (encoder.emit(sequence, abi) != x86::EncodeError::None
         || encoder.relocationCount() != 0)
      {
         return false;
      }
   }

   return true;
}

struct ExceptionControlExpectation
{
   ByteCode code;
   pos_t singleThreadInstructionCount;
   pos_t multiThreadInstructionCount;
};

static bool verifyExceptionControlRelocations(
   ByteCode code,
   ThreadingMode threadingMode,
   x86::Encoder& encoder)
{
   if (threadingMode == ThreadingMode::SingleThread) {
      return encoder.relocationCount() == 1
         && encoder.relocation(0).kind == x86::RelocationKind::RuntimeData
         && encoder.relocation(0).value == (unsigned int)RuntimeDataReference::SingleContent;
   }

   if (code == ByteCode::Exclude) {
      return encoder.relocationCount() == 2
         && encoder.relocation(0).kind == x86::RelocationKind::RuntimeData
         && encoder.relocation(0).value == (unsigned int)RuntimeDataReference::GCDataSignal
         && encoder.relocation(1).kind == x86::RelocationKind::RuntimeCall
         && encoder.relocation(1).value == (unsigned int)RuntimeOperation::WaitForGC;
   }

   if (code == ByteCode::Include) {
      return encoder.relocationCount() == 1
         && encoder.relocation(0).kind == x86::RelocationKind::RuntimeData
         && encoder.relocation(0).value == (unsigned int)RuntimeDataReference::GCDataLock;
   }

   return encoder.relocationCount() == 0;
}

static bool verifyExceptionControlSequence(
   ByteCode code,
   ThreadingMode threadingMode,
   const RuntimeSpec& runtime,
   x86::Sequence& sequence)
{
   x86::Opcode loadThread = threadingMode == ThreadingMode::MultiThread
      ? x86::Opcode::LoadCurrentThread
      : x86::Opcode::MoveRuntimeData;

   if (code != ByteCode::Include && sequence.instruction(0).opcode != loadThread)
      return false;

   if (code == ByteCode::Throw)
      return sequence.instruction(sequence.count() - 1).opcode == x86::Opcode::JumpRegister;

   if (threadingMode == ThreadingMode::SingleThread)
      return true;

   bool foundThreadStateTransition = false;
   bool foundWaitForGC = false;

   for (pos_t i = 0; i < sequence.count(); i++) {
      x86::Instruction& instruction = sequence.instruction(i);

      if (instruction.opcode == x86::Opcode::AtomicCompareExchangeDWord
         && instruction.immediate == runtime.dataLayout.threadContent.flags)
      {
         foundThreadStateTransition = true;
      }

      if (instruction.opcode == x86::Opcode::CallRuntime
         && instruction.immediate == (int)RuntimeOperation::WaitForGC)
      {
         foundWaitForGC = true;
      }
   }

   if (code == ByteCode::Exclude)
      return foundThreadStateTransition && foundWaitForGC;

   if (code == ByteCode::Include)
      return !foundWaitForGC;

   return true;
}

static bool testX86ExceptionControl()
{
   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86,
      TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64,
      TargetPlatform::LinuxAMD64
   };

   ExceptionControlExpectation expectations[] = {
      {
         .code = ByteCode::Throw,
         .singleThreadInstructionCount = 4,
         .multiThreadInstructionCount = 4
      },
      {
         .code = ByteCode::Unhook,
         .singleThreadInstructionCount = 7,
         .multiThreadInstructionCount = 7
      },
      {
         .code = ByteCode::Exclude,
         .singleThreadInstructionCount = 6,
         .multiThreadInstructionCount = 15
      },
      {
         .code = ByteCode::Include,
         .singleThreadInstructionCount = 5,
         .multiThreadInstructionCount = 15
      }
   };

   ThreadingMode threadingModes[] = {
      ThreadingMode::SingleThread,
      ThreadingMode::MultiThread
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI managedABI = {};

      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, managedABI))
      {
         return false;
      }

      for (ThreadingMode threadingMode : threadingModes) {
         RuntimeSpec runtime = RuntimeProvider::legacy(threadingMode, target);
         RuntimeOperation callOperation = threadingMode == ThreadingMode::MultiThread
            ? RuntimeOperation::WaitForGC
            : RuntimeOperation::AllocateYoung;
         x86::RuntimeCallABI runtimeABI = {};

         if (!x86::RuntimeABIProvider::get(callOperation, runtime, managedABI, runtimeABI))
            return false;

         for (const ExceptionControlExpectation& expectation : expectations) {
            x86::Sequence sequence;
            ByteCommand command(expectation.code);
            x86::LoweringContext context = { 0, 0, 0, target.platform };

            if (x86::ECodeLowering::lower(
               command, runtime, managedABI, runtimeABI, context, sequence) != x86::LowerError::None)
            {
               return false;
            }

            pos_t expectedCount = threadingMode == ThreadingMode::MultiThread
               ? expectation.multiThreadInstructionCount
               : expectation.singleThreadInstructionCount;

            if (sequence.count() != expectedCount
               || !verifyExceptionControlSequence(expectation.code, threadingMode, runtime, sequence))
            {
               return false;
            }

            const x86::RuntimeCallABI* encoderABI = threadingMode == ThreadingMode::MultiThread
               && expectation.code == ByteCode::Exclude ? &runtimeABI : nullptr;
            TestMemory code;
            MemoryWriter writer(&code);
            x86::Encoder encoder(target, writer);

            if (encoder.emit(sequence, managedABI, encoderABI) != x86::EncodeError::None
               || !verifyExceptionControlRelocations(expectation.code, threadingMode, encoder))
            {
               return false;
            }
         }
      }
   }

   return true;
}

static bool testX86ExceptionHook()
{
   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86,
      TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64,
      TargetPlatform::LinuxAMD64
   };

   ThreadingMode threadingModes[] = {
      ThreadingMode::SingleThread,
      ThreadingMode::MultiThread
   };

   constexpr int sourceFrameOffset = -16;
   constexpr int dataHeaderOffset = 12;
   constexpr int resolvedFrameOffset = 28;
   constexpr pos_t targetLabel = 7;

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI managedABI = {};

      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, managedABI))
      {
         return false;
      }

      for (ThreadingMode threadingMode : threadingModes) {
         RuntimeSpec runtime = RuntimeProvider::legacy(threadingMode, target);
         x86::RuntimeCallABI runtimeABI = {};

         if (!x86::RuntimeABIProvider::get(
            RuntimeOperation::AllocateYoung,
            runtime,
            managedABI,
            runtimeABI))
         {
            return false;
         }

         x86::LoweringContext context = {
            0,
            0,
            0,
            platform,
            false,
            dataHeaderOffset
         };

         ByteCommand command(
            ByteCode::XHookDPR,
            sourceFrameOffset,
            (arg_t)(targetLabel | mskLabelRef));

         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(
               command,
               runtime,
               managedABI,
               runtimeABI,
               context,
               sequence)
               != x86::LowerError::None
            || sequence.count() != 9
            || sequence.instruction(0).opcode
               != x86::Opcode::AddressOffsetFrom
            || sequence.instruction(0).immediate != resolvedFrameOffset
            || sequence.instruction(6).opcode
               != x86::Opcode::MoveLabelAddress
            || sequence.instruction(6).immediate != (int)targetLabel)
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target, writer);

         if (encoder.emit(sequence, managedABI) != x86::EncodeError::None)
            return false;

         pos_t labelRelocation = threadingMode == ThreadingMode::SingleThread
            ? 1
            : 0;
         pos_t expectedRelocations = labelRelocation + 1;

         if (encoder.relocationCount() != expectedRelocations
            || encoder.relocation(labelRelocation).kind
               != x86::RelocationKind::ProcedureLabel
            || encoder.relocation(labelRelocation).value != targetLabel)
         {
            return false;
         }

         if (threadingMode == ThreadingMode::SingleThread
            && (encoder.relocation(0).kind
                  != x86::RelocationKind::RuntimeData
               || encoder.relocation(0).value
                  != (unsigned int)RuntimeDataReference::SingleContent))
         {
            return false;
         }

         ByteCommand referenceCommand(
            ByteCode::XHookDPR,
            sourceFrameOffset,
            (arg_t)(mskProcedureRef | 3));

         x86::Sequence referenceSequence;
         if (x86::ECodeLowering::lower(
               referenceCommand,
               runtime,
               managedABI,
               runtimeABI,
               context,
               referenceSequence)
               != x86::LowerError::None
            || referenceSequence.count() != 9
            || referenceSequence.instruction(6).opcode
               != x86::Opcode::MoveReferenceAddress)
         {
            return false;
         }

         TestMemory referenceCode;
         MemoryWriter referenceWriter(&referenceCode);
         x86::Encoder referenceEncoder(target, referenceWriter);

         if (referenceEncoder.emit(referenceSequence, managedABI)
               != x86::EncodeError::None
            || referenceEncoder.relocationCount() != expectedRelocations
            || referenceEncoder.relocation(labelRelocation).kind
               != x86::RelocationKind::ModuleReferenceValue
            || referenceEncoder.relocation(labelRelocation).value
               != (unsigned int)(mskProcedureRef | 3))
         {
            return false;
         }
      }
   }

   return true;
}

static bool testX86SafeRegions()
{
   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86, TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64, TargetPlatform::LinuxAMD64
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec staRuntime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI staABI = {};
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
            staRuntime, abi, staABI))
      {
         return false;
      }
      for (int system = 8; system <= 9; system++) {
         x86::LoweringContext context = { 0, 0, 0, target.platform };
         x86::Sequence staSequence;
         if (x86::ECodeLowering::lower(ByteCommand(ByteCode::System, system),
               staRuntime, abi, staABI, context, staSequence) != x86::LowerError::None
            || staSequence.count() != 1
            || staSequence.instruction(0).opcode != x86::Opcode::Nop)
         {
            return false;
         }
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::MultiThread, target);
      x86::RuntimeCallABI waitABI = {};
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::WaitForGC,
         runtime, abi, waitABI))
      {
         return false;
      }

      for (int system = 8; system <= 9; system++) {
         ByteCommand command(ByteCode::System, system);
         x86::LoweringContext context = { 0, 0, 0, target.platform };
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(command, runtime, abi, waitABI, context, sequence)
               != x86::LowerError::None
            || sequence.count() != 23
            || sequence.instruction(5).opcode
               != x86::Opcode::AtomicCompareExchangeDWord
            || sequence.instruction(14).opcode != x86::Opcode::CallRuntime
            || sequence.instruction(18).immediate != (system == 8 ? 1 : 0)
            || !test(sequence.instruction(14).effects,
               x86::MIREffect::Safepoint | x86::MIREffect::Synchronize
                  | x86::MIREffect::ReadTLS | x86::MIREffect::RelocateRoots)
            || test(sequence.instruction(14).effects,
               x86::MIREffect::Allocate | x86::MIREffect::MayThrow))
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target, writer);
         if (encoder.emit(sequence, abi, &waitABI) != x86::EncodeError::None
            || encoder.relocationCount() != 5)
         {
            return false;
         }

         RuntimeDataReference references[] = {
            RuntimeDataReference::GCDataLock,
            RuntimeDataReference::GCDataSignal,
            RuntimeDataReference::GCDataLock,
            RuntimeDataReference::GCDataLock
         };
         pos_t dataIndex = 0;
         for (pos_t j = 0; j < encoder.relocationCount(); j++) {
            x86::Relocation& relocation = encoder.relocation(j);
            if (j == 3) {
               if (relocation.kind != x86::RelocationKind::RuntimeCall
                  || relocation.value != (unsigned int)RuntimeOperation::WaitForGC)
               {
                  return false;
               }
            }
            else if (relocation.kind != x86::RelocationKind::RuntimeData
               || relocation.value != (unsigned int)references[dataIndex++])
            {
               return false;
            }
         }

         unsigned char bytes[256] = {};
         if (code.length() > sizeof(bytes)
            || !code.read(0, bytes, code.length()))
         {
            return false;
         }
         unsigned int compareExchange = 0;
         unsigned int exchangeAdd = 0;
         for (pos_t j = 0; j + 3 < code.length(); j++) {
            if (bytes[j] == 0xF0 && bytes[j + 1] == 0x0F
               && bytes[j + 2] == 0xB1 && bytes[j + 3] == 0x0F)
            {
               compareExchange++;
            }
            if (bytes[j] == 0xF0 && bytes[j + 1] == 0x0F
               && bytes[j + 2] == 0xC1 && bytes[j + 3] == 0x0F)
            {
               exchangeAdd++;
            }
         }
         if (compareExchange != 1 || exchangeAdd != 2)
            return false;
      }
   }

   return true;
}

static bool testX86GCLock()
{
   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86,
      TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64,
      TargetPlatform::LinuxAMD64,
      TargetPlatform::FreeBSDAMD64,
      TargetPlatform::MacOSAMD64
   };

   for (size_t i = 0; i < sizeof(platforms) / sizeof(platforms[0]); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec staRuntime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread,
         target);
      x86::RuntimeCallABI staABI = {};
      if (!x86::RuntimeABIProvider::get(
            RuntimeOperation::AllocateYoung,
            staRuntime,
            abi,
            staABI))
      {
         return false;
      }

      for (int system = 6; system <= 7; system++) {
         x86::LoweringContext context = { 0, 0, 0, target.platform };
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(
               ByteCommand(ByteCode::System, system),
               staRuntime,
               abi,
               staABI,
               context,
               sequence) != x86::LowerError::InvalidRuntime)
         {
            return false;
         }
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::MultiThread,
         target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(
            RuntimeOperation::AllocateYoung,
            runtime,
            abi,
            runtimeABI))
      {
         return false;
      }

      for (int system = 6; system <= 7; system++) {
         x86::LoweringContext context = { 0, 0, 0, target.platform };
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(
               ByteCommand(ByteCode::System, system),
               runtime,
               abi,
               runtimeABI,
               context,
               sequence) != x86::LowerError::None)
         {
            return false;
         }

         pos_t atomicIndex = system == 6 ? 4 : 2;
         x86::Opcode expectedOpcode = system == 6
            ? x86::Opcode::AtomicCompareExchangeDWord
            : x86::Opcode::AtomicExchangeAddDWord;
         if (sequence.count() != (pos_t)(system == 6 ? 6 : 3)
            || sequence.instruction(0).opcode != x86::Opcode::MoveRuntimeData
            || sequence.instruction(0).immediate
               != (int)RuntimeDataReference::GCDataLock
            || sequence.instruction(atomicIndex).opcode != expectedOpcode
            || !test(sequence.instruction(atomicIndex).effects,
               x86::MIREffect::Synchronize))
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target, writer);
         if (encoder.emit(sequence, abi) != x86::EncodeError::None
            || encoder.relocationCount() != 1
            || encoder.relocation(0).kind
               != x86::RelocationKind::RuntimeData
            || encoder.relocation(0).value
               != (unsigned int)RuntimeDataReference::GCDataLock)
         {
            return false;
         }
      }
   }

   return true;
}

static bool testX86ObjectLocks()
{
   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86,
      TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64,
      TargetPlatform::LinuxAMD64,
      TargetPlatform::FreeBSDAMD64,
      TargetPlatform::MacOSAMD64
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platform
      };

      RuntimeSpec staRuntime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread,
         target);
      x86::RuntimeCallABI staABI = {};
      if (!x86::RuntimeABIProvider::get(
            RuntimeOperation::AllocateYoung,
            staRuntime,
            abi,
            staABI))
      {
         return false;
      }

      x86::Sequence staTry;
      x86::Sequence staRelease;
      if (x86::ECodeLowering::lower(
            ByteCommand(ByteCode::TryLock),
            staRuntime,
            abi,
            staABI,
            context,
            staTry) != x86::LowerError::None
         || x86::ECodeLowering::lower(
            ByteCommand(ByteCode::FreeLock),
            staRuntime,
            abi,
            staABI,
            context,
            staRelease) != x86::LowerError::None
         || staTry.count() != 1
         || staTry.instruction(0).opcode != x86::Opcode::Clear
         || staRelease.count() != 1
         || staRelease.instruction(0).opcode != x86::Opcode::Nop)
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::MultiThread,
         target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(
            RuntimeOperation::AllocateYoung,
            runtime,
            abi,
            runtimeABI))
      {
         return false;
      }

      x86::Sequence tryLock;
      x86::Sequence releaseLock;
      if (x86::ECodeLowering::lower(
            ByteCommand(ByteCode::TryLock),
            runtime,
            abi,
            runtimeABI,
            context,
            tryLock) != x86::LowerError::None
         || x86::ECodeLowering::lower(
            ByteCommand(ByteCode::FreeLock),
            runtime,
            abi,
            runtimeABI,
            context,
            releaseLock) != x86::LowerError::None
         || tryLock.count() != 4
         || tryLock.instruction(2).opcode
            != x86::Opcode::AtomicCompareExchangeByte
         || releaseLock.count() != 2
         || releaseLock.instruction(1).opcode
            != x86::Opcode::AtomicExchangeAddByte)
      {
         return false;
      }

      TestMemory tryCode;
      MemoryWriter tryWriter(&tryCode);
      x86::Encoder tryEncoder(target, tryWriter);
      TestMemory releaseCode;
      MemoryWriter releaseWriter(&releaseCode);
      x86::Encoder releaseEncoder(target, releaseWriter);
      if (tryEncoder.emit(tryLock, abi) != x86::EncodeError::None
         || releaseEncoder.emit(releaseLock, abi) != x86::EncodeError::None)
      {
         return false;
      }

      const unsigned char tryX86[] = {
         0x31, 0xC0,
         0xB9, 0x01, 0x00, 0x00, 0x00,
         0xF0, 0x0F, 0xB0, 0x4B, 0xFF,
         0x85, 0xC0
      };
      const unsigned char tryAMD64[] = {
         0x31, 0xC0,
         0xB9, 0x01, 0x00, 0x00, 0x00,
         0xF0, 0x0F, 0xB0, 0x4B, 0xF8,
         0x85, 0xC0
      };
      const unsigned char releaseX86[] = {
         0xB9, 0xFF, 0xFF, 0xFF, 0xFF,
         0xF0, 0x0F, 0xC0, 0x4B, 0xFF
      };
      const unsigned char releaseAMD64[] = {
         0xB9, 0xFF, 0xFF, 0xFF, 0xFF,
         0xF0, 0x0F, 0xC0, 0x4B, 0xF8
      };

      if (target.architecture == Architecture::X86) {
         if (!equals(tryCode, tryX86, sizeof(tryX86))
            || !equals(releaseCode, releaseX86, sizeof(releaseX86)))
         {
            return false;
         }
      }
      else if (!equals(tryCode, tryAMD64, sizeof(tryAMD64))
         || !equals(releaseCode, releaseAMD64, sizeof(releaseAMD64)))
      {
         return false;
      }
   }

   return true;
}

static bool testX86ThreadLocalStorage()
{
   constexpr ref_t threadLocalReference = mskTLSVariable | 1;

   TargetPlatform platforms[] = {
      TargetPlatform::WindowsX86,
      TargetPlatform::LinuxX86,
      TargetPlatform::WindowsAMD64,
      TargetPlatform::LinuxAMD64
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::MultiThread,
         target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(
            RuntimeOperation::AllocateYoung,
            runtime,
            abi,
            runtimeABI))
      {
         return false;
      }

      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platform
      };

      ByteCode operations[] = {
         ByteCode::PeekTLS,
         ByteCode::StoreTLS
      };
      for (ByteCode operation : operations) {
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(
               ByteCommand(operation, threadLocalReference),
               runtime,
               abi,
               runtimeABI,
               context,
               sequence) != x86::LowerError::None
            || sequence.count() != 1)
         {
            return false;
         }

         bool load = operation == ByteCode::PeekTLS;
         if (sequence.instruction(0).opcode != (load
               ? x86::Opcode::LoadThreadLocal
               : x86::Opcode::StoreThreadLocal)
            || sequence.instruction(0).immediate != (int)threadLocalReference)
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target, writer);
         if (encoder.emit(sequence, abi) != x86::EncodeError::None
            || encoder.relocationCount() != 1
            || encoder.relocation(0).kind
               != x86::RelocationKind::ThreadLocalOffset
            || encoder.relocation(0).value != threadLocalReference)
         {
            return false;
         }

         const unsigned char windowsX86Load[] = {
            0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00,
            0x8B, 0x00,
            0x05, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0x18
         };
         const unsigned char windowsX86Store[] = {
            0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00,
            0x8B, 0x00,
            0x05, 0x00, 0x00, 0x00, 0x00,
            0x89, 0x18
         };
         const unsigned char linuxX86Load[] = {
            0x65, 0xA1, 0x00, 0x00, 0x00, 0x00,
            0x2D, 0x00, 0x00, 0x00, 0x00,
            0x83, 0xE8, 0x04,
            0x8B, 0x18
         };
         const unsigned char linuxX86Store[] = {
            0x65, 0xA1, 0x00, 0x00, 0x00, 0x00,
            0x2D, 0x00, 0x00, 0x00, 0x00,
            0x83, 0xE8, 0x04,
            0x89, 0x18
         };
         const unsigned char windowsAMD64Load[] = {
            0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0x00,
            0x48, 0x05, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0x18
         };
         const unsigned char windowsAMD64Store[] = {
            0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0x00,
            0x48, 0x05, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x89, 0x18
         };
         const unsigned char linuxAMD64Load[] = {
            0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x2D, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x83, 0xE8, 0x08,
            0x48, 0x8B, 0x18
         };
         const unsigned char linuxAMD64Store[] = {
            0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x2D, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x83, 0xE8, 0x08,
            0x48, 0x89, 0x18
         };

         bool valid = false;
         if (platform == TargetPlatform::WindowsX86) {
            valid = load
               ? equals(code, windowsX86Load, sizeof(windowsX86Load))
               : equals(code, windowsX86Store, sizeof(windowsX86Store));
         }
         else if (platform == TargetPlatform::LinuxX86) {
            valid = load
               ? equals(code, linuxX86Load, sizeof(linuxX86Load))
               : equals(code, linuxX86Store, sizeof(linuxX86Store));
         }
         else if (platform == TargetPlatform::WindowsAMD64) {
            valid = load
               ? equals(code, windowsAMD64Load, sizeof(windowsAMD64Load))
               : equals(code, windowsAMD64Store, sizeof(windowsAMD64Store));
         }
         else {
            valid = load
               ? equals(code, linuxAMD64Load, sizeof(linuxAMD64Load))
               : equals(code, linuxAMD64Store, sizeof(linuxAMD64Store));
         }

         if (!valid)
            return false;
      }
   }

   TargetSpec target = {};
   x86::ManagedABI abi = {};
   if (!TargetProvider::get(TargetPlatform::LinuxAMD64, target)
      || !x86::ManagedABIProvider::get(target.architecture, abi))
   {
      return false;
   }

   RuntimeSpec runtime = RuntimeProvider::legacy(
      ThreadingMode::MultiThread,
      target);
   x86::RuntimeCallABI runtimeABI = {};
   if (!x86::RuntimeABIProvider::get(
         RuntimeOperation::AllocateYoung,
         runtime,
         abi,
         runtimeABI))
   {
      return false;
   }

   x86::LoweringContext context = { 0, 0, 0, target.platform };
   x86::Sequence invalid;

   return x86::ECodeLowering::lower(
      ByteCommand(ByteCode::PeekTLS, 1),
      runtime,
      abi,
      runtimeABI,
      context,
      invalid) == x86::LowerError::InvalidArgument;
}

static bool testX86FillIR()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86, TargetPlatform::LinuxAMD64
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }
      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, runtimeABI))
      {
         return false;
      }

      ByteCommand fill(ByteCode::FillIR, 7, (arg_t)mskVMTRef);
      ByteCommand zeroFill(ByteCode::FillIR, 7, 0);
      ByteCommand invalid(ByteCode::FillIR, -1, 0);
      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platforms[i]
      };
      x86::Sequence sequence;
      x86::Sequence invalidSequence;
      x86::Sequence zeroSequence;
      if (x86::ECodeLowering::lower(fill, runtime, abi, runtimeABI,
            context, sequence) != x86::LowerError::None
         || x86::ECodeLowering::lower(zeroFill, runtime, abi, runtimeABI,
            context, zeroSequence) != x86::LowerError::None
         || x86::ECodeLowering::lower(invalid, runtime, abi, runtimeABI,
            context, invalidSequence) != x86::LowerError::InvalidArgument)
      {
         return false;
      }

      TestMemory code;
      MemoryWriter writer(&code);
      x86::Encoder encoder(target.architecture, writer);
      if (encoder.emit(sequence, abi) != x86::EncodeError::None
         || encoder.relocationCount() != 1
         || encoder.relocation(0).kind != x86::RelocationKind::ModuleReferenceValue
         || encoder.relocation(0).value != (unsigned int)fill.arg2
         || encoder.relocation(0).position
            != (target.architecture == Architecture::X86 ? 1 : 2))
      {
         return false;
      }

      TestMemory zeroCode;
      MemoryWriter zeroWriter(&zeroCode);
      x86::Encoder zeroEncoder(target.architecture, zeroWriter);
      if (zeroEncoder.emit(zeroSequence, abi) != x86::EncodeError::None
         || zeroEncoder.relocationCount() != 0)
      {
         return false;
      }
   }

   return true;
}

static bool testX86FrameAddressing()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86, TargetPlatform::LinuxAMD64
   };
   ByteCode codes[] = {
      ByteCode::SetDP, ByteCode::SetFP, ByteCode::SetSP, ByteCode::LoadDP,
      ByteCode::XCmpDP, ByteCode::XAddDP, ByteCode::XSetFP, ByteCode::XAssignI
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }
      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, runtimeABI))
      {
         return false;
      }
      x86::LoweringContext context = {
         32, 1, target.architecture == Architecture::X86 ? 16 : 32,
         target.platform
      };

      for (size_t j = 0; j < sizeof(codes) / sizeof(ByteCode); j++) {
         ByteCommand command(codes[j], codes[j] == ByteCode::SetSP ? 2 : 300);
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(command, runtime, abi, runtimeABI,
            context, sequence) != x86::LowerError::None)
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target.architecture, writer);
         if (encoder.emit(sequence, abi) != x86::EncodeError::None)
            return false;

         if (codes[j] == ByteCode::SetSP) {
            unsigned char x86Bytes[] = { 0x8D, 0x5C, 0x24, 0x0C };
            unsigned char amd64Bytes[] = { 0x48, 0x8D, 0x5C, 0x24, 0x18 };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
         else if (codes[j] == ByteCode::XSetFP) {
            unsigned char x86Bytes[] = {
               0x8D, 0x9C, 0x95, 0x50, 0xFB, 0xFF, 0xFF
            };
            unsigned char amd64Bytes[] = {
               0x48, 0x8D, 0x9C, 0xD5, 0xA0, 0xF6, 0xFF, 0xFF
            };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
         else if (codes[j] == ByteCode::LoadDP) {
            unsigned char x86Bytes[] = {
               0x8B, 0x95, 0xD4, 0xFE, 0xFF, 0xFF
            };
            unsigned char amd64Bytes[] = {
               0x48, 0x63, 0x95, 0xD4, 0xFE, 0xFF, 0xFF
            };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
      }
   }

   return true;
}

static bool testX86ParameterizedScalars()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86, TargetPlatform::LinuxAMD64
   };
   ByteCommand commands[] = {
      ByteCommand(ByteCode::Shl, 35),
      ByteCommand(ByteCode::Shr, 34),
      ByteCommand(ByteCode::XSaveN, -1),
      ByteCommand(ByteCode::MovM, 0x12345678),
      ByteCommand(ByteCode::MovN, -7),
      ByteCommand(ByteCode::AddN, 9),
      ByteCommand(ByteCode::SubN, 5),
      ByteCommand(ByteCode::AndN, 0xFF),
      ByteCommand(ByteCode::OrN, 0x40000000),
      ByteCommand(ByteCode::MulN, -3),
      ByteCommand(ByteCode::CmpN, 11),
      ByteCommand(ByteCode::SetR, 0x01001234)
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }
      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, runtimeABI))
      {
         return false;
      }

      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platforms[i]
      };

      for (size_t j = 0; j < sizeof(commands) / sizeof(ByteCommand); j++) {
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(commands[j], runtime, abi, runtimeABI,
            context, sequence) != x86::LowerError::None)
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target.architecture, writer);
         if (encoder.emit(sequence, abi) != x86::EncodeError::None)
            return false;

         if (commands[j].code == ByteCode::Shl) {
            unsigned char expected[] = { 0xC1, 0xE2, 0x03 };
            if (!equals(code, expected, sizeof(expected)))
               return false;
         }
         else if (commands[j].code == ByteCode::XSaveN) {
            unsigned char expected[] = {
               0xC7, 0x03, 0xFF, 0xFF, 0xFF, 0xFF
            };
            if (!equals(code, expected, sizeof(expected)))
               return false;
         }
         else if (commands[j].code == ByteCode::MulN) {
            unsigned char expected[] = {
               0x69, 0xD2, 0xFD, 0xFF, 0xFF, 0xFF
            };
            if (!equals(code, expected, sizeof(expected)))
               return false;
         }
         else if (commands[j].code == ByteCode::MovM) {
            unsigned char expected[] = { 0xBA, 0, 0, 0, 0 };
            if (!equals(code, expected, sizeof(expected))
               || encoder.relocationCount() != 1)
            {
               return false;
            }

            x86::Relocation& relocation = encoder.relocation(0);
            if (relocation.kind != x86::RelocationKind::ModuleMessage
               || relocation.position != 1
               || relocation.value != (unsigned int)commands[j].arg1)
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::SetR) {
            if (encoder.relocationCount() != 1
               || encoder.relocation(0).kind
                  != x86::RelocationKind::ModuleReferenceValue
               || encoder.relocation(0).value
                  != (unsigned int)commands[j].arg1)
            {
               return false;
            }
         }
      }
   }

   return true;
}

static bool testX86RuntimeDataAndReferences()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64
   };
   ByteCommand commands[] = {
      ByteCommand(ByteCode::MovEnv),
      ByteCommand(ByteCode::LoadV),
      ByteCommand(ByteCode::XCmp),
      ByteCommand(ByteCode::PeekR, 0x1234),
      ByteCommand(ByteCode::StoreR, 0x1234)
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      x86::RuntimeCallABI runtimeABI = {};
      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, runtimeABI))
      {
         return false;
      }

      x86::LoweringContext context = {
         .frameOffset = 0,
         .stackOffset = 0,
         .vmtSize = 0,
         .platform = platform
      };

      for (ByteCommand command : commands) {
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(command, runtime, abi, runtimeABI,
            context, sequence) != x86::LowerError::None)
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target.architecture, writer);
         if (encoder.emit(sequence, abi) != x86::EncodeError::None)
            return false;

         if (command.code == ByteCode::MovEnv) {
            unsigned char x86Bytes[] = { 0xBA, 0, 0, 0, 0 };
            unsigned char amd64Bytes[] = {
               0x48, 0xBA, 0, 0, 0, 0, 0, 0, 0, 0
            };
            if (sequence.count() != 1
               || sequence.instruction(0).opcode
                  != x86::Opcode::MoveRuntimeData
               || encoder.relocationCount() != 1
               || encoder.relocation(0).kind
                  != x86::RelocationKind::RuntimeData
               || encoder.relocation(0).value != (unsigned int)
                  RuntimeDataReference::SystemEnvironment
               || (target.architecture == Architecture::X86
                  ? !equals(code, x86Bytes, sizeof(x86Bytes))
                  : !equals(code, amd64Bytes, sizeof(amd64Bytes))))
            {
               return false;
            }
         }
         else if (command.code == ByteCode::LoadV) {
            unsigned char expected[] = {
               0x81, 0xE2, 0x1F, 0x00, 0x00, 0x00,
               0x8B, 0x4B, 0x00,
               0x81, 0xE1, 0xE0, 0xFF, 0xFF, 0xFF,
               0x09, 0xCA
            };
            if (sequence.count() != 4
               || sequence.instruction(0).opcode
                  != x86::Opcode::AndImmediate
               || sequence.instruction(1).opcode
                  != (target.architecture == Architecture::X86
                     ? x86::Opcode::LoadOffset
                     : x86::Opcode::LoadDWordOffset)
               || sequence.instruction(2).opcode
                  != x86::Opcode::AndImmediate
               || sequence.instruction(3).opcode != x86::Opcode::Or
               || encoder.relocationCount() != 0
               || !equals(code, expected, sizeof(expected)))
            {
               return false;
            }
         }
         else if (command.code == ByteCode::XCmp) {
            unsigned char expected[] = {
               0x8B, 0x4B, 0x00,
               0x39, 0xCA
            };
            if (sequence.count() != 2
               || sequence.instruction(1).opcode != x86::Opcode::Compare
               || encoder.relocationCount() != 0
               || !equals(code, expected, sizeof(expected)))
            {
               return false;
            }
         }
         else {
            bool peek = command.code == ByteCode::PeekR;
            unsigned char x86Peek[] = {
               0xB8, 0, 0, 0, 0,
               0x8B, 0x58, 0x00
            };
            unsigned char x86Store[] = {
               0xB8, 0, 0, 0, 0,
               0x89, 0x58, 0x00
            };
            unsigned char amd64Peek[] = {
               0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
               0x48, 0x8B, 0x58, 0x00
            };
            unsigned char amd64Store[] = {
               0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
               0x48, 0x89, 0x58, 0x00
            };
            bool validBytes = target.architecture == Architecture::X86
               ? peek
                  ? equals(code, x86Peek, sizeof(x86Peek))
                  : equals(code, x86Store, sizeof(x86Store))
               : peek
                  ? equals(code, amd64Peek, sizeof(amd64Peek))
                  : equals(code, amd64Store, sizeof(amd64Store));
            if (sequence.count() != 2
               || sequence.instruction(0).opcode
                  != x86::Opcode::MoveReferenceValue
               || sequence.instruction(1).opcode != (peek
                  ? x86::Opcode::LoadOffset : x86::Opcode::StoreOffset)
               || encoder.relocationCount() != 1
               || encoder.relocation(0).kind
                  != x86::RelocationKind::ModuleReferenceValue
               || encoder.relocation(0).value != (unsigned int)command.arg1
               || !validBytes)
            {
               return false;
            }
         }
      }
   }

   return true;
}

static bool testX86ControlTransfers()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64
   };
   ByteCode codes[] = {
      ByteCode::SNop,
      ByteCode::Quit,
      ByteCode::XJump,
      ByteCode::XCall,
      ByteCode::XQuit
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(
         RuntimeOperation::AllocateYoung, runtime, abi, runtimeABI))
      {
         return false;
      }

      for (ByteCode code : codes) {
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(
               ByteCommand(code), runtime, abi, runtimeABI, sequence)
            != x86::LowerError::None)
         {
            return false;
         }

         TestMemory memory;
         MemoryWriter writer(&memory);
         x86::Encoder encoder(target.architecture, writer);
         if (encoder.emit(sequence, abi) != x86::EncodeError::None
            || encoder.relocationCount() != 0)
         {
            return false;
         }

         unsigned char returnBytes[] = { 0xC3 };
         unsigned char x86Jump[] = { 0xFF, 0xE3 };
         unsigned char amd64Jump[] = { 0x48, 0xFF, 0xE3 };
         unsigned char x86Call[] = { 0xFF, 0xD3 };
         unsigned char amd64Call[] = { 0x48, 0xFF, 0xD3 };
         unsigned char x86Exit[] = { 0x89, 0xD0, 0xC3 };
         unsigned char amd64Exit[] = { 0x48, 0x89, 0xD0, 0xC3 };
         bool amd64 = target.architecture == Architecture::AMD64;
         bool valid = code == ByteCode::SNop
            ? memory.length() == 0
            : code == ByteCode::Quit
               ? equals(memory, returnBytes, sizeof(returnBytes))
            : code == ByteCode::XJump
               ? amd64
                  ? equals(memory, amd64Jump, sizeof(amd64Jump))
                  : equals(memory, x86Jump, sizeof(x86Jump))
            : code == ByteCode::XCall
               ? amd64
                  ? equals(memory, amd64Call, sizeof(amd64Call))
                  : equals(memory, x86Call, sizeof(x86Call))
            : amd64
               ? equals(memory, amd64Exit, sizeof(amd64Exit))
               : equals(memory, x86Exit, sizeof(x86Exit));
         if (!valid)
            return false;
      }
   }

   return true;
}

static bool testX86FrameAndStackSlots()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86, TargetPlatform::LinuxAMD64
   };
   ByteCommand commands[] = {
      ByteCommand(ByteCode::SaveDP, -4),
      ByteCommand(ByteCode::StoreFI, -2),
      ByteCommand(ByteCode::SaveSI, 0),
      ByteCommand(ByteCode::SaveSI, 2),
      ByteCommand(ByteCode::StoreSI, 2),
      ByteCommand(ByteCode::XFlushSI, 0),
      ByteCommand(ByteCode::XRefreshSI, 0),
      ByteCommand(ByteCode::PeekFI, -2),
      ByteCommand(ByteCode::PeekSI, 1),
      ByteCommand(ByteCode::LSaveDP, -4),
      ByteCommand(ByteCode::LLoadDP, -4),
      ByteCommand(ByteCode::GetI, -1),
      ByteCommand(ByteCode::XStoreI, 2),
      ByteCommand(ByteCode::LLoadSI, 0),
      ByteCommand(ByteCode::LLoadSI, 2),
      ByteCommand(ByteCode::LoadSI, 0),
      ByteCommand(ByteCode::LoadSI, 2),
      ByteCommand(ByteCode::XLoadArgFI, -2)
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }
      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      x86::RuntimeCallABI runtimeABI = {};
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, runtimeABI))
      {
         return false;
      }
      x86::LoweringContext context = {
         32, 1, target.architecture == Architecture::X86 ? 16 : 32,
         target.platform
      };

      for (size_t j = 0; j < sizeof(commands) / sizeof(ByteCommand); j++) {
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(commands[j], runtime, abi, runtimeABI,
            context, sequence) != x86::LowerError::None)
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target.architecture, writer);
         if (encoder.emit(sequence, abi) != x86::EncodeError::None)
            return false;

         if (commands[j].code == ByteCode::SaveDP) {
            unsigned char x86Bytes[] = { 0x89, 0x55, 0x08 };
            unsigned char amd64Bytes[] = { 0x89, 0x55, 0x0C };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::StoreFI) {
            unsigned char x86Bytes[] = { 0x89, 0x5D, 0x28 };
            unsigned char amd64Bytes[] = { 0x48, 0x89, 0x5D, 0x30 };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::SaveSI) {
            unsigned char x86Cached[] = { 0x89, 0xD6 };
            unsigned char amd64Cached[] = { 0x49, 0x89, 0xD2 };
            unsigned char x86Stack[] = { 0x89, 0x54, 0x24, 0x0C };
            unsigned char amd64Stack[] = {
               0x89, 0xD0, 0x48, 0x89, 0x44, 0x24, 0x18
            };
            bool stackSlot = commands[j].arg1 == 2;
            if (target.architecture == Architecture::X86
               ? stackSlot
                  ? !equals(code, x86Stack, sizeof(x86Stack))
                  : !equals(code, x86Cached, sizeof(x86Cached))
               : stackSlot
                  ? !equals(code, amd64Stack, sizeof(amd64Stack))
                  : !equals(code, amd64Cached, sizeof(amd64Cached)))
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::XFlushSI) {
            unsigned char x86Bytes[] = { 0x89, 0x74, 0x24, 0x04 };
            unsigned char amd64Bytes[] = {
               0x4C, 0x89, 0x54, 0x24, 0x08
            };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::PeekSI) {
            unsigned char x86Bytes[] = { 0x8B, 0x5C, 0x24, 0x08 };
            unsigned char amd64Bytes[] = { 0x4C, 0x89, 0xDB };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::GetI) {
            unsigned char x86Bytes[] = { 0x8B, 0x5B, 0xEC };
            unsigned char amd64Bytes[] = { 0x48, 0x8B, 0x5B, 0xD8 };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::LLoadSI) {
            unsigned char x86Cached[] = { 0x89, 0xF0, 0x31, 0xD2 };
            unsigned char amd64Cached[] = { 0x4C, 0x89, 0xD2 };
            unsigned char x86Stack[] = {
               0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x54, 0x24, 0x10
            };
            unsigned char amd64Stack[] = { 0x48, 0x8B, 0x54, 0x24, 0x18 };
            bool stackSlot = commands[j].arg1 == 2;
            if (target.architecture == Architecture::X86
               ? stackSlot
                  ? !equals(code, x86Stack, sizeof(x86Stack))
                  : !equals(code, x86Cached, sizeof(x86Cached))
               : stackSlot
                  ? !equals(code, amd64Stack, sizeof(amd64Stack))
                  : !equals(code, amd64Cached, sizeof(amd64Cached)))
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::LoadSI) {
            unsigned char x86Cached[] = { 0x89, 0xF2 };
            unsigned char amd64Cached[] = { 0x4C, 0x89, 0xD2 };
            unsigned char x86Stack[] = { 0x8B, 0x54, 0x24, 0x0C };
            unsigned char amd64Stack[] = { 0x48, 0x63, 0x54, 0x24, 0x18 };
            bool stackSlot = commands[j].arg1 == 2;
            if (target.architecture == Architecture::X86
               ? stackSlot
                  ? !equals(code, x86Stack, sizeof(x86Stack))
                  : !equals(code, x86Cached, sizeof(x86Cached))
               : stackSlot
                  ? !equals(code, amd64Stack, sizeof(amd64Stack))
                  : !equals(code, amd64Cached, sizeof(amd64Cached)))
            {
               return false;
            }
         }
         else if (commands[j].code == ByteCode::XLoadArgFI) {
            unsigned char x86Bytes[] = { 0x8B, 0x55, 0x28 };
            unsigned char amd64Bytes[] = { 0x48, 0x8B, 0x55, 0x30 };
            if (target.architecture == Architecture::X86
               ? !equals(code, x86Bytes, sizeof(x86Bytes))
               : !equals(code, amd64Bytes, sizeof(amd64Bytes)))
            {
               return false;
            }
         }
      }
   }

   return true;
}

static bool testX86PermanentAllocation()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86, TargetPlatform::LinuxAMD64
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      for (int mode = 0; mode < 2; mode++) {
         RuntimeSpec runtime = RuntimeProvider::legacy(mode == 0
            ? ThreadingMode::SingleThread : ThreadingMode::MultiThread, target);
         x86::RuntimeCallABI runtimeABI = {};
         if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocatePermanent,
            runtime, abi, runtimeABI))
         {
            return false;
         }

         ByteCommand command(ByteCode::XCreateR, (arg_t)mskVMTRef);
         x86::Sequence sequence;
         if (x86::ECodeLowering::lower(command, runtime, abi, runtimeABI,
               sequence) != x86::LowerError::None
            || sequence.count() != 14
            || sequence.instruction(8).opcode != x86::Opcode::CallRuntime
            || sequence.instruction(8).immediate
               != (int)RuntimeOperation::AllocatePermanent
            || (mode == 0
               ? test(sequence.instruction(8).effects, x86::MIREffect::Synchronize)
               : !test(sequence.instruction(8).effects,
                  x86::MIREffect::Synchronize | x86::MIREffect::ReadTLS
                     | x86::MIREffect::Safepoint
                     | x86::MIREffect::RelocateRoots)))
         {
            return false;
         }

         TestMemory code;
         MemoryWriter writer(&code);
         x86::Encoder encoder(target.architecture, writer);
         if (encoder.emit(sequence, abi, &runtimeABI) != x86::EncodeError::None
            || encoder.relocationCount() != 2
            || encoder.relocation(0).kind
               != x86::RelocationKind::RuntimeCall
            || encoder.relocation(0).value
               != (unsigned int)RuntimeOperation::AllocatePermanent
            || encoder.relocation(1).kind
               != x86::RelocationKind::ModuleReferenceValue
            || encoder.relocation(1).value != (unsigned int)mskVMTRef)
         {
            return false;
         }
      }
   }

   return true;
}

static bool testX86Collections()
{
   TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86, TargetPlatform::LinuxAMD64
   };
   for (size_t i = 0; i < sizeof(platforms) / sizeof(TargetPlatform); i++) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      if (!TargetProvider::get(platforms[i], target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      for (int mode = 0; mode < 2; mode++) {
         RuntimeSpec runtime = RuntimeProvider::legacy(mode == 0
            ? ThreadingMode::SingleThread : ThreadingMode::MultiThread, target);
         x86::RuntimeCallABI runtimeABI = {};
         if (!x86::RuntimeABIProvider::get(RuntimeOperation::Collect,
            runtime, abi, runtimeABI))
         {
            return false;
         }

         for (int full = 0; full < 2; full++) {
            ByteCommand command(ByteCode::System, full + 1);
            x86::LoweringContext context = { 0, 0, 0, target.platform };
            x86::Sequence sequence;
            if (x86::ECodeLowering::lower(command, runtime, abi, runtimeABI, context, sequence)
                  != x86::LowerError::None)
            {
               return false;
            }

            pos_t callIndex = sequence.count();
            for (pos_t j = 0; j < sequence.count(); j++) {
               if (sequence.instruction(j).opcode == x86::Opcode::CallRuntime) {
                  callIndex = j;
                  break;
               }
            }
            if (callIndex == sequence.count()
               || sequence.instruction(callIndex).immediate
                  != (int)RuntimeOperation::Collect
               || (mode == 0
                  ? test(sequence.instruction(callIndex).effects,
                     x86::MIREffect::Synchronize)
                  : !test(sequence.instruction(callIndex).effects,
                     x86::MIREffect::Synchronize | x86::MIREffect::ReadTLS
                        | x86::MIREffect::Safepoint
                        | x86::MIREffect::RelocateRoots)))
            {
               return false;
            }

            TestMemory code;
            MemoryWriter writer(&code);
            x86::Encoder encoder(target.architecture, writer);
            if (encoder.emit(sequence, abi, &runtimeABI)
                  != x86::EncodeError::None
               || encoder.relocationCount() != (pos_t)(mode == 0 ? 1 : 2)
               || encoder.relocation(mode == 0 ? 0 : 1).kind
                  != x86::RelocationKind::RuntimeCall
               || encoder.relocation(mode == 0 ? 0 : 1).value
                  != (unsigned int)RuntimeOperation::Collect
               || (mode != 0
                  && (encoder.relocation(0).kind
                        != x86::RelocationKind::RuntimeData
                     || encoder.relocation(0).value != (unsigned int)
                        RuntimeDataReference::GCDataLock)))
            {
               return false;
            }
         }
      }
   }

   return true;
}

static bool testECodeMetadata()
{
   int valid = 0;
   for (unsigned int i = 0; i <= 0xFF; i++) {
      ECodeInfo info = {};
      if (ECodeProvider::get((ByteCode)i, info))
         valid++;
   }

   ECodeInfo jump = {};
   ECodeInfo redirect = {};
   ECodeInfo call = {};
   ECodeInfo tls = {};

   return valid == 195
      && !ECodeProvider::get(ByteCode::MaxSingleOp, call)
      && !ECodeProvider::get((ByteCode)0xBE, call)
      && !ECodeProvider::get((ByteCode)0xBF, call)
      && !ECodeProvider::get((ByteCode)0xC7, call)
      && !ECodeProvider::get((ByteCode)0xFF, call)
      && ECodeProvider::get(ByteCode::Jump, jump)
      && jump.operandCount == 1
      && jump.has(ECodeProperty::DirectTarget | ECodeProperty::EndsBlock)
      && !jump.has(ECodeProperty::Fallthrough)
      && ECodeProvider::get(ByteCode::Redirect, redirect)
      && redirect.has(ECodeProperty::IndirectTarget | ECodeProperty::Fallthrough)
      && redirect.has(ECodeFeature::Dispatch | ECodeFeature::ControlFlow)
      && ECodeProvider::get(ByteCode::CallMR, call)
      && call.operandCount == 2
      && call.encodedSize == 1 + sizeof(arg_t) * 2
      && call.has(ECodeFeature::Call)
      && ECodeProvider::get(ByteCode::StoreTLS, tls)
      && tls.has(ECodeFeature::TLS | ECodeFeature::Memory);
}

static bool testECodeCFG()
{
   TestMemory memory;
   ECodeProcedure procedure;
   if (!beginProcedure(memory, 0)
      || !writeCommand(memory, ByteCode::Nop)
      || !writeCommand(memory, ByteCode::Jeq, 5)
      || !writeCommand(memory, ByteCode::XRedirectM, 1)
      || !writeCommand(memory, ByteCode::Quit)
      || !endProcedure(memory, 0))
   {
      return false;
   }

   if (ECodeDecoder::decode(&memory, 0, procedure) != ECodeDecodeError::None)
      return false;

   return procedure.bodyOffset() == sizeof(pos_t)
      && procedure.bodyLength() == 12
      && procedure.instructionCount() == 4
      && procedure.blockCount() == 3
      && procedure.edgeCount() == 4
      && procedure.block(0).offset == 0
      && procedure.block(0).instructionCount == 2
      && procedure.block(1).offset == 6
      && procedure.block(2).offset == 11
      && procedure.edge(0).kind == ECodeEdgeKind::Taken
      && procedure.edge(0).targetBlock == 2
      && procedure.edge(1).kind == ECodeEdgeKind::Fallthrough
      && procedure.edge(1).targetBlock == 1
      && procedure.edge(2).kind == ECodeEdgeKind::Indirect
      && procedure.edge(2).targetBlock == INVALID_POS
      && procedure.edge(3).kind == ECodeEdgeKind::Fallthrough
      && procedure.edge(3).targetBlock == 2;
}

static bool testECodeLabelTarget()
{
   constexpr pos_t procedureOffset = 7;
   constexpr pos_t bodyOffset = procedureOffset + sizeof(pos_t);

   TestMemory memory;
   ECodeProcedure procedure;
   if (!beginProcedure(memory, procedureOffset)
      || !writeCommand(memory, ByteCode::XHookDPR, 16,
         (arg_t)((bodyOffset + 10) | mskLabelRef))
      || !writeCommand(memory, ByteCode::Nop)
      || !writeCommand(memory, ByteCode::Quit)
      || !endProcedure(memory, procedureOffset))
   {
      return false;
   }

   if (ECodeDecoder::decode(&memory, procedureOffset, procedure)
      != ECodeDecodeError::None)
   {
      return false;
   }

   return procedure.instruction(0).labelOffset == 10
      && procedure.blockCount() == 2
      && procedure.block(1).offset == 10;
}

static bool testECodeExitEdge()
{
   TestMemory memory;
   ECodeProcedure procedure;
   if (!beginProcedure(memory, 0)
      || !writeCommand(memory, ByteCode::Nop)
      || !endProcedure(memory, 0))
   {
      return false;
   }

   if (ECodeDecoder::decode(&memory, 0, procedure) != ECodeDecodeError::None)
      return false;

   return procedure.blockCount() == 1
      && procedure.edgeCount() == 1
      && procedure.edge(0).sourceBlock == 0
      && procedure.edge(0).targetBlock == INVALID_POS
      && procedure.edge(0).kind == ECodeEdgeKind::Exit;
}

static ECodeDecodeError decodeSingle(ByteCode code, arg_t arg1 = 0, arg_t arg2 = 0)
{
   TestMemory memory;
   ECodeProcedure procedure;
   if (!beginProcedure(memory, 0) || !writeCommand(memory, code, arg1, arg2)
      || !endProcedure(memory, 0))
   {
      return ECodeDecodeError::InvalidSource;
   }

   return ECodeDecoder::decode(&memory, 0, procedure);
}

static bool testECodeDecodeErrors()
{
   TestMemory invalidOpcode;
   ECodeProcedure procedure;
   unsigned char reserved = (unsigned char)ByteCode::MaxSingleOp;
   if (!beginProcedure(invalidOpcode, 0)
      || !invalidOpcode.write(invalidOpcode.length(), &reserved, sizeof(reserved))
      || !endProcedure(invalidOpcode, 0))
   {
      return false;
   }

   TestMemory truncated;
   unsigned char shl = (unsigned char)ByteCode::Shl;
   if (!beginProcedure(truncated, 0)
      || !truncated.write(truncated.length(), &shl, sizeof(shl))
      || !endProcedure(truncated, 0))
   {
      return false;
   }

   TestMemory middleTarget;
   if (!beginProcedure(middleTarget, 0)
      || !writeCommand(middleTarget, ByteCode::Jump, -3)
      || !writeCommand(middleTarget, ByteCode::Quit)
      || !endProcedure(middleTarget, 0))
   {
      return false;
   }

   TestMemory invalidSize;
   pos_t oversized = 1;
   if (!invalidSize.write(0, &oversized, sizeof(oversized)))
      return false;

   TestMemory empty;
   if (!beginProcedure(empty, 0) || !endProcedure(empty, 0))
      return false;

   return ECodeDecoder::decode(nullptr, 0, procedure) == ECodeDecodeError::InvalidSource
      && ECodeDecoder::decode(&empty, 1, procedure) == ECodeDecodeError::MissingHeader
      && ECodeDecoder::decode(&invalidSize, 0, procedure)
         == ECodeDecodeError::InvalidProcedureSize
      && ECodeDecoder::decode(&empty, 0, procedure) == ECodeDecodeError::EmptyProcedure
      && ECodeDecoder::decode(&invalidOpcode, 0, procedure)
         == ECodeDecodeError::InvalidOpcode
      && ECodeDecoder::decode(&truncated, 0, procedure)
         == ECodeDecodeError::TruncatedInstruction
      && decodeSingle(ByteCode::Jump, 100) == ECodeDecodeError::InvalidBranchTarget
      && ECodeDecoder::decode(&middleTarget, 0, procedure)
         == ECodeDecodeError::BranchTargetNotInstruction
      && decodeSingle(ByteCode::Nop) == ECodeDecodeError::None
      && decodeSingle(ByteCode::XHookDPR, 0, (arg_t)mskLabelRef)
         == ECodeDecodeError::InvalidLabelTarget;
}

static bool hasDispatchAction(const DispatchSpec& spec, DispatchAction action)
{
   for (unsigned int i = 0; i < spec.actionCount; i++) {
      if (spec.actions[i] == action)
         return true;
   }

   return false;
}

static bool testDispatchContract()
{
   DispatchSpec fixedDirect = {};
   DispatchSpec variadicDirect = {};
   DispatchSpec receiverLists = {};
   DispatchSpec variadicReceiverLists = {};
   DispatchSpec virtualTarget = {};
   DispatchSpec alternativeVirtual = {};
   DispatchSpec invalid = {};

   mssg_t fixedMessage = encodeMessage(7, 3, 0);
   mssg_t variadicMessage = encodeMessage(9, 2,
      FUNCTION_MESSAGE | VARIADIC_MESSAGE);

   if (!DispatchProvider::get(
         ByteCommand(ByteCode::XDispatchMR, fixedMessage, 0x1234), false,
         fixedDirect)
      || !DispatchProvider::get(
         ByteCommand(ByteCode::XDispatchMR, variadicMessage, 0x5678), false,
         variadicDirect)
      || !DispatchProvider::get(
         ByteCommand(ByteCode::XDispatchMR, fixedMessage, 0), false,
         receiverLists)
      || !DispatchProvider::get(
         ByteCommand(ByteCode::XDispatchMR, variadicMessage, 0), false,
         variadicReceiverLists)
      || !DispatchProvider::get(
         ByteCommand(ByteCode::DispatchMR, fixedMessage, 0x1234), false,
         virtualTarget)
      || !DispatchProvider::get(
         ByteCommand(ByteCode::DispatchMR, fixedMessage, 0x1234), true,
         alternativeVirtual)
      || DispatchProvider::get(ByteCommand(ByteCode::Load), false, invalid))
   {
      return false;
   }

   DispatchControlFlow fixedControl = {};
   DispatchControlFlow variadicControl = {};
   DispatchControlFlow receiverControl = {};
   DispatchControlFlow variadicReceiverControl = {};
   DispatchControlFlow virtualControl = {};
   DispatchControlFlow alternativeVirtualControl = {};
   DispatchFrameLayout fixedFrame = {};
   DispatchFrameLayout variadicFrame = {};
   DispatchFrameLayout receiverFrame = {};
   DispatchFrameLayout variadicReceiverFrame = {};
   if (!DispatchProvider::buildControlFlow(fixedDirect, fixedControl)
      || !DispatchProvider::buildControlFlow(variadicDirect, variadicControl)
      || !DispatchProvider::buildControlFlow(receiverLists, receiverControl)
      || !DispatchProvider::buildControlFlow(variadicReceiverLists,
         variadicReceiverControl)
      || !DispatchProvider::buildControlFlow(virtualTarget, virtualControl)
      || !DispatchProvider::buildControlFlow(alternativeVirtual,
         alternativeVirtualControl)
      || !DispatchProvider::buildFrameLayout(fixedDirect, fixedFrame)
      || !DispatchProvider::buildFrameLayout(variadicDirect, variadicFrame)
      || !DispatchProvider::buildFrameLayout(receiverLists, receiverFrame)
      || !DispatchProvider::buildFrameLayout(variadicReceiverLists,
         variadicReceiverFrame))
   {
      return false;
   }

   EIRFunction fixedEIR;
   EIRFunction variadicEIR;
   EIRFunction receiverEIR;
   EIRFunction variadicReceiverEIR;
   EIRFunction virtualEIR;
   EIRFunction alternativeVirtualEIR;
   if (DispatchEIRProvider::lower(fixedDirect, fixedEIR)
         != EIRVerifyError::None
      || DispatchEIRProvider::lower(variadicDirect, variadicEIR)
         != EIRVerifyError::None
      || DispatchEIRProvider::lower(receiverLists, receiverEIR)
         != EIRVerifyError::None
      || DispatchEIRProvider::lower(variadicReceiverLists,
         variadicReceiverEIR) != EIRVerifyError::None
      || DispatchEIRProvider::lower(virtualTarget, virtualEIR)
         != EIRVerifyError::None
      || DispatchEIRProvider::lower(alternativeVirtual,
         alternativeVirtualEIR) != EIRVerifyError::None)
   {
      return false;
   }

   unsigned char sentinelBlockId = variadicControl.blockId(
      DispatchPhase::TestArgumentSentinel);
   unsigned char fallthroughBlockId = variadicControl.blockId(
      DispatchPhase::Fallthrough);
   EIRBlock& sentinelBlock = variadicEIR.block(sentinelBlockId);
   EIRBlock& fallthroughBlock = variadicEIR.block(fallthroughBlockId);
   EIRInstruction& sentinelTerminator = variadicEIR.instruction(
      sentinelBlock.firstInstruction + sentinelBlock.instructionCount - 1);
   EIRInstruction& fallthroughTerminator = variadicEIR.instruction(
      fallthroughBlock.firstInstruction
         + fallthroughBlock.instructionCount - 1);
   EIRBlock& virtualSlotBlock = virtualEIR.block(virtualControl.blockId(
      DispatchPhase::ResolveTarget));
   EIRBlock& virtualVMTBlock = virtualEIR.block(virtualControl.blockId(
      DispatchPhase::LoadReceiverVMT));
   EIRBlock& virtualTargetBlock = virtualEIR.block(virtualControl.blockId(
      DispatchPhase::ResolveVirtualTarget));
   EIRInstruction& virtualSlot = virtualEIR.instruction(
      virtualSlotBlock.firstInstruction);
   EIRInstruction& virtualVMT = virtualEIR.instruction(
      virtualVMTBlock.firstInstruction);
   EIRInstruction& resolvedVirtualTarget = virtualEIR.instruction(
      virtualTargetBlock.firstInstruction);

   return fixedDirect.options == DispatchOption::None
      && fixedDirect.message == fixedMessage
      && fixedDirect.listReference == 0x1234
      && fixedDirect.firstArgument == 1
      && fixedDirect.fixedArgumentCount == 3
      && !hasDispatchAction(fixedDirect, DispatchAction::CountArguments)
      && hasDispatchAction(fixedDirect, DispatchAction::ResolveDirectTarget)
      && variadicDirect.has(DispatchOption::Variadic)
      && variadicDirect.firstArgument == 0
      && variadicDirect.fixedArgumentCount == 3
      && hasDispatchAction(variadicDirect, DispatchAction::CountArguments)
      && receiverLists.has(DispatchOption::ReceiverLists)
      && receiverLists.listReference == 0
      && receiverLists.firstArgument == 0
      && hasDispatchAction(receiverLists, DispatchAction::SelectList)
      && fixedControl.blockCount == 16
      && variadicControl.blockCount == 19
      && receiverControl.blockCount == 18
      && variadicReceiverControl.blockCount == 21
      && virtualControl.blockCount == 18
      && alternativeVirtualControl.blockCount == 19
      && fixedFrame.slotCount == 1
      && variadicFrame.slotCount == 3
      && receiverFrame.slotCount == 2
      && variadicReceiverFrame.slotCount == 4
      && variadicReceiverFrame[DispatchFrameSlot::Object] == 0
      && variadicReceiverFrame[DispatchFrameSlot::ListIndex] == 1
      && variadicReceiverFrame[DispatchFrameSlot::SignatureCursor] == 2
      && variadicReceiverFrame[DispatchFrameSlot::ArgumentCount] == 3
      && sentinelBlockId != DispatchControlFlow::InvalidBlock
      && sentinelBlock.instructionCount == 2
      && sentinelTerminator.opcode == EIROpcode::ConditionalBranch
      && fallthroughBlockId != DispatchControlFlow::InvalidBlock
      && fallthroughTerminator.opcode == EIROpcode::Fallthrough
      && virtualSlot.result.type == EIRType::Word
      && virtualVMT.result.type == EIRType::VMT
      && resolvedVirtualTarget.result.type == EIRType::Pointer
      && resolvedVirtualTarget.operandCount == 3
      && receiverControl.blocks[receiverControl.blockId(
         DispatchPhase::SelectList)].has(
            DispatchBlockProperty::Conditional)
      && receiverControl.blocks[receiverControl.blockId(
         DispatchPhase::BranchTarget)].has(DispatchBlockProperty::Terminal)
      && fixedEIR.blockCount() == fixedControl.blockCount
      && variadicEIR.blockCount() == variadicControl.blockCount
      && receiverEIR.blockCount() == receiverControl.blockCount
      && variadicReceiverEIR.blockCount()
         == variadicReceiverControl.blockCount
      && alternativeVirtual.has(DispatchOption::VirtualTarget)
      && alternativeVirtual.has(DispatchOption::AlternativeVMT)
      && hasDispatchAction(alternativeVirtual,
         DispatchAction::ResolveAlternativeVMT)
      && hasDispatchAction(alternativeVirtual,
         DispatchAction::ResolveVirtualTarget)
      && !hasDispatchAction(alternativeVirtual,
         DispatchAction::ResolveDirectTarget);
}

static bool testX86FixedDirectDispatch()
{
   const TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64
   };

   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      x86::RuntimeCallABI callABI = {};
      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, callABI))
      {
         return false;
      }

      mssg_t message = encodeMessage(11, 3, 0);
      ByteCommand command(ByteCode::XDispatchMR, message, 0x1234);
      x86::LoweringContext context = { 0, 3, 0, platform };
      x86::Sequence sequence;
      if (x86::ECodeLowering::lower(command, runtime, abi, callABI,
         context, sequence) != x86::LowerError::None)
      {
         return false;
      }

      bool indirectJump = false;
      bool indexedLoad = false;
      bool locatedArguments = false;

      for (pos_t i = 0; i < sequence.count(); i++) {
         x86::Instruction& instruction = sequence.instruction(i);

         indirectJump |= instruction.opcode == x86::Opcode::JumpRegister;
         indexedLoad |= instruction.opcode == x86::Opcode::LoadMemory
            && instruction.index != x86::Register::None;

         if (instruction.opcode == x86::Opcode::AddressOffsetFrom) {
            locatedArguments = instruction.immediate
               == runtime.objectLayout.fieldSize;
         }
      }

      if (!indirectJump || !indexedLoad || !locatedArguments)
         return false;

      TestMemory memory;
      MemoryWriter writer(&memory);
      x86::Encoder encoder(target, writer);
      if (encoder.emit(sequence, abi) != x86::EncodeError::None
         || memory.length() == 0 || encoder.relocationCount() != 6)
      {
         return false;
      }

      unsigned int references = 0;
      unsigned int metadata = 0;
      unsigned int constants = 0;
      unsigned int messages = 0;
      for (pos_t i = 0; i < encoder.relocationCount(); i++) {
         switch (encoder.relocation(i).kind) {
            case MachineRelocationKind::ModuleReference:
               references++;
               break;
            case MachineRelocationKind::Metadata:
               metadata++;
               break;
            case MachineRelocationKind::RuntimeConstant:
               constants++;
               break;
            case MachineRelocationKind::ModuleMessage:
               messages++;
               break;
            default:
               return false;
         }
      }
      if (references != 3 || metadata != 1 || constants != 1
         || messages != 1)
      {
         return false;
      }

      const unsigned char jump[] = { 0xFF, 0xE0 };
      if (!contains(memory, jump, sizeof(jump)))
         return false;

      ByteCommand variadic(ByteCode::XDispatchMR,
         encodeMessage(12, 1, FUNCTION_MESSAGE | VARIADIC_MESSAGE), 0x5678);
      x86::Sequence variadicSequence;
      if (x86::ECodeLowering::lower(variadic, runtime, abi, callABI,
         context, variadicSequence) != x86::LowerError::None)
      {
         return false;
      }

      bool comparesCount = false;
      for (pos_t i = 0; i < variadicSequence.count(); i++) {
         comparesCount |= variadicSequence.instruction(i).opcode
            == x86::Opcode::CompareMemory;
      }

      TestMemory variadicMemory;
      MemoryWriter variadicWriter(&variadicMemory);
      x86::Encoder variadicEncoder(target, variadicWriter);
      if (!comparesCount
         || variadicEncoder.emit(variadicSequence, abi)
            != x86::EncodeError::None
         || variadicEncoder.relocationCount() != 6
         || !contains(variadicMemory, jump, sizeof(jump)))
      {
         return false;
      }

      ByteCommand receiver(ByteCode::XDispatchMR,
         encodeMessage(13, 3, 0), 0);
      x86::Sequence receiverSequence;
      if (x86::ECodeLowering::lower(receiver, runtime, abi, callABI,
         context, receiverSequence) != x86::LowerError::None)
      {
         return false;
      }

      unsigned int receiverIndexedLoads = 0;
      bool receiverStartsWithObject = false;
      for (pos_t i = 0; i < receiverSequence.count(); i++) {
         x86::Instruction& instruction = receiverSequence.instruction(i);
         if (instruction.opcode == x86::Opcode::AddressOffsetFrom) {
            receiverStartsWithObject = instruction.immediate == 0;
         }
         if (instruction.opcode == x86::Opcode::LoadMemory
            && instruction.index != x86::Register::None)
         {
            receiverIndexedLoads++;
         }
      }

      TestMemory receiverMemory;
      MemoryWriter receiverWriter(&receiverMemory);
      x86::Encoder receiverEncoder(target, receiverWriter);
      if (!receiverStartsWithObject
         || receiverIndexedLoads < 6
         || receiverEncoder.emit(receiverSequence, abi)
            != x86::EncodeError::None
         || receiverEncoder.relocationCount() != 3
         || !contains(receiverMemory, jump, sizeof(jump)))
      {
         return false;
      }

      unsigned int receiverReferences = 0;
      unsigned int receiverMetadata = 0;
      unsigned int receiverConstants = 0;
      unsigned int receiverMessages = 0;
      for (pos_t i = 0; i < receiverEncoder.relocationCount(); i++) {
         switch (receiverEncoder.relocation(i).kind) {
            case MachineRelocationKind::ModuleReference:
               receiverReferences++;
               break;
            case MachineRelocationKind::Metadata:
               receiverMetadata++;
               break;
            case MachineRelocationKind::RuntimeConstant:
               receiverConstants++;
               break;
            case MachineRelocationKind::ModuleMessage:
               receiverMessages++;
               break;
            default:
               return false;
         }
      }
      if (receiverReferences != 0 || receiverMetadata != 1
         || receiverConstants != 1 || receiverMessages != 1)
      {
         return false;
      }

      ByteCommand variadicReceiver(ByteCode::XDispatchMR,
         encodeMessage(14, 1, FUNCTION_MESSAGE | VARIADIC_MESSAGE), 0);
      x86::Sequence variadicReceiverSequence;
      if (x86::ECodeLowering::lower(variadicReceiver, runtime, abi, callABI,
         context, variadicReceiverSequence) != x86::LowerError::None)
      {
         return false;
      }

      bool receiverComparesCount = false;
      for (pos_t i = 0; i < variadicReceiverSequence.count(); i++) {
         receiverComparesCount |= variadicReceiverSequence.instruction(i).opcode
            == x86::Opcode::CompareMemory;
      }

      TestMemory variadicReceiverMemory;
      MemoryWriter variadicReceiverWriter(&variadicReceiverMemory);
      x86::Encoder variadicReceiverEncoder(target, variadicReceiverWriter);
      if (!receiverComparesCount
         || variadicReceiverEncoder.emit(variadicReceiverSequence, abi)
            != x86::EncodeError::None
         || variadicReceiverEncoder.relocationCount() != 3
         || !contains(variadicReceiverMemory, jump, sizeof(jump)))
      {
         return false;
      }

      ByteCommand virtualCommands[] = {
         ByteCommand(ByteCode::DispatchMR,
            encodeMessage(15, 3, 0), 0x3456),
         ByteCommand(ByteCode::DispatchMR,
            encodeMessage(16, 1, FUNCTION_MESSAGE | VARIADIC_MESSAGE),
            0x4567),
         ByteCommand(ByteCode::DispatchMR,
            encodeMessage(17, 3, 0), 0x5678),
         ByteCommand(ByteCode::DispatchMR,
            encodeMessage(18, 1, FUNCTION_MESSAGE | VARIADIC_MESSAGE),
            0x6789)
      };
      bool alternativeModes[] = { false, false, true, true };
      bool variadicModes[] = { false, true, false, true };
      for (unsigned int j = 0;
         j < sizeof(virtualCommands) / sizeof(virtualCommands[0]); j++)
      {
         context.alternativeMode = alternativeModes[j];
         x86::Sequence virtualSequence;
         if (x86::ECodeLowering::lower(virtualCommands[j], runtime, abi,
            callABI, context, virtualSequence) != x86::LowerError::None)
         {
            return false;
         }

         bool resolvesVirtualTarget = false;
         bool selectsAlternativeVMT = false;
         bool countsVirtualArguments = false;
         for (pos_t k = 0; k < virtualSequence.count(); k++) {
            x86::Instruction& instruction = virtualSequence.instruction(k);
            resolvesVirtualTarget |= instruction.opcode
                  == x86::Opcode::LoadMemory
               && instruction.destination.reg == x86::Register::A
               && instruction.source.reg == x86::Register::C
               && instruction.index == x86::Register::A
               && instruction.immediate == runtime.vmtLayout.methodOffset;
            selectsAlternativeVMT |= instruction.opcode == x86::Opcode::Add;
            countsVirtualArguments |= instruction.opcode
               == x86::Opcode::CompareMemory;
         }

         TestMemory virtualMemory;
         MemoryWriter virtualWriter(&virtualMemory);
         x86::Encoder virtualEncoder(target, virtualWriter);
         if (!resolvesVirtualTarget
            || selectsAlternativeVMT != alternativeModes[j]
            || countsVirtualArguments != variadicModes[j]
            || virtualEncoder.emit(virtualSequence, abi)
               != x86::EncodeError::None
            || virtualEncoder.relocationCount() != 6
            || !contains(virtualMemory, jump, sizeof(jump)))
         {
            return false;
         }
      }
      context.alternativeMode = false;
   }

   return true;
}

static bool testVirtualMethodTransfer()
{
   mssg_t messages[] = {
      encodeMessage(19, 2, 0),
      encodeMessage(20, 1, FUNCTION_MESSAGE)
   };
   ByteCode codes[] = { ByteCode::VCallMR, ByteCode::VJumpMR };
   bool alternativeModes[] = { false, true };

   for (unsigned int codeIndex = 0;
      codeIndex < sizeof(codes) / sizeof(codes[0]); codeIndex++)
   {
      for (bool alternativeMode : alternativeModes) {
         ByteCommand command(codes[codeIndex], messages[codeIndex], 0x1234);
         VirtualMethodSpec method = {};
         if (!VirtualMethodProvider::get(command, alternativeMode, method))
            return false;

         EIRFunction function;
         if (VirtualMethodEIRProvider::lower(method, function)
               != EIRVerifyError::None
            || function.blockCount() != 1)
         {
            return false;
         }

         bool loadsVMT = false;
         bool loadsMethodOffset = false;
         bool selectsAlternativeVMT = false;
         bool resolvesMethod = false;
         bool calls = false;
         bool jumps = false;
         for (pos_t i = 0; i < function.instructionCount(); i++) {
            EIRInstruction& instruction = function.instruction(i);
            loadsVMT |= instruction.opcode == EIROpcode::ObjectVMT;
            loadsMethodOffset |= instruction.opcode == EIROpcode::MethodOffset;
            selectsAlternativeVMT |= instruction.opcode
               == EIROpcode::SelectAlternativeVMT;
            resolvesMethod |= instruction.opcode
               == EIROpcode::ResolveVirtualMethod;
            calls |= instruction.opcode == EIROpcode::CallIndirect;
            jumps |= instruction.opcode == EIROpcode::IndirectBranch;
         }
         if (!loadsVMT || !loadsMethodOffset || !resolvesMethod
            || selectsAlternativeVMT != alternativeMode
            || calls != (codes[codeIndex] == ByteCode::VCallMR)
            || jumps != (codes[codeIndex] == ByteCode::VJumpMR))
         {
            return false;
         }
      }
   }

   VirtualMethodSpec invalid = {};
   if (VirtualMethodProvider::get(
      ByteCommand(ByteCode::Load, 1, 1), false, invalid))
   {
      return false;
   }

   const TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64
   };
   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      x86::RuntimeCallABI callABI = {};
      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, callABI))
      {
         return false;
      }

      for (unsigned int codeIndex = 0;
         codeIndex < sizeof(codes) / sizeof(codes[0]); codeIndex++)
      {
         for (bool alternativeMode : alternativeModes) {
            ByteCommand command(codes[codeIndex], messages[codeIndex], 0x1234);
            x86::LoweringContext context = { 0, 0, 0, platform,
               alternativeMode };
            x86::Sequence sequence;
            if (x86::ECodeLowering::lower(command, runtime, abi, callABI,
               context, sequence) != x86::LowerError::None)
            {
               return false;
            }

            bool methodAddressField = false;
            bool selectsAlternativeVMT = false;
            bool calls = false;
            bool jumps = false;
            for (pos_t i = 0; i < sequence.count(); i++) {
               x86::Instruction& instruction = sequence.instruction(i);
               methodAddressField |= instruction.opcode
                     == x86::Opcode::LoadMemory
                  && instruction.destination.reg == x86::Register::A
                  && instruction.source.reg == x86::Register::A
                  && instruction.index == x86::Register::C
                  && instruction.immediate == runtime.vmtLayout.methodOffset;
               selectsAlternativeVMT |= instruction.opcode == x86::Opcode::Add;
               calls |= instruction.opcode == x86::Opcode::CallRegister;
               jumps |= instruction.opcode == x86::Opcode::JumpRegister;
            }
            if (!methodAddressField
               || selectsAlternativeVMT != alternativeMode
               || calls != (codes[codeIndex] == ByteCode::VCallMR)
               || jumps != (codes[codeIndex] == ByteCode::VJumpMR))
            {
               return false;
            }

            TestMemory memory;
            MemoryWriter writer(&memory);
            x86::Encoder encoder(target, writer);
            MachineRelocationKind expectedRelocation = alternativeMode
               ? MachineRelocationKind::HMTMethodOffset
               : MachineRelocationKind::VMTMethodOffset;
            const unsigned char call[] = { 0xFF, 0xD0 };
            const unsigned char jump[] = { 0xFF, 0xE0 };
            if (encoder.emit(sequence, abi) != x86::EncodeError::None
               || encoder.relocationCount() != 1
               || encoder.relocation(0).kind != expectedRelocation
               || encoder.relocation(0).value != messages[codeIndex]
               || (codes[codeIndex] == ByteCode::VCallMR
                  ? !contains(memory, call, sizeof(call))
                  : !contains(memory, jump, sizeof(jump))))
            {
               return false;
            }
         }
      }
   }

   return true;
}

static bool testManagedMethodTransfer()
{
   mssg_t callMessage = encodeMessage(21, 2, 0);
   mssg_t jumpMessage = encodeMessage(22, 1, FUNCTION_MESSAGE);
   ByteCommand commands[] = {
      ByteCommand(ByteCode::CallR, 0x01001234),
      ByteCommand(ByteCode::CallVI, 3),
      ByteCommand(ByteCode::JumpVI, 2),
      ByteCommand(ByteCode::CallMR, callMessage, 0x04005678),
      ByteCommand(ByteCode::JumpMR, jumpMessage, 0x04006789)
   };

   for (ByteCommand command : commands) {
      ManagedMethodSpec method = {};
      if (!ManagedMethodProvider::get(command, false, method))
         return false;

      EIRFunction function;
      if (ManagedMethodEIRProvider::lower(method, function)
            != EIRVerifyError::None
         || function.blockCount() != 1)
      {
         return false;
      }

      bool direct = false;
      bool indexed = false;
      bool methodAddress = false;
      bool call = false;
      bool jump = false;
      for (pos_t i = 0; i < function.instructionCount(); i++) {
         EIRInstruction& instruction = function.instruction(i);
         direct |= instruction.opcode == EIROpcode::CallDirect;
         indexed |= instruction.opcode == EIROpcode::ResolveVirtualIndex;
         methodAddress |= instruction.opcode == EIROpcode::MethodAddress;
         call |= instruction.opcode == EIROpcode::CallDirect
            || instruction.opcode == EIROpcode::CallIndirect;
         jump |= instruction.opcode == EIROpcode::IndirectBranch;
      }

      ManagedMethodTarget expectedTarget = command.code == ByteCode::CallR
         ? ManagedMethodTarget::Symbol
         : command.code == ByteCode::CallVI
            || command.code == ByteCode::JumpVI
         ? ManagedMethodTarget::VMTIndex
         : ManagedMethodTarget::VMTMethod;
      bool expectedCall = command.code == ByteCode::CallR
         || command.code == ByteCode::CallVI
         || command.code == ByteCode::CallMR;
      if (method.target != expectedTarget
         || direct != (expectedTarget == ManagedMethodTarget::Symbol)
         || indexed != (expectedTarget == ManagedMethodTarget::VMTIndex)
         || methodAddress != (expectedTarget == ManagedMethodTarget::VMTMethod)
         || call != expectedCall
         || jump == expectedCall)
      {
         return false;
      }
   }

   ManagedMethodSpec alternative = {};
   ManagedMethodSpec invalid = {};
   if (!ManagedMethodProvider::get(commands[3], true, alternative)
      || !alternative.has(MethodLookupOption::AlternativeVMT)
      || ManagedMethodProvider::get(
         ByteCommand(ByteCode::Load), false, invalid)
      || ManagedMethodProvider::get(
         ByteCommand(ByteCode::CallVI, -1), false, invalid))
   {
      return false;
   }

   const TargetPlatform platforms[] = {
      TargetPlatform::LinuxX86,
      TargetPlatform::LinuxAMD64
   };
   for (TargetPlatform platform : platforms) {
      TargetSpec target = {};
      x86::ManagedABI abi = {};
      x86::RuntimeCallABI callABI = {};
      if (!TargetProvider::get(platform, target)
         || !x86::ManagedABIProvider::get(target.architecture, abi))
      {
         return false;
      }

      RuntimeSpec runtime = RuntimeProvider::legacy(
         ThreadingMode::SingleThread, target);
      if (!x86::RuntimeABIProvider::get(RuntimeOperation::AllocateYoung,
         runtime, abi, callABI))
      {
         return false;
      }

      for (unsigned int i = 0;
         i < sizeof(commands) / sizeof(commands[0]); i++)
      {
         bool methodTarget = commands[i].code == ByteCode::CallMR
            || commands[i].code == ByteCode::JumpMR;
         unsigned int modeCount = methodTarget ? 2 : 1;
         for (unsigned int mode = 0; mode < modeCount; mode++) {
            bool alternativeMode = mode != 0;
            x86::LoweringContext context = {
               0, 0, 0, platform, alternativeMode
            };
            x86::Sequence sequence;
            if (x86::ECodeLowering::lower(commands[i], runtime, abi,
               callABI, context, sequence) != x86::LowerError::None)
            {
               return false;
            }

            TestMemory memory;
            MemoryWriter writer(&memory);
            x86::Encoder encoder(target, writer);
            if (encoder.emit(sequence, abi) != x86::EncodeError::None)
               return false;

            bool call = commands[i].code == ByteCode::CallR
               || commands[i].code == ByteCode::CallVI
               || commands[i].code == ByteCode::CallMR;
            const unsigned char directCall[] = { 0xE8 };
            const unsigned char directJump[] = { 0xE9 };
            const unsigned char indirectCall[] = { 0xFF, 0xD0 };
            const unsigned char indirectJump[] = { 0xFF, 0xE0 };

            if (commands[i].code == ByteCode::CallR) {
               if (encoder.relocationCount() != 1
                  || encoder.relocation(0).kind
                     != MachineRelocationKind::ModuleCode
                  || encoder.relocation(0).value
                     != (unsigned int)commands[i].arg1
                  || !contains(memory, directCall, sizeof(directCall)))
               {
                  return false;
               }
            }
            else if (commands[i].code == ByteCode::CallVI
               || commands[i].code == ByteCode::JumpVI)
            {
               int expectedOffset = commands[i].arg1
                     * runtime.vmtLayout.entrySize
                  + runtime.vmtLayout.methodOffset;
               bool loadsTarget = false;
               for (pos_t j = 0; j < sequence.count(); j++) {
                  x86::Instruction& instruction = sequence.instruction(j);
                  loadsTarget |= instruction.opcode == x86::Opcode::LoadOffset
                     && instruction.destination.reg == x86::Register::A
                     && instruction.source.reg == x86::Register::A
                     && instruction.immediate == expectedOffset;
               }
               if (!loadsTarget || encoder.relocationCount() != 0
                  || (call
                     ? !contains(memory, indirectCall, sizeof(indirectCall))
                     : !contains(memory, indirectJump, sizeof(indirectJump))))
               {
                  return false;
               }
            }
            else {
               MachineRelocationKind expected = alternativeMode
                  ? MachineRelocationKind::HMTMethodAddress
                  : MachineRelocationKind::VMTMethodAddress;
               if (encoder.relocationCount() != 1
                  || encoder.relocation(0).kind != expected
                  || encoder.relocation(0).value
                     != (unsigned int)commands[i].arg1
                  || (call
                     ? !contains(memory, directCall, sizeof(directCall))
                     : !contains(memory, directJump, sizeof(directJump))))
               {
                  return false;
               }
            }
         }
      }
   }

   return true;
}

int main()
{
   return testTargets()
      && testInvalidTarget()
      && testTargetValidation()
      && testRuntime()
      && testRuntimeValidation()
      && testRuntimeCore(TargetPlatform::WindowsX86, ThreadingMode::SingleThread)
      && testRuntimeCore(TargetPlatform::WindowsX86, ThreadingMode::MultiThread)
      && testRuntimeCore(TargetPlatform::LinuxX86, ThreadingMode::SingleThread)
      && testRuntimeCore(TargetPlatform::LinuxX86, ThreadingMode::MultiThread)
      && testRuntimeCore(TargetPlatform::WindowsAMD64, ThreadingMode::SingleThread)
      && testRuntimeCore(TargetPlatform::WindowsAMD64, ThreadingMode::MultiThread)
      && testRuntimeCore(TargetPlatform::LinuxAMD64, ThreadingMode::SingleThread)
      && testRuntimeCore(TargetPlatform::LinuxAMD64, ThreadingMode::MultiThread)
      && testRuntimeCore(TargetPlatform::FreeBSDAMD64, ThreadingMode::SingleThread)
      && testRuntimeCore(TargetPlatform::FreeBSDAMD64, ThreadingMode::MultiThread)
      && testRuntimeCore(TargetPlatform::MacOSAMD64, ThreadingMode::SingleThread)
      && testRuntimeCore(TargetPlatform::MacOSAMD64, ThreadingMode::MultiThread)
      && testPermanentRuntimeCore(TargetPlatform::WindowsX86)
      && testPermanentRuntimeCore(TargetPlatform::LinuxX86)
      && testPermanentRuntimeCore(TargetPlatform::WindowsAMD64)
      && testPermanentRuntimeCore(TargetPlatform::LinuxAMD64)
      && testPermanentRuntimeCore(TargetPlatform::FreeBSDAMD64)
      && testPermanentRuntimeCore(TargetPlatform::MacOSAMD64)
      && testPrepareRuntimeCore(TargetPlatform::WindowsX86)
      && testPrepareRuntimeCore(TargetPlatform::LinuxX86)
      && testPrepareRuntimeCore(TargetPlatform::WindowsAMD64)
      && testPrepareRuntimeCore(TargetPlatform::LinuxAMD64)
      && testPrepareRuntimeCore(TargetPlatform::FreeBSDAMD64)
      && testPrepareRuntimeCore(TargetPlatform::MacOSAMD64)
      && testCollectRuntimeCore(TargetPlatform::WindowsX86)
      && testCollectRuntimeCore(TargetPlatform::LinuxX86)
      && testCollectRuntimeCore(TargetPlatform::WindowsAMD64)
      && testCollectRuntimeCore(TargetPlatform::LinuxAMD64)
      && testCollectRuntimeCore(TargetPlatform::FreeBSDAMD64)
      && testCollectRuntimeCore(TargetPlatform::MacOSAMD64)
      && testRuntimeCoreProtocol()
      && testWaitRuntimeCore(TargetPlatform::WindowsX86, true)
      && testWaitRuntimeCore(TargetPlatform::LinuxX86, true)
      && testWaitRuntimeCore(TargetPlatform::WindowsAMD64, true)
      && testWaitRuntimeCore(TargetPlatform::LinuxAMD64, true)
      && testWaitRuntimeCore(TargetPlatform::FreeBSDAMD64, true)
      && testWaitRuntimeCore(TargetPlatform::MacOSAMD64, false)
      && testExceptionRuntimeCore(
         TargetPlatform::WindowsX86,
         ThreadingMode::SingleThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::WindowsX86,
         ThreadingMode::MultiThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::LinuxX86,
         ThreadingMode::SingleThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::LinuxX86,
         ThreadingMode::MultiThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::WindowsAMD64,
         ThreadingMode::SingleThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::WindowsAMD64,
         ThreadingMode::MultiThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::LinuxAMD64,
         ThreadingMode::SingleThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::LinuxAMD64,
         ThreadingMode::MultiThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::FreeBSDAMD64,
         ThreadingMode::SingleThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::FreeBSDAMD64,
         ThreadingMode::MultiThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::MacOSAMD64,
         ThreadingMode::SingleThread,
         true)
      && testExceptionRuntimeCore(
         TargetPlatform::MacOSAMD64,
         ThreadingMode::MultiThread,
         false)
      && testX86ABIs()
      && testEIR()
      && testEIRPhi()
      && testEIRVerificationErrors()
      && testEIRSwitch()
      && testX86Encoder()
      && testX86NewIR()
      && testX86DynamicAllocation()
      && testX86PermanentAllocation()
      && testX86Collections()
      && testX86ThreadStartup()
      && testX86SystemStartup()
      && testStackReference()
      && testGenericECodeLowering()
      && testX86ManagedFrames()
      && testX86RootStackAllocation()
      && testX86Copy()
      && testX86ExternalFrames()
      && testX86ExceptionControl()
      && testX86ExceptionHook()
      && testX86GCLock()
      && testX86ObjectLocks()
      && testX86ThreadLocalStorage()
      && testX86SafeRegions()
      && testX86NLen()
      && testX86FillIR()
      && testX86FrameAddressing()
      && testX86ParameterizedScalars()
      && testX86RuntimeDataAndReferences()
      && testX86ControlTransfers()
      && testX86FrameAndStackSlots()
      && testECodeMetadata()
      && testECodeCFG()
      && testECodeLabelTarget()
      && testECodeExitEdge()
      && testECodeDecodeErrors()
      && testDispatchContract()
      && testX86FixedDirectDispatch()
      && testVirtualMethodTransfer()
      && testManagedMethodTransfer()
      ? 0 : 1;
}
