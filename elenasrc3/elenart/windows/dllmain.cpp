#include <cstdio> // !! temporal

#include "elena.h"
#include "rtcommon.h"
#include "elenartmachine.h"
#include "windows/winconsts.h"
// --------------------------------------------------------------------------------
#include <windows.h>

using namespace elena_lang;

#ifdef _M_IX86

constexpr auto CURRENT_PLATFORM = PlatformType::Win_x86;

#elif _M_X64

constexpr auto CURRENT_PLATFORM = PlatformType::Win_x86_64;

#endif

constexpr auto CONFIG_FILE = "elenart60.cfg";

#define EXTERN_DLL_EXPORT extern "C" __declspec(dllexport)

static ELENARTMachine* machine = nullptr;
static SystemEnv* systemEnv = nullptr;

void loadModulePath(HMODULE hModule, PathString& rootPath, bool includeName)
{
   wchar_t path[MAX_PATH + 1];

   ::GetModuleFileName(hModule, path, MAX_PATH);

   if (includeName) {
      rootPath.copy(path);
   }
   else rootPath.copySubPath(path, false);

   rootPath.lower();
}

void init(HMODULE hModule)
{
   // get DLL path
   PathString rootPath;
   loadModulePath(hModule, rootPath, false);

   // get EXE path
   PathString execPath;
   loadModulePath(0, execPath, true);

   PathString configPath(CONFIG_FILE);

   // !! temporal : hard-coded constants
   machine = new ELENARTMachine(
      *rootPath, *execPath, *configPath, CURRENT_PLATFORM,
      __routineProvider.RetrieveMDataPtr((void*)IMAGE_BASE, 0x1000000));
}

// --- API export ---

EXTERN_DLL_EXPORT void InitializeSTLA(SystemEnv* env, void* entry, void* criricalHandler)
{
   systemEnv = env;

#ifdef DEBUG_OUTPUT
   printf("InitializeSTA.6 %x,%x\n", (int)env, (int)criricalHandler);

   fflush(stdout);
#endif

   __routineProvider.InitSTAExceptionHandling(env, criricalHandler);

   machine->startSTA(env, entry);
}

EXTERN_DLL_EXPORT void InitializeMTLA(SystemEnv* env, SymbolList* entry, void* criricalHandler)
{
   systemEnv = env;

#ifdef DEBUG_OUTPUT
   printf("InitializeSTA.6 %x,%x\n", (int)env, (int)criricalHandler);

   fflush(stdout);
#endif

   int index = machine->allocateThreadEntry(env);
   __routineProvider.InitMTAExceptionHandling(env, index, criricalHandler);
   __routineProvider.InitMTASignals(env, index);

   machine->startSTA(env, entry);
}

EXTERN_DLL_EXPORT void ExitLA(int retVal)
{
   if (retVal) {
      printf("Aborted:%x\n", retVal);
      fflush(stdout);
   }

   __routineProvider.Exit(retVal);
}

// NOTE : arg must be unique for every separate thread
EXTERN_DLL_EXPORT void* CreateThreadLA(void* arg, void* threadProc, int flags)
{
   return machine->allocateThread(systemEnv, arg, threadProc, flags);
}

EXTERN_DLL_EXPORT void StartThreadLA(SystemEnv* env, void* criricalHandler, void* entryPoint, int index)
{
#ifdef DEBUG_OUTPUT
   printf("StartThreadLA.6 %x,%x\n", (int)env, (int)criricalHandler);

   fflush(stdout);
#endif

   __routineProvider.InitMTAExceptionHandling(env, index, criricalHandler);
   __routineProvider.InitMTASignals(env, index);

   machine->startThread(env, entryPoint, index);
}

EXTERN_DLL_EXPORT void* CollectPermGCLA(size_t size)
{
   return __routineProvider.GCRoutinePerm(systemEnv->gc_table, size);
}

EXTERN_DLL_EXPORT void* CollectGCLA(void* roots, size_t size)
{
//   printf("CollectGCLA %llx %llx\n", (long long)roots, size);

   return __routineProvider.GCRoutine(systemEnv->gc_table, (GCRoot*)roots, size, false);
}

