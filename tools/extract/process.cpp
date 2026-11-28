#include "extract/process.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace swm::extract {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& utf16) {
    if (utf16.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), int(utf16.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), int(utf16.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string path_utf8(const std::filesystem::path& path) {
    return narrow(path.wstring());
}

std::filesystem::path utf8_path(const std::string& utf8) {
    return std::filesystem::path(widen(utf8));
}

std::wstring command_line(const std::vector<std::string>& argv) {
    std::wstring out;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) {
            out += L' ';
        }
        const std::wstring arg = widen(argv[i]);
        if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
            out += arg;
            continue;
        }
        // Backslashes are literal unless they precede a quote: then each one
        // is doubled, and the quote escaped.
        out += L'"';
        size_t backslashes = 0;
        for (const wchar_t c : arg) {
            if (c == L'\\') {
                ++backslashes;
            } else if (c == L'"') {
                out.append(backslashes * 2 + 1, L'\\');
                out += L'"';
                backslashes = 0;
            } else {
                out.append(backslashes, L'\\');
                out += c;
                backslashes = 0;
            }
        }
        out.append(backslashes * 2, L'\\');
        out += L'"';
    }
    return out;
}

int run_process(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                const std::filesystem::path& log, const std::atomic<bool>* cancel) {
    if (argv.empty()) {
        return process_not_started;
    }
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    const HANDLE out = CreateFileW(log.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        return process_not_started;
    }
    const HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);

    // Only the log and NUL are inherited, not every inheritable handle we hold.
    HANDLE handles[2] = {out, in};
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<char> attr_buffer(attr_size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buffer.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                              in == INVALID_HANDLE_VALUE ? sizeof(HANDLE) : sizeof(handles), nullptr, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = out;
    si.StartupInfo.hStdError = out;
    si.StartupInfo.hStdInput = in == INVALID_HANDLE_VALUE ? nullptr : in;
    si.lpAttributeList = attrs;

    // A job kills the whole tree when cancelled or if we die.
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    std::wstring cmd = command_line(argv);
    const std::wstring dir = cwd.wstring();
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(widen(argv[0]).c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                   dir.empty() ? nullptr : dir.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(out);
    if (in != INVALID_HANDLE_VALUE) {
        CloseHandle(in);
    }
    if (!ok) {
        CloseHandle(job);
        return process_not_started;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    int result = 0;
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 100) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            result = int(code);
            break;
        }
        if (cancel && cancel->load()) {
            TerminateJobObject(job, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            result = process_cancelled;
            break;
        }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return result;
}

} // namespace swm::extract
