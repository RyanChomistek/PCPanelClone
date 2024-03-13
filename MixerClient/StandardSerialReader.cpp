#include <windows.h>
#include <tchar.h>
#include <mutex>

#include "StandardSerialReader.h"

enum { EOF_Char = 27 };

int ShowError(LONG lError, LPCTSTR lptszMessage)
{
	// Generate a message text
	TCHAR tszMessage[256];
	wsprintf(tszMessage, _T("%s\n(error code %d)"), lptszMessage, lError);

	// Display message-box and return with an error-code
	::MessageBox(0, tszMessage, _T("Listener"), MB_ICONSTOP | MB_OK);
	return 1;
}

HRESULT StandardSerialReader::HrConfigure(LPCTSTR lpszDevice)
{
	int cMaxConnectionAttempts = 20;
	for (int iConnectionAttempt = 0; iConnectionAttempt < cMaxConnectionAttempts; ++iConnectionAttempt)
	{
		// Attempt to open the serial port (COM1)
		lLastError = m_serial.Open(lpszDevice, 0, 0, false);
		if (lLastError == ERROR_SUCCESS)
			break;

		Sleep(100);
	}

	if (lLastError != ERROR_SUCCESS)
		return ::ShowError(m_serial.GetLastError(), _T("Unable to open COM-port"));

	// Setup the serial port (9600,8N1, which is the default setting)
	lLastError = m_serial.Setup(CSerial::EBaud115200, CSerial::EData8, CSerial::EParNone, CSerial::EStop1);

	if (lLastError != ERROR_SUCCESS)
		return ::ShowError(m_serial.GetLastError(), _T("Unable to set COM-port setting"));

	// Register only for the receive event
	lLastError = m_serial.SetMask(CSerial::EEventBreak |
		CSerial::EEventCTS |
		CSerial::EEventDSR |
		CSerial::EEventError |
		CSerial::EEventRing |
		CSerial::EEventRLSD |
		CSerial::EEventRecv);
	if (lLastError != ERROR_SUCCESS)
		return ::ShowError(m_serial.GetLastError(), _T("Unable to set COM-port event mask"));

	// Use 'non-blocking' reads, because we don't know how many bytes
	// will be received. This is normally the most convenient mode
	// (and also the default mode for reading data).
	lLastError = m_serial.SetupReadTimeouts(CSerial::EReadTimeoutNonblocking);
	if (lLastError != ERROR_SUCCESS)
		return ::ShowError(m_serial.GetLastError(), _T("Unable to set COM-port read timeout."));
}

HRESULT StandardSerialReader::HrReadLoop()
{
	std::once_flag fOnConnected;

	// Keep reading data, until an EOF (CTRL-Z) has been received
	bool fContinue = true;
	do
	{
		// Wait for an event
		lLastError = m_serial.WaitEvent();
		if (lLastError != ERROR_SUCCESS)
			return ::ShowError(m_serial.GetLastError(), _T("Unable to wait for a COM-port event."));

		std::call_once(fOnConnected, [this]() {
				OnConnected();
			});

		// Save event
		const CSerial::EEvent eEvent = m_serial.GetEventType();

		// Handle break event
		if (eEvent & CSerial::EEventBreak)
		{
			printf("\n### BREAK received ###\n");
		}

		// Handle CTS event
		if (eEvent & CSerial::EEventCTS)
		{
			printf("\n### Clear to send %s ###\n", m_serial.GetCTS() ? "on" : "off");
		}

		// Handle DSR event
		if (eEvent & CSerial::EEventDSR)
		{
			printf("\n### Data set ready %s ###\n", m_serial.GetDSR() ? "on" : "off");
		}

		// Handle error event
		if (eEvent & CSerial::EEventError)
		{
			printf("\n### ERROR: ");
			switch (m_serial.GetError())
			{
			case CSerial::EErrorBreak:		printf("Break condition");			break;
			case CSerial::EErrorFrame:		printf("Framing error");			break;
			case CSerial::EErrorIOE:		printf("IO device error");			break;
			case CSerial::EErrorMode:		printf("Unsupported mode");			break;
			case CSerial::EErrorOverrun:	printf("Buffer overrun");			break;
			case CSerial::EErrorRxOver:		printf("Input buffer overflow");	break;
			case CSerial::EErrorParity:		printf("Input parity error");		break;
			case CSerial::EErrorTxFull:		printf("Output buffer full");		break;
			default:						printf("Unknown");					break;
			}
			printf(" ###\n");
		}

		// Handle ring event
		if (eEvent & CSerial::EEventRing)
		{
			printf("\n### RING ###\n");
		}

		// Handle RLSD/CD event
		if (eEvent & CSerial::EEventRLSD)
		{
			printf("\n### RLSD/CD %s ###\n", m_serial.GetRLSD() ? "on" : "off");
		}

		// Handle data receive event
		if (eEvent & CSerial::EEventRecv)
		{
			// Read data, until there is nothing left
			DWORD dwBytesRead = 0;
			char szBuffer[101];
			do
			{
				// Read data from the COM-port
				lLastError = m_serial.Read(szBuffer, sizeof(szBuffer) - 1, &dwBytesRead);
				if (lLastError != ERROR_SUCCESS)
					return ::ShowError(m_serial.GetLastError(), _T("Unable to read from COM-port."));

				if (dwBytesRead > 0)
				{
					// Finalize the data, so it is a valid string
					szBuffer[dwBytesRead] = '\0';

					m_currentReadBuffer += szBuffer;

					// Display the data
					ReadInput();

					// Check if EOF (CTRL+'[') has been specified
					if (strchr(szBuffer, EOF_Char))
						fContinue = false;
				}
			} while (dwBytesRead == sizeof(szBuffer) - 1);
		}
	} while (fContinue && !fDisconnect);

	// Close the port again
	m_serial.Close();
}