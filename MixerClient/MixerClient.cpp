#include "util.h"
#include "VolumeMixerController.h"
#include <tchar.h>
#include <vector>
#include <iostream>

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

int main()
{
	// try to find the right serial port that has the mixer
	while (true)
	{
		std::vector<std::wstring> availablePorts = StandardSerialReader::ScanForAvailablePorts();
		for (std::wstring port : availablePorts)
		{
			VolumeMixerController reader;
			if (FAILED(reader.HrConfigure(port.c_str())))
			{
				continue;
			}

			std::wcout << L"connecting to " << port << '\n';

			// if we disconnect in hrReadLoop it might just be that the PC went to sleep or we otherwise dropped the connection
			// keep retrying to connect
			if (FAILED(reader.HrReadLoop()))
			{
				continue;
			}
		}

		// we didn't find the mixer, wait a second and try again
		// TODO maybe there is a way to wait until a new serial connection is available
		Sleep(1000);
	};

	return NOERROR;
}
