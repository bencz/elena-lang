#include "runtime.h"

using namespace elena_lang;
using namespace elena_lang::codegen;

static constexpr RuntimeCallEffect AllocationEffects = RuntimeCallEffect::ReadHeap
   | RuntimeCallEffect::WriteHeap
   | RuntimeCallEffect::ReadGlobal
   | RuntimeCallEffect::WriteGlobal
   | RuntimeCallEffect::Call
   | RuntimeCallEffect::Allocate
   | RuntimeCallEffect::Safepoint
   | RuntimeCallEffect::MayThrow
   | RuntimeCallEffect::RelocateRoots;

static constexpr RuntimeCallEffect ConcurrentEffects = RuntimeCallEffect::Synchronize
   | RuntimeCallEffect::ReadTLS;

static constexpr RuntimeCallEffect PrepareEffects = RuntimeCallEffect::ReadGlobal
   | RuntimeCallEffect::WriteGlobal
   | RuntimeCallEffect::Call;

static constexpr RuntimeCallEffect WaitForGCEffects = RuntimeCallEffect::ReadGlobal
   | RuntimeCallEffect::WriteGlobal
   | RuntimeCallEffect::Call
   | RuntimeCallEffect::Safepoint
   | RuntimeCallEffect::Synchronize
   | RuntimeCallEffect::ReadTLS
   | RuntimeCallEffect::RelocateRoots;

template <class Field>
static unsigned char byteOffset(unsigned char wordSize, Field field)
{
   return (unsigned char)RuntimeLayout::offsetOf(wordSize, field);
}

template <class Field>
static unsigned short shortOffset(unsigned short wordSize, Field field)
{
   return (unsigned short)RuntimeLayout::offsetOf(wordSize, field);
}

static ObjectLayoutSpec createObjectLayout(unsigned char pointerSize)
{
   return {
      .fieldSize = pointerSize,
      .headerSize = byteOffset(pointerSize, ObjectHeaderField::End),
      .sizeOffset = (unsigned char)RuntimeLayout::ObjectSizeOffset,
      .vmtOffset = byteOffset(pointerSize, ObjectHeaderField::VMT),
      .synchronizationOffset = (unsigned char)(pointerSize == 4
         ? 1
         : pointerSize),
      .allocationAlignment = (unsigned char)(pointerSize == 4 ? 16 : 32),
      .structMask = pointerSize == 4 ? 0x00800000u : 0x40000000u,
      .objectSizeMask = pointerSize == 4 ? 0x00FFFFFFu : 0x7FFFFFFFu
   };
}

static VMTLayoutSpec createVMTLayout(unsigned char pointerSize)
{
   return {
      .sizeOffset = byteOffset(pointerSize, VMTHeaderField::Size),
      .flagsOffset = byteOffset(pointerSize, VMTHeaderField::Flags),
      .parentOffset = byteOffset(pointerSize, VMTHeaderField::Parent),
      .methodOffset = byteOffset(pointerSize, VMTTableField::FirstMethod),
      .entrySize = byteOffset(pointerSize, VMTTableField::EntryEnd)
   };
}

static GCDataLayoutSpec createGCDataLayout(unsigned short wordSize)
{
   return {
      .header = shortOffset(wordSize, GCDataField::Header),
      .start = shortOffset(wordSize, GCDataField::Start),
      .youngStart = shortOffset(wordSize, GCDataField::YoungStart),
      .youngCurrent = shortOffset(wordSize, GCDataField::YoungCurrent),
      .youngEnd = shortOffset(wordSize, GCDataField::YoungEnd),
      .shadow = shortOffset(wordSize, GCDataField::Shadow),
      .shadowEnd = shortOffset(wordSize, GCDataField::ShadowEnd),
      .matureStart = shortOffset(wordSize, GCDataField::MatureStart),
      .matureCurrent = shortOffset(wordSize, GCDataField::MatureCurrent),
      .end = shortOffset(wordSize, GCDataField::End),
      .matureWriteBarrier = shortOffset(wordSize, GCDataField::MatureWriteBarrier),
      .permanentStart = shortOffset(wordSize, GCDataField::PermanentStart),
      .permanentEnd = shortOffset(wordSize, GCDataField::PermanentEnd),
      .permanentCurrent = shortOffset(wordSize, GCDataField::PermanentCurrent),
      .lock = shortOffset(wordSize, GCDataField::Lock),
      .signal = shortOffset(wordSize, GCDataField::Signal),
      .queueSemaphore = shortOffset(wordSize, GCDataField::QueueSemaphore),
      .size = shortOffset(wordSize, GCDataField::TableEnd)
   };
}

