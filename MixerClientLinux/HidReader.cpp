#include "HidReader.h"

#include <hidapi/hidapi.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include "Util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

// ---------------------------------------------------------------------------
// HID descriptor parser
//
// Produces a flat list of input fields.  Each Variable input item yields one
// field per reportCount entry; each Array input item yields one field per
// usage in the usage range (button semantics).
// ---------------------------------------------------------------------------

struct InputField {
    uint16_t usagePage;
    uint16_t usage;
    int32_t  logicalMin;
    int32_t  logicalMax;
    uint32_t reportSize;   // bits
    int      bitOffset;    // bit offset in payload (excludes report-ID byte)
    bool     isRelative;
    bool     isButton;
    int      dataIndex;
    uint8_t  reportID;
};

static int32_t signExtend(uint32_t v, int bits)
{
    if (bits == 0 || bits >= 32) return (int32_t)v;
    uint32_t mask = 1u << (bits - 1);
    return (int32_t)((v ^ mask) - mask);
}

static std::vector<InputField> parseDescriptor(const uint8_t* desc, int len,
                                               int& outInputBytes,
                                               int& outOutputBytes)
{
    std::vector<InputField> fields;

    // Global state
    uint32_t usagePage   = 0;
    int32_t  logicalMin  = 0;
    int32_t  logicalMax  = 0;
    uint32_t reportSize  = 0;
    uint32_t reportCount = 0;
    uint32_t reportID    = 0;

    // Local state (reset after each Main item)
    std::vector<uint32_t> usages;
    uint32_t usageMin    = 0;
    uint32_t usageMax    = 0;
    bool     hasRange    = false;

    // Bit offset per report ID for input items; separate tracker for output
    std::map<uint32_t, int> inputBitOff;
    std::map<uint32_t, int> outputBitOff;

    int dataIndex = 0;

    int i = 0;
    while (i < len) {
        uint8_t b = desc[i++];

        // Long item — skip entirely
        if (b == 0xFE) {
            if (i < len) { int skip = desc[i++]; i += skip + 1; }
            continue;
        }

        uint8_t bSize = b & 0x03;
        uint8_t bType = (b >> 2) & 0x03;
        uint8_t bTag  = (b >> 4) & 0x0F;

        int dataLen = (bSize == 3) ? 4 : bSize;

        // Read little-endian data
        uint32_t udata = 0;
        for (int j = 0; j < dataLen && i < len; ++j)
            udata |= ((uint32_t)desc[i++]) << (8 * j);

        if (bType == 1) { // Global
            switch (bTag) {
                case 0x0: usagePage   = udata; break;
                case 0x1: logicalMin  = signExtend(udata, dataLen * 8); break;
                case 0x2: logicalMax  = signExtend(udata, dataLen * 8); break;
                case 0x7: reportSize  = udata; break;
                case 0x8: reportID    = udata; break;
                case 0x9: reportCount = udata; break;
                default: break;
            }
        } else if (bType == 2) { // Local
            switch (bTag) {
                case 0x0: usages.push_back(udata); break;
                case 0x1: usageMin = udata; hasRange = true; break;
                case 0x2: usageMax = udata; hasRange = true; break;
                default: break;
            }
        } else if (bType == 0) { // Main
            if (bTag == 0x8) { // Input
                bool isConst    = (udata & 0x01) != 0;
                bool isVariable = (udata & 0x02) != 0;
                bool isRelative = (udata & 0x04) != 0;

                int& bitOff = inputBitOff[reportID];

                if (!isConst) {
                    if (isVariable) {
                        for (uint32_t k = 0; k < reportCount; ++k) {
                            InputField f{};
                            f.usagePage  = usagePage;
                            f.logicalMin = logicalMin;
                            f.logicalMax = logicalMax;
                            f.reportSize = reportSize;
                            f.bitOffset  = bitOff;
                            f.isRelative = isRelative;
                            f.reportID   = (uint8_t)reportID;
                            f.dataIndex  = dataIndex++;

                            if (hasRange)
                                f.usage = (uint16_t)(usageMin + k);
                            else if (k < usages.size())
                                f.usage = (uint16_t)usages[k];
                            else if (!usages.empty())
                                f.usage = (uint16_t)usages.back();

                            // Button: usage page 0x09, or 1-bit 0–1 field
                            f.isButton = (usagePage == 0x09) ||
                                         (logicalMin == 0 && logicalMax == 1 && reportSize == 1);

                            bitOff += (int)reportSize;
                            fields.push_back(f);
                        }
                    } else {
                        // Array: each usage in range is a potential button press
                        uint32_t uMin = hasRange ? usageMin : 0;
                        uint32_t uMax = hasRange ? usageMax : (!usages.empty() ? usages.back() : 0);
                        for (uint32_t u = uMin; u <= uMax; ++u) {
                            InputField f{};
                            f.usagePage  = usagePage;
                            f.usage      = (uint16_t)u;
                            f.logicalMin = logicalMin;
                            f.logicalMax = logicalMax;
                            f.reportSize = reportSize;
                            f.bitOffset  = bitOff;
                            f.isRelative = false;
                            f.isButton   = true;
                            f.reportID   = (uint8_t)reportID;
                            f.dataIndex  = dataIndex++;
                            fields.push_back(f);
                        }
                        bitOff += (int)(reportSize * reportCount);
                    }
                } else {
                    bitOff += (int)(reportSize * reportCount);
                }

                // Track largest input report payload seen
                int totalBits = bitOff;
                int payloadBytes = (totalBits + 7) / 8;
                int reportBytes  = payloadBytes + (reportID != 0 ? 1 : 0);
                if (reportBytes > outInputBytes) outInputBytes = reportBytes;

            } else if (bTag == 0x9) { // Output
                int& bitOff = outputBitOff[reportID];
                bitOff += (int)(reportSize * reportCount);
                int payloadBytes = (bitOff + 7) / 8;
                int reportBytes  = payloadBytes + (reportID != 0 ? 1 : 0);
                if (reportBytes > outOutputBytes) outOutputBytes = reportBytes;
            }
            // Reset local state
            usages.clear();
            usageMin = usageMax = 0;
            hasRange = false;
        }
    }

    return fields;
}

