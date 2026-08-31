#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>

#ifdef _MSC_VER

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <windows.h>
#include <dbghelp.h>
#include <eh.h>


#pragma comment(lib, "dbghelp.lib")

namespace CT::Util {

class SehException final : public std::exception {
public:
    SehException(
        unsigned int         code,
        void*                address,
        unsigned int         flags          = 0,
        unsigned long        parameterCount = 0,
        const ULONG_PTR*     parameters     = nullptr,
        _EXCEPTION_POINTERS* ep             = nullptr
    ) noexcept
    : mCode(code),
      mAddress(address),
      mFlags(flags),
      mParameterCount((std::min)(parameterCount, static_cast<unsigned long>(EXCEPTION_MAXIMUM_PARAMETERS))) {
        if (parameters && mParameterCount > 0) {
            for (size_t i = 0; i < mParameterCount; ++i) {
                mParameters[i] = parameters[i];
            }
        }
        buildMessage(ep);
    }

    unsigned int  code() const noexcept { return mCode; }
    void*         address() const noexcept { return mAddress; }
    unsigned int  flags() const noexcept { return mFlags; }
    unsigned long parameterCount() const noexcept { return mParameterCount; }
    ULONG_PTR     parameter(size_t index) const noexcept { return index < mParameterCount ? mParameters[index] : 0; }
    const char*   codeName() const noexcept { return codeNameFromCode(mCode); }

    const char* what() const noexcept override { return mWhat.data(); }

private:
    static const char* codeNameFromCode(unsigned int code) noexcept {
        switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:
            return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:
            return "EXCEPTION_FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION:
            return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:
            return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:
            return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:
            return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:
            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:
            return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:
            return "EXCEPTION_INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:
            return "EXCEPTION_SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:
            return "EXCEPTION_STACK_OVERFLOW";
        default:
            return "UNKNOWN_SEH";
        }
    }

    static const char* accessTypeFromParameter(ULONG_PTR p0) noexcept {
        switch (p0) {
        case 0:
            return "read";
        case 1:
            return "write";
        case 8:
            return "execute";
        default:
            return "unknown";
        }
    }

    void appendf(size_t& offset, const char* fmt, ...) noexcept {
        if (offset >= mWhat.size()) return;
        va_list args;
        va_start(args, fmt);
        int n = std::vsnprintf(mWhat.data() + offset, mWhat.size() - offset, fmt, args);
        va_end(args);
        if (n <= 0) return;
        offset += static_cast<size_t>(n);
        if (offset >= mWhat.size()) {
            offset        = mWhat.size() - 1;
            mWhat[offset] = '\0';
        }
    }

    static const char* leafName(const char* path) noexcept {
        if (!path) return "<unknown>";
        const char* last = path;
        for (const char* p = path; *p != '\0'; ++p) {
            if (*p == '\\' || *p == '/') {
                last = p + 1;
            }
        }
        return last;
    }

    static void ensureDbghelpInitialized() noexcept {
        static std::once_flag once;
        std::call_once(once, [] {
            HANDLE process = GetCurrentProcess();
            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
            (void)SymInitialize(process, nullptr, TRUE);
        });
    }

    static std::mutex& dbghelpMutex() noexcept {
        static std::mutex m;
        return m;
    }

