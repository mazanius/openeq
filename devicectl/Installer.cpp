#include "Installer.h"

#include <Endpoints.h>
#include <OpenEqShared.h>

#include <windows.h>
#include <aclapi.h>
#include <audioclient.h>
#include <sddl.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace openeq::ctl {

using namespace openeq::shared;

namespace {

constexpr REGSAM kWow64 = KEY_WOW64_64KEY;

// --------------------------------------------------------------- helpers

std::wstring knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

std::wstring errorText(DWORD err)
{
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring s = buf ? buf : L"";
    LocalFree(buf);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' ')) s.pop_back();
    return s + L" (" + std::to_wstring(err) + L")";
}

std::wstring exeDirectory()
{
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}

bool enablePrivilege(const wchar_t* name)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(token, FALSE, &tp, sizeof tp, nullptr, nullptr) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

class RegKey {
public:
    RegKey() = default;
    explicit RegKey(HKEY h) : m_h(h) {}
    ~RegKey() { if (m_h) RegCloseKey(m_h); }
    RegKey(const RegKey&) = delete;
    RegKey& operator=(const RegKey&) = delete;
    RegKey(RegKey&& o) noexcept : m_h(o.m_h) { o.m_h = nullptr; }
    HKEY get() const { return m_h; }
    explicit operator bool() const { return m_h != nullptr; }

private:
    HKEY m_h = nullptr;
};

// Opens (creating if needed) an HKLM key for writing. MMDevices keys are
// often ACL'd to SYSTEM/TrustedInstaller only; fall back to backup/restore
// semantics, which administrators hold, in that case.
RegKey openForWrite(const std::wstring& path, bool create, LSTATUS* status = nullptr)
{
    HKEY h = nullptr;
    LSTATUS st = create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, 0,
                                          KEY_READ | KEY_WRITE | kWow64, nullptr, &h, nullptr)
                        : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | KEY_WRITE | kWow64, &h);
    if (st == ERROR_ACCESS_DENIED) {
        enablePrivilege(SE_BACKUP_NAME);
        enablePrivilege(SE_RESTORE_NAME);
        st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, REG_OPTION_BACKUP_RESTORE,
                             KEY_READ | KEY_WRITE | kWow64, nullptr, &h, nullptr);
    }
    if (status) *status = st;
    return RegKey(st == ERROR_SUCCESS ? h : nullptr);
}

RegKey openForRead(const std::wstring& path)
{
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | kWow64, &h) != ERROR_SUCCESS) return RegKey();
    return RegKey(h);
}

struct RawValue {
    DWORD type = REG_NONE;
    std::vector<BYTE> data;
};

std::optional<RawValue> readValue(HKEY key, const std::wstring& name)
{
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return std::nullopt;
    RawValue v;
    v.type = type;
    v.data.resize(size);
    if (RegQueryValueExW(key, name.c_str(), nullptr, &type, v.data.data(), &size) != ERROR_SUCCESS)
        return std::nullopt;
    v.data.resize(size);
    return v;
}

LSTATUS writeValue(HKEY key, const std::wstring& name, const RawValue& v)
{
    return RegSetValueExW(key, name.c_str(), 0, v.type, v.data.data(), static_cast<DWORD>(v.data.size()));
}

RawValue stringValue(const std::wstring& s)
{
    RawValue v;
    v.type = REG_SZ;
    const auto* p = reinterpret_cast<const BYTE*>(s.c_str());
    v.data.assign(p, p + (s.size() + 1) * sizeof(wchar_t));
    return v;
}

RawValue multiStringValue(const std::vector<std::wstring>& items)
{
    std::wstring flat;
    for (const auto& i : items) { flat += i; flat.push_back(L'\0'); }
    flat.push_back(L'\0');
    RawValue v;
    v.type = REG_MULTI_SZ;
    const auto* p = reinterpret_cast<const BYTE*>(flat.data());
    v.data.assign(p, p + flat.size() * sizeof(wchar_t));
    return v;
}

