//---------------------------------------------------------------------------
//		E L E N A   P r o j e c t:  ELENA command-Line Compiler
//
//		This file contains the main body of the Linux command-line compiler
//
//                                             (C)2021-2024, by Aleksey Rakov
//---------------------------------------------------------------------------

#include "elena.h"
// --------------------------------------------------------------------------
#include "cliconst.h"
#include "compiling.h"
#include "project.h"
#include "elfimage.h"

#if defined(__x86_64__)

#include "elflinker32.h"
#include "elflinker64.h"
#include "x86compiler.h"
#include "x86_64compiler.h"

#elif defined(__i386__)

#include "elflinker32.h"
#include "x86compiler.h"

#elif defined(__PPC64__)

#include "elfppclinker64.h"
#include "ppc64compiler.h"
#include "elfppcimage.h"

#elif defined(__aarch64__)

#include "elfarmlinker64.h"
#include "arm64compiler.h"
#include "elfarmimage.h"

#endif

#include "constants.h"
#include "messages.h"
#include "linux/presenter.h"
#include "linux/pathmanager.h"

#include <stdarg.h>

using namespace elena_lang;

#if defined(__x86_64__)

constexpr auto CURRENT_PLATFORM           = PlatformType::Linux_x86_64;

constexpr int MINIMAL_ARG_LIST            = 2;

constexpr auto DEFAULT_STACKALIGNMENT     = 2;
constexpr auto DEFAULT_RAW_STACKALIGNMENT = 16;
constexpr auto DEFAULT_EHTABLE_ENTRY_SIZE = 32;

typedef ElfAmd64Linker           LinuxLinker;
typedef ElfAmd64ImageFormatter   LinuxImageFormatter;

#elif defined(__i386__)

constexpr auto CURRENT_PLATFORM           = PlatformType::Linux_x86;

constexpr int MINIMAL_ARG_LIST            = 1;

constexpr auto DEFAULT_STACKALIGNMENT     = 1;
constexpr auto DEFAULT_RAW_STACKALIGNMENT = 4;
constexpr auto DEFAULT_EHTABLE_ENTRY_SIZE = 16;

typedef ElfI386Linker            LinuxLinker;
typedef ElfI386ImageFormatter    LinuxImageFormatter;

#elif defined(__PPC64__)

constexpr auto CURRENT_PLATFORM           = PlatformType::Linux_PPC64le;

constexpr int MINIMAL_ARG_LIST            = 2;

constexpr auto DEFAULT_STACKALIGNMENT     = 2;
constexpr auto DEFAULT_RAW_STACKALIGNMENT = 16;
constexpr auto DEFAULT_EHTABLE_ENTRY_SIZE = 32;

typedef ElfPPC64leLinker         LinuxLinker;
typedef ElfPPC64leImageFormatter LinuxImageFormatter;

#elif defined(__aarch64__)

constexpr auto CURRENT_PLATFORM           = PlatformType::Linux_ARM64;

constexpr int MINIMAL_ARG_LIST            = 2;

constexpr auto DEFAULT_STACKALIGNMENT     = 2;
constexpr auto DEFAULT_RAW_STACKALIGNMENT = 16;
constexpr auto DEFAULT_EHTABLE_ENTRY_SIZE = 32;

typedef ElfARM64Linker         LinuxLinker;
typedef ElfARM64ImageFormatter LinuxImageFormatter;

#endif

constexpr int DEFAULT_MGSIZE = 688128;
constexpr int DEFAULT_YGSIZE = 204800;
constexpr int DEFAULT_STACKRESERV = 0x100000;

class Presenter : public LinuxConsolePresenter
{
public:
   ustr_t getMessage(int code)
   {
      for(size_t i = 0; i < MessageLength; i++) {
         if (Messages[i].value1 == code)
            return Messages[i].value2;
      }

      return errMsgUnrecognizedError;
   }

   static Presenter& getInstance()
   {
      static Presenter instance;

      return instance;
   }

private:
   Presenter() = default;

public:
   Presenter(Presenter const&) = delete;
   void operator=(Presenter const&) = delete;

   ~Presenter() = default;
};

JITCompilerBase* createJITCompiler(LibraryLoaderBase* loader, PlatformType platform)
{
   switch (platform) {
#if defined(__i386__) || defined(__x86_64__)
      case PlatformType::Linux_x86:
         return new X86JITCompiler();
#endif
#if defined(__x86_64__)
      case PlatformType::Linux_x86_64:
         return new X86_64JITCompiler();
#endif
#if defined(__PPC64__)
      case PlatformType::Linux_PPC64le:
         return new PPC64leJITCompiler();
#endif
#if defined(__aarch64__)
      case PlatformType::Linux_ARM64:
         return new ARM64JITCompiler();
#endif
      default:
         return nullptr;
   }
}

const char* dataFileList[] = { BC_RULES_FILE, BT_RULES_FILE, SYNTAX60_FILE };

