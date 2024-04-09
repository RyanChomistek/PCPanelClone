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
#include <mutex>
#include <algorithm>
#include "scope_guard.h"

#include "VolumeMixerController.h"

#pragma comment(lib, "shell32.lib")

#undef max
#undef min

static constexpr bool s_fEnableLogging = true;

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
	
	if (s_fEnableLogging)
		std::cout << cbSessionCount << std::endl;
	
	for (int index = 0; index < cbSessionCount; index++)
	{
		IfFailThrow(pSessionList->GetSession(index, &pSessionControl));
		IfFailThrow(pSessionControl->GetDisplayName(&pswSession));

		if (s_fEnableLogging)
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
				if (s_fEnableLogging)
					std::wcout << wsImageName << std::endl;

				ISimpleAudioVolume* pSimpleAudioVolume;
				IfFailThrow(pSessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleAudioVolume));

				float volume = 0;
				pSimpleAudioVolume->GetMasterVolume(&volume);
				if (s_fEnableLogging)
					std::cout << volume << std::endl;
			}
			CloseHandle(hProcess);
		}
	}
}

template <class T, class TReleaser>
class Holder
{
public:
	Holder(T p)
		: m_p(p)
	{}

	Holder& operator= (const T& other)
	{
		m_p = other;
		return *this;
	}

	Holder(std::nullptr_t)
		: m_p(nullptr)
	{}

	Holder() = default;

	Holder(Holder&& sp)
		: m_p(sp.m_p)
	{
		sp.m_p = nullptr;
	}

	Holder& operator= (Holder&& other)
	{
		m_p = other.m_p;
		other.m_p = nullptr;
		return *this;
	}

	virtual ~Holder()
	{
		Release();
	}

	void Release()
	{
		TReleaser::Release(m_p);
	}

	T operator->() const
	{
		return m_p;
	}

	T& operator*() const
	{
		return *m_p;
	}

	T* operator&()
	{
		return &m_p;
	}

	bool operator== (Holder other)
	{
		return m_p == other.m_p;
	}

	bool operator!= (Holder other)
	{
		return m_p != other.m_p;
	}

	bool FEmpty() { return m_p == nullptr; }

	T m_p = nullptr;
};

template<class T>
struct IUnknownReleaser
{
	static void Release(T*& p)
	{
		if ((p) != nullptr)
		{
			(p)->Release();
			p = nullptr;
		}
	}
};

struct HANDLEReleaser
{
	static void Release(HANDLE& p)
	{
		if ((p) != nullptr)
		{
			CloseHandle(p);
			p = nullptr;
		}
	}
};

using HandleHolder = Holder<HANDLE, HANDLEReleaser>;

struct AudioSessions
{
	Holder<IMMDevice*, IUnknownReleaser<IMMDevice>> m_pDevice;
	Holder<IMMDeviceEnumerator*, IUnknownReleaser<IMMDeviceEnumerator>> m_pEnumerator;
	Holder<IAudioSessionManager2*, IUnknownReleaser<IAudioSessionManager2>> m_pSessionManager;
	Holder<IAudioSessionEnumerator*, IUnknownReleaser<IAudioSessionEnumerator>> m_pSessionList;
	int m_cSessions = 0;

	AudioSessions()
	{
		// get the list of audio sessions (all the processes that are outputing audio)
		IfFailThrow(CoInitialize(NULL))

			// Create the device enumerator.
			IfFailThrow(CoCreateInstance(
				__uuidof(MMDeviceEnumerator),
				NULL, CLSCTX_ALL,
				__uuidof(IMMDeviceEnumerator),
				(void**)&m_pEnumerator));

		// Get the default audio device.
		IfFailThrow(m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_pDevice));
		IfFailThrow(m_pDevice->Activate(__uuidof(IAudioSessionManager2),
			CLSCTX_ALL,
			NULL, (void**)&m_pSessionManager));