RawValue dwordValue(DWORD d)
{
    RawValue v;
    v.type = REG_DWORD;
    const auto* p = reinterpret_cast<const BYTE*>(&d);
    v.data.assign(p, p + sizeof d);
    return v;
}

std::wstring asString(const RawValue& v)
{
    if ((v.type != REG_SZ && v.type != REG_EXPAND_SZ) || v.data.size() < sizeof(wchar_t)) return {};
    std::wstring s(reinterpret_cast<const wchar_t*>(v.data.data()), v.data.size() / sizeof(wchar_t));
    while (!s.empty() && s.back() == L'\0') s.pop_back();
    return s;
}

std::vector<std::wstring> asMultiString(const RawValue& v)
{
    std::vector<std::wstring> out;
    if (v.type != REG_MULTI_SZ) return out;
    const auto* p = reinterpret_cast<const wchar_t*>(v.data.data());
    const size_t n = v.data.size() / sizeof(wchar_t);
    size_t i = 0;
    while (i < n) {
        std::wstring s;
        while (i < n && p[i] != L'\0') s.push_back(p[i++]);
        ++i;
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

bool sameGuid(const std::wstring& a, const std::wstring& b)
{
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring endpointRoot(const std::wstring& guid) { return std::wstring(kRegRenderRoot) + L"\\" + guid; }
std::wstring stateKey(const std::wstring& guid) { return std::wstring(kRegEndpoints) + L"\\" + guid; }

// Records the original value (or its absence) before we change it.
class Backup {
public:
    Backup(HKEY backupKey) : m_key(backupKey) {}

    void save(const wchar_t* scope, HKEY source, const std::wstring& name)
    {
        const std::wstring tag = std::wstring(scope) + L"|" + name;
        if (auto v = readValue(source, name)) writeValue(m_key, tag, *v);
        else m_absent.push_back(tag);
    }

    void commit()
    {
        if (!m_absent.empty()) writeValue(m_key, L"absent", multiStringValue(m_absent));
    }

private:
    HKEY m_key;
    std::vector<std::wstring> m_absent;
};

} // namespace

// ================================================================ public

bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION e{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &e, sizeof e, &size) && e.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

std::wstring dataDirectory()
{
    return knownFolder(FOLDERID_ProgramData) + L"\\" + kDataDirName;
}

std::wstring installDirectory()
{
    return knownFolder(FOLDERID_ProgramFiles) + L"\\openeq";
}

bool Installer::ensureDataDirs()
{
    const std::wstring root = dataDirectory();
    const std::wstring devices = root + L"\\" + kDevicesDirName;
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(devices.c_str(), nullptr);

    // SYSTEM, Administrators: full. Users: modify (the UI writes configs).
    // LOCAL SERVICE (audiodg.exe): modify (reads configs, writes apo.log).
    const wchar_t* sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;BU)(A;OICI;0x1301bf;;;LS)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) {
        log(L"Could not build ACL: " + errorText(GetLastError()));
        return false;
    }
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
    const DWORD err = SetNamedSecurityInfoW(const_cast<LPWSTR>(root.c_str()), SE_FILE_OBJECT,
                                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                            nullptr, dacl, nullptr);
    LocalFree(sd);
    if (err != ERROR_SUCCESS) {
        log(L"Could not set permissions on " + root + L": " + errorText(err));
        return false;
    }
    return true;
}

