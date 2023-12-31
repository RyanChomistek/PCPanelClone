#include "util.h"
#include "winerror.h"
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <fstream>
#include <iostream>
#include <mmdeviceapi.h>
#include <nlohmann/json.hpp>
#include <shlobj.h>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <tchar.h>
#include <windows.h>

#include "VolumeMixerController.h"

#pragma comment(lib, "shell32.lib")

using namespace nlohmann::literals;

void PrintVolumes()
{
	IMMDevice* pDevice = NULL;
	IMMDeviceEnumerator* pEnumerator = NULL;
	IAudioSessionControl* pSessionControl = NULL;
	IAudioSessionControl2* pSessionControl2 = NULL;
	IAudioSessionManager2* pSessionManager = NULL;

	IfFailThrow(CoInitialize(NULL));

	// Create the device enumerator.
	IfFailThrow(CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pEnumerator));

	// Get the default audio device.
	IfFailThrow(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice));
	IfFailThrow(pDevice->Activate(__uuidof(IAudioSessionManager2),
		CLSCTX_ALL,
		NULL, (void**)&pSessionManager));

	IfFailThrow(pSessionManager->GetAudioSessionControl(0, FALSE, &pSessionControl));

	// Get the extended session control interface pointer.
	IfFailThrow(pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2));

	// Check whether this is a system sound.
	IfFailThrow(pSessionControl2->IsSystemSoundsSession());

	int cbSessionCount = 0;
	LPWSTR pswSession = NULL;

	IAudioSessionEnumerator* pSessionList = NULL;

	IfFailThrow(pSessionManager->GetSessionEnumerator(&pSessionList));
	IfFailThrow(pSessionList->GetCount(&cbSessionCount));
	std::cout << cbSessionCount << std::endl;
	for (int index = 0; index < cbSessionCount; index++)
	{
		IfFailThrow(pSessionList->GetSession(index, &pSessionControl));
		IfFailThrow(pSessionControl->GetDisplayName(&pswSession));
		std::wcout << "Session Name: " << pswSession << std::endl;

		IfFailThrow(pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2));

		DWORD nPID = 0;
		IfFailThrow(pSessionControl2->GetProcessId(&nPID));
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, nPID);
		if (hProcess != NULL)
		{
			WCHAR wsImageName[MAX_PATH + 1];
			DWORD nSize = MAX_PATH;
			if (QueryFullProcessImageNameW(hProcess, NULL, wsImageName, &nSize))
			{
				std::wcout << wsImageName << std::endl;

				ISimpleAudioVolume* pSimpleAudioVolume;
				IfFailThrow(pSessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleAudioVolume));

				float volume = 0;
				pSimpleAudioVolume->GetMasterVolume(&volume);
				std::cout << volume << std::endl;
			}
			CloseHandle(hProcess);
		}
	}
}

/// <summary>
/// offsets the volume of a specific process by volumeDelta amount
/// </summary>
void SetVolume(const std::wstring& processName, float volumeDelta)
{
	IMMDevice* pDevice = NULL;
	IMMDeviceEnumerator* pEnumerator = NULL;
	IAudioSessionControl* pSessionControl = NULL;
	IAudioSessionControl2* pSessionControl2 = NULL;
	IAudioSessionManager2* pSessionManager = NULL;

	IfFailThrow(CoInitialize(NULL))

	// Create the device enumerator.
	IfFailThrow(CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pEnumerator));

	// Get the default audio device.
	IfFailThrow(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice));
	IfFailThrow(pDevice->Activate(__uuidof(IAudioSessionManager2),
		CLSCTX_ALL,
		NULL, (void**)&pSessionManager));

	// get the list of audio sessions (all the processes that are outputing audio)
	IAudioSessionEnumerator* pSessionList = NULL;
	IfFailThrow(pSessionManager->GetSessionEnumerator(&pSessionList));
	int cbSessionCount = 0;
	IfFailThrow(pSessionList->GetCount(&cbSessionCount));

	// loop through all of the sessions and check if any are the process we are looking for
	// NOTE: we don't return early in this loop even if we found the process we are looking for because some processes have more than one session (i.e. discord and teams)
	for (int index = 0; index < cbSessionCount; index++)
	{
		// Get the session from the list
		IfFailThrow(pSessionList->GetSession(index, &pSessionControl));
		IfFailThrow(pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2));

		DWORD nPID = 0;
		IfFailThrow(pSessionControl2->GetProcessId(&nPID));

		// query the process to try to grab its name
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, nPID);
		if (hProcess != NULL)
		{
			WCHAR wsImageName[MAX_PATH + 1];
			DWORD nSize = MAX_PATH;
			if (QueryFullProcessImageNameW(hProcess, NULL, wsImageName, &nSize))
			{
				// find just the executable name
				std::wstring executableName = std::wstring(wsImageName).substr(std::wstring(wsImageName).find_last_of(L"\\")+1);
				if (wcsstr(wsImageName, processName.c_str()) != NULL)
				{
					// if the name matches what we were looking for then get the volume interface
					std::wcout << executableName << " " << processName << std::endl;
					ISimpleAudioVolume* pSimpleAudioVolume;
					IfFailThrow(pSessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleAudioVolume));

					// find the current volume
					float volume = 0;
					pSimpleAudioVolume->GetMasterVolume(&volume);
					std::cout << volume << " " << volumeDelta << std::endl;

					// offset it be the delta
					volume += volumeDelta;

					// make sure we don't set it greater than 1 (if it is > 1 then windows will toss the input)
					float newVolume = max(min(volume, 1), 0);

					// Set the volume
					pSimpleAudioVolume->SetMasterVolume(newVolume, NULL);
				}
			}
			CloseHandle(hProcess);
		}
	}
}