		IfFailThrow(m_pSessionManager->GetSessionEnumerator(&m_pSessionList));
		IfFailThrow(m_pSessionList->GetCount(&m_cSessions));
	}

	struct AudioSessionIterator
	{
		AudioSessionIterator(AudioSessions& audioSessions)
			: m_audioSessions(audioSessions)
		{
			GetCurrentSessionInfo();
		}

		AudioSessionIterator(AudioSessions& audioSessions, int iSession)
			: m_audioSessions(audioSessions), m_iSession(iSession)
		{
			GetCurrentSessionInfo();
		}

		void GetCurrentSessionInfo()
		{
			if (0 > m_iSession || m_iSession >= m_audioSessions.m_cSessions)
			{
				return;
			}

			clear();

			while (m_hProcess == nullptr)
			{
				if (0 > m_iSession || m_iSession >= m_audioSessions.m_cSessions)
				{
					return;
				}

				// Get the session from the list
				IfFailThrow(m_audioSessions.m_pSessionList->GetSession(m_iSession, &m_pSessionControl));
				IfFailThrow(m_pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&m_pSessionControl2));
				m_hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetProcessID());

				if (m_hProcess == nullptr)
				{
					clear();
					++m_iSession;
				}
			}

		}

		AudioSessionIterator& operator++()
		{
			++m_iSession;

			GetCurrentSessionInfo();

			if (m_iSession >= m_audioSessions.m_cSessions)
			{
				return *this;
			}

			return *this;
		}

		Holder<ISimpleAudioVolume*, IUnknownReleaser<ISimpleAudioVolume>> GetAudioVolume()
		{
			Holder<ISimpleAudioVolume*, IUnknownReleaser<ISimpleAudioVolume>> pSimpleAudioVolume;
			IfFailThrow(m_pSessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleAudioVolume));
			return std::move(pSimpleAudioVolume);
		}

		DWORD GetProcessID()
		{
			DWORD nPID = 0;
			IfFailThrow(m_pSessionControl2->GetProcessId(&nPID));
			return nPID;
		}

		bool HasProcess() { return !m_hProcess.FEmpty(); }

		const WCHAR* GetProcessPath()
		{
			if (!HasProcess())
			{
				wsImageName[0] = '\0';
				return wsImageName;
			}

			nSize = bufferSize;
			if (!QueryFullProcessImageNameW(m_hProcess.m_p, NULL, wsImageName, &nSize))
			{
				DWORD error = GetLastError();
				wsImageName[0] = '\0';
				return wsImageName;
			}

			return wsImageName;
		}


		bool operator==(const AudioSessionIterator& other) const
		{
			return m_iSession == other.m_iSession;
		}

		AudioSessionIterator SeekToEnd()
		{
			return { m_audioSessions, m_audioSessions.m_cSessions };
		}

		int m_iSession = 0;
		AudioSessions& m_audioSessions;

		Holder<IAudioSessionControl*, IUnknownReleaser<IAudioSessionControl>> m_pSessionControl;
		Holder<IAudioSessionControl2*, IUnknownReleaser<IAudioSessionControl2>> m_pSessionControl2;
		HandleHolder m_hProcess;

		
		DWORD nSize = 0;
		static constexpr DWORD bufferSize = MAX_PATH;
		WCHAR wsImageName[bufferSize + 1];

		private:
			void clear()
			{
				m_pSessionControl.Release();
				m_pSessionControl2.Release();
				m_hProcess.Release();
				wsImageName[0] = '\0';
			}
	};

	AudioSessionIterator begin()
	{
		return AudioSessionIterator(*this);
	}

	AudioSessionIterator end()
	{
		return AudioSessionIterator(*this, m_cSessions);
	}
};

