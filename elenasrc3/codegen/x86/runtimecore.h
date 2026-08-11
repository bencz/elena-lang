#ifndef CODEGEN_X86_RUNTIMECORE_H
#define CODEGEN_X86_RUNTIMECORE_H

#include "abi.h"
#include "../runtimecore.h"

namespace elena_lang::codegen::x86
{
   class RuntimeCoreEncoder
   {
      struct Label
      {
         pos_t position;
         bool bound;
      };

      struct LabelFixup
      {
         pos_t position;
         unsigned char label;
      };

      TargetSpec _target;
      MemoryWriter& _writer;
      CachedList<RuntimeCoreRelocation, 4> _relocations;
      CachedList<Label, 4> _labels;
      CachedList<LabelFixup, 8> _fixups;

      bool writeByte(unsigned char value);
      bool writeDWord(unsigned int value);
      bool writeQWord(unsigned long long value);
      bool write(const unsigned char* bytes, pos_t length);
      unsigned char newLabel();
      bool bind(unsigned char label);
      bool branch(unsigned char condition, unsigned char label);
      bool jump(unsigned char label);
      bool addRelocation(RuntimeCoreRelocationKind kind, RuntimeCoreSymbol symbol, int addend, pos_t size);

      RuntimeCoreError fixLabels();

      RuntimeCoreError encodeAllocateYoungX86(const RuntimeSpec& runtime);
      RuntimeCoreError encodeAllocateYoungAMD64(const RuntimeSpec& runtime);

      RuntimeCoreError encodeAllocatePermanentX86(
         const RuntimeSpec& runtime, const ExternalABI& externalABI, const RuntimeCoreProtocol& protocol);
      RuntimeCoreError encodeAllocatePermanentAMD64(
         const RuntimeSpec& runtime, const ExternalABI& externalABI, const RuntimeCoreProtocol& protocol);

      RuntimeCoreError encodeAllocatePermanentMTAX86(
         const RuntimeSpec& runtime, const ExternalABI& externalABI, const RuntimeCoreProtocol& protocol);
      RuntimeCoreError encodeAllocatePermanentMTAAMD64(
         const RuntimeSpec& runtime, const ExternalABI& externalABI, const RuntimeCoreProtocol& protocol);

      RuntimeCoreError encodeCollectX86(
         const RuntimeSpec& runtime, const ExternalABI& externalABI, const RuntimeCoreProtocol& protocol);
      RuntimeCoreError encodeCollectAMD64(
         const RuntimeSpec& runtime, const ExternalABI& externalABI, const RuntimeCoreProtocol& protocol);

      RuntimeCoreError encodeCollectMTAX86(
         const RuntimeSpec& runtime, const ExternalABI& externalABI, const RuntimeCoreProtocol& protocol);
      RuntimeCoreError encodeCollectMTAAMD64(
         const RuntimeSpec& runtime, const ExternalABI& externalABI, const RuntimeCoreProtocol& protocol);

      RuntimeCoreError encodePrepareX86(const ExternalABI& externalABI);
      RuntimeCoreError encodePrepareAMD64(const ExternalABI& externalABI);

      RuntimeCoreError encodeCurrentThreadX86(const RuntimeSpec& runtime);
      RuntimeCoreError encodeCurrentThreadAMD64(const RuntimeSpec& runtime);

      RuntimeCoreError encodeWaitForGCX86(const RuntimeSpec& runtime, const ExternalABI& externalABI);
      RuntimeCoreError encodeWaitForGCAMD64(const RuntimeSpec& runtime, const ExternalABI& externalABI);

      RuntimeCoreError encodeExceptionDispatcherX86(const RuntimeSpec& runtime);
      RuntimeCoreError encodeExceptionDispatcherAMD64(const RuntimeSpec& runtime);

      RuntimeCoreError encodeX86Operation(
         RuntimeOperation operation,
         const RuntimeSpec& runtime,
         const ExternalABI& externalABI,
         const RuntimeCoreProtocol& protocol);
      RuntimeCoreError encodeAMD64Operation(
         RuntimeOperation operation,
         const RuntimeSpec& runtime,
         const ExternalABI& externalABI,
         const RuntimeCoreProtocol& protocol);

   public:
      RuntimeCoreError encode(
         RuntimeOperation operation,
         const RuntimeSpec& runtime,
         const ManagedABI& managedABI,
         const RuntimeCallABI& callABI);

      RuntimeCoreError encode(
         RuntimeCoreEntry entry,
         const RuntimeSpec& runtime,
         const ManagedABI& managedABI);

      pos_t relocationCount() const;
      RuntimeCoreRelocation& relocation(pos_t index);

      RuntimeCoreEncoder(const TargetSpec& target, MemoryWriter& writer);
   };
}

#endif
