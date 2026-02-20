#include <string>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>

#include <coreinit/cache.h>
#include <coreinit/exception.h>
#include <coreinit/filesystem.h>
#include <coreinit/memorymap.h>
#include <coreinit/title.h>
#include <kernel/kernel.h>
#include <whb/crash.h>
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/storage.h>
#include <notifications/notifications.h>


#include "utils/logger.h"
#include "globals.h"
#include "handler.h"

#define FS_MAX_LOCALPATH_SIZE           511
#define FS_MAX_MOUNTPATH_SIZE           128
#define FS_MAX_FULLPATH_SIZE            (FS_MAX_LOCALPATH_SIZE + FS_MAX_MOUNTPATH_SIZE)

#define ENABLED_CONFIG_ID "enabled"
#define NOTIFICATIONS_CONFIG_ID "notifications"

WUPS_PLUGIN_NAME("CafeLoader");
WUPS_PLUGIN_DESCRIPTION("Loader for custom code.");
WUPS_PLUGIN_VERSION("v1.1");
WUPS_PLUGIN_AUTHOR("AboodXD & JohnP55");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("cafeloader");

bool clientEnabled;
FSFileHandle file;
int fd;

bool enabled = true;
bool notifications = true;

void Notify(const char *text) {
    if (!notifications) return;
    NotificationModuleHandle handle;
    NotificationModule_AddDynamicNotification(text, &handle);
    NotificationModule_FinishDynamicNotification(handle, 3);
}

int exists(const char *fname) {
    int f = open(fname, O_RDONLY);

    if (f < 0)
        return 0;

    close(f);
    return 1;
}

uint32_t getFileLength(const char *fname) {
    struct stat fileStat;
    stat(fname, &fileStat);
    uint32_t length = fileStat.st_size;

    return length;
}

char * readBuf(const char *fname, int f) {
    char *buffer = 0;
    uint32_t length = getFileLength(fname);

    buffer = (char *)malloc(length);
    if (buffer)
        read(f, buffer, length);

    return buffer;
}

void KernelCopyDataV(void *dest, void *source, uint32_t len) {
    ICInvalidateRange(source, len);
    DCFlushRange(source, len);

    KernelCopyData(OSEffectiveToPhysical((uint32_t)dest), OSEffectiveToPhysical((uint32_t)source), len);

    ICInvalidateRange(dest, len);
    DCFlushRange(dest, len);
}

void Patch(char *buffer) {
    uint16_t count = *(uint16_t *)buffer; buffer += 2;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t bytes = *(uint16_t *)buffer; buffer += 2;
        uint32_t addr = *(uint32_t *)buffer; buffer += 4;
        KernelCopyDataV((void *)addr, buffer, bytes); buffer += bytes;
    }
}

void DeinitModules() {
    NotificationModule_DeInitLibrary();
}

INITIALIZE_PLUGIN() {
    // Logging only works when compiled with `make DEBUG=1`
    initLogging();

    NotificationModule_InitLibrary();

    // Open storage to read values
    /*
    WUPSStorageError storageRes;
    if (WUPS_OpenStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE("Failed to open storage %s (%d)", WUPS_GetStorageStatusStr(storageRes), storageRes);
    } else {
        // Try to get values from storage
        if ((storageRes = WUPSStorageAPI_GetBool(nullptr, ENABLED_CONFIG_ID, &enabled)) == WUPS_STORAGE_ERROR_NOT_FOUND) {
            // Add the value to the storage if it is missing
            if (WUPSStorageAPI_StoreBool(nullptr, ENABLED_CONFIG_ID, enabled) != WUPS_STORAGE_ERROR_SUCCESS) {
                DEBUG_FUNCTION_LINE("Failed to store bool");
            }
        } else if (storageRes != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE("Failed to get bool %s (%d)", WUPS_GetStorageStatusStr(storageRes), storageRes);
        }

        if ((storageRes = WUPSStorageAPI_GetBool(nullptr, NOTIFICATIONS_CONFIG_ID, &notifications)) == WUPS_STORAGE_ERROR_NOT_FOUND) {
            // Add the value to the storage if it is missing
            if (WUPSStorageAPI_StoreBool(nullptr, NOTIFICATIONS_CONFIG_ID, notifications) != WUPS_STORAGE_ERROR_SUCCESS) {
                DEBUG_FUNCTION_LINE("Failed to store bool");
            }
        } else if (storageRes != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE("Failed to get bool %s (%d)", WUPS_GetStorageStatusStr(storageRes), storageRes);
        }

        // Close storage
       // if (WUPS_CloseStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
       //     DEBUG_FUNCTION_LINE("Failed to close storage");
       // }
    }
    */
    deinitLogging();
}

DEINITIALIZE_PLUGIN() {
    NotificationModule_DeInitLibrary();
}

