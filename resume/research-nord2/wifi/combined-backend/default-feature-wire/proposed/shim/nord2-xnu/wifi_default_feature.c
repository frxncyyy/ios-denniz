#include "wifi_default_feature.h"
#include <string.h>
static const char *const defaults[11][2] = {
    {"ScreenOnBeaconTimeoutCount", "10"},
    {"BeaconTimoutFilterDurationMs", "2000"},
    {"AdapScan", "0x0"},
    {"DropPacketsIPV4Low", "0x12DE"},
    {"DropPacketsIPV4High", "0x0"},
    {"DropPacketsIPV6Low", "0x2"},
    {"DropPacketsIPV6High", "0x0"},
    {"2GTxMaskDPDOn", "1"},
    {"2GTxPMinus1dbAtHighTemp", "1"},
    {"TssiGroupBackupRestore", "1"},
    {"LdtBTONullHwLifeTime", "5"},
};
int ndf_body(unsigned index, uint8_t out[NDF_BODY_BYTES])
{
    uint8_t body[NDF_BODY_BYTES] = {0};
    unsigned first, count, i;
    if (!out || index >= NDF_COMMANDS) return -1;
    first = index * 4u; count = index == 2u ? 3u : 4u;
    body[4] = 1; body[8] = (uint8_t)count;
    body[10] = (uint8_t)(count * 68u); body[11] = (uint8_t)((count * 68u) >> 8);
    for (i = 0; i < count; ++i) {
        uint8_t *item = body + 12u + i * 68u;
        unsigned k = (unsigned)strlen(defaults[first+i][0]);
        unsigned v = (unsigned)strlen(defaults[first+i][1]);
        item[0] = 3; item[1] = (uint8_t)k; item[2] = (uint8_t)v;
        memcpy(item + 4, defaults[first+i][0], k);
        memcpy(item + 36, defaults[first+i][1], v);
    }
    memcpy(out, body, sizeof(body)); return 0;
}
