#include "status_audio.h"

#include <windows.h>
#include <mmreg.h>
#include <endpointvolume.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>

namespace md {
namespace {

using Microsoft::WRL::ComPtr;

// Interface de Windows (non documentée, stable depuis Windows 7) qui choisit la sortie par défaut, comme le
// panneau Son. Seule SetDefaultEndpoint sert ; l'ordre des méthodes doit rester celui de Windows.
struct DECLSPEC_UUID("f8679f50-850a-41cf-9c72-430f290290c8") IPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR id, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
constexpr CLSID kPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

std::wstring idOf(IMMDevice* d) {
    LPWSTR id = nullptr;
    std::wstring out;
    if (d && SUCCEEDED(d->GetId(&id)) && id) out = id;
    CoTaskMemFree(id);
    return out;
}

class Notifier : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                                      IAudioEndpointVolumeCallback, IMMNotificationClient> {
public:
    Notifier(HWND hwnd, UINT msg) : hwnd_(hwnd), msg_(msg) {}
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override { return post(0); }
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        return flow == eRender && role == eConsole ? post(1) : S_OK;   // wParam 1 : rappeler watch
    }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    HRESULT post(WPARAM what) {
        PostMessageW(hwnd_, msg_, what, 0);   // fil de Core Audio : on ne fait que prévenir la barre
        return S_OK;
    }
    HWND hwnd_;
    UINT msg_;
};

} // namespace

ComPtr<IUnknown> makeAudioNotifier(HWND hwnd, UINT msg) {
    ComPtr<IUnknown> out;
    Microsoft::WRL::Make<Notifier>(hwnd, msg).As(&out);
    return out;
}

struct AudioStatus::Impl {
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IUnknown> notifier;                // watch : abonnements en cours
    ComPtr<IMMNotificationClient> client;     // inscrit auprès de devices
    ComPtr<IAudioEndpointVolume> watched;     // sortie suivie
    ComPtr<IAudioEndpointVolumeCallback> volumeCallback;

    void stopVolume() {
        if (watched && volumeCallback) watched->UnregisterControlChangeNotify(volumeCallback.Get());
        watched.Reset();
    }

    ComPtr<IAudioEndpointVolume> endpoint() {
        ComPtr<IMMDevice> d;
        ComPtr<IAudioEndpointVolume> v;
        if (!devices || FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &d)) ||
            FAILED(d->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(v.GetAddressOf()))))
            return nullptr;
        return v;
    }
};

AudioStatus::AudioStatus() : impl_(std::make_unique<Impl>()) {}
AudioStatus::~AudioStatus() { unwatch(); }

bool AudioStatus::init() {
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&impl_->devices))))
        return false;
    return impl_->endpoint() != nullptr;
}

float AudioStatus::volume() {
    float v = -1;
    if (auto e = impl_->endpoint(); !e || FAILED(e->GetMasterVolumeLevelScalar(&v))) return -1;
    return std::clamp(v, 0.0f, 1.0f);
}

bool AudioStatus::muted() {
    BOOL m = FALSE;
    auto e = impl_->endpoint();
    return e && SUCCEEDED(e->GetMute(&m)) && m;
}

bool AudioStatus::setVolume(float v) {
    auto e = impl_->endpoint();
    if (!e) return false;
    e->SetMute(FALSE, nullptr);
    return SUCCEEDED(e->SetMasterVolumeLevelScalar(std::clamp(v, 0.0f, 1.0f), nullptr));
}

bool AudioStatus::setMuted(bool m) {
    auto e = impl_->endpoint();
    return e && SUCCEEDED(e->SetMute(m ? TRUE : FALSE, nullptr));
}

std::vector<AudioOutput> AudioStatus::outputs() {
    std::vector<AudioOutput> out;
    ComPtr<IMMDeviceCollection> all;
    if (!impl_->devices || FAILED(impl_->devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &all))) return out;
    ComPtr<IMMDevice> def;
    impl_->devices->GetDefaultAudioEndpoint(eRender, eConsole, &def);
    const std::wstring defId = idOf(def.Get());
    UINT n = 0;
    all->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        ComPtr<IMMDevice> d;
        ComPtr<IPropertyStore> props;
        if (FAILED(all->Item(i, &d)) || FAILED(d->OpenPropertyStore(STGM_READ, &props))) continue;
        PROPVARIANT name;
        PropVariantInit(&name);
        AudioOutput o;
        o.id = idOf(d.Get());
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) o.name = name.pwszVal;
        PropVariantClear(&name);
        if (o.id.empty() || o.name.empty()) continue;
        o.isDefault = o.id == defId;
        out.push_back(std::move(o));
    }
    return out;
}

bool AudioStatus::setDefault(const std::wstring& id) {
    const auto outs = outputs();
    if (std::none_of(outs.begin(), outs.end(), [&](const AudioOutput& o) { return o.id == id; })) return false;
    ComPtr<IPolicyConfig> policy;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig),
                                reinterpret_cast<void**>(policy.GetAddressOf()))))
        return false;
    bool ok = true;
    for (ERole role : {eConsole, eMultimedia, eCommunications}) ok = SUCCEEDED(policy->SetDefaultEndpoint(id.c_str(), role)) && ok;
    return ok;
}

bool AudioStatus::watch(HWND hwnd, UINT msg) {
    if (!impl_->devices) return false;
    if (!impl_->notifier) {
        impl_->notifier = makeAudioNotifier(hwnd, msg);
        if (!impl_->notifier || FAILED(impl_->notifier.As(&impl_->volumeCallback)) ||
            FAILED(impl_->notifier.As(&impl_->client)) ||
            FAILED(impl_->devices->RegisterEndpointNotificationCallback(impl_->client.Get()))) {
            impl_->client.Reset();
            impl_->volumeCallback.Reset();
            impl_->notifier.Reset();
            return false;
        }
    }
    impl_->stopVolume();   // la sortie par défaut a pu changer
    auto e = impl_->endpoint();
    if (!e || FAILED(e->RegisterControlChangeNotify(impl_->volumeCallback.Get()))) return false;
    impl_->watched = e;
    return true;
}

void AudioStatus::unwatch() {
    impl_->stopVolume();
    if (impl_->devices && impl_->client) impl_->devices->UnregisterEndpointNotificationCallback(impl_->client.Get());
    impl_->client.Reset();
    impl_->volumeCallback.Reset();
    impl_->notifier.Reset();
}

} // namespace md
