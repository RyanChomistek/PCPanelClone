#pragma once
#include <stdexcept>

#define IfFailRet(expr) do { auto _r = (expr); if (_r < 0) return _r; } while(0)
#define IfFailThrow(expr) do { if ((expr) < 0) throw std::runtime_error(#expr " failed"); } while(0)
