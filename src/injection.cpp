#define NOMINMAX

#include "injection.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>

namespace util {
    template<>
    void raii_free<HMODULE>(HMODULE &module)
    {
        if (!module)
            return;

        FreeLibrary(module);
        module = 0;
    }

    template<>
    void raii_initialize<HANDLE>(HANDLE &handle)
    {
        handle = INVALID_HANDLE_VALUE;
    }
    template<>
    void raii_free<HANDLE>(HANDLE &handle)
    {
        if (handle == INVALID_HANDLE_VALUE)
            return;

        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

namespace {
    static std::ptrdiff_t findRemoteBaseAddress(DWORD processId, const wchar_t *moduleBaseName)
    {
        util::raii<HANDLE> snapshot;
        MODULEENTRY32 me;
        me.dwSize = sizeof(me);

        do { // "If the function fails with ERROR_BAD_LENGTH, retry the function until it succeeds." [sic]
            *snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
        } while (*snapshot == INVALID_HANDLE_VALUE && GetLastError() == ERROR_BAD_LENGTH);

        if (*snapshot != INVALID_HANDLE_VALUE && Module32First(*snapshot, &me))
            do {
                if (_wcsicmp(me.szModule, moduleBaseName) != 0)
                    continue;

                return (std::ptrdiff_t)me.modBaseAddr;
            } while (Module32Next(*snapshot, &me));

        return 0;
    }

    class RemoteMemory
    {
        HANDLE      m_process = 0;
        void       *m_memory  = nullptr;
        std::size_t m_size = 0;

    public:
        bool allocate(HANDLE hProcess, std::size_t size)
        {
            m_process = hProcess;
            m_size    = size;

            if (m_process && size)
                m_memory = VirtualAllocEx(m_process, NULL, m_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

            return m_memory;
        }

        bool fill(void *data, std::size_t size)
        {
            if (!m_process || !m_memory || !m_size)
                return false;

            return WriteProcessMemory(m_process, m_memory, data, std::min(size, m_size), nullptr);
        }

        void *address() const
        {
            return m_memory;
        }

        std::size_t size() const
        {
            return m_size;
        }

        ~RemoteMemory()
        {
            if (m_process && m_memory)
                VirtualFreeEx(m_process, m_memory, 0, MEM_RELEASE);
        }
    };
}

uint32_t injection::process_id_for_name(const wchar_t* name)
{
    util::raii<HANDLE> snapshot;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    *snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (Process32First(*snapshot, &pe))
        do {
            if (_wcsicmp(pe.szExeFile, name) != 0)
                continue;

            return static_cast<uint32_t>(pe.th32ProcessID);
        } while (Process32Next(*snapshot, &pe));

    return 0;
}

bool injection::is_dll_loaded(uint32_t processId, const wchar_t* dllBaseName)
{
    return findRemoteBaseAddress(processId, dllBaseName) != 0;
}

std::ptrdiff_t injection::get_function_offset(const wchar_t* dllBaseName, const char* functionName)
{
    HMODULE dll = GetModuleHandle(dllBaseName);
    if (!dll)
        return 0;

    FARPROC func = GetProcAddress(dll, functionName);
    if (!func)
        return 0;

    return (char*)func - (char*)dll;
}

bool injection::call_remote_func(uint32_t       target,
                                 const wchar_t *dllBaseName,
                                 std::ptrdiff_t functionOffset,
                                 void          *argument,
                                 std::size_t    argumentLength,
                                 uint32_t      *returnValue,
                                 uint32_t       wait)
{
    // find the remote offset
    std::ptrdiff_t dllOffset;
    util::raii<HANDLE> process;
    util::raii<HANDLE> thread;
    RemoteMemory argumentMemory;
    void *remoteArg;

    // first, find the base address of the module
    dllOffset = findRemoteBaseAddress(target, dllBaseName);
    if (!dllOffset)
        return false;

    // then open the target process
    *process = OpenProcess(
        PROCESS_DUP_HANDLE|PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION,
        FALSE, target);
    if (!*process) {
        logger << "Failed to open process " << target << std::endl;
        return false;
    }

    // If necessary, allocate memory in the target process
    if (argumentLength) {
        if (!argumentMemory.allocate(*process, argumentLength)) {
            logger << "Failed to allocate remote memory" << std::endl;
            return false;
        }

        if (!argumentMemory.fill(argument, argumentLength)) {
            logger << "Failed to fill remote memory" << std::endl;
            return false;
        }

        remoteArg = argumentMemory.address();
    } else {
        remoteArg = argument;
    }

    // create a remote thread using the resolved function
    LPTHREAD_START_ROUTINE func = (LPTHREAD_START_ROUTINE)(dllOffset + functionOffset);
    *thread = CreateRemoteThread(*process, 0, 0, func, remoteArg, 0, NULL);
    if (!*thread) {
        logger << "Failed to start remote thread: " << GetLastError() << std::endl;
        return false;
    }

    // wait for the thread to finish
    if (wait || argumentLength)
        WaitForSingleObject(*thread, argumentLength ? INFINITE : (DWORD)wait);

    // grab the return value
    if (returnValue)
        GetExitCodeThread(thread, (DWORD*)returnValue);

    return true;
}


