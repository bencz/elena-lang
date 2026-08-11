#include "target.h"

using namespace elena_lang::codegen;

namespace
{
   constexpr ExternalABISpec WindowsX86ExternalABI = {
      .integerArgumentRegisters = 0,
      .floatingArgumentRegisters = 0,
      .stackAlignment = 4,
      .stackSlotSize = 4,
      .shadowSpace = 0,
      .redZone = 0,
      .linkRegister = false
   };

   constexpr ExternalABISpec WindowsX64ExternalABI = {
      .integerArgumentRegisters = 4,
      .floatingArgumentRegisters = 4,
      .stackAlignment = 16,
      .stackSlotSize = 8,
      .shadowSpace = 32,
      .redZone = 0,
      .linkRegister = false
   };

   constexpr ExternalABISpec SystemVX86ExternalABI = {
      .integerArgumentRegisters = 0,
      .floatingArgumentRegisters = 0,
      .stackAlignment = 16,
      .stackSlotSize = 4,
      .shadowSpace = 0,
      .redZone = 0,
      .linkRegister = false
   };

   constexpr ExternalABISpec SystemVAMD64ExternalABI = {
      .integerArgumentRegisters = 6,
      .floatingArgumentRegisters = 8,
      .stackAlignment = 16,
      .stackSlotSize = 8,
      .shadowSpace = 0,
      .redZone = 128,
      .linkRegister = false
   };

   constexpr ExternalABISpec AAPCS64ExternalABI = {
      .integerArgumentRegisters = 8,
      .floatingArgumentRegisters = 8,
      .stackAlignment = 16,
      .stackSlotSize = 8,
      .shadowSpace = 0,
      .redZone = 0,
      .linkRegister = true
   };

   constexpr ExternalABISpec DarwinAAPCS64ExternalABI = {
      .integerArgumentRegisters = 8,
      .floatingArgumentRegisters = 8,
      .stackAlignment = 16,
      .stackSlotSize = 8,
      .shadowSpace = 0,
      .redZone = 128,
      .linkRegister = true
   };

   constexpr ExternalABISpec PPC64ELFv2ExternalABI = {
      .integerArgumentRegisters = 8,
      .floatingArgumentRegisters = 13,
      .stackAlignment = 16,
      .stackSlotSize = 8,
      .shadowSpace = 0,
      .redZone = 0,
      .linkRegister = true
   };

   constexpr ManagedABISpec X86ManagedABI = {
      .cachedArgumentCount = 1,
      .stackAlignment = 1,
      .rawStackAlignment = 4,
      .stackSlotSize = 4,
      .ehTableEntrySize = 16
   };

   constexpr ManagedABISpec ManagedABI64 = {
      .cachedArgumentCount = 2,
      .stackAlignment = 2,
      .rawStackAlignment = 16,
      .stackSlotSize = 8,
      .ehTableEntrySize = 32
   };

