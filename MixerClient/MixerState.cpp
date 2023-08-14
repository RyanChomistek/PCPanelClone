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

void InitMixerState()
{
	states[3].m_processName = L"firefox.exe";


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

void HandleSerialInput(const char* szBuffer)
{
	static std::string str;
	
	str += szBuffer;
	
	while (str.find(';') != std::string::npos)
	{
		std::string token = str.substr(0, str.find(';'));
		std::stringstream ss(token);

		int dialId, type;
		ss >> dialId >> type;

		switch (static_cast<EventType>(type))
		{
			case EventType::Button:
				std::cout << "btn id:" << dialId << '\n';
				break;
			case EventType::Dial:
				int dir, cnt;
				ss >> dir >> cnt;
				std::cout << "dial id:" << dialId << " dir:" << dir << " cnt:" << cnt << '\n';

				int deltaCnt = states[dialId].m_Counter - cnt;

				SetVolume(states[dialId].m_processName, deltaCnt*.05f);

				states[dialId].m_Counter = cnt;
				break;
		}

		std::cout << token << '\n';
		str = str.substr(str.find(';') + 1, str.length());
	}
}
