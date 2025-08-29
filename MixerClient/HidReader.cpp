#include "util.h"
#include "VolumeMixerController.h"
#include <tchar.h>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <algorithm>

#include "hidsdi.h"
#include "setupapi.h"
#include "winerror.h"
#include "windows.h"

#include "HidReader.h"

void HidReader::BuildInputValueIndex(
	PHIDP_PREPARSED_DATA ppd)
{
	hidValues.clear();

	// Query how many value caps first
	USHORT valueCapsCount = 0;
	HidP_GetValueCaps(HidP_Input, NULL, &valueCapsCount, ppd);
	if (valueCapsCount == 0) return;

	std::vector<HIDP_VALUE_CAPS> valueCaps(valueCapsCount);
	if (HidP_GetValueCaps(HidP_Input, valueCaps.data(), &valueCapsCount, ppd) != HIDP_STATUS_SUCCESS) return;

	std::vector<int> dialIndexes;
	for (USHORT i = 0; i < valueCapsCount; ++i) {
		const auto& c = valueCaps[i];
		if (!c.IsRange) {
			hidValues[c.NotRange.DataIndex] = { .caps = c};
			dialIndexes.push_back(c.NotRange.DataIndex);
		}
		else {
			for (int dataIndex = c.Range.DataIndexMin; dataIndex <= c.Range.DataIndexMax; dataIndex++) {
				hidValues[dataIndex] = { .caps = c };
				dialIndexes.push_back(dataIndex);
			}
		}
	}
	
	std::vector<int> buttonIndexes;
	USHORT buttonCapsCount = 0;
	HidP_GetButtonCaps(HidP_Input, NULL, &buttonCapsCount, ppd);
	if (buttonCapsCount == 0) return;
	std::vector<HIDP_BUTTON_CAPS> buttonCaps(buttonCapsCount);
	if (HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &buttonCapsCount, ppd) != HIDP_STATUS_SUCCESS) return;
	for (USHORT i = 0; i < buttonCapsCount; ++i) {
		const auto& c = buttonCaps[i];

		if (!c.IsRange) {
			hidButtons[c.NotRange.DataIndex] = { .caps = c };
			buttonIndexes.push_back(c.NotRange.DataIndex);
		}
		else {
			for (int dataIndex = c.Range.DataIndexMin; dataIndex <= c.Range.DataIndexMax; dataIndex++) {
				hidButtons[dataIndex] = { .caps = c };
				buttonIndexes.push_back(dataIndex);
			}
		}
	}

	std::ranges::sort(dialIndexes);
	std::ranges::sort(buttonIndexes);

	std::cout << "\n dials: ";

	for (int iDial = 0; iDial < dialIndexes.size(); iDial++) {
		hidValues[dialIndexes[iDial]].typeIndex = iDial;
		std::cout << "[" << iDial << ", " << dialIndexes[iDial] << "] ";
	}

	std::cout << "buttons: ";
	for (int iButton = 0; iButton < buttonIndexes.size(); iButton++) {
		hidButtons[buttonIndexes[iButton]].typeIndex = iButton;
		std::cout << "[" << iButton << ", " << buttonIndexes[iButton] << "] ";
	}

	std::cout << "\n";

}

LONG NormalizeHidValue(ULONG rawValue, HIDP_VALUE_CAPS* caps)
{
	LONG signedValue;

	// If logical min < 0 → signed
	if (caps->LogicalMin < 0) {
		// Sign extend from caps->BitSize
		LONG mask = 1 << (caps->BitSize - 1);         // sign bit
		LONG value = (LONG)rawValue;
		if (value & mask) {
			// negative number, extend
			value |= -1 << caps->BitSize;
		}
		signedValue = value;
	}
	else {
		signedValue = (LONG)rawValue;
	}

	// Now normalize between -1.0 … 1.0 (or 0 … 1 depending on your needs)
	double range = (double)(caps->LogicalMax - caps->LogicalMin);
	double normalized = 0.0;
	if (range != 0.0) {
		normalized = (signedValue - caps->LogicalMin) / range;
	}

	//printf("Raw=%ld Signed=%ld Normalized=%.3f\n",
	//	rawValue, signedValue, normalized);

	return signedValue; // or return normalized as double if you prefer
}

static bool IsDial(USAGE page, USAGE usage)
{
	return (page == 0x01 && usage == 0x37); // Generic Desktop / Dial
}

