#include "runtimecore.h"

using namespace elena_lang;
using namespace elena_lang::codegen;
using namespace elena_lang::codegen::x86;

RuntimeCoreEncoder :: RuntimeCoreEncoder(const TargetSpec& target, MemoryWriter& writer)
   : _target(target), _writer(writer)
{
}

bool RuntimeCoreEncoder :: writeByte(unsigned char value)
{
   return _writer.writeByte(value);
}

bool RuntimeCoreEncoder :: writeDWord(unsigned int value)
{
   return _writer.writeDWord(value);
}

bool RuntimeCoreEncoder :: writeQWord(unsigned long long value)
{
   return _writer.writeQWord(value);
}

bool RuntimeCoreEncoder :: write(const unsigned char* bytes, pos_t length)
{
   return _writer.write(bytes, length);
}

unsigned char RuntimeCoreEncoder :: newLabel()
{
   unsigned char index = (unsigned char)_labels.count_pos();
   _labels.add({
      .position = 0,
      .bound = false
   });

   return index;
}

bool RuntimeCoreEncoder :: bind(unsigned char label)
{
   if (label >= _labels.count_pos() || _labels.get(label).bound)
      return false;

   _labels.get(label) = {
      .position = _writer.position(),
      .bound = true
   };

   return true;
}

bool RuntimeCoreEncoder :: branch(unsigned char condition, unsigned char label)
{
   if (label >= _labels.count_pos()
      || !writeByte(0x0F)                         // Jcc rel32 escape
      || !writeByte(0x80 | (condition & 0x0F)))  // Jcc condition opcode
   {
      return false;
   }

   _fixups.add({
      .position = _writer.position(),
      .label = label
   });

   return writeDWord(0);
}

bool RuntimeCoreEncoder :: jump(unsigned char label)
{
   if (label >= _labels.count_pos() || !writeByte(0xE9)) // jmp rel32
      return false;

   _fixups.add({
      .position = _writer.position(),
      .label = label
   });

   return writeDWord(0);
}

bool RuntimeCoreEncoder :: addRelocation(
   RuntimeCoreRelocationKind kind,
   RuntimeCoreSymbol symbol,
   int addend,
   pos_t size)
{
   _relocations.add({
      .kind = kind,
      .symbol = symbol,
      .position = _writer.position(),
      .addend = addend
   });

   return size == 4 ? writeDWord(0) : size == 8 ? writeQWord(0) : false;
}

RuntimeCoreError RuntimeCoreEncoder :: fixLabels()
{
   for (pos_t i = 0; i < _fixups.count_pos(); i++) {
      LabelFixup& fixup = _fixups.get(i);
      if (fixup.label >= _labels.count_pos() || !_labels.get(fixup.label).bound)
         return RuntimeCoreError::InvalidLabel;

      int displacement = (int)(_labels.get(fixup.label).position - fixup.position - 4);
      if (!_writer.Memory()->write(fixup.position, &displacement, sizeof(displacement)))
         return RuntimeCoreError::WriteFailed;
   }

   return RuntimeCoreError::None;
}

RuntimeCoreError RuntimeCoreEncoder :: encodeX86Operation(
   RuntimeOperation operation,
   const RuntimeSpec& runtime,
   const ExternalABI& externalABI,
   const RuntimeCoreProtocol& protocol)
{
   switch (operation) {
      case RuntimeOperation::AllocateYoung:
         return encodeAllocateYoungX86(runtime);
      case RuntimeOperation::AllocatePermanent:
         return encodeAllocatePermanentX86(runtime, externalABI, protocol);
      case RuntimeOperation::Collect:
         return encodeCollectX86(runtime, externalABI, protocol);
      case RuntimeOperation::Prepare:
         return encodePrepareX86(externalABI);
      case RuntimeOperation::WaitForGC:
         return encodeWaitForGCX86(runtime, externalABI);
      default:
         return RuntimeCoreError::InvalidOperation;
   }
}

RuntimeCoreError RuntimeCoreEncoder :: encodeAMD64Operation(
   RuntimeOperation operation,
   const RuntimeSpec& runtime,
   const ExternalABI& externalABI,
   const RuntimeCoreProtocol& protocol)
{
   switch (operation) {
      case RuntimeOperation::AllocateYoung:
         return encodeAllocateYoungAMD64(runtime);
      case RuntimeOperation::AllocatePermanent:
         return encodeAllocatePermanentAMD64(runtime, externalABI, protocol);
      case RuntimeOperation::Collect:
         return encodeCollectAMD64(runtime, externalABI, protocol);
      case RuntimeOperation::Prepare:
         return encodePrepareAMD64(externalABI);
      case RuntimeOperation::WaitForGC:
         return encodeWaitForGCAMD64(runtime, externalABI);
      default:
         return RuntimeCoreError::InvalidOperation;
   }
}

RuntimeCoreError RuntimeCoreEncoder :: encode(
   RuntimeOperation operation,
   const RuntimeSpec& runtime,
   const ManagedABI& managedABI,
   const RuntimeCallABI& callABI)
{
   RuntimeCallSpec call = {};

   if (!_target.isValid() || !runtime.isValid(_target)
      || !RuntimeCallProvider::get(operation, runtime, call))
   {
      return RuntimeCoreError::InvalidRuntime;
   }

   if (_target.architecture != managedABI.architecture
      || !callABI.isValid(managedABI, call))
   {
      return RuntimeCoreError::InvalidABI;
   }

   _relocations.clear();
   _labels.clear();
   _fixups.clear();

   ExternalABI externalABI = {};
   if (!ExternalABIProvider::get(_target, externalABI))
      return RuntimeCoreError::InvalidABI;

   RuntimeCoreProtocol protocol = {};
   if ((operation == RuntimeOperation::Collect
         || operation == RuntimeOperation::AllocatePermanent)
      && !RuntimeCoreProtocolProvider::get(operation, runtime, protocol))
   {
      return RuntimeCoreError::InvalidRuntime;
   }

   switch (_target.architecture) {
      case Architecture::X86:
         return encodeX86Operation(operation, runtime, externalABI, protocol);

      case Architecture::AMD64:
         return encodeAMD64Operation(operation, runtime, externalABI, protocol);

      default:
         return RuntimeCoreError::InvalidABI;
   }
}

RuntimeCoreError RuntimeCoreEncoder :: encode(
   RuntimeCoreEntry entry,
   const RuntimeSpec& runtime,
   const ManagedABI& managedABI)
{
   if (!_target.isValid()
      || !runtime.isValid(_target)
      || _target.architecture != managedABI.architecture
      || !managedABI.isValid())
   {
      return RuntimeCoreError::InvalidABI;
   }

   _relocations.clear();
   _labels.clear();
   _fixups.clear();

   if (entry != RuntimeCoreEntry::ExceptionDispatcher)
      return RuntimeCoreError::InvalidOperation;

   switch (_target.architecture) {
      case Architecture::X86:
         return encodeExceptionDispatcherX86(runtime);

      case Architecture::AMD64:
         return encodeExceptionDispatcherAMD64(runtime);

      default:
         return RuntimeCoreError::InvalidABI;
   }
}

pos_t RuntimeCoreEncoder :: relocationCount() const
{
   return _relocations.count_pos();
}

RuntimeCoreRelocation& RuntimeCoreEncoder :: relocation(pos_t index)
{
   return _relocations.get(index);
}
