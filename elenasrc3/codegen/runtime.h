#ifndef CODEGEN_RUNTIME_H
#define CODEGEN_RUNTIME_H

#include "target.h"
#include "../common/runtimelayout.h"

namespace elena_lang::codegen
{
   enum class ThreadingMode : unsigned char
   {
      SingleThread,
      MultiThread
   };

   enum class GCMode : unsigned char
   {
      None,
      GenerationalMoving
   };

   enum class RootStrategy : unsigned char
   {
      LegacyFrames
   };

   enum class SafepointStrategy : unsigned char
   {
      AllocationOnly,
      Cooperative
   };

   enum class WriteBarrierMode : unsigned char
   {
      None,
      CardTable
   };

   enum class RuntimeOperation : unsigned char
   {
      AllocateYoung,
      AllocatePermanent,
      Collect,
      Prepare,
      WaitForGC,
      Count
   };

   enum class RuntimeDataReference : unsigned char
   {
      ThreadTableSlots,
      GCDataLock,
      GCDataSignal,
      SingleContent,
      SingleContentStackRoot,
      SingleContentStackFrame,
      SystemEnvironment
   };

   enum class RuntimeCallEffect : unsigned short
   {
      None          = 0x0000,
      ReadHeap      = 0x0001,
      WriteHeap     = 0x0002,
      ReadGlobal    = 0x0004,
      WriteGlobal   = 0x0008,
      Call          = 0x0010,
      Allocate      = 0x0020,
      Safepoint     = 0x0040,
      MayThrow      = 0x0080,
      Synchronize   = 0x0100,
      ReadTLS       = 0x0200,
      RelocateRoots = 0x0400
   };

   constexpr RuntimeCallEffect operator | (RuntimeCallEffect left, RuntimeCallEffect right)
   {
      return (RuntimeCallEffect)((unsigned short)left | (unsigned short)right);
   }

   constexpr bool test(RuntimeCallEffect value, RuntimeCallEffect mask)
   {
      return ((unsigned short)value & (unsigned short)mask) == (unsigned short)mask;
   }

   struct ObjectLayoutSpec
   {
      unsigned char fieldSize;
      unsigned char headerSize;
      unsigned char sizeOffset;
      unsigned char vmtOffset;
      unsigned char synchronizationOffset;
      unsigned char allocationAlignment;
      unsigned int structMask;
      unsigned int objectSizeMask;

      bool isValid(const TargetSpec& target) const;
      bool payloadSize(int fieldCount, unsigned int& size) const;
      bool allocationSize(int fieldCount, unsigned int& size) const;
      bool binarySize(int byteSize, unsigned int& size) const;
      bool binaryAllocationSize(int byteSize, unsigned int& size) const;
   };

   struct VMTLayoutSpec
   {
      unsigned char sizeOffset;
      unsigned char flagsOffset;
      unsigned char parentOffset;
      unsigned char methodOffset;
      unsigned char entrySize;

      bool isValid(const TargetSpec& target) const;
   };

   struct GCDataLayoutSpec
   {
      unsigned short header;
      unsigned short start;
      unsigned short youngStart;
      unsigned short youngCurrent;
      unsigned short youngEnd;
      unsigned short shadow;
      unsigned short shadowEnd;
      unsigned short matureStart;
      unsigned short matureCurrent;
      unsigned short end;
      unsigned short matureWriteBarrier;
      unsigned short permanentStart;
      unsigned short permanentEnd;
      unsigned short permanentCurrent;
      unsigned short lock;
      unsigned short signal;
      unsigned short queueSemaphore;
      unsigned short size;

      bool isValid(const TargetSpec& target) const;
   };

   struct ThreadContentLayoutSpec
   {
      unsigned short criticalHandler;
      unsigned short currentException;
      unsigned short stackFrame;
      unsigned short syncEvent;
      unsigned short flags;
      unsigned short stackRoot;
      unsigned short size;

      bool isValid(const TargetSpec& target) const;
   };

   struct ThreadTableLayoutSpec
   {
      unsigned short count;
      unsigned short slots;
      unsigned short slotContent;
      unsigned short slotArgument;
      unsigned short slotSize;

      bool isValid(const TargetSpec& target) const;
   };

   struct SystemEnvironmentLayoutSpec
   {
      unsigned short staticRootCount;
      unsigned short tlsSize;
      unsigned short gcData;
      unsigned short singleContent;
      unsigned short threadTable;
      unsigned short reserved;
      unsigned short exceptionHandler;
      unsigned short matureSize;
      unsigned short youngSize;
      unsigned short threadCount;
      unsigned short serializedSize;

      bool isValid(const TargetSpec& target) const;
   };

   struct RuntimeDataLayoutSpec
   {
      GCDataLayoutSpec             gc;
      ThreadContentLayoutSpec      threadContent;
      ThreadTableLayoutSpec        threadTable;
      SystemEnvironmentLayoutSpec  environment;

      bool isValid(const TargetSpec& target) const;
   };

   struct RuntimeSpec
   {
      ThreadingMode     threadingMode;
      GCMode            gcMode;
      RootStrategy      rootStrategy;
      SafepointStrategy safepointStrategy;
      WriteBarrierMode  writeBarrierMode;
      ObjectLayoutSpec  objectLayout;
      VMTLayoutSpec     vmtLayout;
      RuntimeDataLayoutSpec dataLayout;

      bool isValid(const TargetSpec& target) const;
   };

   struct RuntimeCallSpec
   {
      RuntimeOperation operation;
      RuntimeCallEffect effects;
      unsigned char argumentCount;
      unsigned char resultCount;
      bool requiresManagedFrame;
      bool returnsReference;

      bool isValid(const RuntimeSpec& runtime) const;
   };

   class RuntimeProvider
   {
   public:
      static RuntimeSpec legacy(ThreadingMode threadingMode, const TargetSpec& target);
   };

   class RuntimeCallProvider
   {
   public:
      static bool get(RuntimeOperation operation, const RuntimeSpec& runtime, RuntimeCallSpec& spec);
   };
}

#endif
