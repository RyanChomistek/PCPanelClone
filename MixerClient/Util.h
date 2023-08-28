#pragma once
#define IfFailRet(hr) if(FAILED((HRESULT)hr)) return hr;
#define IfFailThrow(hr) if(FAILED((HRESULT)hr)) throw hr;