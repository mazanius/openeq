// apo_host: loads openeq_apo.dll in-process (no registration, no audiodg) and
// drives it like the audio engine does, to verify processing offline.
//
//   apo_host <path-to-openeq_apo.dll>
//
// Uses OPENEQ_CONFIG_DIR to point the APO at a temporary config directory.

#include <OpenEqShared.h>

#include <windows.h>
#include <audioenginebaseapo.h>
#include <audioengineextensionapo.h>
#include <audiomediatype.h>
#include <mmreg.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

namespace {

const CLSID kClsid = {0x078e6f18, 0xc4a4, 0x4a7d, {0x9c, 0xa0, 0x7b, 0xd7, 0x99, 0x69, 0xaa, 0x53}};
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

constexpr double kRate = 48000.0;
constexpr int kChannels = 2;
constexpr UINT32 kBlock = 480;
constexpr double kAmp = 0.1;
constexpr double kFreq = 1000.0;

int g_failures = 0;

void check(bool ok, const char* what, double value = NAN)
{
    if (std::isnan(value)) std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    else std::printf("[%s] %s (%.3f)\n", ok ? "PASS" : "FAIL", what, value);
    if (!ok) ++g_failures;
}

class MediaType final : public IAudioMediaType {
public:
    MediaType()
    {
        m_fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        m_fmt.Format.nChannels = kChannels;
        m_fmt.Format.nSamplesPerSec = static_cast<DWORD>(kRate);
        m_fmt.Format.wBitsPerSample = 32;
        m_fmt.Format.nBlockAlign = kChannels * 4;
        m_fmt.Format.nAvgBytesPerSec = m_fmt.Format.nSamplesPerSec * m_fmt.Format.nBlockAlign;
        m_fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        m_fmt.Samples.wValidBitsPerSample = 32;
        m_fmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        m_fmt.SubFormat = kSubtypeFloat;
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioMediaType)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
    STDMETHODIMP_(ULONG) Release() override { return --m_refs; } // stack-owned
    STDMETHODIMP IsCompressedFormat(BOOL* p) override { *p = FALSE; return S_OK; }
    STDMETHODIMP IsEqual(IAudioMediaType* other, DWORD* flags) override
    {
        *flags = other == this ? 0xF : 0;
        return S_OK;
    }
    const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat() override { return &m_fmt.Format; }
    STDMETHODIMP GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* u) override
    {
        u->guidFormatType = kSubtypeFloat;
        u->dwSamplesPerFrame = kChannels;
        u->dwBytesPerSampleContainer = 4;
        u->dwValidBitsPerSample = 32;
        u->fFramesPerSecond = static_cast<float>(kRate);
        u->dwChannelMask = m_fmt.dwChannelMask;
        return S_OK;
    }

private:
    WAVEFORMATEXTENSIBLE m_fmt{};
    std::atomic<ULONG> m_refs{1};
};

// Stands in for the audio engine's wrapper object: audiodg creates APOs
// aggregated, passing itself as the outer IUnknown.
class Outer final : public IUnknown {
public:
    IUnknown* inner = nullptr; // the APO's non-delegating unknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown)) {
            *ppv = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(riid, ppv) : E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override { return --refs; } // stack-owned
    std::atomic<ULONG> refs{1};
};