// ---------------------------------------------------------------------------
// Extract a |bitCount|-wide value starting at |bitOffset| bits into |data|.
// ---------------------------------------------------------------------------
static int32_t extractBits(const uint8_t* data, int dataLen, int bitOffset, int bitCount)
{
    uint32_t value = 0;
    for (int b = 0; b < bitCount; ++b) {
        int byteIdx = (bitOffset + b) / 8;
        int bitIdx  = (bitOffset + b) % 8;
        if (byteIdx < dataLen && ((data[byteIdx] >> bitIdx) & 1))
            value |= (1u << b);
    }
    return (int32_t)value;
}

// ---------------------------------------------------------------------------
// HidReader implementation
// ---------------------------------------------------------------------------

HidReader::HidReader()
{
    hid_init();
}

HidReader::~HidReader()
{
    if (dev) {
        hid_close(dev);
        dev = nullptr;
    }
    hid_exit();
}

void HidReader::buildInputIndex(const uint8_t* desc, int descLen)
{
    int inBytes = 0, outBytes = 0;
    std::vector<InputField> fields = parseDescriptor(desc, descLen, inBytes, outBytes);

    inputReportLen  = inBytes;
    outputReportLen = outBytes;

    hidValues.clear();
    hidButtons.clear();

    // Separate dials and buttons, assign typeIndex by sorted dataIndex
    std::vector<int> dialIndexes, buttonIndexes;

    for (const auto& f : fields) {
        if (f.isButton) {
            HidButton hb{};
            hb.caps.usagePage  = f.usagePage;
            hb.caps.usage      = f.usage;
            hb.caps.bitOffset  = f.bitOffset;
            hb.caps.reportID   = f.reportID;
            hidButtons[f.dataIndex] = hb;
            buttonIndexes.push_back(f.dataIndex);
        } else {
            HidValue hv{};
            hv.caps.usagePage  = f.usagePage;
            hv.caps.usage      = f.usage;
            hv.caps.logicalMin = f.logicalMin;
            hv.caps.logicalMax = f.logicalMax;
            hv.caps.reportSize = f.reportSize;
            hv.caps.bitOffset  = f.bitOffset;
            hv.caps.isRelative = f.isRelative;
            hv.caps.reportID   = f.reportID;
            hidValues[f.dataIndex] = hv;
            dialIndexes.push_back(f.dataIndex);
        }
    }

    std::ranges::sort(dialIndexes);
    std::ranges::sort(buttonIndexes);

    DLOG("\ndials: ");
    for (int i = 0; i < (int)dialIndexes.size(); ++i) {
        hidValues[dialIndexes[i]].typeIndex = (int)dialIndexes.size() - 1 - i;
        DLOG("[%d,%d] ", i, dialIndexes[i]);
    }
    DLOG("  buttons: ");
    for (int i = 0; i < (int)buttonIndexes.size(); ++i) {
        hidButtons[buttonIndexes[i]].typeIndex = i;
        DLOG("[%d,%d] ", i, buttonIndexes[i]);
    }
    DLOG("\n");
}

