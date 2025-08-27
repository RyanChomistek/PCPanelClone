#pragma once

#include "setupapi.h"
#include "hidsdi.h"
#include "setupapi.h"


class HidReader {
public:
	HidReader();
	bool loadHidDevice(int index, GUID& hidGuid, HDEVINFO& deviceInfoSet);
	HRESULT HrReadLoop();
	virtual void ReadInput() = 0;
private:
	void ReadHidLoop(HANDLE hDevice, ULONG reportLength);
	void ReadLoopDecodeDial(HANDLE hDevice, PHIDP_PREPARSED_DATA ppd, ULONG inputLen);
};