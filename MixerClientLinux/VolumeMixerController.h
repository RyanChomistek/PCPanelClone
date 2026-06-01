#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include <ctime>
#include "HidReader.h"

enum class ClientToDeviceEventType : int
{
    Color      = 0,
    Brightness = 1,
    HeartBeat  = 2,
};

enum class Direction : int
{
    CCW = 0,
    CW  = 1
};

enum class TargetType : int
{
    Process = 0,
    All     = 1,
    Device  = 2,
    Focus   = 3
};

class VolumeMixerController : public HidReader
{
public:
    VolumeMixerController();

protected:
    void ReadDial(int iDial, int64_t value) override;
    void ReadButton(int iButton, bool value) override;
    void OnSync() override;

private:
    struct DialState
    {
        int                      m_Counter = 0;
        std::vector<std::string> m_vecProcessNames;
        TargetType               m_targetType = TargetType::All;
        int                      r = 255, g = 255, b = 255;
        bool                     fMute = false;
    };

    void  WriteColorData();
    void  FlashEncoderVolumeToLeds(const DialState&, float volume);

    float SetMasterVolume(float volumeDelta);
    float SetVolume(const std::string& processName, float volumeDelta);
    float SetFocusedVolume(float volumeDelta);

    bool  ToggleMute(const std::string& processName, std::optional<bool> optfMute);
    bool  ToggleFocusedMute();
    bool  ToggleMasterMute();
    bool  QueryAllMuteStates();

    static constexpr int   numDials                = 4;
    static constexpr float singleTickRotationAmount = 0.05f;

    DialState   states[numDials];
    std::once_flag fFirstMessage;

    std::optional<std::time_t> encoderFlashingStart;
    int iVolumeChangeFlashLength = 1;

    std::time_t lastMuteQuery{};
    int         iMuteQueryInterval = 5;
};
