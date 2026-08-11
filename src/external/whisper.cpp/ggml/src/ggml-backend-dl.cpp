#include "ggml-backend-dl.h"

#include <cstdio>
#include <string>

#ifdef _WIN32

// Captured right after the failed LoadLibraryW so callers see WHY loading
// failed (ggml-cuda.dll most commonly fails with ERROR_MOD_NOT_FOUND when one
// of its bundled CUDA runtime deps - cudart/cublas/cublasLt - is missing).
thread_local std::string g_dl_last_error;

dl_handle * dl_load_library(const fs::path & path) {
    // suppress error dialogs for missing DLLs
    DWORD old_mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    SetErrorMode(old_mode | SEM_FAILCRITICALERRORS);

    HMODULE handle = LoadLibraryW(path.wstring().c_str());

    DWORD err = handle ? ERROR_SUCCESS : GetLastError();

    SetErrorMode(old_mode);

    if (handle) {
        g_dl_last_error.clear();
        return handle;
    }

    LPWSTR wmsg = nullptr;
    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR) &wmsg, 0, nullptr);
    std::string msg;
    if (n != 0 && wmsg) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wmsg, (int) n, nullptr, 0, nullptr, nullptr);
        msg.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, wmsg, (int) n, &msg[0], len, nullptr, nullptr);
        LocalFree(wmsg);
        while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n' || msg.back() == ' ')) {
            msg.pop_back();
        }
    }
    char prefix[96];
    snprintf(prefix, sizeof(prefix), "Windows error %lu", err);
    g_dl_last_error = std::string(prefix) + (msg.empty() ? std::string() : (" (" + msg + ")"));
    return nullptr;
}

void * dl_get_sym(dl_handle * handle, const char * name) {
    DWORD old_mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    SetErrorMode(old_mode | SEM_FAILCRITICALERRORS);

    void * p = (void *) GetProcAddress(handle, name);

    SetErrorMode(old_mode);

    return p;
}

const char * dl_error() {
    return g_dl_last_error.c_str();
}

#else

dl_handle * dl_load_library(const fs::path & path) {
    dl_handle * handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    return handle;
}

void * dl_get_sym(dl_handle * handle, const char * name) {
    return dlsym(handle, name);
}

const char * dl_error() {
    const char *rslt = dlerror();
    return rslt != nullptr ? rslt : "";
}

#endif
