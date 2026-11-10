#pragma once
// The openeq system-effect APO.
//
// Threading model:
//   * Initialize / LockForProcess / UnlockForProcess / Reset run on engine
//     control threads while APOProcess is not running.
//   * APOProcess runs on the real-time audio thread: no locks, no allocation,
//     no I/O. It picks up new filter chains from m_pending, crossfades into
//     them, and hands the old chain back through m_retired.
//   * The ConfigWatcher thread parses config changes, builds new chains
//     (allocation happens here) and frees retired ones.

#include "ConfigWatcher.h"

#include <FilterChain.h>

#include <windows.h>
#include <audioenginebaseapo.h>
#include <baseaudioprocessingobject.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace openeq::apo {

extern const CLSID CLSID_OpenEqApo;

// The "inner" IUnknown of an aggregatable COM object. Same vtable layout as
// IUnknown, so a pointer to it can be handed out as the object's IUnknown.
struct INonDelegatingUnknown {
    virtual HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void** ppv) = 0;
    virtual ULONG STDMETHODCALLTYPE NonDelegatingAddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE NonDelegatingRelease() = 0;
};

// The audio engine creates APOs aggregated (it passes an outer IUnknown to
// CreateInstance), so the IUnknown methods delegate to the outer object and
// the real reference count lives in the INonDelegatingUnknown methods.
class OpenEqApo final : public CBaseAudioProcessingObject, public IAudioSystemEffects2, public INonDelegatingUnknown {
public:
    static const CRegAPOProperties<1> sRegProperties;

    explicit OpenEqApo(IUnknown* outer = nullptr);
    ~OpenEqApo() override;

    // IUnknown (delegates to the outer object when aggregated)
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // INonDelegatingUnknown
    HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE NonDelegatingAddRef() override;
    ULONG STDMETHODCALLTYPE NonDelegatingRelease() override;

    // IAudioProcessingObject
    STDMETHOD(Initialize)(UINT32 cbDataSize, BYTE* pbyData) override;
    STDMETHOD(IsInputFormatSupported)(IAudioMediaType* pOutputFormat, IAudioMediaType* pRequestedInputFormat,
                                      IAudioMediaType** ppSupportedInputFormat) override;
    STDMETHOD(IsOutputFormatSupported)(IAudioMediaType* pInputFormat, IAudioMediaType* pRequestedOutputFormat,
                                       IAudioMediaType** ppSupportedOutputFormat) override;
    STDMETHOD(GetLatency)(HNSTIME* pTime) override;
    STDMETHOD(Reset)() override;

    // IAudioProcessingObjectConfiguration
    STDMETHOD(LockForProcess)(UINT32 u32NumInputConnections, APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                              UINT32 u32NumOutputConnections,
                              APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;
    STDMETHOD(UnlockForProcess)() override;

    // IAudioProcessingObjectRT
    STDMETHOD_(void, APOProcess)(UINT32 u32NumInputConnections, APO_CONNECTION_PROPERTY** ppInputConnections,
                                 UINT32 u32NumOutputConnections,
                                 APO_CONNECTION_PROPERTY** ppOutputConnections) override;

    // IAudioSystemEffects2
    STDMETHOD(GetEffectsList)(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event) override;

private:
    void loadWrappedApo(UINT32 cbDataSize, const BYTE* pbyData);
    void releaseWrappedApo();
    void onConfig(const EqConfig& cfg);
    void freeRetired();
    void freeAllChains();
    void processRT(float* buffer, UINT32 frames, bool inputSilent);

    std::atomic<ULONG> m_refs{1};
    IUnknown* m_outer; // the aggregating object, or our own non-delegating unknown

    std::wstring m_endpointGuid;   // "{...}" lower case, or "default"
    bool m_discoveryOnly = false;

    // Vendor APO we replaced in the FX slot and now host in front of us.
    IAudioProcessingObject* m_inner = nullptr;
    IAudioProcessingObjectRT* m_innerRT = nullptr;
    IAudioProcessingObjectConfiguration* m_innerCfg = nullptr;
    bool m_innerLocked = false;
    bool m_selfLocked = false;
    UINT32 m_frameBytes = 0;

    // Format negotiated in LockForProcess (guarded by m_cfgMutex).
    std::mutex m_cfgMutex;
    EqConfig m_config;
    bool m_formatValid = false;
    double m_sampleRate = 48000.0;
    int m_channels = 2;
    UINT32 m_maxFrames = 0;

    // Chain hand-off between the watcher thread and the RT thread.
    std::atomic<FilterChain*> m_pending{nullptr};
    std::atomic<FilterChain*> m_retired{nullptr};

    // RT-thread owned.
    FilterChain* m_active = nullptr;
    FilterChain* m_fadeFrom = nullptr;
    UINT32 m_fadePos = 0;
    UINT32 m_fadeLen = 480;
    std::vector<float> m_scratch;

    ConfigWatcher m_watcher;
};

} // namespace openeq::apo
