#include "MixerState.h"
#include <audiopolicy.h>
#include <iostream>
#include <mmdeviceapi.h>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <tchar.h>
#include <windows.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <endpointvolume.h>
#include "Serial.h"

#pragma comment(lib, "shell32.lib")

using namespace nlohmann::literals;

void PrintVolumes()
{
	IMMDevice* pDevice = NULL;
	IMMDeviceEnumerator* pEnumerator = NULL;
	IAudioSessionControl* pSessionControl = NULL;
	IAudioSessionControl2* pSessionControl2 = NULL;
	IAudioSessionManager2* pSessionManager = NULL;

	int hr = CoInitialize(NULL);

	// Create the device enumerator.
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pEnumerator);

	// Get the default audio device.
	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	hr = pDevice->Activate(__uuidof(IAudioSessionManager2),
		CLSCTX_ALL,
		NULL, (void**)&pSessionManager);

	hr = pSessionManager->GetAudioSessionControl(0, FALSE, &pSessionControl);

	// Get the extended session control interface pointer.
	hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);

	// Check whether this is a system sound.
	hr = pSessionControl2->IsSystemSoundsSession();

	int cbSessionCount = 0;
	LPWSTR pswSession = NULL;

	IAudioSessionEnumerator* pSessionList = NULL;

	hr = pSessionManager->GetSessionEnumerator(&pSessionList);
	hr = pSessionList->GetCount(&cbSessionCount);
	std::cout << cbSessionCount << std::endl;
	for (int index = 0; index < cbSessionCount; index++)
	{
		hr = pSessionList->GetSession(index, &pSessionControl);
		hr = pSessionControl->GetDisplayName(&pswSession);
		std::wcout << "Session Name: " << pswSession << std::endl;

		hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);

		DWORD nPID = 0;
		hr = pSessionControl2->GetProcessId(&nPID);
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, nPID);
		if (hProcess != NULL)
		{
			WCHAR wsImageName[MAX_PATH + 1];
			DWORD nSize = MAX_PATH;
			if (QueryFullProcessImageNameW(hProcess, NULL, wsImageName, &nSize))
			{
				std::wcout << wsImageName << std::endl;

				ISimpleAudioVolume* pSimpleAudioVolume;
				hr = pSessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleAudioVolume);

				float volume = 0;
				pSimpleAudioVolume->GetMasterVolume(&volume);
				std::cout << volume << std::endl;
			}
			CloseHandle(hProcess);
		}
	}
}

constexpr int numDials = 4;
static MixerState states[numDials];

const MixerState* RgMixerState(_Out_ int& cStates)
{
	cStates = numDials;
	return states;
}

void WriteColorData(CSerial& serial)
{
	std::stringstream ss;

	for (int iState = 0; iState < numDials; iState++)
	{
		const MixerState& state = states[iState];
		ss << (int)ClientToDeviceEventType::Color << " " << iState << " " << state.r << " " << state.g << " " << state.b << ";";
	}

	std::cout << ss.str() << "\n";
	serial.Write(ss.str().c_str(), ss.str().size());
}

void InitMixerState()
{
	//PrintVolumes();

	WCHAR my_documents[MAX_PATH];
	HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, my_documents);

	nlohmann::json settings;
	std::wstring filepath = std::wstring(my_documents) + L"\\MixerSettings.json";
	std::fstream f(filepath);

	if (!f.good())
	{
		// setup default state
		settings = R"(
			{
				"dials":
				[
					{
						"Target":3,
						"Color": [2,0,0]
					},
					{
						"Target":1,
						"Color": [0,2,0]
					},
					{
						"Target":0,
						"Processes":["firefox.exe", "msedge.exe"],
						"Color": [0,0,2]
					},
					{
						"Target":0,
						"Processes":["Discord.exe", "teams.exe"],
						"Color": [2,0,1]
					}
				]
			})"_json;

		f.close();
		f.open(filepath, std::ios::out);
		f << settings.dump();
	}
	else
	{
		settings = nlohmann::json::parse(f);
	}

	f.close();
	
	int iDial = 0;
	// get the settings out
	for (auto& dialSetting : settings["dials"])
	{
		MixerState& state = states[iDial];
		state.m_targetType = dialSetting["Target"].get<TargetType>();

		switch (state.m_targetType)
		{
			case TargetType::Process:
			{
				std::vector<std::wstring> processNames;
				for (auto& processSetting : dialSetting["Processes"])
				{
					std::cout << processSetting.get<std::string>() << "\n";
					std::string processName = processSetting.get<std::string>();
					processNames.push_back(std::wstring(processName.begin(), processName.end()));
				}

				state.m_vecProcessNames = std::move(processNames);
				break;
			}
			case TargetType::All:
				break;
			case TargetType::Device:
				break;
			case TargetType::Focus:
				break;
		}

		state.r = dialSetting["Color"][0].get<int>();
		state.g = dialSetting["Color"][1].get<int>();
		state.b = dialSetting["Color"][2].get<int>();

		iDial++;
	}
}