int main(int argc, char* argv[])
{
   try
   {
      bool cleanMode = false;

      PathString dataPath(PathHelper::retrievePath(dataFileList, 3, DATA_PATH));

      JITSettings      defaultCoreSettings = { DEFAULT_MGSIZE, DEFAULT_YGSIZE, DEFAULT_STACKRESERV, 1, true, true };
      ErrorProcessor   errorProcessor(&Presenter::getInstance());
      Project          project(*dataPath, CURRENT_PLATFORM, &Presenter::getInstance());
      LinuxLinker      linker(&errorProcessor, &LinuxImageFormatter::getInstance(&project));
      CompilingProcess process(*dataPath, nullptr, "<moduleProlog>", "<prolog>", "<epilog>",
         &Presenter::getInstance(), &errorProcessor,
         VA_ALIGNMENT, defaultCoreSettings, createJITCompiler);

      process.greeting();

      // Initializing...
      path_t defaultConfigPath = PathHelper::retrieveFilePath(LOCAL_DEFAULT_CONFIG);
      if (defaultConfigPath.compare(LOCAL_DEFAULT_CONFIG)) {
         // if the local config file was not found
         defaultConfigPath = DEFAULT_CONFIG;
      }

      PathString configPath(*dataPath, PathHelper::retrieveFilePath(defaultConfigPath));
      project.loadConfig(*configPath, nullptr, false);

       // Reading command-line arguments...
      if (argc < 2) {
         Presenter::getInstance().printLine(ELC_HELP_INFO);
         return -3;
      }

      IdentifierString profile;
      for (int i = 1; i < argc; i++) {
         if (argv[i][0] == '-') {
            switch (argv[i][1]) {
               case 'f':
               {
                  IdentifierString setting(argv[i] + 2);
                  process.addForward(*setting);

                  break;
               }
               case 'l':
                  profile.copy(argv[i] + 2);
                  break;
               case 'm':
                  project.addBoolSetting(ProjectOption::MappingOutputMode, true);
                  break;
               case 'o':
                  if (argv[i][2] == '0') {
                     project.addIntSetting(ProjectOption::OptimizationMode, optNone);
                  }
                  else if (argv[i][2] == '1') {
                     project.addIntSetting(ProjectOption::OptimizationMode, optLow);
                  }
                  else if (argv[i][2] == '2') {
                     project.addIntSetting(ProjectOption::OptimizationMode, optMiddle);
                  }
                  else if (argv[i][2] == '3') {
                     project.addIntSetting(ProjectOption::OptimizationMode, optHigh);
                  }
                  break;
               case 'p':
                  project.setBasePath(argv[i] + 2);
                  break;
               case 'r':
                  cleanMode = true;
                  break;
               case 's':
               {
                  IdentifierString setting(argv[i] + 2);
                  if (setting.compare("stackReserv:", 0, 12)) {
                     ustr_t valStr = *setting + 12;
                     int val = StrConvertor::toInt(valStr, 10);
                     project.addIntSetting(ProjectOption::StackReserved, val);
                  }
                  break;
               }
               case 't':
               {
                  IdentifierString configName(argv[i] + 2);

                  project.loadConfigByName(*dataPath, *configName, true);
                  break;
               }
               case 'v':
                  process.setVerboseOn();
                  break;
               case 'w':
                  if (argv[i][2] == '0') {
                     errorProcessor.setWarningLevel(WarningLevel::Level0);
                  }
                  else if (argv[i][2] == '1') {
                     errorProcessor.setWarningLevel(WarningLevel::Level1);
                  }
                  else if (argv[i][2] == '2') {
                     errorProcessor.setWarningLevel(WarningLevel::Level2);
                  }
                  else if (argv[i][2] == '3') {
                     errorProcessor.setWarningLevel(WarningLevel::Level3);
                  }
                  break;
               case 'x':
                  if (argv[i][2] == 'b') {
                     project.addBoolSetting(ProjectOption::ConditionalBoxing, argv[i][3] != '-');
                  }
                  else if (argv[i][2] == 'e') {
                     project.addBoolSetting(ProjectOption::EvaluateOp, argv[i][3] != '-');
                  }
                  else if (argv[i][2] == 'p') {
                     project.addBoolSetting(ProjectOption::GenerateParamNameInfo, argv[i][3] != '-');
                  }
                  break;
               default:
                  break;
            }
         }
         else if (PathUtil::checkExtension(argv[i], "project")) {
            PathString path(argv[i]);

            if (!project.loadProject(*path, *profile)) {
               errorProcessor.raisePathError(errProjectAlreadyLoaded, *path);
            }
            else if (profile.empty() && project.availableProfileList.count() != 0) {
               IdentifierString profileList;
               for (auto it = project.availableProfileList.start(); !it.eof(); ++it) {
                  if (profileList.length() != 0)
                     profileList.append(", ");

                  profileList.append(*it);
               }

               Presenter::getInstance().printLine(ELC_PROFILE_WARNING, *profileList);
            }
         }
         else {
            FileNameString fileName(argv[i]);

            project.addSource(*fileName, argv[i], nullptr, nullptr);
         }
      }

      if (cleanMode) {
         return process.clean(project);
      }
      else {
         // Building...
         return process.build(project, linker,
            DEFAULT_STACKALIGNMENT,
            DEFAULT_RAW_STACKALIGNMENT,
            DEFAULT_EHTABLE_ENTRY_SIZE,
            MINIMAL_ARG_LIST,
            *profile);
      }
   }
   catch (CLIException e)
   {
      return -2;
   }
}
