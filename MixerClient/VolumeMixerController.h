#pragma once
#include <string>
#include <vector>
#include <audioclient.h>
#include "StandardSerialReader.h"

/// <summary>
/// specifies what event is being sent from device to client
/// </summary>
enum class DeviceToClientEventType : int
{
	Button = 0, // a button is pressed
	Dial = 1, // a dial is turned
	StartUp = 2 // the device restarted
};

/// <summary>
/// specifies what event is being sent from client to device
/// </summary>
enum class ClientToDeviceEventType : int
{
	Color = 0, // color event, the client is sending what colors the device should show on its LEDs
	Brightness = 1
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

class VolumeMixerController : public StandardSerialReader
{
public:
	/// <summary>
	/// initialized the dial states by reading from a settings file
	/// </summary>
	VolumeMixerController();
	void ReadInput() final;

private:

	/// <summary>
	/// sends color data to the device
	/// </summary>
	void WriteColorData();
	void OnConnected() final;

private:
	struct DialState
	{
		int m_Counter;
		std::vector<std::wstring> m_vecProcessNames;
		TargetType m_targetType;
		int r, g, b;
	};

	static constexpr int numDials = 4;
	static constexpr float singleTickRotationAmount = .05f;
	DialState states[numDials];
};