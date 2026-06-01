#include "VolumeMixerController.h"

#include <pulse/pulseaudio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <cstdio>
#include <unistd.h>

using namespace nlohmann::literals;

static constexpr bool s_fEnableLogging = true;

// ---------------------------------------------------------------------------
// PulseAudio data types
// ---------------------------------------------------------------------------

struct PaSinkInputInfo {
    uint32_t    index;
    std::string processName;
    pa_cvolume  volume;
    bool        mute;
    uint8_t     channels;
};

struct PaSinkInfo {
    uint32_t   index = PA_INVALID_INDEX;
    pa_cvolume volume{};
    bool       mute = false;
};

// ---------------------------------------------------------------------------
// Callback helpers — all list callbacks must signal the mainloop on eol so
// the waitOp loop in PulseSession can unblock.
// ---------------------------------------------------------------------------

namespace {

struct SinkInputListCtx {
    pa_threaded_mainloop*        ml;
    std::vector<PaSinkInputInfo> result;
};

void sinkInputListCb(pa_context*, const pa_sink_input_info* info,
                      int eol, void* userdata)
{
    auto* ctx = static_cast<SinkInputListCtx*>(userdata);
    if (eol) { pa_threaded_mainloop_signal(ctx->ml, 0); return; }
    if (!info) return;

    PaSinkInputInfo si{};
    si.index    = info->index;
    si.volume   = info->volume;
    si.mute     = info->mute != 0;
    si.channels = info->volume.channels;

    const char* bin = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
    if (bin) si.processName = bin;

    ctx->result.push_back(si);
}

struct SinkInfoCtx {
    pa_threaded_mainloop*  ml;
    std::vector<PaSinkInfo> result;
};

void sinkInfoCb(pa_context*, const pa_sink_info* info, int eol, void* userdata)
{
    auto* ctx = static_cast<SinkInfoCtx*>(userdata);
    if (eol) { pa_threaded_mainloop_signal(ctx->ml, 0); return; }
    if (!info) return;
    PaSinkInfo si{};
    si.index  = info->index;
    si.volume = info->volume;
    si.mute   = info->mute != 0;
    ctx->result.push_back(si);
}

struct ServerInfoCtx {
    pa_threaded_mainloop* ml;
    std::string           defaultSinkName;
};

void serverInfoCb(pa_context*, const pa_server_info* info, void* userdata)
{
    auto* ctx = static_cast<ServerInfoCtx*>(userdata);
    if (info && info->default_sink_name)
        ctx->defaultSinkName = info->default_sink_name;
    pa_threaded_mainloop_signal(ctx->ml, 0);
}

void successCb(pa_context*, int, void* userdata)
{
    pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop*>(userdata), 0);
}

} // namespace

// ---------------------------------------------------------------------------
// PulseSession — synchronous wrapper around the threaded mainloop
// ---------------------------------------------------------------------------

class PulseSession {
public:
    PulseSession()
    {
        ml  = pa_threaded_mainloop_new();
        api = pa_threaded_mainloop_get_api(ml);
        ctx = pa_context_new(api, "MixerClientLinux");

        pa_context_set_state_callback(ctx, [](pa_context*, void* u) {
            pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop*>(u), 0);
        }, ml);

        pa_threaded_mainloop_start(ml);
        pa_threaded_mainloop_lock(ml);
        pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

