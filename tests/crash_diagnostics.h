#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
inline LONG WINAPI CrashDiagnostic(EXCEPTION_POINTERS* exception) {
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    const auto address = reinterpret_cast<DWORD64>(exception->ExceptionRecord->ExceptionAddress);
    DWORD64 displacement = 0;
    std::fprintf(stderr, "crash_code=%lx address=%llx eip=%lx\n",
        exception->ExceptionRecord->ExceptionCode, address, exception->ContextRecord->Eip);
    if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol))
        std::fprintf(stderr, "crash_symbol=%s offset=%llu\n", symbol->Name, displacement);
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module)) {
        wchar_t filename[MAX_PATH]{};
        GetModuleFileNameW(module, filename, MAX_PATH);
        std::fwprintf(stderr, L"crash_module=%ls module_offset=%llx\n", filename,
            address - reinterpret_cast<DWORD64>(module));
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
