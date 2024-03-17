#pragma once

#include "Serial.h"
#include "winerror.h"
#include <string>

class StandardSerialReader
{
public:
	static std::vector<std::wstring> ScanForAvailablePorts();

	/// <summary>
	/// configures the serial connection
	/// </summary>
	/// <param name="lpszDevice">what com port to connect to</param>
	HRESULT HrConfigure(LPCTSTR lpszDevice);

	/// <summary>
	/// contains a loop that will continuously check for new data from the serial port
	/// </summary>
	HRESULT HrReadLoop();

protected:
	/// <summary>
	/// called every time the client has read new data from the device
	/// </summary>
	virtual void ReadInput() = 0;

	/// <summary>
	/// called once the device is connected to the serial stream
	/// </summary>
	virtual void OnConnected() {};

protected:
	CSerial m_serial;
	std::string m_currentReadBuffer;
	static constexpr int c_serialReadBufferSize = 101;
	LONG    lLastError = ERROR_SUCCESS;
	bool fDisconnect = false; // set to true to tell the connection to disconnect at the next opportunity
};