static ThreadContentLayoutSpec createThreadContentLayout(unsigned short wordSize)
{
   return {
      .criticalHandler = shortOffset(wordSize, ThreadContentField::CriticalHandler),
      .currentException = shortOffset(wordSize, ThreadContentField::CurrentException),
      .stackFrame = shortOffset(wordSize, ThreadContentField::StackFrame),
      .syncEvent = shortOffset(wordSize, ThreadContentField::SyncEvent),
      .flags = shortOffset(wordSize, ThreadContentField::Flags),
      .stackRoot = shortOffset(wordSize, ThreadContentField::StackRoot),
      .size = shortOffset(wordSize, ThreadContentField::ContentEnd)
   };
}

static ThreadTableLayoutSpec createThreadTableLayout(unsigned short wordSize)
{
   return {
      .count = shortOffset(wordSize, ThreadTableField::Count),
      .slots = shortOffset(wordSize, ThreadTableField::Slots),
      .slotContent = shortOffset(wordSize, ThreadSlotField::Content),
      .slotArgument = shortOffset(wordSize, ThreadSlotField::Argument),
      .slotSize = shortOffset(wordSize, ThreadSlotField::SlotEnd)
   };
}

static SystemEnvironmentLayoutSpec createSystemEnvironmentLayout(unsigned short wordSize)
{
   return {
      .staticRootCount = shortOffset(wordSize, SystemEnvironmentField::StaticRootCount),
      .tlsSize = shortOffset(wordSize, SystemEnvironmentField::TLSSize),
      .gcData = shortOffset(wordSize, SystemEnvironmentField::GCData),
      .singleContent = shortOffset(wordSize, SystemEnvironmentField::SingleContent),
      .threadTable = shortOffset(wordSize, SystemEnvironmentField::ThreadTable),
      .reserved = shortOffset(wordSize, SystemEnvironmentField::Reserved),
      .exceptionHandler = shortOffset(wordSize, SystemEnvironmentField::ExceptionHandler),
      .matureSize = shortOffset(wordSize, SystemEnvironmentField::MatureSize),
      .youngSize = shortOffset(wordSize, SystemEnvironmentField::YoungSize),
      .threadCount = shortOffset(wordSize, SystemEnvironmentField::ThreadCount),
      .serializedSize = shortOffset(wordSize, SystemEnvironmentField::EnvironmentEnd)
   };
}

static RuntimeDataLayoutSpec createRuntimeDataLayout(unsigned short wordSize)
{
   return {
      .gc = createGCDataLayout(wordSize),
      .threadContent = createThreadContentLayout(wordSize),
      .threadTable = createThreadTableLayout(wordSize),
      .environment = createSystemEnvironmentLayout(wordSize)
   };
}

bool ObjectLayoutSpec :: isValid(const TargetSpec& target) const
{
   return target.isValid()
      && fieldSize == target.pointerSize
      && headerSize == RuntimeLayout::offsetOf(
         target.pointerSize, ObjectHeaderField::End)
      && sizeOffset == RuntimeLayout::ObjectSizeOffset
      && vmtOffset == RuntimeLayout::offsetOf(
         target.pointerSize, ObjectHeaderField::VMT)
      && synchronizationOffset == (target.pointerSize == 4
         ? 1
         : target.pointerSize)
      && allocationAlignment == (target.pointerSize == 4 ? 16 : 32)
      && structMask == (target.pointerSize == 4 ? 0x00800000u : 0x40000000u)
      && objectSizeMask == (target.pointerSize == 4 ? 0x00FFFFFFu : 0x7FFFFFFFu);
}