EXTERN_DLL_EXPORT void* ForcedCollectGCLA(void* roots, int fullMode)
{
   return __routineProvider.GCRoutine(systemEnv->gc_table, (GCRoot*)roots, INVALID_SIZE, fullMode != 0);
}

EXTERN_DLL_EXPORT size_t LoadMessageNameLA(size_t message, char* buffer, size_t length)
{
   return machine->loadMessageName((mssg_t)message, buffer, length);
}

EXTERN_DLL_EXPORT size_t LoadActionNameLA(size_t message, char* buffer, size_t length)
{
   return machine->loadActionName((mssg_t)message, buffer, length);
}

EXTERN_DLL_EXPORT size_t LoadCallStackLA(uintptr_t framePtr, uintptr_t* list, size_t length)
{
   return __routineProvider.LoadCallStack(framePtr, list, length);
}

EXTERN_DLL_EXPORT size_t LoadAddressInfoLM(size_t retPoint, char* lineInfo, size_t length)
{
   return machine->loadAddressInfo(retPoint, lineInfo, length);
}

EXTERN_DLL_EXPORT addr_t LoadSymbolByStringLA(const char* symbolName)
{
   return machine->loadSymbol(symbolName);
}

EXTERN_DLL_EXPORT addr_t LoadClassByStringLA(const char* symbolName)
{
   return machine->loadClassReference(symbolName);
}

EXTERN_DLL_EXPORT addr_t LoadClassByBufferLA(void* referenceName, size_t index, size_t length)
{
   if (length < 0x100) {
      IdentifierString str((const char*)referenceName + index, length);

      return LoadClassByStringLA(*str);
   }
   else {
      DynamicString<char> str((const char*)referenceName, index, length);

      return LoadClassByStringLA(str.str());
   }
}

EXTERN_DLL_EXPORT addr_t LoadSymbolByString2LA(const char* ns, const char* symbolName)
{
   ReferenceName fullName(ns, symbolName);

   return machine->loadSymbol(*fullName);
}

EXTERN_DLL_EXPORT mssg_t LoadMessageLA(const char* messageName)
{
   return machine->loadMessage(messageName);
}

EXTERN_DLL_EXPORT mssg_t LoadActionLA(const char* actionName)
{
   return machine->loadAction(actionName);
}

/// <summary>
/// Fills the passed dispatch list with references to extension message overload list
/// </summary>
/// <param name="moduleList">List of imported modules separated by semicolon</param>
/// <param name="message">Extension message</param>
/// <param name="output">Dispatch list</param>
/// <returns></returns>
EXTERN_DLL_EXPORT int LoadExtensionDispatcherLA(const char* moduleList, mssg_t message, void* output)
{
   return machine->loadExtensionDispatcher(moduleList, message, output);
}

EXTERN_DLL_EXPORT void GetRandomSeedLA(SeedStruct& seed)
{
   machine->initRandomSeed(seed);
}

EXTERN_DLL_EXPORT unsigned int GetRandomIntLA(SeedStruct& seed)
{
   return machine->getRandomNumber(seed);
}

EXTERN_DLL_EXPORT void SignalStopGCLA(void* handle)
{
   SystemRoutineProvider::GCSignalStop(handle);
}

EXTERN_DLL_EXPORT void WaitForSignalGCLA(void* handle)
{
   SystemRoutineProvider::GCWaitForSignal(handle);
}

EXTERN_DLL_EXPORT void SignalClearGCLA(void* handle)
{
   SystemRoutineProvider::GCSignalClear(handle);
}

EXTERN_DLL_EXPORT void WaitForSignalsGCLA(size_t count, void* handles)
{
   SystemRoutineProvider::GCWaitForSignals(count, handles);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
       case DLL_PROCESS_ATTACH:
          init(hModule);
          break;
       case DLL_THREAD_ATTACH:
       case DLL_THREAD_DETACH:
       case DLL_PROCESS_DETACH:
           break;
       default:
          // to make compiler happy
          break;
    }
    return TRUE;
}