   constexpr TargetSpec Targets[] = {
      {
         .platform = TargetPlatform::WindowsX86,
         .architecture = Architecture::X86,
         .operatingSystem = OperatingSystem::Windows,
         .abi = PlatformABI::WindowsX86,
         .binaryFormat = BinaryFormat::PE,
         .tlsModel = TLSModel::Windows,
         .endianness = Endianness::Little,
         .pointerSize = 4,
         .externalABI = WindowsX86ExternalABI,
         .managedABI = X86ManagedABI
      },
      {
         .platform = TargetPlatform::WindowsAMD64,
         .architecture = Architecture::AMD64,
         .operatingSystem = OperatingSystem::Windows,
         .abi = PlatformABI::WindowsX64,
         .binaryFormat = BinaryFormat::PE,
         .tlsModel = TLSModel::Windows,
         .endianness = Endianness::Little,
         .pointerSize = 8,
         .externalABI = WindowsX64ExternalABI,
         .managedABI = ManagedABI64
      },
      {
         .platform = TargetPlatform::LinuxX86,
         .architecture = Architecture::X86,
         .operatingSystem = OperatingSystem::Linux,
         .abi = PlatformABI::SystemVX86,
         .binaryFormat = BinaryFormat::ELF,
         .tlsModel = TLSModel::ELF,
         .endianness = Endianness::Little,
         .pointerSize = 4,
         .externalABI = SystemVX86ExternalABI,
         .managedABI = X86ManagedABI
      },
      {
         .platform = TargetPlatform::LinuxAMD64,
         .architecture = Architecture::AMD64,
         .operatingSystem = OperatingSystem::Linux,
         .abi = PlatformABI::SystemVAMD64,
         .binaryFormat = BinaryFormat::ELF,
         .tlsModel = TLSModel::ELF,
         .endianness = Endianness::Little,
         .pointerSize = 8,
         .externalABI = SystemVAMD64ExternalABI,
         .managedABI = ManagedABI64
      },
      {
         .platform = TargetPlatform::LinuxARM64,
         .architecture = Architecture::ARM64,
         .operatingSystem = OperatingSystem::Linux,
         .abi = PlatformABI::AAPCS64,
         .binaryFormat = BinaryFormat::ELF,
         .tlsModel = TLSModel::ELF,
         .endianness = Endianness::Little,
         .pointerSize = 8,
         .externalABI = AAPCS64ExternalABI,
         .managedABI = ManagedABI64
      },
      {
         .platform = TargetPlatform::LinuxPPC64le,
         .architecture = Architecture::PPC64le,
         .operatingSystem = OperatingSystem::Linux,
         .abi = PlatformABI::PPC64ELFv2,
         .binaryFormat = BinaryFormat::ELF,
         .tlsModel = TLSModel::ELF,
         .endianness = Endianness::Little,
         .pointerSize = 8,
         .externalABI = PPC64ELFv2ExternalABI,
         .managedABI = ManagedABI64
      },
      {
         .platform = TargetPlatform::FreeBSDAMD64,
         .architecture = Architecture::AMD64,
         .operatingSystem = OperatingSystem::FreeBSD,
         .abi = PlatformABI::SystemVAMD64,
         .binaryFormat = BinaryFormat::ELF,
         .tlsModel = TLSModel::ELF,
         .endianness = Endianness::Little,
         .pointerSize = 8,
         .externalABI = SystemVAMD64ExternalABI,
         .managedABI = ManagedABI64
      },
      {
         .platform = TargetPlatform::MacOSAMD64,
         .architecture = Architecture::AMD64,
         .operatingSystem = OperatingSystem::MacOS,
         .abi = PlatformABI::SystemVAMD64,
         .binaryFormat = BinaryFormat::MachO,
         .tlsModel = TLSModel::MachO,
         .endianness = Endianness::Little,
         .pointerSize = 8,
         .externalABI = SystemVAMD64ExternalABI,
         .managedABI = ManagedABI64
      },
      {
         .platform = TargetPlatform::MacOSARM64,
         .architecture = Architecture::ARM64,
         .operatingSystem = OperatingSystem::MacOS,
         .abi = PlatformABI::AAPCS64,
         .binaryFormat = BinaryFormat::MachO,
         .tlsModel = TLSModel::MachO,
         .endianness = Endianness::Little,
         .pointerSize = 8,
         .externalABI = DarwinAAPCS64ExternalABI,
         .managedABI = ManagedABI64
      }
   };
}

static bool areEqual(const ExternalABISpec& left, const ExternalABISpec& right)
{
   return left.integerArgumentRegisters == right.integerArgumentRegisters
      && left.floatingArgumentRegisters == right.floatingArgumentRegisters
      && left.stackAlignment == right.stackAlignment
      && left.stackSlotSize == right.stackSlotSize
      && left.shadowSpace == right.shadowSpace
      && left.redZone == right.redZone
      && left.linkRegister == right.linkRegister;
}