void SetVolume(const std::wstring& processName, float volumeDelta)
{
	IMMDevice* pDevice = NULL;
	IMMDeviceEnumerator* pEnumerator = NULL;
	IAudioSessionControl* pSessionControl = NULL;
	IAudioSessionControl2* pSessionControl2 = NULL;
	IAudioSessionManager2* pSessionManager = NULL;

	int hr = CoInitialize(NULL);

	// Create the device enumerator.
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pEnumerator);

	// Get the default audio device.
	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	hr = pDevice->Activate(__uuidof(IAudioSessionManager2),
		CLSCTX_ALL,
		NULL, (void**)&pSessionManager);

	hr = pSessionManager->GetAudioSessionControl(0, FALSE, &pSessionControl);

	// Get the extended session control interface pointer.
	hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);

	// Check whether this is a system sound.
	hr = pSessionControl2->IsSystemSoundsSession();

	int cbSessionCount = 0;
	LPWSTR pswSession = NULL;

	IAudioSessionEnumerator* pSessionList = NULL;

	hr = pSessionManager->GetSessionEnumerator(&pSessionList);
	hr = pSessionList->GetCount(&cbSessionCount);
	for (int index = 0; index < cbSessionCount; index++)
	{
		hr = pSessionList->GetSession(index, &pSessionControl);
		hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);

		DWORD nPID = 0;
		hr = pSessionControl2->GetProcessId(&nPID);
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, nPID);
		if (hProcess != NULL)
		{
			WCHAR wsImageName[MAX_PATH + 1];
			DWORD nSize = MAX_PATH;
			if (QueryFullProcessImageNameW(hProcess, NULL, wsImageName, &nSize))
			{
				std::wstring executableName = std::wstring(wsImageName).substr(std::wstring(wsImageName).find_last_of(L"\\")+1);
				if (wcsstr(wsImageName, processName.c_str()) != NULL)
				{
					std::wcout << executableName << " " << processName << std::endl;
					ISimpleAudioVolume* pSimpleAudioVolume;
					hr = pSessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleAudioVolume);

					float volume = 0;
					pSimpleAudioVolume->GetMasterVolume(&volume);
					std::cout << volume << " " << volumeDelta << std::endl;

					volume += volumeDelta;

					float newVolume = max(min(volume, 1), 0);
					pSimpleAudioVolume->SetMasterVolume(newVolume, NULL);
				}
			}
			CloseHandle(hProcess);
		}
	}
}

void SetMasterVolume(float volumeDelta)
{
	HRESULT hr;
	CoInitialize(NULL);
	IMMDeviceEnumerator* deviceEnumerator = NULL;
	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator);
	IMMDevice* defaultDevice = NULL;

	hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice);
	deviceEnumerator->Release();
	deviceEnumerator = NULL;

	IAudioEndpointVolume* endpointVolume = NULL;
	hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume);
	defaultDevice->Release();
	defaultDevice = NULL;

	float volume = 0;
	endpointVolume->GetMasterVolumeLevelScalar(&volume);
	std::cout << volume << " " << volumeDelta << std::endl;
	volume += volumeDelta;

	float newVolume = max(min(volume, 1), 0);
	endpointVolume->SetMasterVolumeLevelScalar(newVolume, NULL);
}

void SetFocusedVolume(float volumeDelta)
{
	HWND wndFocused = GetForegroundWindow();

	DWORD pid = NULL;
	DWORD tid = GetWindowThreadProcessId(wndFocused, &pid);

	if (tid == 0)
	{
		return;
	}

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (hProcess != NULL)
	{
		WCHAR wsImageName[MAX_PATH + 1];
		DWORD nSize = MAX_PATH;
		if (QueryFullProcessImageNameW(hProcess, NULL, wsImageName, &nSize))
		{
			std::wstring executableName = std::wstring(wsImageName).substr(std::wstring(wsImageName).find_last_of(L"\\") + 1);
			SetVolume(executableName, volumeDelta);
		}
		CloseHandle(hProcess);
	}
}

void HandleSerialInput(const char* szBuffer, CSerial& serial)
{
	static std::string str;
	
	str += szBuffer;
	
	while (str.find('|') != std::string::npos)
	{
		std::string comment = str.substr(0, str.find('|'));
		std::cout << "<" << comment << ">" << '\n';
		str = str.substr(str.find('|') + 1, str.length());
	}

	while (str.find(';') != std::string::npos)
	{
		std::string token = str.substr(0, str.find(';'));
		std::stringstream ss(token);

		int dialId, type;
		ss >> dialId >> type;

		switch (static_cast<DeviceToClientEventType>(type))
		{
			case DeviceToClientEventType::Button:
				std::cout << "btn id:" << dialId << '\n';
				break;
			case DeviceToClientEventType::Dial:
			{
				int dir, cnt;
				ss >> dir >> cnt;
				std::cout << "dial id:" << dialId << " dir:" << dir << " cnt:" << cnt << '\n';

				int deltaCnt = states[dialId].m_Counter - cnt;

				switch (states[dialId].m_targetType)
				{
				case TargetType::Process:
					for (std::wstring processName : states[dialId].m_vecProcessNames)
						SetVolume(processName, deltaCnt * .05f);
					break;
				case TargetType::All:
					SetMasterVolume(deltaCnt * .05f);
					break;
				case TargetType::Focus:
					SetFocusedVolume(deltaCnt * .05f);
				}

				states[dialId].m_Counter = cnt;
				break;
			}
			case DeviceToClientEventType::StartUp:
				WriteColorData(serial);
				break;
		}

		std::cout << token << '\n';
		str = str.substr(str.find(';') + 1, str.length());
	}
}
