#pragma once

#include <cstdint>
#include <cstddef>

namespace injection {
    /**
     * Searches for a process with the given name
     *
     * @return The process ID, or 0 if the process couldn't be found.
     */
    uint32_t process_id_for_name(const wchar_t *processName);

    /**
     * Checks whether the given DLL is loaded by the given process
     */
    bool     is_dll_loaded(uint32_t processId, const wchar_t *dllBaseName);

    /**
     * Calls a function on a DLL in the target process.
     *
     * @param target            Process ID of the target process.
     * @param dllBaseName       Base name of the DLL which contains the callee.
     * @param functionOffset    Offset of a function conforming to the ThreadProc prototype inside the DLL.
     * @param argument          Argument to pass into the DLL.
     * @param argumentLength    Length of the memory pointed to by @a argument. If @a argument does not point
     *                          to a block of memory, this parameter can be 0. If @a argumentLength is >0, the
     *                          memory block pointed to by @a argument is copied into the address space of the
     *                          target process, and @a argument is adjusted properly before passing it into the
     *                          remote function.
     * @param returnValue       Pointer to a memory location where an eventual return value of the function
     *                          can be stored. If the timeout elapses, the memory will be left untouched.
     * @param wait              Time to wait for the injected thread in milliseconds. If @a argumentLength is
     *                          greater than zero, the timeout will always be inifinite as the allocated memory
     *                          has to be freed somehow.
     *
     * @returns whether the call was made successfully
     */
    bool     call_remote_func(uint32_t target,
                              const wchar_t *dllBaseName,
                              std::ptrdiff_t functionOffset,
                              void *argument = nullptr,
                              std::size_t argumentLength = 0,
                              uint32_t *returnValue = nullptr,
                              uint32_t wait = 0xFFFFFFFF);

    /**
     * Utility function: Retrieve function offset of a locally loaded DLL
     *
     * @returns the offset, or 0 if the offset could not be determined.
     */
    std::ptrdiff_t get_function_offset(const wchar_t *dllBaseName, const char *functionName);
}