void HidReader::ReadLoopDecodeDial(HANDLE hDevice, PHIDP_PREPARSED_DATA ppd, ULONG inputLen)
{
	OVERLAPPED ol{};
	ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!ol.hEvent) {
		printf("CreateEvent failed: %lu\n", GetLastError());
		return;
	}

	// Build DataIndex → meta map once
	BuildInputValueIndex(ppd);

	std::vector<BYTE> report(inputLen);

	printf("Listening for HID input reports...\n");
	while (true) {
		ResetEvent(ol.hEvent);
		DWORD bytesRead = 0;

		BOOL ok = ReadFile(hDevice, report.data(), (DWORD)report.size(), &bytesRead, &ol);
		if (!ok) {
			DWORD err = GetLastError();
			if (err != ERROR_IO_PENDING) {
				printf("ReadFile failed: %lu\n", err);
				break;
			}
		}

		DWORD w = WaitForSingleObject(ol.hEvent, INFINITE);
		if (w != WAIT_OBJECT_0) {
			printf("Wait failed: %lu\n", GetLastError());
			CancelIo(hDevice);
			break;
		}

		if (!GetOverlappedResult(hDevice, &ol, &bytesRead, FALSE)) {
			DWORD err = GetLastError();
			if (err == ERROR_DEVICE_NOT_CONNECTED) {
				printf("Device removed.\n");
				break;
			}
			printf("GetOverlappedResult failed: %lu\n", err);
			break;
		}

		// Parse report into HIDP_DATA[]
		ULONG dataCount = 32; // adjust if you expect more controls
		HIDP_DATA dataList[32]{};

		NTSTATUS st = HidP_GetData(
			HidP_Input,
			dataList,
			&dataCount,
			ppd,
			(PCHAR)report.data(),
			bytesRead
		);

		if (st != HIDP_STATUS_SUCCESS) {
			printf("HidP_GetData failed: 0x%08X\n", (UINT)st);
			continue;
		}

		// Iterate controls reported in this packet
		for (ULONG i = 0; i < dataCount; ++i) {
			const HIDP_DATA& d = dataList[i];
			bool isDial = hidValues.find(d.DataIndex) != hidValues.end();
			bool isButton = hidButtons.find(d.DataIndex) != hidButtons.end();

			if (isDial) {
				HidValue& hidValue = hidValues.at(d.DataIndex);
				LONG val = NormalizeHidValue((LONG)d.RawValue, &hidValue.caps);
				hidValue.value = static_cast<uint64_t>(val);
				printf("DIAL: %ld delta= %ld | %ld\n", d.DataIndex, d.RawValue, val);
				ReadDial(hidValue.typeIndex, val);
			}
			else if (isButton) {
				hidButtons[d.DataIndex].value = d.On ? true : false;
				printf("BUTTON DI=%u On=%d\n", d.DataIndex, d.On ? 1 : 0);
				ReadButton(hidButtons[d.DataIndex].typeIndex, d.On ? true : false);
			}
		}

		std::cout << '\n';

		
	}

	CloseHandle(ol.hEvent);
}

bool HidReader::loadHidDevice(int index, GUID& hidGuid, HDEVINFO& deviceInfoSet) {
	SP_DEVICE_INTERFACE_DATA devInterfaceData = { 0 };
	devInterfaceData.cbSize = sizeof(devInterfaceData);
	if (!SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, index, &devInterfaceData)
		&& GetLastError() == ERROR_NO_MORE_ITEMS) {
		return false;
	}

	DWORD requiredSize = 0;
	SetupDiGetDeviceInterfaceDetail(
		deviceInfoSet, &devInterfaceData,
		NULL, 0, &requiredSize, NULL);
	if (requiredSize == 0) {
		printf("Error: Could not get required size\n");
		return true;
	}

	SP_DEVICE_INTERFACE_DETAIL_DATA* pDetailData = (SP_DEVICE_INTERFACE_DETAIL_DATA*)malloc(requiredSize);
	pDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

	// Second call: actually get the device path
	if (!SetupDiGetDeviceInterfaceDetail(
		deviceInfoSet, &devInterfaceData,
		pDetailData, requiredSize,
		NULL, NULL)) {
		printf("Error: SetupDiGetDeviceInterfaceDetail failed\n");
		free(pDetailData);
		return true;
	}



	HANDLE hDevice = CreateFile(pDetailData->DevicePath, GENERIC_READ | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

	if (hDevice == INVALID_HANDLE_VALUE) {
		HRESULT hr = GetLastError();
		//std::cout << "failed to make file\n";
		return true;
	}

	PHIDP_PREPARSED_DATA ppd;
	HidD_GetPreparsedData(hDevice, &ppd);
	HIDP_CAPS caps;
	HidP_GetCaps(ppd, &caps);
	//HidD_FreePreparsedData(ppd);

	// vid_0424&pid_274
	// usage 1 UsagePage ff00

	// VID_0424& PID_5734
	std::wstring pid, vid, sid;
	WCHAR buf[256];

	// Product string
	if (HidD_GetProductString(hDevice, buf, sizeof(buf))) {
		pid = buf;
	}

	// Manufacturer string
	if (HidD_GetManufacturerString(hDevice, buf, sizeof(buf))) {
		vid = buf;

	}

	// Serial number string
	if (HidD_GetSerialNumberString(hDevice, buf, sizeof(buf))) {
		sid = buf;

	}
	std::wcout << pid << " | " << vid << " | " << sid << "\n";
	if (caps.Usage != 0x37 || caps.UsagePage != 1) {
		return true;
	}

	std::wcout << "Device Path: " << std::wstring(pDetailData->DevicePath) << "\n";

	std::wcout << pid << " | " << vid << " | " << sid << "\n";

	std::wcout
		<< "usage " << std::hex << caps.Usage << " "
		<< "UsagePage " << caps.UsagePage << " "
		<< "NumberInputButtonCaps " << caps.NumberInputButtonCaps << " "
		<< "NumberInputValueCaps " << caps.NumberInputValueCaps << " "
		<< "NumberInputDataIndices " << caps.NumberInputDataIndices << " "
		<< "\n\n"
		;

	printf("Input report length: %d\n", caps.InputReportByteLength);


	ReadLoopDecodeDial(hDevice, ppd, caps.InputReportByteLength * 10);

	free(pDetailData);
	return true;
}

HRESULT HidReader::HrReadLoop() {
	GUID hidGuid;
	HidD_GetHidGuid(&hidGuid);
	HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	int index = 0;
	HRESULT hr = NO_ERROR;
	while (loadHidDevice(index, hidGuid, deviceInfoSet)) {
		index++;
	}

	SetupDiDestroyDeviceInfoList(deviceInfoSet);
	return NO_ERROR;
}

HidReader::HidReader() {

}