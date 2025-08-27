#include "util.h"
#include "VolumeMixerController.h"
#include <tchar.h>
#include <vector>
#include <iostream>
#include <unordered_map>

#include "hidsdi.h"
#include "setupapi.h"
#include "winerror.h"
#include "windows.h"

#include "HidReader.h"

static void BuildInputValueIndex(
	PHIDP_PREPARSED_DATA ppd,
	std::unordered_map<USHORT, HIDP_VALUE_CAPS>& mapOut)
{
	// Query how many value caps first
	USHORT valueCapsCount = 0;
	HidP_GetValueCaps(HidP_Input, NULL, &valueCapsCount, ppd);
	if (valueCapsCount == 0) return;

	std::vector<HIDP_VALUE_CAPS> caps(valueCapsCount);
	if (HidP_GetValueCaps(HidP_Input, caps.data(), &valueCapsCount, ppd) != HIDP_STATUS_SUCCESS) return;

	for (USHORT i = 0; i < valueCapsCount; ++i) {
		const auto& c = caps[i];
		if (!c.IsRange) {
			mapOut[c.NotRange.DataIndex] = c;
		}
		else {
			// For ranged items, the DataIndex field is the first index in the range.
			// DataIndexMax is available on KM side; in UM you typically rely on HidP_GetData
			// returning DataIndex for each control instance. We’ll stash the base and check ranges on the fly.
			mapOut[c.Range.DataIndexMin] = c;
		}
	}
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

	printf("Raw=%ld Signed=%ld Normalized=%.3f\n",
		rawValue, signedValue, normalized);

	return signedValue; // or return normalized as double if you prefer
}

static bool IsDial(USAGE page, USAGE usage)
{
	return (page == 0x01 && usage == 0x37); // Generic Desktop / Dial
}

// Optional: map DataIndex to concrete Usage for ranged entries.
// For ranged ValueCaps, HidP_GetData will return distinct DataIndex values
// (base + controlInstance). We try to resolve them to a specific Usage.
static bool ResolveUsageForDataIndex(
	USHORT dataIndex,
	const std::unordered_map<USHORT, HIDP_VALUE_CAPS>& idxMap,
	USAGE& outPage,
	USAGE& outUsage,
	HIDP_VALUE_CAPS& caps)
{
	// Exact hit?
	auto it = idxMap.find(dataIndex);
	if (it != idxMap.end()) {
		const auto& info = it->second;
		outPage = info.UsagePage;
		if (!info.IsRange) {
			outUsage = info.NotRange.Usage;
			return true;
		}
		// Ranged: DataIndex for instance k might be (base + k).
		// Walk backwards to find the nearest base we stored.
		// (In practice this is usually contiguous; we search up to 32 back for safety.)
		for (int back = 0; back < 32; ++back) {
			USHORT candidate = (USHORT)(dataIndex - back);
			auto jt = idxMap.find(candidate);
			if (jt == idxMap.end()) break;

			caps = jt->second;
			if (jt->second.UsagePage == info.UsagePage && jt->second.IsRange) {
				USHORT offset = dataIndex - candidate;
				USHORT usage = jt->second.Range.UsageMin + offset;
				if (usage <= jt->second.Range.UsageMax) {
					outUsage = usage;
					return true;
				}
			}
		}
		// Fallback: we at least know the page
		outUsage = 0;
		return true;
	}

	// Not found
	return false;
}

static bool GetLogicalRangeForDataIndex(
	USHORT dataIndex,
	const std::unordered_map<USHORT, HIDP_VALUE_CAPS>& idxMap,
	LONG& outMin,
	LONG& outMax)
{
	// Exact or nearest ranged base (same trick as above)
	auto it = idxMap.find(dataIndex);
	if (it != idxMap.end()) {
		outMin = it->second.LogicalMin;
		outMax = it->second.LogicalMax;
		return true;
	}
	for (int back = 0; back < 32; ++back) {
		USHORT candidate = (USHORT)(dataIndex - back);
		auto jt = idxMap.find(candidate);
		if (jt == idxMap.end()) break;
		outMin = jt->second.LogicalMin;
		outMax = jt->second.LogicalMax;
		return true;
	}
	return false;
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
	std::unordered_map<USHORT, HIDP_VALUE_CAPS> idxMap;
	BuildInputValueIndex(ppd, idxMap);

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

			// Buttons yield .On, Values yield .RawValue. We want numeric (value) controls.
			// Heuristic: if Logical range exists for DataIndex, treat as value.
			LONG lmin = 0, lmax = 0;
			bool hasRange = GetLogicalRangeForDataIndex(d.DataIndex, idxMap, lmin, lmax);

			if (hasRange) {
				USAGE page = 0, usage = 0;
				HIDP_VALUE_CAPS caps;
				if (!ResolveUsageForDataIndex(d.DataIndex, idxMap, page, usage, caps)) {
					//// Unknown value control — still print for debugging
					//LONG val = NormalizeHidValue((LONG)d.RawValue, &caps);
					//printf("Value DI=%u UP=0x%X U=0x%X Raw=%ld\n",
					//	d.DataIndex, page, usage, val);
					continue;
				}

				LONG val = NormalizeHidValue((LONG)d.RawValue, &caps);

				if (IsDial(page, usage)) {
					// Many dial devices report delta per packet (e.g., -1, +1).
					// Others report an absolute position with wrap; you can infer delta between packets if needed.
					printf("DIAL: %ld delta=%ld (logical [%ld..%ld])\n", d.RawValue, val, lmin, lmax);
				}
				else {
					// Other numeric control (wheel, slider, etc.)
					printf("VALUE: UP=0x%X U=0x%X Raw=%ld\n", page, usage, val);
				}
			}
			else {
				// Likely a button
				printf("BUTTON DI=%u On=%d\n", d.DataIndex, d.On ? 1 : 0);
			}
		}
	}

	CloseHandle(ol.hEvent);
}

void HidReader::ReadHidLoop(HANDLE hDevice, ULONG reportLength) {
	OVERLAPPED ol = { 0 };
	ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (!ol.hEvent) {
		printf("Failed to create event: %lu\n", GetLastError());
		return;
	}

	BYTE* report = (BYTE*)malloc(reportLength);
	if (!report) return;

	while (1) {
		DWORD bytesRead = 0;
		ResetEvent(ol.hEvent);

		// Post an async read
		BOOL ok = ReadFile(hDevice, report, reportLength, &bytesRead, &ol);
		if (!ok) {
			DWORD err = GetLastError();
			if (err != ERROR_IO_PENDING) {
				printf("ReadFile failed: %lu\n", err);
				break;
			}
		}

		// Wait for the read to complete (or timeout)
		DWORD wait = WaitForSingleObject(ol.hEvent, INFINITE); // block until report
		if (wait == WAIT_OBJECT_0) {
			if (GetOverlappedResult(hDevice, &ol, &bytesRead, FALSE)) {
				// ✅ Got a HID report
				printf("Report (%lu bytes): ", bytesRead);
				for (DWORD i = 0; i < bytesRead; i++) {
					printf("%02X ", report[i]);
				}
				printf("\n");

				// TODO: parse HID report here (HidP_GetData, etc.)
			}
			else {
				printf("GetOverlappedResult failed: %lu\n", GetLastError());
				break;
			}
		}
		else {
			printf("Wait failed or timeout\n");
			CancelIo(hDevice);
			break;
		}
	}

	free(report);
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