ON_APPLICATION_START() {
    initLogging();
    
    clientEnabled = false;

   // DEBUG_FUNCTION_LINE("Setting the ExceptionCallbacks\n");
   // OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, DSIHandler_Fatal);
   // OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, ISIHandler_Fatal);
   // OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, ProgramHandler_Fatal);

    char TitleIDString[FS_MAX_FULLPATH_SIZE];
    snprintf(TitleIDString,FS_MAX_FULLPATH_SIZE,"%016llX",OSGetTitleID());

    std::string patchTitleIDPath = "fs:/vol/external01/cafeloader/";
    patchTitleIDPath += TitleIDString;
    DEBUG_FUNCTION_LINE("patchTitleIDPath: %s\n", patchTitleIDPath.c_str());

    std::string patchesPath = patchTitleIDPath + "/Patches.hax";
    std::string addrPath    = patchTitleIDPath + "/Addr.bin";
    std::string codePath    = patchTitleIDPath + "/Code.bin";
    std::string dataPath    = patchTitleIDPath + "/Data.bin";
    std::string ipPath      = "fs:/vol/external01/cafeloader/ip.bin";

    uint32_t CODE_ADDR;
    uint32_t DATA_ADDR;

    uint32_t length = 0;
    uint16_t reply = 0;

    if (clientEnabled == false && exists(ipPath.c_str())) {
        DEBUG_FUNCTION_LINE("IP file found!\n");
      //  Notify("IP file found!");

        int   ipFile   = open(ipPath.c_str(), O_RDONLY);
        char *ipBuffer = readBuf(ipPath.c_str(), ipFile);

        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = 2557;
        serverAddr.sin_addr.s_addr = *(uint32_t *)ipBuffer;
        connect(fd, (struct sockaddr *)&serverAddr, 16);

        close(ipFile);
        free(ipBuffer);

        send(fd, "\x01", 1, 0);
        send(fd, TitleIDString, FS_MAX_FULLPATH_SIZE, 0);
        recv(fd, &reply, 2, 0);
        if (reply == 0xCAFE) {
            DEBUG_FUNCTION_LINE("Client connected!\n");
           // Notify("Client connected!");
            clientEnabled = true;
            file = 0;
        }
    }

    if (exists(patchesPath.c_str())) {
        DEBUG_FUNCTION_LINE("Patches.hax found!\n");
       // Notify("Patches.hax found!");

        int   patchesFile   = open(patchesPath.c_str(), O_RDONLY);
        char *patchesBuffer = readBuf(patchesPath.c_str(), patchesFile);
        Patch(patchesBuffer);

        close(patchesFile);
        free(patchesBuffer);

        DEBUG_FUNCTION_LINE("Loaded Patches.hax!\n");
      //  Notify("Loaded Patches.hax!");
    }

    if (exists(addrPath.c_str()) && exists(codePath.c_str()) && exists(dataPath.c_str())) {
        DEBUG_FUNCTION_LINE("Code patches found!\n");
        Notify("Code patches found!");

        int   addrFile   = open(addrPath.c_str(), O_RDONLY);
        char *addrBuffer = readBuf(addrPath.c_str(), addrFile);
        CODE_ADDR = *(uint32_t *)(addrBuffer + 0);
        DATA_ADDR = *(uint32_t *)(addrBuffer + 4);

        close(addrFile);
        free(addrBuffer);

        DEBUG_FUNCTION_LINE("Loaded Addr.bin!\n");
       // Notify("Loadded Addr.bin!");

        int   codeFile   = open(codePath.c_str(), O_RDONLY);
        char *codeBuffer = readBuf(codePath.c_str(), codeFile);
        length = getFileLength(codePath.c_str());
        KernelCopyDataV((void *)CODE_ADDR, codeBuffer, length);

        close(codeFile);
        free(codeBuffer);

      //  DEBUG_FUNCTION_LINE("Loaded Code.bin!\n");
        Notify("Loaded Code.bin!");

        int   dataFile   = open(dataPath.c_str(), O_RDONLY);
        char *dataBuffer = readBuf(dataPath.c_str(), dataFile);
        length = getFileLength(dataPath.c_str());
        KernelCopyDataV((void *)DATA_ADDR, dataBuffer, length);

        close(dataFile);
        free(dataBuffer);

        DEBUG_FUNCTION_LINE("Loaded Data.bin!\n");
      //  Notify("Loaded Data.bin!");

        void *debugPtr = (void *)&WHBLogPrintf;
        KernelCopyDataV((void *)(DATA_ADDR - 4), &debugPtr, sizeof(uint32_t));
        DEBUG_FUNCTION_LINE("WHBLogPrintf address: %p\n", &debugPtr);
    }
}

/* WUPS_GET_CONFIG() {
    // Open the storage, so we can persist the configuration the user made
    if (WUPS_OpenStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE("Failed to open storage");
        return 0;
    }

    WUPSConfigHandle config;
    WUPSConfig_CreateHandled(&config, "Code Patch Plugin");

    WUPSConfigCategoryHandle cat;
    WUPSConfig_AddCategoryByNameHandled(config, "Settings", &cat);

    WUPSConfigItemBoolean_AddToCategoryHandled(config, cat, ENABLED_CONFIG_ID, "Enabled (effective next application launch)", enabled, &enabledChanged);
    WUPSConfigItemBoolean_AddToCategoryHandled(config, cat, NOTIFICATIONS_CONFIG_ID, "Notifications", notifications, &notificationsChanged);

    return config;
} */