/// <summary>
/// offsets the volume of a specific process by volumeDelta amount
/// </summary>
float VolumeMixerController::SetVolume(const std::wstring& processName, float volumeDelta)
{
	float newVolume = -1;
	bool foundProcess = false;
	AudioSessions sessions;
	auto iter = sessions.begin();

	// loop through all of the sessions and check if any are the process we are looking for
	// NOTE: we don't return early in this loop even if we found the process we are looking for because some processes have more than one session (i.e. discord and teams)
	for(; iter != sessions.end(); ++iter)
	{
		const WCHAR* szProcessPath = iter.GetProcessPath();
		if (!iter.HasProcess() || wcsstr(szProcessPath, processName.c_str()) == nullptr)
		{
			continue;
		}

		foundProcess = true;

		// if the name matches what we were looking for then get the volume interface
		if (s_fEnableLogging)
		{
			std::wstring executable;
			if (iter.GetProcessPath() != nullptr)
			{
				executable = iter.GetProcessPath();
			}

			std::wcout << executable << " " << processName << std::endl;
		}

		// find the current volume
		float volume = 0;

		Holder<ISimpleAudioVolume*, IUnknownReleaser<ISimpleAudioVolume>> audioVolume = std::move(iter.GetAudioVolume());
		audioVolume->GetMasterVolume(&volume);

		if (s_fEnableLogging)
			std::cout << volume << " " << volumeDelta << std::endl;

		// offset it be the delta
		volume += volumeDelta;

		// make sure we don't set it greater than 1 (if it is > 1 then windows will toss the input)
		newVolume = std::max(std::min(volume, 1.0f), 0.0f);

		// Set the volume
		audioVolume->SetMasterVolume(newVolume, NULL);
	}

	return newVolume;
}

/// <summary>
/// offsets the master volume by volumeDelta amount
/// </summary>
float VolumeMixerController::SetMasterVolume(float volumeDelta)
{
	IfFailThrow(CoInitialize(NULL));

	// get the device enumerator
	Holder<IMMDeviceEnumerator*, IUnknownReleaser<IMMDeviceEnumerator>> deviceEnumerator;
	IfFailThrow(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator));
	
	// grab the default device
	Holder<IMMDevice*, IUnknownReleaser<IMMDevice>> defaultDevice;
	IfFailThrow(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice));

	// get the audio endpoint interface from the device
	Holder<IAudioEndpointVolume*, IUnknownReleaser<IAudioEndpointVolume>> endpointVolume;
	IfFailThrow(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume));

	// get the master volume
	float volume = 0;
	endpointVolume->GetMasterVolumeLevelScalar(&volume);
	if (s_fEnableLogging)
		std::cout << volume << " " << volumeDelta << std::endl;
	volume += volumeDelta;

	// set the volume to an offset from the current value
	float newVolume = std::max(std::min(volume, 1.0f), 0.0f);
	endpointVolume->SetMasterVolumeLevelScalar(newVolume, NULL);

	return newVolume;
}

/// <summary>
/// offsets the focused window volume by volumeDelta amount
/// </summary>
float VolumeMixerController::SetFocusedVolume(float volumeDelta)
{
	float newVolume = -1;
	HWND wndFocused = GetForegroundWindow();

	DWORD pid = NULL;
	DWORD tid = GetWindowThreadProcessId(wndFocused, &pid);

	if (tid == 0)
	{
		return -1;
	}

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (hProcess != NULL)
	{
		WCHAR wsImageName[MAX_PATH + 1];
		DWORD nSize = MAX_PATH;
		if (QueryFullProcessImageNameW(hProcess, NULL, wsImageName, &nSize))
		{
			std::wstring executableName = std::wstring(wsImageName).substr(std::wstring(wsImageName).find_last_of(L"\\") + 1);
			newVolume = SetVolume(executableName, volumeDelta);
		}
		CloseHandle(hProcess);
	}

	return newVolume;
}

bool VolumeMixerController::ToggleFocusedMute()
{
	bool fMute = false;
	HWND wndFocused = GetForegroundWindow();

	DWORD pid = NULL;
	DWORD tid = GetWindowThreadProcessId(wndFocused, &pid);

	if (tid == 0)
	{
		return false;
	}

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (hProcess != NULL)
	{
		WCHAR wsImageName[MAX_PATH + 1];
		DWORD nSize = MAX_PATH;
		if (QueryFullProcessImageNameW(hProcess, NULL, wsImageName, &nSize))
		{
			std::wstring executableName = std::wstring(wsImageName).substr(std::wstring(wsImageName).find_last_of(L"\\") + 1);
			fMute = ToggleMute(executableName, std::nullopt);
		}
		CloseHandle(hProcess);
	}

	return fMute;
}