    void appendStackFrame(size_t& offset, size_t index, DWORD64 address) noexcept {
        if (address == 0) {
            return;
        }
        HANDLE    process              = GetCurrentProcess();
        char      modulePath[MAX_PATH] = "<unknown>";
        uintptr_t moduleBase           = 0;
        HMODULE   module               = nullptr;

        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(address)),
                &module
            )) {
            moduleBase = reinterpret_cast<uintptr_t>(module);
            (void)GetModuleFileNameA(module, modulePath, static_cast<DWORD>(sizeof(modulePath)));
            modulePath[sizeof(modulePath) - 1] = '\0';
        }

        const unsigned long long rel =
            moduleBase == 0 ? 0ull : static_cast<unsigned long long>(address - static_cast<DWORD64>(moduleBase));

        appendf(
            offset,
            "\n  #%02llu %s+0x%llX [0x%llX]",
            static_cast<unsigned long long>(index),
            leafName(modulePath),
            rel,
            static_cast<unsigned long long>(address)
        );

        SYMBOL_INFO_PACKAGE sip{};
        sip.si.SizeOfStruct     = sizeof(SYMBOL_INFO);
        sip.si.MaxNameLen       = MAX_SYM_NAME;
        DWORD64 symDisplacement = 0;
        if (SymFromAddr(process, address, &symDisplacement, &sip.si)) {
            appendf(offset, " %s", sip.si.Name);
            if (symDisplacement != 0) {
                appendf(offset, "+0x%llX", static_cast<unsigned long long>(symDisplacement));
            }
        }

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct      = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line) && line.FileName) {
            appendf(offset, " (%s:%lu", leafName(line.FileName), static_cast<unsigned long>(line.LineNumber));
            if (lineDisplacement != 0) {
                appendf(offset, "+0x%lX", static_cast<unsigned long>(lineDisplacement));
            }
            appendf(offset, ")");
        }
    }

    void appendStackTrace(size_t& offset, _EXCEPTION_POINTERS* ep) noexcept {
        if (!ep || !ep->ContextRecord) {
            appendf(offset, "\nStack trace (module+offset): <unavailable>");
            return;
        }

        ensureDbghelpInitialized();
        std::lock_guard<std::mutex> lock(dbghelpMutex());

        CONTEXT context = *ep->ContextRecord;

        STACKFRAME64 frame{};
        DWORD        machineType = 0;

#if defined(_M_X64)
        machineType            = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset    = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
        machineType            = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset    = context.Eip;
        frame.AddrFrame.Offset = context.Ebp;
        frame.AddrStack.Offset = context.Esp;
#else
        appendf(offset, "\nStack trace (module+offset): <unsupported arch>");
        return;
#endif
        frame.AddrPC.Mode    = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        HANDLE process = GetCurrentProcess();
        HANDLE thread  = GetCurrentThread();

        appendf(offset, "\nStack trace (module+offset):");

        constexpr size_t kMaxFrames = 32;
        size_t           frameIndex = 0;
        while (frameIndex < kMaxFrames && frame.AddrPC.Offset != 0) {
            appendStackFrame(offset, frameIndex, frame.AddrPC.Offset);
            ++frameIndex;

            BOOL ok = StackWalk64(
                machineType,
                process,
                thread,
                &frame,
                &context,
                nullptr,
                SymFunctionTableAccess64,
                SymGetModuleBase64,
                nullptr
            );
            if (!ok) {
                break;
            }
        }

        if (frameIndex == 0) {
            appendf(offset, " <empty>");
        }
    }

    void buildMessage(_EXCEPTION_POINTERS* ep) noexcept {
        mWhat.fill('\0');
        size_t offset = 0;
        appendf(
            offset,
            "Structured exception: %s (code=0x%08X, address=%p, flags=0x%X)",
            codeName(),
            mCode,
            mAddress,
            mFlags
        );

        if ((mCode == EXCEPTION_ACCESS_VIOLATION || mCode == EXCEPTION_IN_PAGE_ERROR) && mParameterCount >= 2) {
            appendf(
                offset,
                ", access=%s, target=%p",
                accessTypeFromParameter(mParameters[0]),
                reinterpret_cast<void*>(mParameters[1])
            );
            if (mCode == EXCEPTION_IN_PAGE_ERROR && mParameterCount >= 3) {
                appendf(offset, ", ntstatus=0x%08llX", static_cast<unsigned long long>(mParameters[2]));
            }
        }

        if (mParameterCount > 0) {
            appendf(offset, ", params=[");
            for (size_t i = 0; i < mParameterCount; ++i) {
                appendf(offset, i == 0 ? "0x%llX" : ",0x%llX", static_cast<unsigned long long>(mParameters[i]));
            }
            appendf(offset, "]");
        }

        appendStackTrace(offset, ep);
    }

    unsigned int                                        mCode{};
    void*                                               mAddress{};
    unsigned int                                        mFlags{};
    unsigned long                                       mParameterCount{};
    std::array<ULONG_PTR, EXCEPTION_MAXIMUM_PARAMETERS> mParameters{};
    std::array<char, 8192>                              mWhat{};
};

// Installs a per-thread SEH translator and restores the previous one on destruction.
class SehTranslatorGuard final {
public:
    SehTranslatorGuard() : mPrev(_set_se_translator(&translate)) {}
    ~SehTranslatorGuard() { _set_se_translator(mPrev); }

    SehTranslatorGuard(const SehTranslatorGuard&)            = delete;
    SehTranslatorGuard& operator=(const SehTranslatorGuard&) = delete;
    SehTranslatorGuard(SehTranslatorGuard&&)                 = delete;
    SehTranslatorGuard& operator=(SehTranslatorGuard&&)      = delete;

private:
    static void translate(unsigned int code, _EXCEPTION_POINTERS* ep) {
        void*            addr           = nullptr;
        unsigned int     flags          = 0;
        unsigned long    parameterCount = 0;
        const ULONG_PTR* parameters     = nullptr;
        if (ep && ep->ExceptionRecord) {
            auto* er       = ep->ExceptionRecord;
            addr           = er->ExceptionAddress;
            flags          = er->ExceptionFlags;
            parameterCount = er->NumberParameters;
            parameters     = er->ExceptionInformation;
        }
        throw SehException(code, addr, flags, parameterCount, parameters, ep);
    }

    _se_translator_function mPrev{};
};

} // namespace CT::Util

#else

namespace CT::Util {
class SehException : public std::exception {
public:
    unsigned int  code() const noexcept { return 0; }
    void*         address() const noexcept { return nullptr; }
    unsigned int  flags() const noexcept { return 0; }
    unsigned long parameterCount() const noexcept { return 0; }
    uintptr_t     parameter(size_t) const noexcept { return 0; }
    const char*   codeName() const noexcept { return "SEH_UNAVAILABLE"; }
    const char*   what() const noexcept override {
        return "Structured exception translation unavailable on this platform";
    }
};
class SehTranslatorGuard {};
} // namespace CT::Util

#endif
