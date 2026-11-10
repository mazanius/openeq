#include "OpenEqApo.h"

#include "Log.h"

#include <OpenEqShared.h>

#include <audioengineextensionapo.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>

#include <cstddef>
#include <xmmintrin.h>
#include <pmmintrin.h>

#include <algorithm>
#include <cstring>

namespace openeq::apo {

// {078E6F18-C4A4-4A7D-9CA0-7BD79969AA53}
const CLSID CLSID_OpenEqApo = {0x078e6f18, 0xc4a4, 0x4a7d, {0x9c, 0xa0, 0x7b, 0xd7, 0x99, 0x69, 0xaa, 0x53}};

#pragma warning(suppress : 4815) // CRegAPOProperties<1> has an intentionally empty extra-IID array
const CRegAPOProperties<1> OpenEqApo::sRegProperties(
    CLSID_OpenEqApo, shared::kApoFriendlyName, shared::kApoCopyright, 1, 0, __uuidof(IAudioProcessingObject),
    static_cast<APO_FLAG>(APO_FLAG_SAMPLESPERFRAME_MUST_MATCH | APO_FLAG_FRAMESPERSECOND_MUST_MATCH |
                          APO_FLAG_BITSPERSAMPLE_MUST_MATCH | APO_FLAG_INPLACE));

extern std::atomic<long> g_objectCount;

namespace {

// PKEY_AudioEndpoint_GUID: {1da5d803-d492-4edd-8c23-e0c0ffee7f0e},4
// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

const PROPERTYKEY kPkeyEndpointGuid = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 4};

std::wstring readEndpointGuid(IPropertyStore* props)
{
    if (!props) return {};
    PROPVARIANT var;
    PropVariantInit(&var);
    std::wstring guid;
    if (SUCCEEDED(props->GetValue(kPkeyEndpointGuid, &var)) && var.vt == VT_LPWSTR && var.pwszVal)
        guid = shared::normaliseGuid(var.pwszVal);
    PropVariantClear(&var);
    return guid;
}

std::wstring readRegString(HKEY root, const std::wstring& subkey, const wchar_t* value)
{
    wchar_t buf[256] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(root, subkey.c_str(), value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}

std::wstring configDirectory()
{
    wchar_t env[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(shared::kEnvConfigDir, env, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return env;
    return dataDir() + L"\\" + shared::kDevicesDirName;
}

struct DenormalGuard {
    unsigned int saved;
    DenormalGuard() : saved(_mm_getcsr()) { _mm_setcsr(saved | 0x8040); } // FTZ | DAZ
    ~DenormalGuard() { _mm_setcsr(saved); }
};

} // namespace

OpenEqApo::OpenEqApo(IUnknown* outer)
    : CBaseAudioProcessingObject(sRegProperties)
    , m_outer(outer ? outer : reinterpret_cast<IUnknown*>(static_cast<INonDelegatingUnknown*>(this)))
{
    ++g_objectCount;
}

OpenEqApo::~OpenEqApo()
{
    m_watcher.stop();
    freeAllChains();
    releaseWrappedApo();
    --g_objectCount;
}

// ---------------------------------------------------------------- IUnknown

STDMETHODIMP OpenEqApo::QueryInterface(REFIID riid, void** ppv)
{
    return m_outer->QueryInterface(riid, ppv);
}

STDMETHODIMP_(ULONG) OpenEqApo::AddRef()
{
    return m_outer->AddRef();
}

STDMETHODIMP_(ULONG) OpenEqApo::Release()
{
    return m_outer->Release();
}

HRESULT STDMETHODCALLTYPE OpenEqApo::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == __uuidof(IUnknown)) {
        // The inner unknown: never delegates, so the outer object can manage our lifetime.
        *ppv = static_cast<INonDelegatingUnknown*>(this);
        NonDelegatingAddRef();
        return S_OK;
    }
    if (riid == __uuidof(IAudioProcessingObject))
        *ppv = static_cast<IAudioProcessingObject*>(this);
    else if (riid == __uuidof(IAudioProcessingObjectRT))
        *ppv = static_cast<IAudioProcessingObjectRT*>(this);
    else if (riid == __uuidof(IAudioProcessingObjectConfiguration))
        *ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
    else if (riid == __uuidof(IAudioSystemEffects) || riid == __uuidof(IAudioSystemEffects2))
        *ppv = static_cast<IAudioSystemEffects2*>(this);
    else
        return E_NOINTERFACE;
    AddRef(); // through the outer object, per the aggregation rules
    return S_OK;
}

ULONG STDMETHODCALLTYPE OpenEqApo::NonDelegatingAddRef()
{
    return ++m_refs;
}

ULONG STDMETHODCALLTYPE OpenEqApo::NonDelegatingRelease()
{
    const ULONG r = --m_refs;
    if (r == 0) delete this;
    return r;
}

// ------------------------------------------------------------ Initialisation

STDMETHODIMP OpenEqApo::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
    log(L"Initialize (cbDataSize=%u)", cbDataSize);
    if (m_bIsInitialized) return APOERR_ALREADY_INITIALIZED;
    if (cbDataSize > 0 && !pbyData) return E_POINTER;

