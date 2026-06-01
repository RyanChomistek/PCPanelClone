#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <hidapi/hidapi.h>

// Mirrors the Windows HID caps structures, but with plain C++ types.
struct HidValueCaps {
    uint16_t usagePage;
    uint16_t usage;
    int32_t  logicalMin;
    int32_t  logicalMax;
    uint32_t reportSize;  // bits per field
    int      bitOffset;   // bit offset within the report payload (after the report-ID byte)
    bool     isRelative;
    uint8_t  reportID;
};

struct HidButtonCaps {
    uint16_t usagePage;
    uint16_t usage;
    int      bitOffset;
    uint8_t  reportID;
};

struct HidValue {
    HidValueCaps caps;
    int64_t      value = 0;
    int          typeIndex = 0;
};

struct HidButton {
    HidButtonCaps caps;
    bool          value = false;
    int           typeIndex = 0;
};

class HidReader {
public:
    HidReader();
    ~HidReader();

    // Enumerate all HID devices; opens the first one with usagePage=0x01, usage=0x37
    // and enters the read loop.  Returns 0 on clean exit, negative on error.
    int HrReadLoop();

    virtual void ReadDial(int iDial, int64_t value) = 0;
    virtual void ReadButton(int iButton, bool value) = 0;
    virtual void OnSync() = 0;

    void WriteHidOut(std::vector<uint8_t>& outReport);

protected:
    // Keyed by DataIndex (assigned sequentially during descriptor parsing)
    std::unordered_map<int, HidValue>  hidValues;
    std::unordered_map<int, HidButton> hidButtons;

    int      outputReportLen = 0;
    int      inputReportLen  = 0;

private:
    hid_device* dev = nullptr;

    // Tries to open a device at |path|; returns true if we should stop enumerating
    // (either we found and handled the device, or an unrecoverable error occurred).
    bool tryDevice(const char* path, uint16_t usagePage, uint16_t usage);

    // Parse the raw HID report descriptor and populate hidValues / hidButtons.
    void buildInputIndex(const uint8_t* desc, int descLen);

    // Decode one input report and fire ReadDial / ReadButton.
    void decodeReport(const uint8_t* report, int len);
};
