#pragma once

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
};

void HandleSerialInput(const char* szBuffer);