    // The engine passes APOInitSystemEffects, ...2 or ...3 depending on the
    // Windows build. All of them start with APOInitBaseStruct followed by the
    // endpoint property store, so read that field by offset and accept any
    // size: rejecting an unknown struct makes the whole endpoint go silent.
    IPropertyStore* endpointProps = nullptr;
    constexpr size_t kPropsEnd = offsetof(APOInitSystemEffects, pAPOEndpointProperties) + sizeof(IPropertyStore*);
    if (cbDataSize >= kPropsEnd)
        endpointProps = reinterpret_cast<APOInitSystemEffects*>(pbyData)->pAPOEndpointProperties;
    if (cbDataSize == sizeof(APOInitSystemEffects2))
        m_discoveryOnly = reinterpret_cast<APOInitSystemEffects2*>(pbyData)->InitializeForDiscoveryOnly != FALSE;
    else if (cbDataSize == sizeof(APOInitSystemEffects3))
        m_discoveryOnly = reinterpret_cast<APOInitSystemEffects3*>(pbyData)->InitializeForDiscoveryOnly != FALSE;

    m_endpointGuid = readEndpointGuid(endpointProps);
    if (m_endpointGuid.empty()) m_endpointGuid = L"default";

    loadWrappedApo(cbDataSize, pbyData);

    log(L"Initialize endpoint=%s wrapped=%s discovery=%d", m_endpointGuid.c_str(), m_inner ? L"yes" : L"no",
        m_discoveryOnly ? 1 : 0);
    if (!m_discoveryOnly) {
        m_watcher.start(
            configDirectory(), m_endpointGuid + L".txt", [this](const EqConfig& cfg) { onConfig(cfg); },
            [this] { freeRetired(); });
    }

    m_bIsInitialized = true;
    return S_OK;
}