bool Installer::installDll()
{
    const std::wstring src = exeDirectory() + L"\\" + kApoDllName;
    const std::wstring dir = installDirectory();
    const std::wstring dst = dir + L"\\" + kApoDllName;

    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        log(L"Cannot find " + src);
        return false;
    }

    if (_wcsicmp(src.c_str(), dst.c_str()) != 0) {
        CreateDirectoryW(dir.c_str(), nullptr);
        if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
            // audiodg.exe may have the old DLL loaded: move it aside and
            // delete it at next reboot, then copy the new one in.
            const std::wstring old = dst + L".old-" + std::to_wstring(GetTickCount64());
            if (!MoveFileExW(dst.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) ||
                !CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
                log(L"Could not copy the APO to " + dst + L": " + errorText(GetLastError()));
                return false;
            }
            MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
        log(L"Copied APO to " + dst);
    }

    HMODULE dll = LoadLibraryExW(dst.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) {
        log(L"Could not load " + dst + L": " + errorText(GetLastError()));
        return false;
    }
    auto reg = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll, "DllRegisterServer"));
    const HRESULT hr = reg ? reg() : E_NOINTERFACE;
    FreeLibrary(dll);
    if (FAILED(hr)) {
        log(L"DllRegisterServer failed: " + errorText(static_cast<DWORD>(hr)));
        return false;
    }
    log(L"Registered APO " + std::wstring(kApoClsidString));
    return true;
}

bool Installer::unregisterDll()
{
    const std::wstring dst = installDirectory() + L"\\" + kApoDllName;
    HMODULE dll = LoadLibraryExW(dst.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) return true; // nothing installed
    auto unreg = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll, "DllUnregisterServer"));
    const HRESULT hr = unreg ? unreg() : E_NOINTERFACE;
    FreeLibrary(dll);
    if (FAILED(hr)) log(L"DllUnregisterServer failed: " + errorText(static_cast<DWORD>(hr)));
    else log(L"Unregistered APO");
    return SUCCEEDED(hr);
}