void writeConfig(const std::wstring& path, const char* text)
{
    // Atomic replace, like the UI does.
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

struct Harness {
    IAudioProcessingObject* apo = nullptr;
    IAudioProcessingObjectRT* rt = nullptr;
    IAudioProcessingObjectConfiguration* cfg = nullptr;
    MediaType format;
    std::vector<float> buffer = std::vector<float>(kBlock * kChannels);
    APO_CONNECTION_DESCRIPTOR desc{};
    APO_CONNECTION_PROPERTY prop{};
    unsigned long long frame = 0;

    double sineAt(unsigned long long n) const { return kAmp * std::sin(2 * std::numbers::pi * kFreq * n / kRate); }

    // Fills a block with the sine, processes it, returns output in buffer.
    void block()
    {
        for (UINT32 i = 0; i < kBlock; ++i)
            for (int c = 0; c < kChannels; ++c)
                buffer[i * kChannels + c] = static_cast<float>(sineAt(frame + i));
        prop.u32ValidFrameCount = kBlock;
        prop.u32BufferFlags = BUFFER_VALID;
        APO_CONNECTION_PROPERTY* in = &prop;
        APO_CONNECTION_PROPERTY* out = &prop;
        rt->APOProcess(1, &in, 1, &out);
        frame += kBlock;
    }

    double measureGainDb(double seconds)
    {
        const int blocks = static_cast<int>(seconds * kRate / kBlock);
        double inSq = 0, outSq = 0;
        for (int b = 0; b < blocks; ++b) {
            const unsigned long long start = frame;
            block();
            for (UINT32 i = 0; i < kBlock; ++i) {
                const double x = sineAt(start + i);
                inSq += x * x;
                outSq += double(buffer[i * kChannels]) * buffer[i * kChannels];
            }
        }
        return 10 * std::log10(outSq / inSq);
    }

    // Keeps processing (so the RT thread can adopt the new chain) while the
    // watcher thread notices the file change. Returns the largest sample-to-
    // sample jump seen, as a click detector.
    double runFor(double seconds)
    {
        const int blocks = static_cast<int>(seconds * kRate / kBlock);
        double maxJump = 0;
        float prev = buffer[(kBlock - 1) * kChannels];
        for (int b = 0; b < blocks; ++b) {
            block();
            Sleep(10); // let the watcher thread run, like a real 10 ms period
            for (UINT32 i = 0; i < kBlock; ++i) {
                const float y = buffer[i * kChannels];
                maxJump = std::max(maxJump, double(std::fabs(y - prev)));
                prev = y;
            }
        }
        return maxJump;
    }
};

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        std::fwprintf(stderr, L"usage: apo_host <openeq_apo.dll>\n");
        return 2;
    }

    // Temporary config directory.
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    const std::wstring dir = std::wstring(tempPath) + L"openeq_apo_host_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    SetEnvironmentVariableW(openeq::shared::kEnvConfigDir, dir.c_str());
    const std::wstring cfgPath = dir + L"\\default.txt";
    writeConfig(cfgPath, "openeq 1\nbypass 0\npreamp 0\nfilter PK 1000 6 1.41 on\n");

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        std::fwprintf(stderr, L"cannot load %s (%lu)\n", argv[1], GetLastError());
        return 1;
    }
    using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
    auto getClassObject = reinterpret_cast<GetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
    check(getClassObject != nullptr, "DllGetClassObject exported");
    if (!getClassObject) return 1;

    IClassFactory* factory = nullptr;
    check(SUCCEEDED(getClassObject(kClsid, __uuidof(IClassFactory), reinterpret_cast<void**>(&factory))),
          "class factory created");
    if (!factory) return 1;

    Harness h;
    // Every init struct generation the engine may pass must be accepted;
    // rejecting one silences the whole endpoint.
    {
        IAudioProcessingObject* probe = nullptr;
        factory->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(&probe));
        APOInitSystemEffects3 init3{};
        init3.APOInit = {sizeof(APOInitSystemEffects3), kClsid};
        const HRESULT hr3 =
            probe ? probe->Initialize(sizeof init3, reinterpret_cast<BYTE*>(&init3)) : E_FAIL;
        check(SUCCEEDED(hr3), "Initialize accepts APOInitSystemEffects3 (Windows 11)");
        if (probe) probe->Release();

        probe = nullptr;
        factory->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(&probe));
        APOInitSystemEffects2 init2{};
        init2.APOInit = {sizeof(APOInitSystemEffects2), kClsid};
        init2.InitializeForDiscoveryOnly = TRUE;
        const HRESULT hr2 =
            probe ? probe->Initialize(sizeof init2, reinterpret_cast<BYTE*>(&init2)) : E_FAIL;
        check(SUCCEEDED(hr2), "Initialize accepts APOInitSystemEffects2 (discovery)");
        if (probe) probe->Release();
    }

    // Non-aggregated creation still works (regsvr32-style hosts, tools).
    {
        IAudioProcessingObject* plain = nullptr;
        const HRESULT hp =
            factory->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(&plain));
        check(SUCCEEDED(hp) && plain, "APO created without aggregation");
        if (plain) plain->Release();
    }

    // Aggregated creation, exactly as audiodg.exe does it. This is the path
    // that failed with CLASS_E_NOAGGREGATION (0x80040110) on a real device.
    Outer outer;
    HRESULT hr = factory->CreateInstance(&outer, __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(&h.apo));
    check(hr == CLASS_E_NOAGGREGATION, "aggregation only hands out the inner IUnknown");
    hr = factory->CreateInstance(&outer, __uuidof(IUnknown), reinterpret_cast<void**>(&outer.inner));
    factory->Release();
    check(SUCCEEDED(hr) && outer.inner, "APO created aggregated (as audiodg does)");
    if (FAILED(hr) || !outer.inner) return 1;
    outer.QueryInterface(__uuidof(IAudioProcessingObject), reinterpret_cast<void**>(&h.apo));
    outer.QueryInterface(__uuidof(IAudioProcessingObjectRT), reinterpret_cast<void**>(&h.rt));
    outer.QueryInterface(__uuidof(IAudioProcessingObjectConfiguration), reinterpret_cast<void**>(&h.cfg));
    IUnknown* back = nullptr;
    if (h.apo) h.apo->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&back));
    check(back == static_cast<IUnknown*>(&outer), "APO's IUnknown is the outer object");
    if (back) back->Release();
    check(h.apo != nullptr, "IAudioProcessingObject via outer");
    check(h.rt && h.cfg, "RT and configuration interfaces");

    IAudioSystemEffects2* fx = nullptr;
    check(SUCCEEDED(h.apo->QueryInterface(&fx)), "IAudioSystemEffects2 supported");
    if (fx) fx->Release();

    APOInitBaseStruct init{sizeof(APOInitBaseStruct), kClsid};
    check(SUCCEEDED(h.apo->Initialize(sizeof init, reinterpret_cast<BYTE*>(&init))), "Initialize");

    IAudioMediaType* supported = nullptr;
    hr = h.apo->IsInputFormatSupported(&h.format, &h.format, &supported);
    check(hr == S_OK, "float32 stereo 48 kHz input supported");
    if (supported) supported->Release();

    h.desc.Type = APO_CONNECTION_BUFFER_TYPE_EXTERNAL;
    h.desc.pBuffer = reinterpret_cast<UINT_PTR>(h.buffer.data());
    h.desc.u32MaxFrameCount = kBlock;
    h.desc.pFormat = &h.format;
    h.desc.u32Signature = APO_CONNECTION_DESCRIPTOR_SIGNATURE;
    h.prop.pBuffer = h.desc.pBuffer;
    h.prop.u32Signature = APO_CONNECTION_PROPERTY_SIGNATURE;

    APO_CONNECTION_DESCRIPTOR* in = &h.desc;
    APO_CONNECTION_DESCRIPTOR* out = &h.desc;
    hr = h.cfg->LockForProcess(1, &in, 1, &out);
    check(SUCCEEDED(hr), "LockForProcess");
    if (FAILED(hr)) return 1;

    // 1) Initial config: +6 dB peak at 1 kHz.
    h.measureGainDb(0.5); // settle
    const double g1 = h.measureGainDb(0.5);
    check(std::fabs(g1 - 6.0) < 0.1, "+6 dB peak measured at 1 kHz", g1);

    // 2) Live change to -6 dB: must be picked up and click-free.
    writeConfig(cfgPath, "openeq 1\nbypass 0\npreamp 0\nfilter PK 1000 -6 1.41 on\n");
    const double jump = h.runFor(0.6);
    // Max slope of the louder (+6 dB) sine is ~0.026 per sample.
    check(jump < 0.06, "config switch is click-free (max sample jump)", jump);
    const double g2 = h.measureGainDb(0.5);
    check(std::fabs(g2 + 6.0) < 0.1, "live update to -6 dB measured", g2);

    // 3) Bypass: output must equal input bit for bit.
    writeConfig(cfgPath, "openeq 1\nbypass 1\npreamp -3\nfilter PK 1000 -6 1.41 on\n");
    h.runFor(0.6);
    bool identical = true;
    for (int b = 0; b < 10; ++b) {
        const unsigned long long start = h.frame;
        h.block();
        for (UINT32 i = 0; i < kBlock && identical; ++i)
            for (int c = 0; c < kChannels; ++c)
                if (h.buffer[i * kChannels + c] != static_cast<float>(h.sineAt(start + i))) identical = false;
    }
    check(identical, "bypass is bit-identical");

    // 4) Malformed config is ignored (keeps bypass).
    writeConfig(cfgPath, "this is not a config\n");
    h.runFor(0.3);
    check(std::fabs(h.measureGainDb(0.2)) < 1e-6, "garbage config ignored");

    check(SUCCEEDED(h.cfg->UnlockForProcess()), "UnlockForProcess");
    h.rt->Release();
    h.cfg->Release();
    h.apo->Release();
    check(outer.refs == 1, "interface references went to the outer object");
    const ULONG left = outer.inner->Release(); // destroys the APO
    check(left == 0, "inner IUnknown owns the APO lifetime");

    DeleteFileW(cfgPath.c_str());
    RemoveDirectoryW(dir.c_str());
    CoUninitialize();

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