void OpenEqApo::loadWrappedApo(UINT32 cbDataSize, const BYTE* pbyData)
{
    if (m_endpointGuid == L"default") return;
    const std::wstring key = std::wstring(shared::kRegEndpoints) + L"\\" + m_endpointGuid;
    const std::wstring clsidStr = readRegString(HKEY_LOCAL_MACHINE, key, L"WrappedClsid");
    if (clsidStr.empty()) return;

    CLSID clsid{};
    if (FAILED(CLSIDFromString(clsidStr.c_str(), &clsid)) || clsid == CLSID_OpenEqApo) return;

    IAudioProcessingObject* inner = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IAudioProcessingObject),
                                  reinterpret_cast<void**>(&inner));
    if (FAILED(hr)) {
        log(L"Wrapped APO %s could not be created (0x%08lx); running without it", clsidStr.c_str(), hr);
        return;
    }

    // Hand the vendor APO the same init data, but with its own CLSID.
    std::vector<BYTE> data(pbyData, pbyData + cbDataSize);
    if (cbDataSize >= sizeof(APOInitBaseStruct))
        reinterpret_cast<APOInitBaseStruct*>(data.data())->clsid = clsid;
    hr = inner->Initialize(cbDataSize, data.empty() ? nullptr : data.data());
    if (FAILED(hr)) {
        log(L"Wrapped APO %s Initialize failed (0x%08lx); running without it", clsidStr.c_str(), hr);
        inner->Release();
        return;
    }

    IAudioProcessingObjectRT* rt = nullptr;
    IAudioProcessingObjectConfiguration* cfg = nullptr;
    if (FAILED(inner->QueryInterface(&rt)) || FAILED(inner->QueryInterface(&cfg))) {
        if (rt) rt->Release();
        if (cfg) cfg->Release();
        inner->Release();
        log(L"Wrapped APO %s lacks RT/configuration interfaces", clsidStr.c_str());
        return;
    }
    m_inner = inner;
    m_innerRT = rt;
    m_innerCfg = cfg;
}

void OpenEqApo::releaseWrappedApo()
{
    if (m_innerCfg) { m_innerCfg->Release(); m_innerCfg = nullptr; }
    if (m_innerRT) { m_innerRT->Release(); m_innerRT = nullptr; }
    if (m_inner) { m_inner->Release(); m_inner = nullptr; }
}

// --------------------------------------------------------- Format handling

STDMETHODIMP OpenEqApo::IsInputFormatSupported(IAudioMediaType* pOutputFormat, IAudioMediaType* pRequestedInputFormat,
                                               IAudioMediaType** ppSupportedInputFormat)
{
    if (m_inner)
        return m_inner->IsInputFormatSupported(pOutputFormat, pRequestedInputFormat, ppSupportedInputFormat);
    return CBaseAudioProcessingObject::IsInputFormatSupported(pOutputFormat, pRequestedInputFormat,
                                                              ppSupportedInputFormat);
}

STDMETHODIMP OpenEqApo::IsOutputFormatSupported(IAudioMediaType* pInputFormat, IAudioMediaType* pRequestedOutputFormat,
                                                IAudioMediaType** ppSupportedOutputFormat)
{
    if (m_inner)
        return m_inner->IsOutputFormatSupported(pInputFormat, pRequestedOutputFormat, ppSupportedOutputFormat);
    return CBaseAudioProcessingObject::IsOutputFormatSupported(pInputFormat, pRequestedOutputFormat,
                                                               ppSupportedOutputFormat);
}

STDMETHODIMP OpenEqApo::GetLatency(HNSTIME* pTime)
{
    if (!pTime) return E_POINTER;
    if (m_inner) return m_inner->GetLatency(pTime);
    *pTime = 0;
    return S_OK;
}

STDMETHODIMP OpenEqApo::Reset()
{
    if (m_inner) m_inner->Reset();
    if (m_active) m_active->reset();
    return S_OK;
}

STDMETHODIMP OpenEqApo::GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event)
{
    if (!ppEffectsIds || !pcEffects) return E_POINTER;
    if (m_inner) {
        IAudioSystemEffects2* fx = nullptr;
        if (SUCCEEDED(m_inner->QueryInterface(&fx))) {
            const HRESULT hr = fx->GetEffectsList(ppEffectsIds, pcEffects, Event);
            fx->Release();
            return hr;
        }
    }
    *ppEffectsIds = nullptr;
    *pcEffects = 0;
    return S_OK;
}

// ---------------------------------------------------------------- Locking

