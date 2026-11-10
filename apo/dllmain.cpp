// COM plumbing for openeq_apo.dll: class factory and self-registration.

#include "Log.h"
#include "OpenEqApo.h"

#include <OpenEqShared.h>

#include <windows.h>
#include <olectl.h>

#include <atomic>
#include <new>
#include <string>

namespace openeq::apo {
std::atomic<long> g_objectCount{0};
} // namespace openeq::apo

using namespace openeq;
using namespace openeq::apo;

namespace {

HMODULE g_module = nullptr;
std::atomic<long> g_lockCount{0};

class ClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = --m_refs;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        // The audio engine aggregates APOs. COM rules: an aggregated object
        // may only be asked for its (inner) IUnknown at creation time.
        if (outer && riid != __uuidof(IUnknown)) return CLASS_E_NOAGGREGATION;
        auto* apo = new (std::nothrow) OpenEqApo(outer);
        if (!apo) return E_OUTOFMEMORY;
        const HRESULT hr = apo->NonDelegatingQueryInterface(riid, ppv);
        apo->NonDelegatingRelease();
        if (FAILED(hr)) log(L"CreateInstance failed (0x%08lx)", hr);
        return hr;
    }

    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) ++g_lockCount;
        else --g_lockCount;
        return S_OK;
    }

private:
    std::atomic<ULONG> m_refs{1};
};

std::wstring modulePath()
{
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(g_module, path, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(path, n) : std::wstring();
}

LSTATUS setString(HKEY key, const wchar_t* name, const std::wstring& value)
{
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return (g_objectCount == 0 && g_lockCount == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_OpenEqApo) return CLASS_E_CLASSNOTAVAILABLE;
    log(L"DllGetClassObject");
    auto* factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer()
{
    const std::wstring path = modulePath();
    if (path.empty()) return E_FAIL;

    const std::wstring clsidKey = std::wstring(L"CLSID\\") + shared::kApoClsidString;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, clsidKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    setString(key, nullptr, shared::kApoFriendlyName);
    RegCloseKey(key);

    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, (clsidKey + L"\\InprocServer32").c_str(), 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    setString(key, nullptr, path);
    setString(key, L"ThreadingModel", L"Both");
    RegCloseKey(key);

    const HRESULT hr = RegisterAPO(OpenEqApo::sRegProperties);
    if (FAILED(hr)) {
        log(L"RegisterAPO failed (0x%08lx)", hr);
        return hr;
    }
    log(L"Registered %s", path.c_str());
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    UnregisterAPO(CLSID_OpenEqApo);
    const std::wstring clsidKey = std::wstring(L"CLSID\\") + shared::kApoClsidString;
    const LSTATUS st = RegDeleteTreeW(HKEY_CLASSES_ROOT, clsidKey.c_str());
    return (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) ? S_OK : SELFREG_E_CLASS;
}
