#pragma once

#include "setupapi.h"
#include "hidsdi.h"
#include "setupapi.h"
#include <unordered_map>
#include <variant>

enum class HidValueType {
	button,
	dial
};

struct HidValue {
	HIDP_VALUE_CAPS caps;
	uint64_t value;
	int typeIndex;
};

struct HidButton {
	HIDP_BUTTON_CAPS caps;
	bool value;
	int typeIndex;
};

class HidReader {
public:
	HidReader();
	bool loadHidDevice(int index, GUID& hidGuid, HDEVINFO& deviceInfoSet);
	HRESULT HrReadLoop();
	virtual void ReadDial(int iDial, int64_t value) = 0;
	virtual void ReadButton(int iButton, bool value) = 0;

	void WriteHidOut(std::vector<UCHAR>& outReport);

private:
	void ReadLoopDecodeDial(PHIDP_PREPARSED_DATA ppd, ULONG inputLen);
	void BuildInputValueIndex(
		PHIDP_PREPARSED_DATA ppd);

protected:
	std::unordered_map<uint16_t, HidValue> hidValues;
	std::unordered_map<uint16_t, HidButton> hidButtons;

	HANDLE hDevice;
	HIDP_CAPS deviceCaps;
};