bool ObjectLayoutSpec :: payloadSize(int fieldCount, unsigned int& size) const
{
   if (fieldCount < 0)
      return false;

   unsigned int count = (unsigned int)fieldCount;
   if (fieldSize == 0 || count > objectSizeMask / fieldSize)
      return false;

   size = count * fieldSize;

   return true;
}

bool ObjectLayoutSpec :: allocationSize(int fieldCount, unsigned int& size) const
{
   unsigned int payload = 0;
   if (!payloadSize(fieldCount, payload) || allocationAlignment == 0)
      return false;

   unsigned int raw = payload + headerSize;
   unsigned int mask = allocationAlignment - 1;
   if ((allocationAlignment & mask) != 0 || raw > 0xFFFFFFFFu - mask)
      return false;

   size = (raw + mask) & ~mask;

   return true;
}

bool ObjectLayoutSpec :: binarySize(int byteSize, unsigned int& size) const
{
   unsigned int binaryMask = objectSizeMask & ~structMask;
   if (byteSize < 0 || (unsigned int)byteSize > binaryMask)
      return false;

   size = (unsigned int)byteSize | structMask;

   return true;
}

bool ObjectLayoutSpec :: binaryAllocationSize(int byteSize, unsigned int& size) const
{
   unsigned int taggedSize = 0;
   if (!binarySize(byteSize, taggedSize) || allocationAlignment == 0)
      return false;

   unsigned int raw = (unsigned int)byteSize + headerSize;
   unsigned int mask = allocationAlignment - 1;
   if ((allocationAlignment & mask) != 0 || raw > 0xFFFFFFFFu - mask)
      return false;

   size = (raw + mask) & ~mask;

   return true;
}

bool VMTLayoutSpec :: isValid(const TargetSpec& target) const
{
   unsigned char word = target.pointerSize;

   return target.isValid()
      && sizeOffset == RuntimeLayout::offsetOf(word, VMTHeaderField::Size)
      && flagsOffset == RuntimeLayout::offsetOf(word, VMTHeaderField::Flags)
      && parentOffset == RuntimeLayout::offsetOf(word, VMTHeaderField::Parent)
      && methodOffset == RuntimeLayout::offsetOf(word,
         VMTTableField::FirstMethod)
      && entrySize == RuntimeLayout::offsetOf(word, VMTTableField::EntryEnd);
}

bool GCDataLayoutSpec :: isValid(const TargetSpec& target) const
{
   unsigned short word = target.pointerSize;

   return header == RuntimeLayout::offsetOf(word, GCDataField::Header)
      && start == RuntimeLayout::offsetOf(word, GCDataField::Start)
      && youngStart == RuntimeLayout::offsetOf(word, GCDataField::YoungStart)
      && youngCurrent == RuntimeLayout::offsetOf(word,
         GCDataField::YoungCurrent)
      && youngEnd == RuntimeLayout::offsetOf(word, GCDataField::YoungEnd)
      && shadow == RuntimeLayout::offsetOf(word, GCDataField::Shadow)
      && shadowEnd == RuntimeLayout::offsetOf(word, GCDataField::ShadowEnd)
      && matureStart == RuntimeLayout::offsetOf(word, GCDataField::MatureStart)
      && matureCurrent == RuntimeLayout::offsetOf(word,
         GCDataField::MatureCurrent)
      && end == RuntimeLayout::offsetOf(word, GCDataField::End)
      && matureWriteBarrier == RuntimeLayout::offsetOf(word,
         GCDataField::MatureWriteBarrier)
      && permanentStart == RuntimeLayout::offsetOf(word,
         GCDataField::PermanentStart)
      && permanentEnd == RuntimeLayout::offsetOf(word,
         GCDataField::PermanentEnd)
      && permanentCurrent == RuntimeLayout::offsetOf(word,
         GCDataField::PermanentCurrent)
      && lock == RuntimeLayout::offsetOf(word, GCDataField::Lock)
      && signal == RuntimeLayout::offsetOf(word, GCDataField::Signal)
      && queueSemaphore == RuntimeLayout::offsetOf(word,
         GCDataField::QueueSemaphore)
      && size == RuntimeLayout::offsetOf(word, GCDataField::TableEnd);
}