/// <summary>
/// offsets the master volume by volumeDelta amount
/// </summary>
void SetMasterVolume(float volumeDelta)
{
	IfFailThrow(CoInitialize(NULL));

	// get the device enumerator
	IMMDeviceEnumerator* deviceEnumerator = NULL;
	IfFailThrow(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator));
	
	// grab the default device
	IMMDevice* defaultDevice = NULL;
	IfFailThrow(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice));
	deviceEnumerator->Release();
	deviceEnumerator = NULL;

	// get the audio endpoint interface from the device
	IAudioEndpointVolume* endpointVolume = NULL;
	IfFailThrow(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume));
	defaultDevice->Release();
	defaultDevice = NULL;

	// get the master volume
	float volume = 0;
	endpointVolume->GetMasterVolumeLevelScalar(&volume);
	std::cout << volume << " " << volumeDelta << std::endl;
	volume += volumeDelta;

	// set the volume to an offset from the current value
	float newVolume = max(min(volume, 1), 0);
	endpointVolume->SetMasterVolumeLevelScalar(newVolume, NULL);
}

/// <summary>
/// offsets the focused window volume by volumeDelta amount
/// </summary>
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

VolumeMixerController::VolumeMixerController()
{
	// read the settings file and initialize our state to that
	WCHAR my_documents[MAX_PATH];
	IfFailThrow(SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, my_documents));

	nlohmann::json settings;
	std::wstring filepath = std::wstring(my_documents) + L"\\MixerSettings.json";
	std::fstream f(filepath);

	// check if the file exists
	if (!f.good())
	{
		// if it doesn't set a default state
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

	// populate the dial settings from the settings
	int iDial = 0;
	for (auto& dialSetting : settings["dials"])
	{
		DialState& state = states[iDial];
		state.m_targetType = dialSetting["Target"].get<TargetType>();

		switch (state.m_targetType)
		{
			case TargetType::Process:
			{
				// for process target we also need to grab the names of the processes we are targeting
				std::vector<std::wstring> processNames;
				for (auto& processSetting : dialSetting["Processes"])
				{
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

void VolumeMixerController::ReadInput()
{
	// check for a comment (delineated by "|")
	while (m_currentReadBuffer.find('|') != std::string::npos)
	{
		std::string comment = m_currentReadBuffer.substr(0, m_currentReadBuffer.find('|'));
		std::cout << "<" << comment << ">" << '\n';
		m_currentReadBuffer = m_currentReadBuffer.substr(m_currentReadBuffer.find('|') + 1, m_currentReadBuffer.length());
	}

	// check for a command (delineated by "\n")
	while (m_currentReadBuffer.find('\n') != std::string::npos)
	{
		std::string token = m_currentReadBuffer.substr(0, m_currentReadBuffer.find('\n'));
		std::stringstream ss(token);

		int dialId, type;
		ss >> dialId >> type;

		switch (static_cast<DeviceToClientEventType>(type))
		{
			case DeviceToClientEventType::StartUp:
				WriteColorData();
				break;
			case DeviceToClientEventType::Button:
				std::cout << "btn id:" << dialId << '\n';
				break;
			case DeviceToClientEventType::Dial:
			{
				int dir, cnt;
				ss >> dir >> cnt;
				std::cout << "dial id:" << dialId << " dir:" << dir << " cnt:" << cnt << '\n';

				int deltaCnt = cnt - states[dialId].m_Counter;

				switch (states[dialId].m_targetType)
				{
				case TargetType::Process:
					for (std::wstring processName : states[dialId].m_vecProcessNames)
						SetVolume(processName, deltaCnt * singleTickRotationAmount);
					break;
				case TargetType::All:
					SetMasterVolume(deltaCnt * singleTickRotationAmount);
					break;
				case TargetType::Focus:
					SetFocusedVolume(deltaCnt * singleTickRotationAmount);
				}

				states[dialId].m_Counter = cnt;
				break;
			}
		}

		std::cout << token << '\n';
		m_currentReadBuffer = m_currentReadBuffer.substr(m_currentReadBuffer.find('\n') + 1, m_currentReadBuffer.length());
	}
}

void VolumeMixerController::WriteColorData()
{
	std::stringstream ss;

	for (int iState = 0; iState < numDials; iState++)
	{
		const DialState& state = states[iState];
		ss << (int)ClientToDeviceEventType::Color << " " << iState << " " << state.r << " " << state.g << " " << state.b << "\n";
	}

	std::cout << ss.str() << "\n";
	m_serial.Write(ss.str().c_str(), ss.str().size());
}

void VolumeMixerController::OnConnected()
{
	WriteColorData();
}