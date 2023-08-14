#pragma once
#include <string>
#include <audioclient.h>

enum class EventType: int
{
	Button = 0,
	Dial = 1
};

enum class Direction: int
{
	CCW = 0,
	CW = 1
};

struct MixerState
{
	int m_Counter;
	std::wstring m_processName;
	ISimpleAudioVolume* m_pVolume;
};

void InitMixerState();

void PrintVolumes();

void HandleSerialInput(const char* szBuffer);