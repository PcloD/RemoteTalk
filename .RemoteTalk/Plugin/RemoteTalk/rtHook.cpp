#include "pch.h"
#ifdef _WIN32
#include "rtHook.h"

namespace rt {

void* AllocExecutable(size_t size, void *location)
{
    static size_t base = (size_t)location;

    void *ret = nullptr;
    const size_t step = 0x10000; // 64kb
    for (size_t i = 0; ret == nullptr; ++i) {
        // increment address until VirtualAlloc() succeed
        // (MSDN says "If the memory is already reserved and is being committed, the address is rounded down to the next page boundary".
        //  https://msdn.microsoft.com/en-us/library/windows/desktop/aa366887.aspx
        //  but it seems VirtualAlloc() return nullptr if memory is already reserved and is being committed)
        ret = ::VirtualAlloc((void*)((size_t)base + (step*i)), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    return ret;
}

void* EmitJmpInstruction(void* from_, void* to_)
{
    BYTE *from = (BYTE*)from_;
    BYTE *to = (BYTE*)to_;
    BYTE *jump_from = from + 5;
    size_t distance = jump_from > to ? jump_from - to : to - jump_from;
    if (distance <= 0x7fff0000) {
        // 0xe9 [RVA]
        *(from++) = 0xe9;
        *(((DWORD*&)from)++) = (DWORD)(to - jump_from);
    }
    else {
        // 0xff 0x25 [RVA] [TargetAddr]
        *(from++) = 0xff;
        *(from++) = 0x25;
#ifdef _M_IX86
        *(((DWORD*&)from)++) = (DWORD)(from + 4);
#elif defined(_M_X64)
        *(((DWORD*&)from)++) = (DWORD)0;
#endif
        *(((DWORD_PTR*&)from)++) = (DWORD_PTR)(to);
    }
    return from;
}

bool IsValidMemory(const void *p)
{
    if (!p)
        return false;
    MEMORY_BASIC_INFORMATION meminfo;
    return ::VirtualQuery(p, &meminfo, sizeof(meminfo)) != 0 && meminfo.State != MEM_FREE;
}

bool IsValidModule(HMODULE mod)
{
    if (!mod)
        return false;
    auto MZ = (const char*)mod;
    if (MZ[0] != 'M' || MZ[1] != 'Z')
        return false;
    return true;
}

HMODULE GetModuleByAddr(const void * p)
{
    HMODULE mod = 0;
    ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)p, &mod);
    return mod;
}

std::string GetModuleDirectory(HMODULE mod)
{
    if (!mod)
        return std::string();
    char buf[MAX_PATH + 1];
    ::GetModuleFileNameA(mod, buf, sizeof(buf));
    std::string ret = buf;
    auto spos = ret.find_last_of("\\");
    if (spos != std::string::npos)
        ret.resize(spos);
    return ret;
}

std::string GetCurrentModuleDirectory()
{
    return GetModuleDirectory(GetModuleByAddr(&GetCurrentModuleDirectory));
}

std::string GetMainModulePath(HANDLE proc)
{
    std::string ret;
    rt::EnumerateModules(proc, [proc, &ret](HMODULE mod) {
        if (ret.empty()) {
            char buf[MAX_PATH + 1];
            ::GetModuleFileNameExA(proc, mod, buf, 1024);
            ret = buf;
        }
    });
    return ret;
}

std::string GetMainModulePath()
{
    char buf[MAX_PATH + 1];
    ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    return buf;
}

std::string GetMainModuleDirectory(HANDLE proc)
{
    auto ret = GetMainModulePath(proc);
    auto spos = ret.find_last_of("\\");
    if (spos != std::string::npos)
        ret.resize(spos);
    return ret;
}

std::string GetMainModuleDirectory()
{
    char buf[MAX_PATH + 1];
    ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    std::string ret = buf;
    auto spos = ret.find_last_of("\\");
    if (spos != std::string::npos)
        ret.resize(spos);
    return ret;
}



void* OverrideEAT(HMODULE mod, const char *funcname, void *replacement, void *&jump_table)
{
    if (!IsValidModule(mod))
        return nullptr;

    size_t ImageBase = (size_t)mod;
    auto pDosHeader = (PIMAGE_DOS_HEADER)ImageBase;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;

    auto pNTHeader = (PIMAGE_NT_HEADERS)(ImageBase + pDosHeader->e_lfanew);
    DWORD RVAExports = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (RVAExports == 0)
        return nullptr;

    auto *pExportDirectory = (IMAGE_EXPORT_DIRECTORY *)(ImageBase + RVAExports);
    DWORD *RVANames = (DWORD*)(ImageBase + pExportDirectory->AddressOfNames);
    WORD *RVANameOrdinals = (WORD*)(ImageBase + pExportDirectory->AddressOfNameOrdinals);
    DWORD *RVAFunctions = (DWORD*)(ImageBase + pExportDirectory->AddressOfFunctions);
    for (DWORD i = 0; i < pExportDirectory->NumberOfFunctions; ++i) {
        char *pName = (char*)(ImageBase + RVANames[i]);
        if (strcmp(pName, funcname) == 0) {
            void *before = (void*)(ImageBase + RVAFunctions[RVANameOrdinals[i]]);
            DWORD RVAJumpTable = (DWORD)((size_t)jump_table - ImageBase);
            ForceWrite<DWORD>(RVAFunctions[RVANameOrdinals[i]], RVAJumpTable);
            jump_table = EmitJmpInstruction(jump_table, replacement);
            return before;
        }
    }
    return nullptr;
}

void* OverrideIAT(HMODULE mod, const char *target_module, const char *target_funcname, void *replacement)
{
    if (!IsValidModule(mod))
        return nullptr;

    size_t ImageBase = (size_t)mod;
    auto pDosHeader = (PIMAGE_DOS_HEADER)ImageBase;
    auto pNTHeader = (PIMAGE_NT_HEADERS)(ImageBase + pDosHeader->e_lfanew);

    size_t RVAImports = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    auto *pImportDesc = (IMAGE_IMPORT_DESCRIPTOR*)(ImageBase + RVAImports);
    while (pImportDesc->Name != 0) {
        const char *dllname = (const char*)(ImageBase + pImportDesc->Name);
        if (stricmp(dllname, target_module) == 0) {
            auto **func_names = (IMAGE_IMPORT_BY_NAME**)(ImageBase + pImportDesc->Characteristics);
            void **import_table = (void**)(ImageBase + pImportDesc->FirstThunk);
            for (size_t i = 0; ; ++i) {
                if ((size_t)func_names[i] == 0) { break; }
                const char *funcname = (const char*)(ImageBase + (size_t)func_names[i]->Name);
                if (strcmp(funcname, target_funcname) == 0) {
                    void *before = import_table[i];
                    ForceWrite<void*>(import_table[i], replacement);
                    return before;
                }
            }
        }
        ++pImportDesc;
    }
    return nullptr;
}

void* Hotpatch(void *target, const void *replacement)
{
    DWORD old;
    BYTE *f = (BYTE*)target;
    void *orig_func = f + 2;
    ::VirtualProtect(f - 5, 7, PAGE_EXECUTE_READWRITE, &old);
    f[-5] = 0xE9; // jmp
    *(DWORD*)(f - 4) = (DWORD)((size_t)replacement - (size_t)f);
    f[0] = 0xEB; f[1] = 0xF9; // short jmp -7
    ::VirtualProtect(f - 5, 7, old, &old);
    return orig_func;
}

void EnumerateModules(HANDLE process, const std::function<void(HMODULE)>& body)
{
    std::vector<HMODULE> modules;
    DWORD num_modules;
    ::EnumProcessModules(process, nullptr, 0, &num_modules);
    modules.resize(num_modules / sizeof(HMODULE));
    ::EnumProcessModules(process, &modules[0], num_modules, &num_modules);
    for (size_t i = 0; i < modules.size(); ++i) {
        body(modules[i]);
    }
}
void EnumerateModules(const std::function<void(HMODULE)>& body)
{
    EnumerateModules(::GetCurrentProcess(), body);
}

void EnumerateDLLImports(HMODULE mod, const char *dllname, const std::function<void(const char*, void *&)> &body)
{
    if (!IsValidModule(mod))
        return;

    size_t ImageBase = (size_t)mod;
    auto pDosHeader = (PIMAGE_DOS_HEADER)ImageBase;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return;

    auto pNTHeader = (PIMAGE_NT_HEADERS)(ImageBase + pDosHeader->e_lfanew);
    DWORD RVAImports = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (RVAImports == 0)
        return;

    auto *pImportDesc = (IMAGE_IMPORT_DESCRIPTOR*)(ImageBase + RVAImports);
    while (pImportDesc->Name != 0) {
        const char *pDLLName = (const char*)(ImageBase + pImportDesc->Name);
        if (dllname == nullptr || _stricmp(pDLLName, dllname) == 0) {
            auto* pThunkOrig = (IMAGE_THUNK_DATA*)(ImageBase + pImportDesc->OriginalFirstThunk);
            auto* pThunk = (IMAGE_THUNK_DATA*)(ImageBase + pImportDesc->FirstThunk);
            while (pThunkOrig->u1.AddressOfData != 0) {
                if ((pThunkOrig->u1.Ordinal & 0x80000000) > 0) {
                    //DWORD Ordinal = pThunkOrig->u1.Ordinal & 0xffff;
                    // nameless function
                }
                else {
                    IMAGE_IMPORT_BY_NAME* pIBN = (IMAGE_IMPORT_BY_NAME*)(ImageBase + pThunkOrig->u1.AddressOfData);
                    body((char*)pIBN->Name, *(void**)pThunk);
                }
                ++pThunkOrig;
                ++pThunk;
            }
        }
        ++pImportDesc;
    }
}

void EnumerateDLLExports(HMODULE mod, const std::function<void(const char*, void *&)> &body)
{
    if (!IsValidModule(mod))
        return;

    size_t ImageBase = (size_t)mod;
    auto pDosHeader = (PIMAGE_DOS_HEADER)ImageBase;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return;

    auto pNTHeader = (PIMAGE_NT_HEADERS)(ImageBase + pDosHeader->e_lfanew);
    DWORD RVAExports = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (RVAExports == 0)
        return;

    auto *pExportDirectory = (IMAGE_EXPORT_DIRECTORY *)(ImageBase + RVAExports);
    DWORD *RVANames = (DWORD*)(ImageBase + pExportDirectory->AddressOfNames);
    WORD *RVANameOrdinals = (WORD*)(ImageBase + pExportDirectory->AddressOfNameOrdinals);
    DWORD *RVAFunctions = (DWORD*)(ImageBase + pExportDirectory->AddressOfFunctions);
    for (DWORD i = 0; i < pExportDirectory->NumberOfFunctions; ++i) {
        char *pName = (char*)(ImageBase + RVANames[i]);
        void *pFunc = (void*)(ImageBase + RVAFunctions[RVANameOrdinals[i]]);
        body(pName, pFunc);
    }
}


HANDLE FindProcess(const char *exe)
{
    DWORD aProcesses[1024], cbNeeded, cProcesses;
    if (!::EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
        return nullptr;

    cProcesses = cbNeeded / sizeof(DWORD);
    for (DWORD i = 0; i < cProcesses; i++) {
        DWORD pid = aProcesses[i];
        HANDLE hProcess = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess)
            continue;

        TCHAR szProcessName[MAX_PATH] = "\0";
        HMODULE hMod;
        DWORD size;
        if (::EnumProcessModules(hProcess, &hMod, sizeof(hMod), &size)) {
            ::GetModuleBaseNameA(hProcess, hMod, szProcessName, sizeof(szProcessName));
            if (_stricmp(szProcessName, exe) == 0)
                return  hProcess;
        }
        ::CloseHandle(hProcess);
    }
    return nullptr;
}

DWORD g_pid;

static BOOL CALLBACK CBEnumerateWindows(HWND w, LPARAM _body)
{
    DWORD pid;
    ::GetWindowThreadProcessId(w, &pid);
    if (pid != g_pid)
        return TRUE;

    auto& body = *(const std::function<void(HWND)>*)_body;
    body(w);
    return TRUE;
}
static BOOL CALLBACK CBEnumerateWindowsR(HWND w, LPARAM _body)
{
    DWORD pid;
    ::GetWindowThreadProcessId(w, &pid);
    if (pid != g_pid)
        return TRUE;

    auto& body = *(const std::function<void(HWND)>*)_body;
    body(w);
    ::EnumChildWindows(w, &CBEnumerateWindows, _body);
    return TRUE;
}

void EnumerateTopWindows(const std::function<void(HWND)>& body)
{
    g_pid = ::GetCurrentProcessId();
    ::EnumWindows(CBEnumerateWindows, (LPARAM)&body);
}
void EnumerateChildWindows(HWND parent, const std::function<void(HWND)>& body)
{
    g_pid = ::GetCurrentProcessId();
    ::EnumChildWindows(parent, &CBEnumerateWindows, (LPARAM)&body);
}
void EnumerateChildWindowsRecursive(HWND parent, const std::function<void(HWND)>& body)
{
    g_pid = ::GetCurrentProcessId();
    ::EnumChildWindows(parent, &CBEnumerateWindowsR, (LPARAM)&body);
}
void EnumerateAllWindows(const std::function<void(HWND)>& body)
{
    g_pid = ::GetCurrentProcessId();
    ::EnumWindows(CBEnumerateWindowsR, (LPARAM)&body);
}


void EnumerateThreads(DWORD pid, const std::function<void(DWORD)> &proc)
{
    HANDLE ss = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (ss != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        if (::Thread32First(ss, &te)) {
            do {
                if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(te.th32OwnerProcessID) &&
                    te.th32OwnerProcessID == pid)
                {
                    proc(te.th32ThreadID);
                }
                te.dwSize = sizeof(te);
            } while (::Thread32Next(ss, &te));
        }
        ::CloseHandle(ss);
    }
}

void EnumerateThreads(const std::function<void(DWORD)> &proc)
{
    EnumerateThreads(::GetCurrentProcessId(), proc);
}

DWORD GetMainThreadID()
{
    static DWORD s_ret;
    if (s_ret == 0) {
        uint64_t oldest = ~0llu;
        EnumerateThreads([&](DWORD tid) {
            HANDLE hThread = ::OpenThread(THREAD_QUERY_INFORMATION, TRUE, tid);
            if (hThread) {
                FILETIME ctime, etime, ktime, utime;
                if (::GetThreadTimes(hThread, &ctime, &etime, &ktime, &utime)) {
                    if ((uint64_t&)ctime < oldest) {
                        oldest = (uint64_t&)ctime;
                        s_ret = tid;
                    }
                }
                ::CloseHandle(hThread);
            }
            });
    }
    return s_ret;
}

bool IsInMainThread()
{
    return GetMainThreadID() == ::GetCurrentThreadId();
}


} // namespace rt
#endif // _WIN32
