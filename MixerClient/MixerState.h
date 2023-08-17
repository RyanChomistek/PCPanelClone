#pragma once
#include <string>
#include <vector>
#include <audioclient.h>

class CSerial;

enum class DeviceToClientEventType : int
{
	Button = 0,
	Dial = 1,
	StartUp = 2
};

enum class ClientToDeviceEventType : int
{
	Color = 0,
};

enum class Direction: int
{
	CCW = 0,
	CW = 1
};

enum class TargetType : int 
{
	Process = 0,
	All = 1,
	Device = 2,
	Focus = 3
};

struct MixerState
{
	int m_Counter;
	std::vector<std::wstring> m_vecProcessNames;
	TargetType m_targetType;
	int r, g, b;
};

void WriteColorData(CSerial& serial);

const MixerState* RgMixerState(_Out_ int& cStates);

void InitMixerState();

void PrintVolumes();

void HandleSerialInput(const char* szBuffer, CSerial& serial);