bool Installer::install(const std::wstring& rawGuid)
{
    const std::wstring guid = normaliseGuid(rawGuid);
    if (!openForRead(endpointRoot(guid))) {
        log(L"No render endpoint " + guid);
        return false;
    }
    if (!ensureDataDirs()) return false;
    if (!installDll()) return false;

    if (isInstalledOn(guid)) {
        log(L"openeq is already installed on " + guid);
        return true;
    }

    LSTATUS st = ERROR_SUCCESS;
    RegKey fx = openForWrite(endpointRoot(guid) + L"\\FxProperties", true, &st);
    if (!fx) {
        log(L"Cannot open FxProperties for writing: " + errorText(st));
        return false;
    }
    RegKey state = openForWrite(stateKey(guid), true, &st);
    RegKey backupKey = openForWrite(stateKey(guid) + L"\\Backup", true, &st);
    if (!state || !backupKey) {
        log(L"Cannot write install state: " + errorText(st));
        return false;
    }
    Backup backup(backupKey.get());

    // Pick the slot. Taking an empty slot is always preferred over replacing
    // (and hosting) an existing effect:
    //   1. composite mode-effect list (Windows 11 drivers): append ourselves
    //   2. modern layout (SFX/MFX/EFX keys): empty EFX, then empty MFX,
    //      and only as a last resort replace and host the existing EFX
    //   3. legacy layout: post-mix slot, hosting what was there
    std::wstring slot, wrapped;
    bool composite = false;
    const auto compositeMode = readValue(fx.get(), kFxCompositeMode);
    const auto mode = readValue(fx.get(), kFxMode);
    const auto stream = readValue(fx.get(), kFxStream);
    const auto endpoint = readValue(fx.get(), kFxEndpoint);
    const auto postMix = readValue(fx.get(), kFxPostMix);
    const bool modeEmpty = !mode || asString(*mode).empty();
    const bool endpointEmpty = !endpoint || asString(*endpoint).empty();

    // Writes our CLSID into a modern slot, adding its processing-modes list if missing.
    auto takeSlot = [&](const wchar_t* fxSlot, const wchar_t* modesKey) {
        slot = fxSlot;
        backup.save(L"fx", fx.get(), slot);
        backup.save(L"fx", fx.get(), modesKey);
        LSTATUS s = writeValue(fx.get(), slot, stringValue(kApoClsidString));
        if (s == ERROR_SUCCESS && !readValue(fx.get(), modesKey))
            s = writeValue(fx.get(), modesKey, multiStringValue({kModeDefault}));
        return s;
    };

    if (compositeMode && compositeMode->type == REG_MULTI_SZ) {
        slot = kFxCompositeMode;
        composite = true;
        backup.save(L"fx", fx.get(), slot);
        auto list = asMultiString(*compositeMode);
        if (std::none_of(list.begin(), list.end(), [](auto& s) { return sameGuid(s, kApoClsidString); }))
            list.push_back(kApoClsidString);
        st = writeValue(fx.get(), slot, multiStringValue(list));
    } else if (stream || mode || endpoint) {
        if (endpointEmpty) {
            st = takeSlot(kFxEndpoint, kFxEfxModes);
        } else if (modeEmpty) {
            st = takeSlot(kFxMode, kFxMfxModes);
        } else {
            wrapped = asString(*endpoint);
            st = takeSlot(kFxEndpoint, kFxEfxModes);
        }
    } else {
        slot = kFxPostMix;
        if (postMix) wrapped = asString(*postMix);
        backup.save(L"fx", fx.get(), slot);
        st = writeValue(fx.get(), slot, stringValue(kApoClsidString));
    }
    if (sameGuid(wrapped, kApoClsidString)) wrapped.clear();

    if (st != ERROR_SUCCESS) {
        log(L"Writing FxProperties failed: " + errorText(st));
        backup.commit();
        uninstall(guid);
        return false;
    }

    // Make sure "Audio enhancements" are on, or Windows will skip the APO.
    // Depending on the Windows build the flag lives in FxProperties or Properties.
    auto enableSysFx = [&](const wchar_t* scope, HKEY key) {
        auto v = readValue(key, kPropDisableSysFx);
        if (v && v->type == REG_DWORD && v->data.size() >= 4 && *reinterpret_cast<DWORD*>(v->data.data()) != 0) {
            backup.save(scope, key, kPropDisableSysFx);
            writeValue(key, kPropDisableSysFx, dwordValue(0));
            log(L"Enabled audio enhancements on the endpoint");
        }
    };
    enableSysFx(L"fx", fx.get());
    if (RegKey props = openForWrite(endpointRoot(guid) + L"\\Properties", false)) enableSysFx(L"prop", props.get());
    backup.commit();

    writeValue(state.get(), L"Slot", stringValue(slot));
    writeValue(state.get(), L"WrappedClsid", stringValue(wrapped));
    writeValue(state.get(), L"Composite", dwordValue(composite ? 1 : 0));

    log(L"Installed on " + guid + L" in slot " + slot +
        (wrapped.empty() ? std::wstring() : L", hosting vendor APO " + wrapped));
    return true;
}