        while (true) {
            pa_context_state_t s = pa_context_get_state(ctx);
            if (s == PA_CONTEXT_READY) break;
            if (!PA_CONTEXT_IS_GOOD(s)) {
                pa_threaded_mainloop_unlock(ml);
                throw std::runtime_error("PulseAudio: context failed to connect");
            }
            pa_threaded_mainloop_wait(ml);
        }
        pa_threaded_mainloop_unlock(ml);
    }

    ~PulseSession()
    {
        pa_threaded_mainloop_lock(ml);
        pa_context_disconnect(ctx);
        pa_threaded_mainloop_unlock(ml);
        pa_threaded_mainloop_stop(ml);
        pa_context_unref(ctx);
        pa_threaded_mainloop_free(ml);
    }

    std::vector<PaSinkInputInfo> getSinkInputs()
    {
        SinkInputListCtx c{ ml, {} };
        lock();
        waitOp(pa_context_get_sink_input_info_list(ctx, sinkInputListCb, &c));
        unlock();
        return std::move(c.result);
    }

    void setSinkInputVolume(uint32_t idx, const pa_cvolume& vol)
    {
        lock();
        waitOp(pa_context_set_sink_input_volume(ctx, idx, &vol, successCb, ml));
        unlock();
    }

    void setSinkInputMute(uint32_t idx, bool mute)
    {
        lock();
        waitOp(pa_context_set_sink_input_mute(ctx, idx, mute ? 1 : 0, successCb, ml));
        unlock();
    }

    PaSinkInfo getDefaultSink()
    {
        std::string name = defaultSinkName();
        SinkInfoCtx c{ ml, {} };
        lock();
        waitOp(pa_context_get_sink_info_by_name(
            ctx, name.empty() ? "@DEFAULT_SINK@" : name.c_str(), sinkInfoCb, &c));
        unlock();
        return c.result.empty() ? PaSinkInfo{} : c.result[0];
    }

    void setDefaultSinkVolume(const pa_cvolume& vol)
    {
        std::string name = defaultSinkName();
        lock();
        waitOp(pa_context_set_sink_volume_by_name(
            ctx, name.empty() ? "@DEFAULT_SINK@" : name.c_str(), &vol, successCb, ml));
        unlock();
    }

    void setDefaultSinkMute(bool mute)
    {
        std::string name = defaultSinkName();
        lock();
        waitOp(pa_context_set_sink_mute_by_name(
            ctx, name.empty() ? "@DEFAULT_SINK@" : name.c_str(), mute ? 1 : 0, successCb, ml));
        unlock();
    }