bool ThreadContentLayoutSpec :: isValid(const TargetSpec& target) const
{
   unsigned short word = target.pointerSize;

   return criticalHandler == RuntimeLayout::offsetOf(word,
         ThreadContentField::CriticalHandler)
      && currentException == RuntimeLayout::offsetOf(word,
         ThreadContentField::CurrentException)
      && stackFrame == RuntimeLayout::offsetOf(word,
         ThreadContentField::StackFrame)
      && syncEvent == RuntimeLayout::offsetOf(word,
         ThreadContentField::SyncEvent)
      && flags == RuntimeLayout::offsetOf(word, ThreadContentField::Flags)
      && stackRoot == RuntimeLayout::offsetOf(word,
         ThreadContentField::StackRoot)
      && size == RuntimeLayout::offsetOf(word,
         ThreadContentField::ContentEnd);
}

bool ThreadTableLayoutSpec :: isValid(const TargetSpec& target) const
{
   unsigned short word = target.pointerSize;

   return count == RuntimeLayout::offsetOf(word, ThreadTableField::Count)
      && slots == RuntimeLayout::offsetOf(word, ThreadTableField::Slots)
      && slotContent == RuntimeLayout::offsetOf(word, ThreadSlotField::Content)
      && slotArgument == RuntimeLayout::offsetOf(word,
         ThreadSlotField::Argument)
      && slotSize == RuntimeLayout::offsetOf(word, ThreadSlotField::SlotEnd);
}

bool SystemEnvironmentLayoutSpec :: isValid(const TargetSpec& target) const
{
   unsigned short word = target.pointerSize;

   return staticRootCount == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::StaticRootCount)
      && tlsSize == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::TLSSize)
      && gcData == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::GCData)
      && singleContent == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::SingleContent)
      && threadTable == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::ThreadTable)
      && reserved == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::Reserved)
      && exceptionHandler == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::ExceptionHandler)
      && matureSize == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::MatureSize)
      && youngSize == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::YoungSize)
      && threadCount == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::ThreadCount)
      && serializedSize == RuntimeLayout::offsetOf(word,
         SystemEnvironmentField::EnvironmentEnd);
}

bool RuntimeDataLayoutSpec :: isValid(const TargetSpec& target) const
{
   return gc.isValid(target) && threadContent.isValid(target)
      && threadTable.isValid(target) && environment.isValid(target);
}

bool RuntimeCallSpec :: isValid(const RuntimeSpec& runtime) const
{
   switch (operation) {
      case RuntimeOperation::AllocateYoung:
      case RuntimeOperation::AllocatePermanent:
         if (argumentCount != 1 || resultCount != 1
            || !requiresManagedFrame || !returnsReference
            || !test(effects, AllocationEffects))
         {
            return false;
         }

         return runtime.threadingMode == ThreadingMode::MultiThread
            ? test(effects, ConcurrentEffects)
            : !test(effects, ConcurrentEffects);

      case RuntimeOperation::Collect:
         if (argumentCount != 2 || resultCount != 1
            || !requiresManagedFrame || !returnsReference
            || !test(effects, AllocationEffects))
         {
            return false;
         }

         return runtime.threadingMode == ThreadingMode::MultiThread
            ? test(effects, ConcurrentEffects)
            : !test(effects, ConcurrentEffects);

      case RuntimeOperation::Prepare:
         return argumentCount == 1 && resultCount == 0
            && !requiresManagedFrame && !returnsReference
            && effects == PrepareEffects;

      case RuntimeOperation::WaitForGC:
         if (argumentCount != 1 || resultCount != 0 || returnsReference)
            return false;

         if (runtime.threadingMode == ThreadingMode::SingleThread)
            return !requiresManagedFrame && effects == RuntimeCallEffect::None;

         return requiresManagedFrame && test(effects, WaitForGCEffects);

      default:
         return false;
   }
}