bool Installer::uninstall(const std::wstring& rawGuid)
{
    const std::wstring guid = normaliseGuid(rawGuid);
    if (!isInstalledOn(guid)) {
        log(L"openeq is not installed on " + guid);
        return true;
    }

    bool ok = true;
    RegKey fx = openForWrite(endpointRoot(guid) + L"\\FxProperties", true);
    RegKey props = openForWrite(endpointRoot(guid) + L"\\Properties", false);
    if (RegKey backupKey = openForRead(stateKey(guid) + L"\\Backup")) {
        for (DWORD i = 0;; ++i) {
            wchar_t name[512];
            DWORD nameLen = 512;
            if (RegEnumValueW(backupKey.get(), i, name, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            const std::wstring tag(name, nameLen);
            auto value = readValue(backupKey.get(), tag);
            if (!value) continue;
            if (tag == L"absent") {
                for (const auto& a : asMultiString(*value)) {
                    const auto bar = a.find(L'|');
                    if (bar == std::wstring::npos) continue;
                    HKEY target = a.compare(0, bar, L"fx") == 0 ? fx.get() : props.get();
                    if (target) RegDeleteValueW(target, a.substr(bar + 1).c_str());
                }
                continue;
            }
            const auto bar = tag.find(L'|');
            if (bar == std::wstring::npos) continue;
            HKEY target = tag.compare(0, bar, L"fx") == 0 ? fx.get() : props.get();
            if (!target || writeValue(target, tag.substr(bar + 1), *value) != ERROR_SUCCESS) {
                log(L"Could not restore " + tag);
                ok = false;
            }
        }
    }

    RegDeleteTreeW(HKEY_LOCAL_MACHINE, stateKey(guid).c_str());
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, stateKey(guid).c_str(), kWow64, 0);
    log(L"Uninstalled from " + guid);

    // Last endpoint gone: unregister the COM class too.
    bool any = false;
    if (RegKey eps = openForRead(kRegEndpoints)) {
        wchar_t sub[256];
        DWORD len = 256;
        any = RegEnumKeyExW(eps.get(), 0, sub, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }
    if (!any) unregisterDll();
    return ok;
}

bool Installer::uninstallAll()
{
    std::vector<std::wstring> guids;
    if (RegKey eps = openForRead(kRegEndpoints)) {
        for (DWORD i = 0;; ++i) {
            wchar_t sub[256];
            DWORD len = 256;
            if (RegEnumKeyExW(eps.get(), i, sub, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            guids.emplace_back(sub, len);
        }
    }
    bool ok = true;
    for (const auto& g : guids) ok = uninstall(g) && ok;
    if (guids.empty()) unregisterDll();
    return ok;
}

bool Installer::restartAudioService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        log(L"Cannot open service manager: " + errorText(GetLastError()));
        return false;
    }
    SC_HANDLE svc = OpenServiceW(scm, L"Audiosrv", SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) {
        log(L"Cannot open the Windows Audio service: " + errorText(GetLastError()));
        CloseServiceHandle(scm);
        return false;
    }

    auto waitFor = [&](DWORD wanted) {
        SERVICE_STATUS_PROCESS ssp{};
        DWORD needed = 0;
        for (int i = 0; i < 150; ++i) {
            if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&ssp), sizeof ssp, &needed))
                return false;
            if (ssp.dwCurrentState == wanted) return true;
            Sleep(100);
        }
        return false;
    };

    SERVICE_STATUS status{};
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
        log(L"Stopping the Windows Audio service failed: " + errorText(GetLastError()));
    waitFor(SERVICE_STOPPED);

    bool ok = StartServiceW(svc, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    ok = ok && waitFor(SERVICE_RUNNING);
    log(ok ? L"Restarted the Windows Audio service" : L"Windows Audio service did not restart: " + errorText(GetLastError()));

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

// ------------------------------------------------------------ verification

namespace {

std::wstring apoLogPath()
{
    return dataDirectory() + L"\\" + kApoLogName;
}

// Opens a shared-mode stream on the endpoint, plays 400 ms of silence and
// checks the engine consumed it. This is what failed for Spotify when the
// endpoint's effect chain could not be built.
bool playbackWorks(const std::wstring& deviceId, std::wstring& detail)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (attempt) Sleep(500); // audio service may still be settling
        IMMDeviceEnumerator* en = nullptr;
        IMMDevice* dev = nullptr;
        IAudioClient* client = nullptr;
        IAudioRenderClient* render = nullptr;
        WAVEFORMATEX* wfx = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&en));
        if (SUCCEEDED(hr)) hr = en->GetDevice(deviceId.c_str(), &dev);
        if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
        if (SUCCEEDED(hr)) hr = client->GetMixFormat(&wfx);
        if (SUCCEEDED(hr)) hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, wfx, nullptr);
        UINT32 frames = 0, padding = 0;
        if (SUCCEEDED(hr)) hr = client->GetBufferSize(&frames);
        if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render));
        BYTE* data = nullptr;
        if (SUCCEEDED(hr)) hr = render->GetBuffer(frames, &data);
        if (SUCCEEDED(hr)) hr = render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
        if (SUCCEEDED(hr)) hr = client->Start();
        if (SUCCEEDED(hr)) {
            Sleep(400);
            hr = client->GetCurrentPadding(&padding);
            client->Stop();
        }
        if (render) render->Release();
        if (client) client->Release();
        if (dev) dev->Release();
        if (en) en->Release();
        CoTaskMemFree(wfx);

        if (SUCCEEDED(hr) && padding < frames) return true;
        detail = FAILED(hr) ? L"stream error 0x" + [&] {
            wchar_t b[16];
            swprintf_s(b, L"%08lx", static_cast<unsigned long>(hr));
            return std::wstring(b);
        }() : L"the audio engine did not consume any audio";
    }
    return false;
}