private:
    pa_threaded_mainloop* ml  = nullptr;
    pa_mainloop_api*      api = nullptr;
    pa_context*           ctx = nullptr;

    void lock()   { pa_threaded_mainloop_lock(ml); }
    void unlock() { pa_threaded_mainloop_unlock(ml); }

    void waitOp(pa_operation* op)
    {
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
            pa_threaded_mainloop_wait(ml);
        pa_operation_unref(op);
    }

    std::string defaultSinkName()
    {
        ServerInfoCtx c{ ml, {} };
        lock();
        waitOp(pa_context_get_server_info(ctx, serverInfoCb, &c));
        unlock();
        return c.defaultSinkName;
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float paVolumeToFloat(const pa_cvolume& vol)
{
    return (float)pa_cvolume_avg(&vol) / (float)PA_VOLUME_NORM;
}

static pa_cvolume floatToPaVolume(float f, uint8_t channels)
{
    f = std::clamp(f, 0.0f, 1.0f);
    pa_cvolume vol;
    pa_cvolume_set(&vol, channels ? channels : 2, (pa_volume_t)(f * PA_VOLUME_NORM));
    return vol;
}

static std::string GetFocusedProcessName()
{
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return {};

    Window focused = None;
    int    revert  = 0;
    XGetInputFocus(dpy, &focused, &revert);

    std::string result;
    if (focused != None && focused != PointerRoot) {
        Atom pidAtom = XInternAtom(dpy, "_NET_WM_PID", True);
        if (pidAtom != None) {
            Atom           actualType;
            int            actualFormat;
            unsigned long  nItems, bytesAfter;
            unsigned char* prop = nullptr;

            if (XGetWindowProperty(dpy, focused, pidAtom, 0, 1, False, XA_CARDINAL,
                                    &actualType, &actualFormat,
                                    &nItems, &bytesAfter, &prop) == Success && prop) {
                pid_t pid = (pid_t)(*(unsigned long*)prop);
                XFree(prop);

                std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
                std::ifstream f(commPath);
                std::getline(f, result);
            }
        }
    }

    XCloseDisplay(dpy);
    return result;
}

static PulseSession& pulse()
{
    static PulseSession session;
    return session;
}

// ---------------------------------------------------------------------------
// VolumeMixerController — audio operations
// ---------------------------------------------------------------------------

float VolumeMixerController::SetVolume(const std::string& processName, float volumeDelta)
{
    float newVolume = -1.0f;
    auto inputs = pulse().getSinkInputs();
    if (s_fEnableLogging) {
        std::cout << "SetVolume(\"" << processName << "\") — active sink inputs:";
        for (const auto& si : inputs)
            std::cout << " [" << si.processName << "]";
        std::cout << '\n';
    }
    for (const auto& si : inputs) {
        if (si.processName.find(processName) == std::string::npos) continue;

        float next = std::clamp(paVolumeToFloat(si.volume) + volumeDelta, 0.0f, 1.0f);
        pulse().setSinkInputVolume(si.index, floatToPaVolume(next, si.channels));
        newVolume = next;

        if (s_fEnableLogging)
            std::cout << si.processName << " vol=" << next << '\n';
    }
    return newVolume;
}

float VolumeMixerController::SetMasterVolume(float volumeDelta)
{
    PaSinkInfo sink = pulse().getDefaultSink();
    float next = std::clamp(paVolumeToFloat(sink.volume) + volumeDelta, 0.0f, 1.0f);
    pulse().setDefaultSinkVolume(floatToPaVolume(next, sink.volume.channels ? sink.volume.channels : 2));
    if (s_fEnableLogging)
        std::cout << "master vol=" << next << '\n';
    return next;
}

float VolumeMixerController::SetFocusedVolume(float volumeDelta)
{
    std::string name = GetFocusedProcessName();
    return name.empty() ? -1.0f : SetVolume(name, volumeDelta);
}

bool VolumeMixerController::ToggleMute(const std::string& processName, std::optional<bool> optfMute)
{
    bool found = false;
    bool target = optfMute.value_or(false);

    for (const auto& si : pulse().getSinkInputs()) {
        if (si.processName.find(processName) == std::string::npos) continue;
        if (!optfMute.has_value() && !found) target = !si.mute;
        pulse().setSinkInputMute(si.index, target);
        found = true;
    }
    return target;
}

bool VolumeMixerController::ToggleFocusedMute()
{
    std::string name = GetFocusedProcessName();
    return name.empty() ? false : ToggleMute(name, std::nullopt);
}

bool VolumeMixerController::ToggleMasterMute()
{
    PaSinkInfo sink = pulse().getDefaultSink();
    bool newMute = !sink.mute;
    pulse().setDefaultSinkMute(newMute);
    return newMute;
}

bool VolumeMixerController::QueryAllMuteStates()
{
    bool fAnyChange = false;
    auto inputs = pulse().getSinkInputs();

    for (DialState& state : states) {
        bool fMute = false;

        switch (state.m_targetType) {
        case TargetType::Process:
            for (const auto& name : state.m_vecProcessNames)
                for (const auto& si : inputs)
                    if (si.processName.find(name) != std::string::npos)
                        fMute = si.mute;
            break;

        case TargetType::All:
            fMute = pulse().getDefaultSink().mute;
            break;

        case TargetType::Focus: {
            std::string name = GetFocusedProcessName();
            for (const auto& si : inputs)
                if (si.processName.find(name) != std::string::npos)
                    fMute = si.mute;
            break;
        }

        default: break;
        }

        fAnyChange |= (state.fMute != fMute);
        state.fMute  = fMute;
    }
    return fAnyChange;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

VolumeMixerController::VolumeMixerController()
{
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";

    nlohmann::json settings;
    std::string    filepath = std::string(home) + "/MixerSettings.json";
    std::fstream   f(filepath);

    if (!f.good()) {
        settings = R"(
        {
            "dials":
            [
                { "Target":3, "Color": [255,0,0] },
                { "Target":1, "Color": [0,255,0] },
                { "Target":0, "Processes":["firefox","chromium","chrome"], "Color": [0,0,255] },
                { "Target":0, "Processes":["Discord","teams"], "Color": [128,0,128] }
            ]
        })"_json;

        f.close();
        f.open(filepath, std::ios::out);
        f << settings.dump(4);
    } else {
        settings = nlohmann::json::parse(f);
    }
    f.close();

    int iDial = 0;
    for (auto& ds : settings["dials"]) {
        if (iDial >= numDials) break;
        DialState& state   = states[iDial];
        state.m_targetType = ds["Target"].get<TargetType>();

        if (state.m_targetType == TargetType::Process)
            for (auto& p : ds["Processes"])
                state.m_vecProcessNames.push_back(p.get<std::string>());

        state.r = ds["Color"][0].get<int>();
        state.g = ds["Color"][1].get<int>();
        state.b = ds["Color"][2].get<int>();
        ++iDial;
    }

    time(&lastMuteQuery);
}

// ---------------------------------------------------------------------------
// HidReader callbacks
// ---------------------------------------------------------------------------

void VolumeMixerController::ReadButton(int iButton, bool value)
{
    if (s_fEnableLogging)
        std::cout << "btn id:" << iButton << " val:" << value << '\n';

    if (iButton < 0 || iButton >= numDials) return;

    switch (states[iButton].m_targetType) {
    case TargetType::Process: {
        std::optional<bool> optMute;
        for (const std::string& name : states[iButton].m_vecProcessNames)
            optMute = ToggleMute(name, optMute);
        states[iButton].fMute = optMute.value_or(false);
        break;
    }
    case TargetType::All:
        states[iButton].fMute = ToggleMasterMute();
        break;
    case TargetType::Focus:
        states[iButton].fMute = ToggleFocusedMute();
        break;
    default: break;
    }

    WriteColorData();
}

void VolumeMixerController::ReadDial(int iDial, int64_t value)
{
    if (s_fEnableLogging)
        std::cout << "dial id:" << iDial << " val:" << value << '\n';

    if (iDial < 0 || iDial >= numDials) return;

    float newVolume = -1.0f;
    switch (states[iDial].m_targetType) {
    case TargetType::Process:
        for (const std::string& name : states[iDial].m_vecProcessNames)
            newVolume = std::max(newVolume, SetVolume(name, (float)value * singleTickRotationAmount));
        break;
    case TargetType::All:
        newVolume = SetMasterVolume((float)value * singleTickRotationAmount);
        break;
    case TargetType::Focus:
        newVolume = SetFocusedVolume((float)value * singleTickRotationAmount);
        break;
    default: break;
    }

    states[iDial].m_Counter += (int)value;
    if (newVolume >= 0.0f)
        FlashEncoderVolumeToLeds(states[iDial], newVolume);
}

void VolumeMixerController::FlashEncoderVolumeToLeds(const DialState& state, float volume)
{
    std::vector<uint8_t> outReport;
    outReport.push_back(1); // Report ID

    float v = volume * numDials;
    for (int i = 0; i < numDials; ++i) {
        if (v >= 1.0f) {
            outReport.push_back((uint8_t)state.r);
            outReport.push_back((uint8_t)state.g);
            outReport.push_back((uint8_t)state.b);
        } else if (v < 0.0f) {
            outReport.push_back(0); outReport.push_back(0); outReport.push_back(0);
        } else {
            outReport.push_back((uint8_t)(state.r * v));
            outReport.push_back((uint8_t)(state.g * v));
            outReport.push_back((uint8_t)(state.b * v));
        }
        v -= 1.0f;
    }

    WriteHidOut(outReport);

    std::time_t now;
    encoderFlashingStart = time(&now);
}

void VolumeMixerController::OnSync()
{
    std::time_t now;
    time(&now);

    if (difftime(now, lastMuteQuery) >= iMuteQueryInterval) {
        QueryAllMuteStates();
        lastMuteQuery = now;
    }

    std::vector<uint8_t> outReport;
    outReport.push_back(1); // Report ID

    for (int i = 0; i < numDials; ++i) {
        const DialState& s = states[i];
        if (!s.fMute) {
            outReport.push_back((uint8_t)s.r);
            outReport.push_back((uint8_t)s.g);
            outReport.push_back((uint8_t)s.b);
        } else {
            outReport.push_back(0); outReport.push_back(0); outReport.push_back(0);
        }
    }

    WriteHidOut(outReport);
}

void VolumeMixerController::WriteColorData()
{
    OnSync();
}