static bool areEqual(const ManagedABISpec& left, const ManagedABISpec& right)
{
   return left.cachedArgumentCount == right.cachedArgumentCount
      && left.stackAlignment == right.stackAlignment
      && left.rawStackAlignment == right.rawStackAlignment
      && left.stackSlotSize == right.stackSlotSize
      && left.ehTableEntrySize == right.ehTableEntrySize;
}

static bool areEqual(const TargetSpec& left, const TargetSpec& right)
{
   return left.platform == right.platform
      && left.architecture == right.architecture
      && left.operatingSystem == right.operatingSystem
      && left.abi == right.abi
      && left.binaryFormat == right.binaryFormat
      && left.tlsModel == right.tlsModel
      && left.endianness == right.endianness
      && left.pointerSize == right.pointerSize
      && areEqual(left.externalABI, right.externalABI)
      && areEqual(left.managedABI, right.managedABI);
}

static bool isArchitectureValid(const TargetSpec& spec)
{
   switch (spec.architecture) {
      case Architecture::X86:
         return spec.pointerSize == 4;
      case Architecture::AMD64:
      case Architecture::ARM64:
      case Architecture::PPC64le:
         return spec.pointerSize == 8;
      default:
         return false;
   }
}

static bool isPlatformValid(const TargetSpec& spec)
{
   switch (spec.operatingSystem) {
      case OperatingSystem::Windows:
         return spec.binaryFormat == BinaryFormat::PE && spec.tlsModel == TLSModel::Windows;
      case OperatingSystem::Linux:
      case OperatingSystem::FreeBSD:
         return spec.binaryFormat == BinaryFormat::ELF && spec.tlsModel == TLSModel::ELF;
      case OperatingSystem::MacOS:
         return spec.binaryFormat == BinaryFormat::MachO && spec.tlsModel == TLSModel::MachO;
      default:
         return false;
   }
}

static bool isABIValid(const TargetSpec& spec)
{
   switch (spec.abi) {
      case PlatformABI::WindowsX86:
         return spec.architecture == Architecture::X86 && spec.operatingSystem == OperatingSystem::Windows;
      case PlatformABI::WindowsX64:
         return spec.architecture == Architecture::AMD64 && spec.operatingSystem == OperatingSystem::Windows;
      case PlatformABI::SystemVX86:
         return spec.architecture == Architecture::X86 && spec.operatingSystem == OperatingSystem::Linux;
      case PlatformABI::SystemVAMD64:
         return spec.architecture == Architecture::AMD64 && spec.operatingSystem != OperatingSystem::Windows;
      case PlatformABI::AAPCS64:
         return spec.architecture == Architecture::ARM64;
      case PlatformABI::PPC64ELFv2:
         return spec.architecture == Architecture::PPC64le && spec.operatingSystem == OperatingSystem::Linux;
      default:
         return false;
   }
}

bool TargetSpec :: isValid() const
{
   if (platform == TargetPlatform::None || platform == TargetPlatform::Count)
      return false;

   if (endianness != Endianness::Little
      || externalABI.stackAlignment == 0
      || externalABI.stackSlotSize != pointerSize
      || managedABI.stackAlignment == 0
      || managedABI.rawStackAlignment == 0
      || managedABI.stackSlotSize != pointerSize
      || !isArchitectureValid(*this)
      || !isPlatformValid(*this)
      || !isABIValid(*this))
   {
      return false;
   }

   for (size_t i = 0; i < sizeof(Targets) / sizeof(TargetSpec); i++) {
      if (Targets[i].platform == platform)
         return areEqual(*this, Targets[i]);
   }

   return false;
}

bool TargetSpec :: is64Bit() const
{
   return pointerSize == 8;
}

bool TargetProvider :: get(TargetPlatform platform, TargetSpec& spec)
{
   for (size_t i = 0; i < sizeof(Targets) / sizeof(TargetSpec); i++) {
      if (Targets[i].platform == platform) {
         spec = Targets[i];

         return true;
      }
   }

   return false;
}