bool RuntimeSpec :: isValid(const TargetSpec& target) const
{
   if (!target.isValid() || gcMode != GCMode::GenerationalMoving)
      return false;

   if (writeBarrierMode != WriteBarrierMode::CardTable)
      return false;

   if (rootStrategy != RootStrategy::LegacyFrames)
      return false;

   if (!objectLayout.isValid(target) || !vmtLayout.isValid(target)
      || !dataLayout.isValid(target))
      return false;

   if (threadingMode == ThreadingMode::MultiThread)
      return safepointStrategy == SafepointStrategy::Cooperative && target.tlsModel != TLSModel::None;

   return safepointStrategy == SafepointStrategy::AllocationOnly;
}

RuntimeSpec RuntimeProvider :: legacy(ThreadingMode threadingMode, const TargetSpec& target)
{
   unsigned char pointerSize = target.pointerSize;

   return {
      .threadingMode = threadingMode,
      .gcMode = GCMode::GenerationalMoving,
      .rootStrategy = RootStrategy::LegacyFrames,
      .safepointStrategy = threadingMode == ThreadingMode::MultiThread
         ? SafepointStrategy::Cooperative
         : SafepointStrategy::AllocationOnly,
      .writeBarrierMode = WriteBarrierMode::CardTable,
      .objectLayout = createObjectLayout(pointerSize),
      .vmtLayout = createVMTLayout(pointerSize),
      .dataLayout = createRuntimeDataLayout(pointerSize)
   };
}

bool RuntimeCallProvider :: get(RuntimeOperation operation, const RuntimeSpec& runtime,
   RuntimeCallSpec& spec)
{
   RuntimeCallEffect effects = RuntimeCallEffect::None;

   switch (operation) {
      case RuntimeOperation::AllocateYoung:
      case RuntimeOperation::AllocatePermanent:
      case RuntimeOperation::Collect:
         effects = AllocationEffects;

         if (runtime.threadingMode == ThreadingMode::MultiThread)
            effects = effects | ConcurrentEffects;

         spec = {
            .operation = operation,
            .effects = effects,
            .argumentCount = (unsigned char)(operation == RuntimeOperation::Collect ? 2 : 1),
            .resultCount = 1,
            .requiresManagedFrame = true,
            .returnsReference = true
         };
         break;

      case RuntimeOperation::Prepare:
         spec = {
            .operation = operation,
            .effects = PrepareEffects,
            .argumentCount = 1,
            .resultCount = 0,
            .requiresManagedFrame = false,
            .returnsReference = false
         };
         break;

      case RuntimeOperation::WaitForGC:
         if (runtime.threadingMode == ThreadingMode::SingleThread) {
            spec = {
               .operation = operation,
               .effects = RuntimeCallEffect::None,
               .argumentCount = 1,
               .resultCount = 0,
               .requiresManagedFrame = false,
               .returnsReference = false
            };
         }
         else {
            spec = {
               .operation = operation,
               .effects = WaitForGCEffects,
               .argumentCount = 1,
               .resultCount = 0,
               .requiresManagedFrame = true,
               .returnsReference = false
            };
         }
         break;

      default:
         return false;
   }

   return spec.isValid(runtime);
}
