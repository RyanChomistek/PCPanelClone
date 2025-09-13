#include "util.h"
#include "VolumeMixerController.h"
#include <tchar.h>
#include <vector>
#include <iostream>
#include <unordered_map>

#include "HidReader.h"

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")


int main()
{
	VolumeMixerController reader;
	while (true) {
		reader.HrReadLoop();
		Sleep(1000);
	}

	return NOERROR;
}