void HidReader::decodeReport(const uint8_t* report, int len)
{
    uint8_t rid = 0;
    int     payloadStart = 0;

    // If any of our fields use a report ID, the first byte is the ID
    bool hasReportIDs = false;
    for (auto& [di, hv] : hidValues)
        if (hv.caps.reportID != 0) { hasReportIDs = true; break; }
    if (!hasReportIDs)
        for (auto& [di, hb] : hidButtons)
            if (hb.caps.reportID != 0) { hasReportIDs = true; break; }

    if (hasReportIDs) {
        rid          = report[0];
        payloadStart = 1;
    }

    const uint8_t* payload  = report + payloadStart;
    int            payloadLen = len - payloadStart;

    for (auto& [di, hv] : hidValues) {
        if (hv.caps.reportID != 0 && hv.caps.reportID != rid) continue;

        int32_t raw = extractBits(payload, payloadLen,
                                   hv.caps.bitOffset, (int)hv.caps.reportSize);

        // Sign-extend relative/signed fields
        if (hv.caps.isRelative || hv.caps.logicalMin < 0) {
            if (raw & (1 << ((int)hv.caps.reportSize - 1)))
                raw |= ~((1 << (int)hv.caps.reportSize) - 1);
        }

        int64_t prev = hv.value;
        hv.value = raw;

        if (raw != 0) {
            DLOG("DIAL di=%d typeIndex=%d delta=%d\n", di, hv.typeIndex, raw);
            ReadDial(hv.typeIndex, (int64_t)raw);
        }
        (void)prev;
    }

    for (auto& [di, hb] : hidButtons) {
        if (hb.caps.reportID != 0 && hb.caps.reportID != rid) continue;

        int32_t raw = extractBits(payload, payloadLen,
                                   hb.caps.bitOffset, 1);
        bool pressed = (raw != 0);

        if (pressed != hb.value) {
            hb.value = pressed;
            DLOG("BUTTON di=%d typeIndex=%d on=%d\n", di, hb.typeIndex, (int)pressed);
            ReadButton(hb.typeIndex, pressed);
        }
    }
}