bool VolumeMixerController::ToggleMute(const std::wstring& processName, std::optional<bool> optfMute)
{
	bool foundProcess = false;
	BOOL selectedMuteSetting = optfMute.value_or(false);
	AudioSessions sessions;
	auto iter = sessions.begin();
	for (; iter != sessions.end(); ++iter)
	{
		if (!iter.HasProcess() || wcsstr(iter.GetProcessPath(), processName.c_str()) == nullptr)
		{
			continue;
		}

		Holder<ISimpleAudioVolume*, IUnknownReleaser<ISimpleAudioVolume>> audioVolume = std::move(iter.GetAudioVolume());

		if (!optfMute.has_value())
		{
			if (!foundProcess)
			{
				BOOL fMute = false;
				IfFailThrow(audioVolume->GetMute(&fMute));
				selectedMuteSetting = !fMute;
			}

			IfFailThrow(audioVolume->SetMute(selectedMuteSetting, nullptr));
		}
		else
		{
			IfFailThrow(audioVolume->SetMute(selectedMuteSetting, nullptr));
		}

		foundProcess = true;
	}

	return selectedMuteSetting;
}

bool VolumeMixerController::ToggleMasterMute()
{
	IfFailThrow(CoInitialize(NULL));

	// get the device enumerator
	Holder<IMMDeviceEnumerator*, IUnknownReleaser<IMMDeviceEnumerator>> deviceEnumerator;
	IfFailThrow(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator));

	// grab the default device
	Holder<IMMDevice*, IUnknownReleaser<IMMDevice>> defaultDevice;
	IfFailThrow(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice));

	// get the audio endpoint interface from the device
	Holder<IAudioEndpointVolume*, IUnknownReleaser<IAudioEndpointVolume>> endpointVolume;
	IfFailThrow(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume));

	BOOL fMute = false;
	IfFailThrow(endpointVolume->GetMute(&fMute));
	IfFailThrow(endpointVolume->SetMute(!fMute, nullptr));

	return !fMute;
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
						"Color": [255,0,0]
					},
					{
						"Target":1,
						"Color": [0,255,0]
					},
					{
						"Target":0,
						"Processes":["firefox.exe", "msedge.exe", "chrome.exe"],
						"Color": [0,0,255]
					},
					{
						"Target":0,
						"Processes":["Discord.exe", "teams.exe"],
						"Color": [128,0,128]
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
		if (s_fEnableLogging)
			std::cout << "<" << comment << ">" << '\n';
		m_currentReadBuffer = m_currentReadBuffer.substr(m_currentReadBuffer.find('|') + 1, m_currentReadBuffer.length());
	}

	// check for a command (delineated by "\n")
	while (m_currentReadBuffer.find('\n') != std::string::npos)
	{
		std::string token = m_currentReadBuffer.substr(0, m_currentReadBuffer.find('\n'));
		std::stringstream ss(token);

		int iType;
		ss >> iType;

		DeviceToClientEventType type = static_cast<DeviceToClientEventType>(iType);

		std::call_once(fFirstMessage, [this, &type]() {
				if (type != DeviceToClientEventType::StartUp)
				{
					fDisconnect = true;
				}
			});

		switch (type)
		{
			case DeviceToClientEventType::StartUp:
				if (s_fEnableLogging)
					std::cout << "Start up" << '\n';
				WriteColorData();
				break;
			case DeviceToClientEventType::Button:
			{
				int dialId;
				ss >> dialId;

				if (s_fEnableLogging)
					std::cout << "btn id:" << dialId << '\n';

				switch (states[dialId].m_targetType)
				{
					case TargetType::Process:
					{
						std::optional<bool> optfMuteState;
						for (std::wstring processName : states[dialId].m_vecProcessNames)
						{
							optfMuteState = ToggleMute(processName, optfMuteState);
						}

						states[dialId].fMute = optfMuteState.value_or(false);

						break;
					}
					case TargetType::All:
						states[dialId].fMute = ToggleMasterMute();
						break;
					case TargetType::Focus:
						states[dialId].fMute = ToggleFocusedMute();
						break;
				}
				
				WriteColorData();

				break;
			}
			case DeviceToClientEventType::Dial:
			{
				int dialId, dir, cnt;
				ss >> dialId >> dir >> cnt;

				if (s_fEnableLogging)
					std::cout << "dial id:" << dialId << " dir:" << dir << " cnt:" << cnt << '\n';

				int deltaCnt = cnt - states[dialId].m_Counter;
				float newVolume = -1;
				switch (states[dialId].m_targetType)
				{
					case TargetType::Process:
						for (std::wstring processName : states[dialId].m_vecProcessNames)
						{
							newVolume = std::max(newVolume, SetVolume(processName, deltaCnt * singleTickRotationAmount));
						}
							
						break;
					case TargetType::All:
						newVolume = SetMasterVolume(deltaCnt * singleTickRotationAmount);
						break;
					case TargetType::Focus:
						newVolume = SetFocusedVolume(deltaCnt * singleTickRotationAmount);
				}

				states[dialId].m_Counter = cnt;
				if(newVolume >= 0)
					FlashEncoderVolumeToLeds(states[dialId], newVolume);
				break;
			}
			case DeviceToClientEventType::HeartBeat:
			{
				// NOP
				std::stringstream ssOut;
				ssOut << (int)ClientToDeviceEventType::HeartBeat;
				m_serial.Write(ssOut.str().c_str(), ssOut.str().size());

				time_t now;
				time(&now);
				if (encoderFlashingStart.has_value() && difftime(now, *encoderFlashingStart) > 1)
				{
					WriteColorData();
					encoderFlashingStart = std::nullopt;
				}

				break;
			}
		}

		m_currentReadBuffer = m_currentReadBuffer.substr(m_currentReadBuffer.find('\n') + 1, m_currentReadBuffer.length());

		if (s_fEnableLogging)
			std::cout << token << '\n';
	}
}

