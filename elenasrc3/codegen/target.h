#ifndef CODEGEN_TARGET_H
#define CODEGEN_TARGET_H

#include "common.h"

namespace elena_lang::codegen
{
   enum class Architecture : unsigned char
   {
      None,
      X86,
      AMD64,
      ARM64,
      PPC64le
   };

   enum class OperatingSystem : unsigned char
   {
      None,
      Windows,
      Linux,
      FreeBSD,
      MacOS
   };

   enum class PlatformABI : unsigned char
   {
      None,
      WindowsX86,
      WindowsX64,
      SystemVX86,
      SystemVAMD64,
      AAPCS64,
      PPC64ELFv2
   };

   enum class BinaryFormat : unsigned char
   {
      None,
      PE,
      ELF,
      MachO
   };

   enum class TLSModel : unsigned char
   {
      None,
      Windows,
      ELF,
      MachO
   };

   enum class Endianness : unsigned char
   {
      Little,
      Big
   };

   enum class TargetPlatform : unsigned char
   {
      None,
      WindowsX86,
      WindowsAMD64,
      LinuxX86,
      LinuxAMD64,
      LinuxARM64,
      LinuxPPC64le,
      FreeBSDAMD64,
      MacOSAMD64,
      MacOSARM64,
      Count
   };

   struct ExternalABISpec
   {
      unsigned char integerArgumentRegisters;
      unsigned char floatingArgumentRegisters;
      unsigned char stackAlignment;
      unsigned char stackSlotSize;
      unsigned short shadowSpace;
      unsigned short redZone;
      bool           linkRegister;
   };

   struct ManagedABISpec
   {
      unsigned char cachedArgumentCount;
      unsigned char stackAlignment;
      unsigned char rawStackAlignment;
      unsigned char stackSlotSize;
      unsigned char ehTableEntrySize;
   };

   struct TargetSpec
   {
      TargetPlatform  platform;
      Architecture    architecture;
      OperatingSystem operatingSystem;
      PlatformABI     abi;
      BinaryFormat    binaryFormat;
      TLSModel        tlsModel;
      Endianness      endianness;
      unsigned char   pointerSize;

      ExternalABISpec externalABI;
      ManagedABISpec managedABI;

      bool isValid() const;
      bool is64Bit() const;
   };

   class TargetProvider
   {
   public:
      static bool get(TargetPlatform platform, TargetSpec& spec);
   };
}

#endif
