#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "os_util.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace imforge::builder {

namespace fs = std::filesystem;

namespace {

// only web URLs and existing folders go to the shell, a file path would get executed
bool IsOpenableTarget(const std::string& target) {
    if (target.empty()) return false;
    if (target.compare(0, 8, "https://") == 0 || target.compare(0, 7, "http://") == 0) return true;
    std::error_code ec;
    return fs::is_directory(fs::u8path(target), ec);
}

}  // namespace

std::string HomeDir() {
#ifdef _WIN32
    if (const char* p = std::getenv("USERPROFILE")) return p;
#else
    if (const char* p = std::getenv("HOME")) return p;
#endif
    return ".";
}

#ifdef _WIN32

namespace {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

bool RunFileDialog(std::string& out, const char* title, bool folders, const char* extension, const std::string& initial) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool uninit = SUCCEEDED(init);
    IFileOpenDialog* dlg = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (folders) opts |= FOS_PICKFOLDERS;
        dlg->SetOptions(opts);
        std::wstring wtitle = Widen(title ? title : "Select");
        dlg->SetTitle(wtitle.c_str());
        std::wstring wext;
        std::wstring wspec;
        COMDLG_FILTERSPEC spec[2];
        if (!folders && extension && *extension) {
            wext = Widen(extension);
            wspec = L"*." + wext;
            spec[0].pszName = wext.c_str();
            spec[0].pszSpec = wspec.c_str();
            spec[1].pszName = L"All files";
            spec[1].pszSpec = L"*.*";
            dlg->SetFileTypes(2, spec);
        }
        if (!initial.empty()) {
            IShellItem* folder = nullptr;
            std::wstring winit = Widen(fs::path(initial).make_preferred().string());
            if (SUCCEEDED(SHCreateItemFromParsingName(winit.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder) {
                dlg->SetFolder(folder);
                folder->Release();
            }
        }
        if (SUCCEEDED(dlg->Show(GetActiveWindow()))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    out = Narrow(path);
                    CoTaskMemFree(path);
                    ok = !out.empty();
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (uninit) CoUninitialize();
    return ok;
}

}  // namespace

bool OpenExternal(const std::string& target) {
    if (!IsOpenableTarget(target)) return false;
    std::wstring w = Widen(target);
    INT_PTR r = (INT_PTR)ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return r > 32;
}

bool NativePickFolder(std::string& out, const char* title, const std::string& initial_dir) {
    return RunFileDialog(out, title, true, nullptr, initial_dir);
}

bool NativePickFile(std::string& out, const char* title, const char* extension, const std::string& initial_dir) {
    return RunFileDialog(out, title, false, extension, initial_dir);
}

bool NativeDialogsAvailable() { return true; }

#else

namespace {

bool HasTool(const char* name) {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

std::string ShellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    return out + "'";
}

bool RunZenity(std::string& out, const char* title, bool folders, const char* extension, const std::string& initial) {
    if (!HasTool("zenity")) return false;
    std::string cmd = "zenity --file-selection --title=" + ShellQuote(title ? title : "Select");
    if (folders) cmd += " --directory";
    else if (extension && *extension) cmd += " --file-filter=" + ShellQuote(std::string("*.") + extension);
    if (!initial.empty()) cmd += " --filename=" + ShellQuote(initial + "/");
    cmd += " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char buf[4096];
    std::string result;
    while (fgets(buf, sizeof(buf), p)) result += buf;
    int rc = pclose(p);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    if (rc != 0 || result.empty()) return false;
    out = result;
    return true;
}

}  // namespace

bool OpenExternal(const std::string& target) {
    if (!IsOpenableTarget(target)) return false;
    std::string safe = target[0] == '-' ? "./" + target : target;
#ifdef __APPLE__
    std::string cmd = "open " + ShellQuote(safe) + " >/dev/null 2>&1 &";
#else
    std::string cmd = "xdg-open " + ShellQuote(safe) + " >/dev/null 2>&1 &";
#endif
    return std::system(cmd.c_str()) == 0;
}

bool NativePickFolder(std::string& out, const char* title, const std::string& initial_dir) {
    return RunZenity(out, title, true, nullptr, initial_dir);
}

bool NativePickFile(std::string& out, const char* title, const char* extension, const std::string& initial_dir) {
    return RunZenity(out, title, false, extension, initial_dir);
}

bool NativeDialogsAvailable() {
    static int cached = -1;
    if (cached < 0) cached = HasTool("zenity") ? 1 : 0;
    return cached == 1;
}

#endif

}  // namespace imforge::builder
