#pragma once
// Shared identifiers and locations used by the APO, devicectl and the UI.
// Keep this header free of Qt so every component can include it.

#include <cwctype>
#include <string>

namespace openeq::shared {

// CLSID of the openeq APO: {078E6F18-C4A4-4A7D-9CA0-7BD79969AA53}
inline constexpr wchar_t kApoClsidString[] = L"{078E6F18-C4A4-4A7D-9CA0-7BD79969AA53}";
inline constexpr wchar_t kApoFriendlyName[] = L"openeq Equalizer";
inline constexpr wchar_t kApoCopyright[]    = L"openeq contributors";
inline constexpr wchar_t kApoDllName[]      = L"openeq_apo.dll";
inline constexpr wchar_t kDeviceCtlExe[]    = L"openeq-devicectl.exe";

// Per-endpoint install state written by devicectl, read by the APO and the UI.
//   HKLM\SOFTWARE\openeq\endpoints\{endpoint-guid}
//     Slot          REG_SZ   value name in FxProperties we wrote ("{d04e...},6" etc.)
//     WrappedClsid  REG_SZ   CLSID of the vendor APO we replaced and now host (may be empty)
//     Composite     REG_DWORD 1 when we appended to a composite (multi-sz) slot
//   HKLM\SOFTWARE\openeq\endpoints\{endpoint-guid}\Backup   original values, restored on uninstall
inline constexpr wchar_t kRegRoot[]         = L"SOFTWARE\\openeq";
inline constexpr wchar_t kRegEndpoints[]    = L"SOFTWARE\\openeq\\endpoints";

// MMDevices render endpoints.
inline constexpr wchar_t kRegRenderRoot[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render";

// FxProperties value names (PKEY_FX_* = {D04E05A6-594B-4FB6-A80D-01AF5EED7D1D},N)
inline constexpr wchar_t kFxPreMix[]        = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1";
inline constexpr wchar_t kFxPostMix[]       = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2";
inline constexpr wchar_t kFxStream[]        = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5";
inline constexpr wchar_t kFxMode[]          = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6";
inline constexpr wchar_t kFxEndpoint[]      = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7";
inline constexpr wchar_t kFxCompositeStream[]   = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13";
inline constexpr wchar_t kFxCompositeMode[]     = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14";
inline constexpr wchar_t kFxCompositeEndpoint[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15";
// PKEY_MFX_ProcessingModes_Supported_For_Streaming
inline constexpr wchar_t kFxMfxModes[]      = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},6";
// PKEY_EFX_ProcessingModes_Supported_For_Streaming
inline constexpr wchar_t kFxEfxModes[]      = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},7";
// AUDIO_SIGNALPROCESSINGMODE_DEFAULT
inline constexpr wchar_t kModeDefault[]     = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";
// PKEY_AudioEndpoint_Disable_SysFx (under the endpoint's Properties key)
inline constexpr wchar_t kPropDisableSysFx[] = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";

// File locations (relative to %ProgramData%).
inline constexpr wchar_t kDataDirName[]     = L"openeq";
inline constexpr wchar_t kDevicesDirName[]  = L"devices";
inline constexpr wchar_t kApoLogName[]      = L"apo.log";

// Test hook: when set, the APO reads configs from this directory instead of
// %ProgramData%\openeq\devices. Only used by tests/apo_host.
inline constexpr wchar_t kEnvConfigDir[]    = L"OPENEQ_CONFIG_DIR";

// Normalises an endpoint GUID to the "{xxxxxxxx-...}" lower-case form used for file names.
inline std::wstring normaliseGuid(std::wstring g)
{
    for (auto& c : g) c = static_cast<wchar_t>(towlower(c));
    if (!g.empty() && g.front() != L'{') g = L"{" + g + L"}";
    return g;
}

// MMDevice IDs look like "{0.0.0.00000000}.{guid}". Returns the trailing guid.
inline std::wstring endpointGuidFromDeviceId(const std::wstring& id)
{
    auto pos = id.rfind(L".{");
    if (pos == std::wstring::npos) return normaliseGuid(id);
    return normaliseGuid(id.substr(pos + 1));
}

} // namespace openeq::shared
