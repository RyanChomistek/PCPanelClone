#include "MixerState.h"
#include <string>
#include <iostream>
#include <sstream>

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
				break;
		}

		std::cout << token << '\n';
		str = str.substr(str.find(';') + 1, str.length());
	}
}