bool HidReader::tryDevice(const char* path, uint16_t targetUsagePage, uint16_t targetUsage)
{
    DLOG("  tryDevice: opening rawFd for %s\n", path);
    int rawFd = open(path, O_RDONLY | O_NONBLOCK);
    if (rawFd < 0) {
        perror("  tryDevice: open() failed");
        return false;
    }
    DLOG("  tryDevice: rawFd=%d OK\n", rawFd);

    struct hidraw_report_descriptor rptDesc{};
    int descSize = 0;
    if (ioctl(rawFd, HIDIOCGRDESCSIZE, &descSize) < 0) {
        perror("  tryDevice: HIDIOCGRDESCSIZE ioctl failed");
        close(rawFd);
        return false;
    }
    if (descSize == 0) {
        DLOG("  tryDevice: descriptor size is 0, skipping\n");
        close(rawFd);
        return false;
    }
    DLOG("  tryDevice: descriptor size = %d bytes\n", descSize);

    rptDesc.size = descSize;
    if (ioctl(rawFd, HIDIOCGRDESC, &rptDesc) < 0) {
        perror("  tryDevice: HIDIOCGRDESC ioctl failed");
        close(rawFd);
        return false;
    }
    DLOG("  tryDevice: descriptor read OK\n");

#ifdef PCPANEL_DEBUG
    printf("  tryDevice: raw descriptor bytes:");
    for (int i = 0; i < descSize; ++i) {
        if (i % 16 == 0) printf("\n    %04x: ", i);
        printf("%02x ", rptDesc.value[i]);
    }
    printf("\n");
#endif

    close(rawFd);

    int inBytes = 0, outBytes = 0;
    std::vector<InputField> fields = parseDescriptor(rptDesc.value, descSize,
                                                     inBytes, outBytes);

#ifdef PCPANEL_DEBUG
    printf("  tryDevice: parsed %zu input fields (inBytes=%d outBytes=%d):\n",
           fields.size(), inBytes, outBytes);
    for (const auto& f : fields) {
        printf("    dataIndex=%d  usagePage=0x%04x  usage=0x%04x  "
               "logMin=%d logMax=%d  reportSize=%u  bitOffset=%d  "
               "isRelative=%d  isButton=%d  reportID=%u\n",
               f.dataIndex, f.usagePage, f.usage,
               f.logicalMin, f.logicalMax,
               f.reportSize, f.bitOffset,
               (int)f.isRelative, (int)f.isButton, f.reportID);
    }
#endif

    bool found = false;
    for (const auto& f : fields) {
        if (f.usagePage == targetUsagePage && f.usage == targetUsage) {
            found = true;
            break;
        }
    }
    DLOG("  tryDevice: usage match (page=0x%04x usage=0x%04x) found=%d\n",
         targetUsagePage, targetUsage, (int)found);

    // hidapi already confirmed usage/usagePage via top-level Application Collection,
    // so trust the enumeration result even if no individual input field carries 0x37.
    if (!found) {
        DLOG("  tryDevice: no input field matched target usage — proceeding anyway "
             "(top-level collection match from hidapi enumeration)\n");
    }

    DLOG("  tryDevice: calling hid_open_path(%s)\n", path);
    dev = hid_open_path(path);
    if (!dev) {
        fprintf(stderr, "  tryDevice: hid_open_path failed: %ls\n", hid_error(nullptr));
        return false;
    }
    DLOG("  tryDevice: hid_open_path OK\n");

    buildInputIndex(rptDesc.value, descSize);

    DLOG("Input report length: %d bytes\n", inputReportLen);
    DLOG("Output report length: %d bytes\n", outputReportLen);

    OnSync();

    std::vector<uint8_t> report(std::max(inputReportLen, 64));

    DLOG("Listening for HID input reports on %s...\n", path);

    while (true) {
        int n = hid_read_timeout(dev, report.data(), report.size(), 1000 /*ms*/);

        if (n < 0) {
            fprintf(stderr, "hid_read_timeout failed: %ls\n", hid_error(dev));
            break;
        }

        if (n == 0) {
            OnSync();
            continue;
        }

#ifdef PCPANEL_DEBUG
        printf("  report: %d bytes:", n);
        for (int i = 0; i < n; ++i) printf(" %02x", report[i]);
        printf("\n");
#endif

        decodeReport(report.data(), n);
        DLOG("\n");
    }

    hid_close(dev);
    dev = nullptr;
    return true;
}

int HidReader::HrReadLoop()
{
    // Enumerate all HID devices and look for usage page 0x01, usage 0x37 (Generic Desktop / Dial)
    struct hid_device_info* devs = hid_enumerate(0, 0);
    for (struct hid_device_info* d = devs; d != nullptr; d = d->next) {
        DLOG("Device: %s  usagePage=0x%04x  usage=0x%04x  manufacturer=%ls  product=%ls\n",
             d->path, d->usage_page, d->usage,
             d->manufacturer_string ? d->manufacturer_string : L"",
             d->product_string      ? d->product_string      : L"");

        if (d->usage_page == 0x01 && d->usage == 0x37) {
            DLOG("  → Candidate dial device, opening...\n");
            tryDevice(d->path, d->usage_page, d->usage);
        }
    }
    hid_free_enumeration(devs);
    return 0;
}

void HidReader::WriteHidOut(std::vector<uint8_t>& outReport)
{
    if (!dev) return;

    outReport.resize(std::max((int)outReport.size(), outputReportLen));

    // hidapi write: buffer must start with report ID
    int n = hid_write(dev, outReport.data(), outReport.size());
    if (n < 0)
        fprintf(stderr, "hid_write failed: %ls\n", hid_error(dev));
}
