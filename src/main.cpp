#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <map>
#include <utility>

#include <string>


#include <coreinit/cache.h>
#include <coreinit/exception.h>
#include <coreinit/memorymap.h>
#include <coreinit/title.h>
#include <nsysnet/socket.h>
#include <utils/logger.h>
#include <wups.h>
#include <kernel/kernel.h>

#include "filesystem.h"
#include "handler.h"
#include "globals.h"

#define FS_MAX_LOCALPATH_SIZE           511
#define FS_MAX_MOUNTPATH_SIZE           141
#define FS_MAX_FULLPATH_SIZE            (FS_MAX_LOCALPATH_SIZE + FS_MAX_MOUNTPATH_SIZE)

WUPS_PLUGIN_NAME("CafeLoader");
WUPS_PLUGIN_DESCRIPTION("Loader for custom code.");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("AboodXD, techmuse (Aroma port)");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("cafeloader");

//WUPS_FS_ACCESS()
//WUPS_ALLOW_KERNEL()

bool clientEnabled;
FSFileHandle file;
int fd;

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

char* readBuf(const char *fname, int f) {
    char* buffer = 0;
    uint32_t length = getFileLength(fname);

    buffer = (char *)malloc(length);
    if (buffer)
        read(f, buffer, length);

    return buffer;
}

void KernelCopyDataWrapper(void *dest, void *source, uint32_t len) {
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
        KernelCopyDataWrapper((void *)addr, buffer, bytes); buffer += bytes;
    }
}

INITIALIZE_PLUGIN() {
    WHBLogCafeInit();
    WHBLogUdpInit();
    DEBUG_FUNCTION_LINE("Is this on\n");

}

ON_APPLICATION_START(){
   // initLogging();
    DEBUG_FUNCTION_LINE("In on app start\n");
    clientEnabled = false;

    DEBUG_FUNCTION_LINE("Setting the ExceptionCallbacks\n");
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, DSIHandler_Fatal);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, ISIHandler_Fatal);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, ProgramHandler_Fatal);

   // if (args.sd_mounted && args.kernel_access) {
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
                clientEnabled = true;
                file = 0;
            }
        }

        if (exists(patchesPath.c_str())) {
            DEBUG_FUNCTION_LINE("Patches.hax found!\n");

            int   patchesFile   = open(patchesPath.c_str(), O_RDONLY);
            char *patchesBuffer = readBuf(patchesPath.c_str(), patchesFile);
            Patch(patchesBuffer);

            close(patchesFile);
            free(patchesBuffer);

            DEBUG_FUNCTION_LINE("Loaded Patches.hax!\n");
        }

        if (exists(addrPath.c_str()) && exists(codePath.c_str()) && exists(dataPath.c_str())) {
            DEBUG_FUNCTION_LINE("Code patches found!\n");

            int   addrFile   = open(addrPath.c_str(), O_RDONLY);
            char *addrBuffer = readBuf(addrPath.c_str(), addrFile);
            CODE_ADDR = *(uint32_t *)(addrBuffer + 0);
            DATA_ADDR = *(uint32_t *)(addrBuffer + 4);

            close(addrFile);
            free(addrBuffer);

            DEBUG_FUNCTION_LINE("Loaded Addr.bin!\n");

            int   codeFile   = open(codePath.c_str(), O_RDONLY);
            char *codeBuffer = readBuf(codePath.c_str(), codeFile);
            length = getFileLength(codePath.c_str());
            KernelCopyDataWrapper((void *)CODE_ADDR, codeBuffer, length);

            close(codeFile);
            free(codeBuffer);

            DEBUG_FUNCTION_LINE("Loaded Code.bin!\n");

            int   dataFile   = open(dataPath.c_str(), O_RDONLY);
            char *dataBuffer = readBuf(dataPath.c_str(), dataFile);
            length = getFileLength(dataPath.c_str());
            KernelCopyDataWrapper((void *)DATA_ADDR, dataBuffer, length);

            close(dataFile);
            free(dataBuffer);

            DEBUG_FUNCTION_LINE("Loaded Data.bin!\n");

            uint32_t debugPtr = (uint32_t)&_printf_r;
            KernelCopyDataWrapper((void *)(DATA_ADDR - 4), &debugPtr, sizeof(uint32_t));

            DEBUG_FUNCTION_LINE("log_printf_ address: 0x%08X\n", debugPtr);
        }
   // }
}

ON_APPLICATION_ENDS() {
    DEBUG_FUNCTION_LINE("In on app end\n");
}