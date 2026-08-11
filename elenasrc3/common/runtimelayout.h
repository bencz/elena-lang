#ifndef RUNTIME_LAYOUT_H
#define RUNTIME_LAYOUT_H

namespace elena_lang
{
   enum class ObjectHeaderField : unsigned char
   {
      VMT = 2,
      End = 2
   };

   enum class VMTHeaderField : unsigned char
   {
      Size = 1,
      Flags = 3,
      Parent = 4
   };

   enum class VMTPreHeaderField : unsigned char
   {
      ClassName = 1
   };

   enum class VMTTableField : unsigned char
   {
      FirstMethod = 1,
      EntryEnd = 2
   };

   enum class MessageTableField : unsigned char
   {
      Action,
      Payload,
      EntryEnd
   };

   enum class GCDataField : unsigned char
   {
      Header,
      Start,
      YoungStart,
      YoungCurrent,
      YoungEnd,
      Shadow,
      ShadowEnd,
      MatureStart,
      MatureCurrent,
      End,
      MatureWriteBarrier,
      PermanentStart,
      PermanentEnd,
      PermanentCurrent,
      Lock,
      Signal,
      QueueSemaphore,
      TableEnd
   };

   enum class ThreadContentField : unsigned char
   {
      CriticalHandler,
      CurrentException,
      StackFrame,
      SyncEvent,
      Flags,
      StackRoot,
      ContentEnd
   };

   enum class ExceptionStructField : unsigned char
   {
      Previous,
      CatchAddress,
      CatchLevel,
      CatchFrame,
      StructEnd
   };

   enum class ThreadTableField : unsigned char
   {
      Count,
      Slots
   };

   enum class ThreadSlotField : unsigned char
   {
      Content,
      Argument,
      SlotEnd
   };

   enum class SystemEnvironmentField : unsigned char
   {
      StaticRootCount,
      TLSSize,
      GCData,
      SingleContent,
      ThreadTable,
      Reserved,
      ExceptionHandler,
      MatureSize,
      YoungSize,
      ThreadCount,
      EnvironmentEnd
   };

   class RuntimeLayout
   {
      static constexpr unsigned int SerializedFieldSize = sizeof(unsigned int);

      template <class Field>
      static constexpr unsigned int wordOffset(unsigned int wordSize, Field field)
      {
         return wordSize * (unsigned int)field;
      }

   public:
      static constexpr unsigned int ObjectSizeOffset = SerializedFieldSize;

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         ObjectHeaderField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         VMTHeaderField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         VMTPreHeaderField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         VMTTableField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         MessageTableField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int entryOffset(unsigned int wordSize,
         unsigned int index, MessageTableField field)
      {
         return index * offsetOf(wordSize, MessageTableField::EntryEnd)
            + offsetOf(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         GCDataField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         ThreadContentField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         ExceptionStructField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         ThreadTableField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         ThreadSlotField field)
      {
         return wordOffset(wordSize, field);
      }

      static constexpr unsigned int offsetOf(unsigned int wordSize,
         SystemEnvironmentField field)
      {
         unsigned int nativeFields = wordOffset(wordSize,
            SystemEnvironmentField::MatureSize);

         switch (field) {
            case SystemEnvironmentField::YoungSize:
               return nativeFields + SerializedFieldSize;
            case SystemEnvironmentField::ThreadCount:
               return offsetOf(wordSize, SystemEnvironmentField::YoungSize)
                  + SerializedFieldSize;
            case SystemEnvironmentField::EnvironmentEnd:
               return offsetOf(wordSize, SystemEnvironmentField::ThreadCount)
                  + SerializedFieldSize;
            default:
               return wordOffset(wordSize, field);
         }
      }
   };
}

#endif
