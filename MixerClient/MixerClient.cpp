#include "util.h"
#include "VolumeMixerController.h"
#include <tchar.h>

int main()
{
	VolumeMixerController reader;
	IfFailRet(reader.HrConfigure(_T("COM5")));
	IfFailRet(reader.HrReadLoop());
	return NOERROR;
}