STDMETHODIMP OpenEqApo::LockForProcess(UINT32 u32NumInputConnections, APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                       UINT32 u32NumOutputConnections, APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
    // Remember the frame size so a pure passthrough can copy out-of-place buffers.
    m_frameBytes = 0;
    if (u32NumInputConnections >= 1 && ppInputConnections[0]->pFormat) {
        UNCOMPRESSEDAUDIOFORMAT f{};
        if (SUCCEEDED(ppInputConnections[0]->pFormat->GetUncompressedAudioFormat(&f)))
            m_frameBytes = f.dwSamplesPerFrame * f.dwBytesPerSampleContainer;
    }

    HRESULT innerHr = S_OK;
    if (m_innerCfg) {
        innerHr = m_innerCfg->LockForProcess(u32NumInputConnections, ppInputConnections, u32NumOutputConnections,
                                             ppOutputConnections);
        m_innerLocked = SUCCEEDED(innerHr);
        if (!m_innerLocked) log(L"Wrapped APO LockForProcess failed (0x%08lx)", innerHr);
    }

    const HRESULT hr = CBaseAudioProcessingObject::LockForProcess(u32NumInputConnections, ppInputConnections,
                                                                  u32NumOutputConnections, ppOutputConnections);
    m_selfLocked = SUCCEEDED(hr);
    if (!m_selfLocked) {
        // Never take the endpoint down: fall back to a plain passthrough.
        log(L"LockForProcess: base validation failed (0x%08lx); passing audio through", hr);
        return S_OK;
    }

    UNCOMPRESSEDAUDIOFORMAT fmt{};
    bool isFloat = false;
    if (u32NumInputConnections >= 1 && ppInputConnections[0]->pFormat &&
        SUCCEEDED(ppInputConnections[0]->pFormat->GetUncompressedAudioFormat(&fmt)))
        isFloat = fmt.guidFormatType == kSubtypeFloat && fmt.dwBytesPerSampleContainer == 4;

    {
        std::lock_guard lock(m_cfgMutex);
        m_sampleRate = GetFramesPerSecond();
        m_channels = static_cast<int>(GetSamplesPerFrame());
        m_maxFrames = ppInputConnections[0]->u32MaxFrameCount;
        m_formatValid = isFloat && m_channels > 0 && m_sampleRate > 0;
        m_fadeLen = std::max<UINT32>(1, static_cast<UINT32>(m_sampleRate * 0.010)); // 10 ms

        freeAllChains();
        if (m_formatValid) {
            m_scratch.assign(static_cast<size_t>(m_maxFrames) * m_channels, 0.0f);
            m_active = new FilterChain(m_config, m_sampleRate, m_channels);
        }
    }

    log(L"LockForProcess endpoint=%s rate=%.0f channels=%d maxFrames=%u float=%d", m_endpointGuid.c_str(),
        m_sampleRate, m_channels, m_maxFrames, isFloat ? 1 : 0);
    return S_OK;
}

STDMETHODIMP OpenEqApo::UnlockForProcess()
{
    {
        std::lock_guard lock(m_cfgMutex);
        m_formatValid = false;
        freeAllChains();
    }
    if (m_innerLocked) {
        m_innerCfg->UnlockForProcess();
        m_innerLocked = false;
    }
    if (m_selfLocked) {
        m_selfLocked = false;
        return CBaseAudioProcessingObject::UnlockForProcess();
    }
    return S_OK;
}

// --------------------------------------------------------- Config hand-off

void OpenEqApo::onConfig(const EqConfig& cfg)
{
    std::lock_guard lock(m_cfgMutex);
    m_config = cfg;
    if (!m_formatValid) return;
    auto* chain = new FilterChain(cfg, m_sampleRate, m_channels);
    delete m_pending.exchange(chain, std::memory_order_acq_rel);
    log(L"Config applied: %zu bands, preamp %.2f dB, bypass %d", cfg.bands.size(), cfg.preampDb, cfg.bypass ? 1 : 0);
}

void OpenEqApo::freeRetired()
{
    delete m_retired.exchange(nullptr, std::memory_order_acq_rel);
}

