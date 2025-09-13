#pragma once
#include <string>
#include <vector>
#include <audioclient.h>
#include "HidReader.h"
#include <mutex>
#include <optional>

/// <summary>
/// specifies what event is being sent from client to device
/// </summary>
enum class ClientToDeviceEventType : int
{
	Color = 0, // color event, the client is sending what colors the device should show on its LEDs
	Brightness = 1,
	HeartBeat = 2,
};

enum class Direction: int
{
	CCW = 0, // counter clock wise
	CW = 1 // clock wise
};

/// <summary>
/// Specifies what the target of the dial is
/// </summary>
enum class TargetType : int 
{
	Process = 0, // targets one or more processes
	All = 1, // targets the master volume
	Device = 2, // targets a specific device
	Focus = 3 // targets the currently focused window
};

class VolumeMixerController : public HidReader
{
public:
	/// <summary>
	/// initialized the dial states by reading from a settings file
	/// </summary>
	VolumeMixerController();

protected:
	void ReadDial(int iDial, int64_t value) override;
	void ReadButton(int iButton, bool value) override;
	void OnSync() override;

private:
	struct DialState
	{
		int m_Counter;
		std::vector<std::wstring> m_vecProcessNames;
		TargetType m_targetType;
		int r, g, b;
		bool fMute = false;
	};

private:

	/// <summary>
	/// sends color data to the device
	/// </summary>
	void WriteColorData();
	//void OnConnected() final;

	void FlashEncoderVolumeToLeds(const DialState&, float volumn);

	float SetMasterVolume(float volumeDelta);
	float SetVolume(const std::wstring& processName, float volumeDelta);
	float SetFocusedVolume(float volumeDelta);
	bool ToggleMute(const std::wstring& processName, std::optional<bool> optfMute);
	bool ToggleFocusedMute();
	bool ToggleMasterMute();
	bool QueryAllMuteStates();
	static constexpr int numDials = 4;
	static constexpr float singleTickRotationAmount = .05f;
	DialState states[numDials];
	std::once_flag fFirstMessage;

	std::optional<std::time_t> encoderFlashingStart;
	int iVolumeChangeFlashLength = 1;

	std::time_t lastMuteQuery;
	int iMuteQueryInterval = 5;
};