void ScaleColor(int& r, int& g, int& b, float scale)
{
	r = (int)(r * scale);
	g = (int)(g * scale);
	b = (int)(b * scale);
}

void VolumeMixerController::FlashEncoderVolumeToLeds(const DialState& state, float volume)
{
	std::stringstream ss;
	volume *= numDials;
	for (int iLed = 0; iLed < numDials; iLed++)
	{
		if (volume >= 1)
		{
			ss << (int)ClientToDeviceEventType::Color << " " << iLed << " " << state.r << " " << state.g << " " << state.b << "\n";
		}
		else if (volume < 0)
		{
			ss << (int)ClientToDeviceEventType::Color << " " << iLed << " " << 0 << " " << 0 << " " << 0 << "\n";
		}
		else
		{
			int r = (int)(state.r * volume);
			int g = (int)(state.g * volume);
			int b = (int)(state.b * volume);
			ss << (int)ClientToDeviceEventType::Color << " " << iLed << " " << r << " " << g << " " << b << "\n";
		}

		volume -= 1;
	}

	if (s_fEnableLogging)
		std::cout << ss.str() << "\n";
	m_serial.Write(ss.str().c_str(), ss.str().size());
	std::time_t now;
	encoderFlashingStart = time(&now);
}

void VolumeMixerController::WriteColorData()
{
	std::stringstream ss;

	for (int iState = 0; iState < numDials; iState++)
	{
		const DialState& state = states[iState];

		if(!state.fMute)
			ss << (int)ClientToDeviceEventType::Color << " " << iState << " " << state.r << " " << state.g << " " << state.b << "\n";
		else
		{
			int r = state.r, g = state.g, b = state.b;
			ScaleColor(r, g, b, 0);
			ss << (int)ClientToDeviceEventType::Color << " " << iState << " " << r << " " << g << " " << b << "\n";
		}
			
	}

	if (s_fEnableLogging)
		std::cout << ss.str() << "\n";
	m_serial.Write(ss.str().c_str(), ss.str().size());
}

void VolumeMixerController::OnConnected()
{
	WriteColorData();
}