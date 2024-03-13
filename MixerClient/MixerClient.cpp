#include "util.h"
#include "VolumeMixerController.h"
#include <tchar.h>

int main()
{
	// TODO make N threads, try to connect to each com port, if the first response is not the hand shake exit
	VolumeMixerController reader;
	IfFailRet(reader.HrConfigure(_T("COM5")));
	IfFailRet(reader.HrReadLoop());
	return NOERROR;
}