// Only called while the RT thread is not running (lock/unlock/destructor).
void OpenEqApo::freeAllChains()
{
    delete m_pending.exchange(nullptr);
    delete m_retired.exchange(nullptr);
    delete m_active;
    m_active = nullptr;
    delete m_fadeFrom;
    m_fadeFrom = nullptr;
    m_fadePos = 0;
}

// --------------------------------------------------------------- Real time

STDMETHODIMP_(void) OpenEqApo::APOProcess(UINT32 u32NumInputConnections, APO_CONNECTION_PROPERTY** ppInputConnections,
                                          UINT32 u32NumOutputConnections, APO_CONNECTION_PROPERTY** ppOutputConnections)
{
    if (u32NumInputConnections < 1 || u32NumOutputConnections < 1) return;
    APO_CONNECTION_PROPERTY* in = ppInputConnections[0];
    APO_CONNECTION_PROPERTY* out = ppOutputConnections[0];

    if (m_innerLocked) {
        m_innerRT->APOProcess(u32NumInputConnections, ppInputConnections, u32NumOutputConnections, ppOutputConnections);
    } else {
        if (in->pBuffer != out->pBuffer && m_frameBytes && in->u32BufferFlags != BUFFER_SILENT)
            std::memcpy(reinterpret_cast<void*>(out->pBuffer), reinterpret_cast<const void*>(in->pBuffer),
                        static_cast<size_t>(in->u32ValidFrameCount) * m_frameBytes);
        out->u32ValidFrameCount = in->u32ValidFrameCount;
        out->u32BufferFlags = in->u32BufferFlags;
    }

    if (!m_selfLocked || !m_active) return;

    processRT(reinterpret_cast<float*>(out->pBuffer), out->u32ValidFrameCount, out->u32BufferFlags == BUFFER_SILENT);
    if (out->u32BufferFlags == BUFFER_SILENT && !m_active->bypassed()) out->u32BufferFlags = BUFFER_VALID;
}

void OpenEqApo::processRT(float* buf, UINT32 frames, bool silent)
{
    // Adopt a new chain only when no crossfade is running and the previous
    // retired chain has been collected by the watcher thread.
    if (!m_fadeFrom && m_retired.load(std::memory_order_acquire) == nullptr) {
        if (FilterChain* next = m_pending.exchange(nullptr, std::memory_order_acq_rel)) {
            next->copyStateFrom(*m_active);
            m_fadeFrom = m_active;
            m_active = next;
            m_fadePos = 0;
        }
    }

    if (frames == 0 || frames > m_maxFrames) return;
    const size_t samples = static_cast<size_t>(frames) * m_channels;

    if (silent) {
        if (m_active->bypassed() && !m_fadeFrom) return;
        std::memset(buf, 0, samples * sizeof(float)); // flush filter tails into silence
    }

    DenormalGuard denormals;

    if (!m_fadeFrom) {
        m_active->process(buf, buf, frames);
        return;
    }

    // Crossfade old -> new chain over m_fadeLen frames.
    float* old = m_scratch.data();
    m_fadeFrom->process(buf, old, frames);
    m_active->process(buf, buf, frames);
    const float inv = 1.0f / static_cast<float>(m_fadeLen);
    for (UINT32 f = 0; f < frames; ++f) {
        const float t = std::min(1.0f, static_cast<float>(m_fadePos + f) * inv);
        float* nf = buf + static_cast<size_t>(f) * m_channels;
        const float* of = old + static_cast<size_t>(f) * m_channels;
        for (int c = 0; c < m_channels; ++c) nf[c] = of[c] + (nf[c] - of[c]) * t;
    }
    m_fadePos += frames;
    if (m_fadePos >= m_fadeLen) {
        m_retired.store(m_fadeFrom, std::memory_order_release);
        m_fadeFrom = nullptr;
    }
}

} // namespace openeq::apo
