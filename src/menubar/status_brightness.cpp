#include "status_brightness.h"

#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace md {
namespace {

using Microsoft::WRL::ComPtr;

ComPtr<IWbemServices> wmi() {
    ComPtr<IWbemLocator> locator;
    ComPtr<IWbemServices> svc;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    const bool ok = SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator))) &&
                    SUCCEEDED(locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc));
    SysFreeString(ns);
    if (!ok) return nullptr;
    CoSetProxyBlanket(svc.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    return svc;
}

ComPtr<IWbemClassObject> first(IWbemServices* svc, const wchar_t* query) {
    BSTR lang = SysAllocString(L"WQL"), q = SysAllocString(query);
    ComPtr<IEnumWbemClassObject> e;
    ComPtr<IWbemClassObject> obj;
    ULONG n = 0;
    if (SUCCEEDED(svc->ExecQuery(lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &e)) && e)
        e->Next(2000, 1, &obj, &n);
    SysFreeString(lang);
    SysFreeString(q);
    return n == 1 ? obj : nullptr;
}

std::optional<double> wmiRead() {
    auto svc = wmi();
    if (!svc) return std::nullopt;
    auto obj = first(svc.Get(), L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active=TRUE");
    VARIANT v;
    VariantInit(&v);
    if (!obj || FAILED(obj->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr))) return std::nullopt;
    const double out = v.vt == VT_UI1 ? v.bVal / 100.0 : v.vt == VT_I4 ? v.lVal / 100.0 : -1;
    VariantClear(&v);
    if (out < 0) return std::nullopt;
    return std::clamp(out, 0.0, 1.0);
}

bool wmiWrite(double value) {
    auto svc = wmi();
    if (!svc) return false;
    auto instance = first(svc.Get(), L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE");
    if (!instance) return false;
    VARIANT path;
    VariantInit(&path);
    if (FAILED(instance->Get(L"__PATH", 0, &path, nullptr, nullptr)) || path.vt != VT_BSTR) return false;
    BSTR cls = SysAllocString(L"WmiMonitorBrightnessMethods"), method = SysAllocString(L"WmiSetBrightness");
    ComPtr<IWbemClassObject> classObj, inDef, in;
    bool ok = SUCCEEDED(svc->GetObject(cls, 0, nullptr, &classObj, nullptr)) &&
              SUCCEEDED(classObj->GetMethod(method, 0, &inDef, nullptr)) && inDef && SUCCEEDED(inDef->SpawnInstance(0, &in));
    if (ok) {
        VARIANT timeout, level;
        VariantInit(&timeout);
        VariantInit(&level);
        timeout.vt = VT_I4;
        timeout.lVal = 0;
        level.vt = VT_UI1;
        level.bVal = BYTE(std::lround(std::clamp(value, 0.0, 1.0) * 100));
        ok = SUCCEEDED(in->Put(L"Timeout", 0, &timeout, 0)) && SUCCEEDED(in->Put(L"Brightness", 0, &level, 0)) &&
             SUCCEEDED(svc->ExecMethod(path.bstrVal, method, 0, nullptr, in.Get(), nullptr, nullptr));
    }
    SysFreeString(cls);
    SysFreeString(method);
    VariantClear(&path);
    return ok;
}

// Écrans physiques de l'écran principal (DDC/CI).
struct Physical {
    std::vector<PHYSICAL_MONITOR> list;
    Physical() {
        HMONITOR mon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        DWORD n = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(mon, &n) || n == 0) return;
        list.resize(n);
        if (!GetPhysicalMonitorsFromHMONITOR(mon, n, list.data())) list.clear();
    }
    ~Physical() {
        if (!list.empty()) DestroyPhysicalMonitors(DWORD(list.size()), list.data());
    }
};

std::optional<double> ddcRead() {
    Physical p;
    for (const auto& m : p.list) {
        DWORD lo = 0, cur = 0, hi = 0;
        if (GetMonitorBrightness(m.hPhysicalMonitor, &lo, &cur, &hi) && hi > lo)
            return std::clamp(double(cur - lo) / double(hi - lo), 0.0, 1.0);
    }
    return std::nullopt;
}

bool ddcWrite(double value) {
    Physical p;
    bool done = false;
    for (const auto& m : p.list) {
        DWORD lo = 0, cur = 0, hi = 0;
        if (!GetMonitorBrightness(m.hPhysicalMonitor, &lo, &cur, &hi) || hi <= lo) continue;
        const DWORD target = lo + DWORD(std::lround(std::clamp(value, 0.0, 1.0) * double(hi - lo)));
        done = SetMonitorBrightness(m.hPhysicalMonitor, target) || done;
    }
    return done;
}

} // namespace

std::optional<double> readBrightness() {
    if (auto v = wmiRead()) return v;
    return ddcRead();
}

bool setBrightness(double v) { return wmiWrite(v) || ddcWrite(v); }

} // namespace md
