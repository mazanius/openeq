#pragma once
// Render endpoint enumeration and install-state queries (header-only, COM).
// The caller must have initialised COM on the calling thread.

#include "OpenEqShared.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <propsys.h>

#include <string>
#include <vector>

namespace openeq::shared {

struct EndpointInfo {
    std::wstring id;    // full MMDevice id
    std::wstring guid;  // "{...}" lower case, the MMDevices registry key name
    std::wstring name;  // "Headphones (WH-1000XM5)"
    bool isDefault = false;
};

inline std::vector<EndpointInfo> enumerateRenderEndpoints()
{
    // PKEY_Device_FriendlyName {a45c254e-df1c-4efd-8020-67d146a850e0},14
    static const PROPERTYKEY kFriendlyName = {
        {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

    std::vector<EndpointInfo> result;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))))
        return result;

    std::wstring defaultId;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &def))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id))) defaultId = id;
        CoTaskMemFree(id);
        def->Release();
    }

    IMMDeviceCollection* devices = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (FAILED(devices->Item(i, &dev))) continue;
            EndpointInfo info;
            LPWSTR id = nullptr;
            if (SUCCEEDED(dev->GetId(&id))) info.id = id;
            CoTaskMemFree(id);
            info.guid = endpointGuidFromDeviceId(info.id);
            info.isDefault = info.id == defaultId;

            IPropertyStore* props = nullptr;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
                    info.name = v.pwszVal;
                PropVariantClear(&v);
                props->Release();
            }
            if (info.name.empty()) info.name = info.guid;
            result.push_back(std::move(info));
            dev->Release();
        }
        devices->Release();
    }
    enumerator->Release();
    return result;
}

// True when devicectl has installed openeq on this endpoint.
inline bool isInstalledOn(const std::wstring& guid)
{
    const std::wstring key = std::wstring(kRegEndpoints) + L"\\" + normaliseGuid(guid);
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS)
        return false;
    RegCloseKey(h);
    return true;
}

} // namespace openeq::shared