bool audiodgHasApoLoaded()
{
    enablePrivilege(SE_DEBUG_NAME);
    bool found = false;
    HANDLE procs = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (procs == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(procs, &pe); ok && !found; ok = Process32NextW(procs, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"audiodg.exe") != 0) continue;
        HANDLE mods = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pe.th32ProcessID);
        if (mods == INVALID_HANDLE_VALUE) continue;
        MODULEENTRY32W me{sizeof me};
        for (BOOL m = Module32FirstW(mods, &me); m; m = Module32NextW(mods, &me))
            if (_wcsicmp(me.szModule, kApoDllName) == 0) { found = true; break; }
        CloseHandle(mods);
    }
    CloseHandle(procs);
    return found;
}

std::string readFrom(const std::wstring& path, unsigned long long offset)
{
    std::string text;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return text;
    LARGE_INTEGER size{}, pos{};
    GetFileSizeEx(h, &size);
    if (static_cast<unsigned long long>(size.QuadPart) < offset) offset = 0; // log was truncated
    pos.QuadPart = static_cast<LONGLONG>(offset);
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    text.resize(static_cast<size_t>(std::min<LONGLONG>(size.QuadPart - pos.QuadPart, 1 << 20)));
    DWORD read = 0;
    if (!text.empty()) ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    text.resize(read);
    CloseHandle(h);
    return text;
}

} // namespace

unsigned long long Installer::apoLogSize()
{
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(apoLogPath().c_str(), GetFileExInfoStandard, &a)) return 0;
    return (static_cast<unsigned long long>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
}

Installer::Verify Installer::verifyEndpoint(const std::wstring& rawGuid, unsigned long long apoLogOffset)
{
    const std::wstring guid = normaliseGuid(rawGuid);
    std::wstring deviceId;
    for (const auto& e : enumerateRenderEndpoints())
        if (e.guid == guid) deviceId = e.id;
    if (deviceId.empty()) {
        log(L"Playback test: the device is not active, so it could not be tested");
        return Verify::PlaysButInactive;
    }

    std::wstring detail;
    if (!playbackWorks(deviceId, detail)) {
        log(L"Playback test FAILED on the device (" + detail + L")");
        return Verify::NoPlayback;
    }
    log(L"Playback test passed");

    // Did audiodg start openeq for this device? Either signal is enough.
    const std::string fresh = readFrom(apoLogPath(), apoLogOffset);
    std::string needle = "LockForProcess endpoint=";
    for (wchar_t c : guid) needle.push_back(static_cast<char>(c)); // GUID text is ASCII
    if (fresh.find(needle) != std::string::npos || audiodgHasApoLoaded()) {
        log(L"openeq is processing audio on this device");
        return Verify::Active;
    }
    log(L"Windows did not start openeq on this device. Its driver may not allow third-party effects.");
    return Verify::PlaysButInactive;
}

} // namespace openeq::ctl
