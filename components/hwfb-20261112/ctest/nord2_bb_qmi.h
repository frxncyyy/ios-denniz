/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * NORD2 BB: what the virtual Mav17 answers from the MediaTek modem's own state. Portable (kernel and host test); no kernel code.
 *
 * The state is a snapshot built from AT queries on the in-kernel AT client (nord2_at_query, sweet_amfi.c): +CFUN, +CPIN, +CLCK "SC",
 * +CEREG, +CREG, +COPS (numeric and long), +CSQ, +CESQ, and once +CGSN, +CGMR, +CIMI and EF_ICCID (+CRSM). The QMI layouts are
 * libqmi's (data/qmi-service-{dms,nas,uim}.json); every builder writes the TLVs after the result TLV of a response, or the whole TLV
 * area of an indication. IMEI, IMSI and ICCID live only in the snapshot and in the QMI answers that carry them; nothing here prints.
 */
#ifndef NORD2_BB_QMI_H
#define NORD2_BB_QMI_H

#include <stdint.h>
#include "nord2_bb_qmux.h"

/* ------------------------------------------------------------------ message ids */
#define NBM_DMS_SET_EVENT_REPORT 0x0001u
#define NBM_DMS_GET_CAPABILITIES 0x0020u
#define NBM_DMS_GET_MANUFACTURER 0x0021u
#define NBM_DMS_GET_REVISION 0x0023u
#define NBM_DMS_GET_IDS 0x0025u
#define NBM_DMS_GET_OPERATING_MODE 0x002du
#define NBM_DMS_SET_OPERATING_MODE 0x002eu
#define NBM_DMS_UIM_GET_ICCID 0x003cu
#define NBM_DMS_UIM_GET_IMSI 0x0043u
#define NBM_DMS_UIM_GET_STATE 0x0044u
#define NBM_DMS_GET_SOFTWARE_VERSION 0x0051u
#define NBM_NAS_SET_EVENT_REPORT 0x0002u
#define NBM_NAS_REGISTER_INDICATIONS 0x0003u
#define NBM_NAS_GET_SIGNAL_STRENGTH 0x0020u
#define NBM_NAS_SERVING_SYSTEM 0x0024u        /* Get Serving System, and its indication */
#define NBM_NAS_GET_HOME_NETWORK 0x0025u
#define NBM_NAS_GET_SYSTEM_INFO 0x004du
#define NBM_NAS_SYSTEM_INFO_IND 0x004eu
#define NBM_NAS_GET_SIGNAL_INFO 0x004fu
#define NBM_NAS_CONFIG_SIGNAL_INFO 0x0050u
#define NBM_UIM_READ_TRANSPARENT 0x0020u
#define NBM_UIM_READ_RECORD 0x0021u
#define NBM_UIM_EVENT_REGISTRATION 0x002eu
#define NBM_UIM_GET_CARD_STATUS 0x002fu
#define NBM_UIM_CARD_STATUS_IND 0x0032u
/* 0277f (after q10bs): more of DMS, NAS, UIM, WDS and WMS, ids from libqmi or Qualcomm's QMI headers */
#define NBM_DMS_GET_MSISDN 0x0024u
#define NBM_DMS_GET_TIME 0x002fu
#define NBM_DMS_GET_BAND_CAPABILITIES 0x0045u
#define NBM_DMS_OPMODE_RESET 4u               /* Set Operating Mode: reset the modem */
#define NBM_NAS_GET_SYSTEM_SELECTION_PREF 0x0034u
#define NBM_NAS_GET_PLMN_NAME 0x0044u
#define NBM_NAS_SIGNAL_INFO_IND 0x0051u
#define NBM_UIM_GET_FILE_ATTRIBUTES 0x0024u
#define NBM_WDS_GET_DORMANCY_STATUS 0x0030u
#define NBM_WMS_GET_MESSAGE_WAITING 0x0043u
#define NBM_WMS_GET_SERVICE_READY_STATUS 0x005cu
/* 0277i (after q10bv): PDS for locationd's CLQmiPdsManager, MFSE (0x2c) for ABM's QMIRFSCommandDriver */
#define NBM_SVC_PDS 0x06u
#define NBM_PDS_RESET 0x0000u
#define NBM_PDS_GET_GPS_SERVICE_STATE 0x0020u
#define NBM_PDS_SET_GPS_SERVICE_STATE 0x0021u
#define NBM_SVC_MFSE 0x2cu
#define NBM_MFSE_RFS_SYNC 0x0020u             /* ABM's "Sending RFS sync request"; answer 0x10 u32 sync token */
#define NBM_MFSE_RFS_SYNC_IND 0x0800u         /* request: enable the indication; indication: "Received RFS Sync indication" */
/* 0277l: cellular data (WDS, DSD) and the current PLMN name indication (NAS 0x61, CommCenter's "Received current PLMN indication") */
#define NBM_WDS_START_NETWORK 0x0020u
#define NBM_WDS_STOP_NETWORK 0x0021u
#define NBM_WDS_PKT_SRVC_STATUS 0x0022u        /* Get Packet Service Status, and its indication */
#define NBM_WDS_GET_RUNTIME_SETTINGS 0x002du
#define NBM_WDS_SET_IP_FAMILY 0x004du
#define NBM_WDS_GET_LTE_ATTACH_PARAMS 0x0085u
#define NBM_WDS_BIND_MUX_DATA_PORT 0x00a2u
#define NBM_SVC_DSD 0x2au
#define NBM_DSD_GET_SYSTEM_STATUS 0x0024u
#define NBM_DSD_SYSTEM_STATUS_IND 0x0026u
#define NBM_NAS_CURRENT_PLMN_NAME_IND 0x0061u
#define NBM_QMI_ERR_CALL_FAILED 0x000eu
#define NBM_QMI_ERR_OUT_OF_CALL 0x000fu

/* ------------------------------------------------------------------ the snapshot */
enum { NBM_SIM_UNKNOWN, NBM_SIM_READY, NBM_SIM_PIN, NBM_SIM_PUK, NBM_SIM_ABSENT, NBM_SIM_ERROR, NBM_SIM_BUSY };

/* the AT queries, in the order a refresh sends them */
enum {
    NBM_Q_CFUN, NBM_Q_CPIN, NBM_Q_CLCK, NBM_Q_CEREG, NBM_Q_CREG, NBM_Q_COPS_NUMERIC, NBM_Q_COPS_READ_NUMERIC, NBM_Q_COPS_LONG,
    NBM_Q_COPS_READ_LONG, NBM_Q_CSQ, NBM_Q_CESQ, NBM_Q_CGSN, NBM_Q_CGMR, NBM_Q_CIMI, NBM_Q_ICCID,
    NBM_Q_GID1_FCP, NBM_Q_GID1, NBM_Q_GID2_FCP, NBM_Q_GID2,       /* 0277j: EF GID1/GID2 of the USIM: GET RESPONSE (size), READ BINARY */
    NBM_Q_COUNT
};
static const char *const nbm_q_cmd[NBM_Q_COUNT] = {
    "AT+CFUN?", "AT+CPIN?", "AT+CLCK=\"SC\",2", "AT+CEREG?", "AT+CREG?", "AT+COPS=3,2", "AT+COPS?", "AT+COPS=3,0", "AT+COPS?",
    "AT+CSQ", "AT+CESQ", "AT+CGSN", "AT+CGMR", "AT+CIMI", "AT+CRSM=176,12258,0,0,10",
    "AT+CRSM=192,28478,0,0,0,,\"7FFF\"", "AT+CRSM=176,28478,0,0,0,,\"7FFF\"", "AT+CRSM=192,28479,0,0,0,,\"7FFF\"",
    "AT+CRSM=176,28479,0,0,0,,\"7FFF\"",
};
#define NBM_Q_STATE_MASK ((1u << NBM_Q_CGSN) - 1u)          /* refreshed every time */
#define NBM_Q_IDENTITY_MASK ((1u << NBM_Q_COUNT) - 1u - NBM_Q_STATE_MASK)

struct nbm_state {
    int cfun;                    /* +CFUN, -1 unknown */
    int sim;                     /* NBM_SIM_* */
    int pin_lock;                /* +CLCK "SC": -1 unknown, 0 off, 1 on */
    int eps_stat, eps_act;       /* +CEREG <stat> (0..5) and <AcT>, -1 unknown */
    int cs_stat, cs_act;         /* +CREG */
    uint32_t tac, eci, lac, ci;
    uint8_t have_tac, have_eci, have_lac, have_ci, have_iccid, pad[3];
    char plmn[8];                /* "22288", "" unknown */
    char oper[32];               /* long name */
    int rssi, ber;               /* +CSQ, 99 unknown */
    int rsrq, rsrp;              /* +CESQ raw, 255 unknown */
    int rxlev, rscp, ecno;       /* 0277t: +CESQ GSM rxlev (99 unknown), UMTS RSCP and Ec/No (255 unknown) */
    char imei[20], imsi[20], rev[64];
    uint8_t iccid_ef[10];        /* EF_ICCID as stored on the card */
    /* 0277j: EF GID1 (6F3E) and GID2 (6F3F) of the USIM as stored (carrier bundle matching: "22288_GID1-0000000099") */
    uint8_t gid[2][16];
    uint8_t gid_len[2];
    uint8_t gid_known[2];        /* 0 not read yet, 1 read, 2 the card has no such file */
    uint16_t gid_size[2];        /* the size the GET RESPONSE gave, 0 unknown */
    uint32_t answered;           /* NBM_Q_* bits answered at least once */
};

static inline void nbm_init(struct nbm_state *s)
{
    __builtin_memset(s, 0, sizeof *s);
    s->cfun = s->pin_lock = s->eps_stat = s->eps_act = s->cs_stat = s->cs_act = -1;
    s->rssi = s->ber = 99;
    s->rsrq = s->rsrp = 255;
    s->rxlev = 99; s->rscp = s->ecno = 255;
}

/* ------------------------------------------------------------------ small text helpers */
static inline int nbm_prefix(const char *l, const char *p)
{
    while (*p) if (*l++ != *p++) return 0;
    return 1;
}
static inline void nbm_copy(char *d, unsigned room, const char *s, unsigned n)
{
    unsigned k = 0;
    for (unsigned i = 0; i < n && s[i] && k + 1 < room; i++)
        if (s[i] >= 32 && s[i] < 127) d[k++] = s[i];
    d[k] = 0;
}
static inline unsigned nbm_strlen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static inline int nbm_hexdigit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Comma separated fields after a prefix: numbers in v[] (-1 empty, -2 quoted), quoted text in q[i]. Returns the count. */
#define NBM_FIELDS 8
#define NBM_QLEN 40
static inline int nbm_fields(const char *s, long v[NBM_FIELDS], char q[NBM_FIELDS][NBM_QLEN])
{
    int n = 0;
    while (n < NBM_FIELDS) {
        long val = -1;
        int neg = 0;
        q[n][0] = 0;
        while (*s == ' ') s++;
        if (*s == '"') {
            unsigned k = 0;
            for (s++; *s && *s != '"'; s++)
                if (k + 1 < NBM_QLEN) q[n][k++] = *s;
            q[n][k] = 0;
            if (*s == '"') s++;
            val = -2;
        } else {
            if (*s == '-') { neg = 1; s++; }
            if (*s >= '0' && *s <= '9')
                for (val = 0; *s >= '0' && *s <= '9' && val < 100000000; s++) val = val * 10 + (*s - '0');
            if (neg && val > 0) val = -val;
        }
        v[n++] = val;
        while (*s && *s != ',') s++;
        if (*s != ',') break;
        s++;
    }
    return n;
}
static inline int nbm_hex32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    unsigned n = 0;
    for (; *s; s++, n++) {
        const int d = nbm_hexdigit(*s);
        if (d < 0 || n >= 8) return 0;
        v = v << 4 | (uint32_t)d;
    }
    if (!n) return 0;
    *out = v;
    return 1;
}
/* a run of 6..20 digits at the start of the line (after an optional "+CGSN:"/"+CIMI:" prefix and quotes) */
static inline int nbm_digits(const char *l, char *out, unsigned room)
{
    unsigned k = 0;
    if (*l == '+') { while (*l && *l != ':') l++; if (*l) l++; }
    while (*l == ' ' || *l == '"') l++;
    while (*l >= '0' && *l <= '9' && k + 1 < room) out[k++] = *l++;
    out[k] = 0;
    if (*l && *l != '"' && *l != ' ') { out[0] = 0; return 0; }
    if (k < 6) { out[0] = 0; return 0; }
    return 1;
}

/* +CRSM: <sw1>,<sw2>[,"<hex>"], the hex possibly split over several lines (sweet_at_emit cuts at 199 characters). */
static inline int nbm_crsm(const char *text, unsigned len, uint8_t *sw1, uint8_t *sw2, uint8_t *data, uint32_t room, uint32_t *n)
{
    unsigned i = 0;
    *n = 0;
    while (i + 6 <= len && !(text[i] == '+' && nbm_prefix(text + i, "+CRSM:"))) {
        while (i < len && text[i] != '\n') i++;
        i++;
    }
    if (i + 6 > len) return 0;
    i += 6;
    long a = -1, b = -1;
    while (i < len && text[i] == ' ') i++;
    for (a = 0; i < len && text[i] >= '0' && text[i] <= '9'; i++) a = a * 10 + (text[i] - '0');
    if (i >= len || text[i] != ',') return 0;
    for (i++, b = 0; i < len && text[i] >= '0' && text[i] <= '9'; i++) b = b * 10 + (text[i] - '0');
    if (a > 255 || b > 255) return 0;
    *sw1 = (uint8_t)a; *sw2 = (uint8_t)b;
    while (i < len && text[i] != '"' && text[i] != '\n') i++;
    if (i >= len || text[i] != '"') return 1;              /* no data */
    int hi = -1;
    for (i++; i < len && text[i] != '"'; i++) {
        const int d = nbm_hexdigit(text[i]);
        if (d < 0) continue;                                 /* line breaks of a split line */
        if (hi < 0) { hi = d; continue; }
        if (*n < room) data[(*n)++] = (uint8_t)(hi << 4 | d);
        hi = -1;
    }
    return 1;
}

/* The FCP template of a GET RESPONSE (TS 102 221 11.1.1.3): 62 L { 82 file descriptor, 83 file id, 80 size, 81 total size, ... }.
 * size: tag 80 (or 81, or record size x count); type: 0 transparent, 1 cyclic, 2 linear fixed, 3 DF, 4 MF. 0 when it is no FCP. */
struct nbm_fcp { uint16_t size, id, rec_size, rec_count; uint8_t type; };
static inline int nbm_fcp_parse(const uint8_t *fcp, uint32_t n, uint16_t file, struct nbm_fcp *f)
{
    f->size = 0; f->id = file; f->rec_size = 0; f->rec_count = 0; f->type = 0;
    if (n < 2 || fcp[0] != 0x62) return 0;
    uint32_t end = 2u + fcp[1] < n ? 2u + fcp[1] : n, at = 2;
    while (at + 2 <= end) {
        const uint8_t tag = fcp[at], l = fcp[at + 1];
        const uint8_t *v = fcp + at + 2;
        if (at + 2u + l > end) break;
        if (tag == 0x82 && l >= 2) {
            const uint8_t d = v[0];
            if ((d & 0x38) == 0x38) f->type = f->id == 0x3f00 ? 4 : 3;
            else if ((d & 0x07) == 0x02) f->type = 2;
            else if ((d & 0x07) == 0x06) f->type = 1;
            else f->type = 0;
            if (l >= 5) { f->rec_size = (uint16_t)(v[2] << 8 | v[3]); f->rec_count = v[4]; }
        } else if (tag == 0x83 && l == 2) {
            f->id = (uint16_t)(v[0] << 8 | v[1]);
        } else if (tag == 0x80 && l >= 2) {
            f->size = (uint16_t)(v[l - 2] << 8 | v[l - 1]);
        } else if (tag == 0x81 && l >= 2 && !f->size) {
            f->size = (uint16_t)(v[l - 2] << 8 | v[l - 1]);
        }
        at += 2u + l;
    }
    if (!f->size && f->rec_size && f->rec_count) f->size = (uint16_t)(f->rec_size * f->rec_count);
    return 1;
}

/* ------------------------------------------------------------------ parsing one query's answer */
static inline void nbm_parse_line(struct nbm_state *s, unsigned q, const char *l)
{
    long v[NBM_FIELDS];
    char t[NBM_FIELDS][NBM_QLEN];
    int n;
    switch (q) {
    case NBM_Q_CFUN:
        if (nbm_prefix(l, "+CFUN:") && nbm_fields(l + 6, v, t) >= 1) s->cfun = (int)v[0];
        break;
    case NBM_Q_CPIN:
        if (nbm_prefix(l, "+CPIN:")) {
            const char *p = l + 6;
            while (*p == ' ') p++;
            s->sim = nbm_prefix(p, "READY") ? NBM_SIM_READY : nbm_prefix(p, "SIM PIN") ? NBM_SIM_PIN :
                     nbm_prefix(p, "SIM PUK") ? NBM_SIM_PUK : NBM_SIM_ERROR;
        } else if (nbm_prefix(l, "+CME ERROR:")) {
            const char *p = l + 11;
            while (*p == ' ') p++;
            s->sim = (nbm_prefix(p, "10") && (p[2] == 0 || p[2] == ' ')) || nbm_prefix(p, "SIM not inserted") ? NBM_SIM_ABSENT :
                     (nbm_prefix(p, "14") && (p[2] == 0 || p[2] == ' ')) || nbm_prefix(p, "SIM busy") ? NBM_SIM_BUSY : NBM_SIM_ERROR;
        }
        break;
    case NBM_Q_CLCK:
        if (nbm_prefix(l, "+CLCK:") && nbm_fields(l + 6, v, t) >= 1 && v[0] >= 0) s->pin_lock = v[0] != 0;
        break;
    case NBM_Q_CEREG:
    case NBM_Q_CREG: {
        const int eps = q == NBM_Q_CEREG;
        if (!nbm_prefix(l, eps ? "+CEREG:" : "+CREG:")) break;
        n = nbm_fields(l + (eps ? 7 : 6), v, t);
        if (n < 1) break;
        /* the read response is <n>,<stat>[,"<lac/tac>","<ci>",<AcT>]; a URC of the same prefix that arrives while the query is in
         * flight is collected with it and starts at <stat> */
        const int at = n >= 2 && v[1] >= 0 ? 1 : 0;
        int *stat = eps ? &s->eps_stat : &s->cs_stat, *act = eps ? &s->eps_act : &s->cs_act;
        uint32_t *area = eps ? &s->tac : &s->lac, *cell = eps ? &s->eci : &s->ci;
        uint8_t *have_area = eps ? &s->have_tac : &s->have_lac, *have_cell = eps ? &s->have_eci : &s->have_ci;
        if (v[at] < 0 || v[at] > 11) break;
        *stat = (int)v[at];
        *have_area = n >= at + 2 && v[at + 1] == -2 && nbm_hex32(t[at + 1], area);
        *have_cell = n >= at + 3 && v[at + 2] == -2 && nbm_hex32(t[at + 2], cell);
        *act = n >= at + 4 && v[at + 3] >= 0 ? (int)v[at + 3] : -1;
        break;
    }
    case NBM_Q_COPS_READ_NUMERIC:
    case NBM_Q_COPS_READ_LONG:
        if (!nbm_prefix(l, "+COPS:")) break;
        n = nbm_fields(l + 6, v, t);
        if (n < 3 || v[2] != -2) {                          /* "+COPS: 0": not registered */
            if (q == NBM_Q_COPS_READ_NUMERIC) s->plmn[0] = 0; else s->oper[0] = 0;
            break;
        }
        if (q == NBM_Q_COPS_READ_NUMERIC && v[1] == 2) nbm_copy(s->plmn, sizeof s->plmn, t[2], NBM_QLEN);
        if (q == NBM_Q_COPS_READ_LONG && v[1] == 0) nbm_copy(s->oper, sizeof s->oper, t[2], NBM_QLEN);
        break;
    case NBM_Q_CSQ:
        if (nbm_prefix(l, "+CSQ:") && nbm_fields(l + 5, v, t) >= 2) { s->rssi = (int)v[0]; s->ber = (int)v[1]; }
        break;
    case NBM_Q_CESQ:
        if (nbm_prefix(l, "+CESQ:") && nbm_fields(l + 6, v, t) >= 6) {
            s->rxlev = (int)v[0]; s->rscp = (int)v[2]; s->ecno = (int)v[3]; s->rsrq = (int)v[4]; s->rsrp = (int)v[5];
        }
        break;
    case NBM_Q_CGSN:
        if (!s->imei[0]) nbm_digits(l, s->imei, sizeof s->imei);
        break;
    case NBM_Q_CIMI:
        if (!s->imsi[0]) nbm_digits(l, s->imsi, sizeof s->imsi);
        break;
    case NBM_Q_CGMR:
        if (!s->rev[0] && l[0] && !nbm_prefix(l, "OK") && !nbm_prefix(l, "ERROR") && !nbm_prefix(l, "+CME")) {
            const char *p = l;
            if (nbm_prefix(p, "+CGMR:")) { p += 6; while (*p == ' ') p++; }
            nbm_copy(s->rev, sizeof s->rev, p, 64);
        }
        break;
    default:
        break;
    }
}

/* One finished query (`final` as nord2_at_done_t). Returns 1 when it carried an answer. */
static inline int nbm_parse(struct nbm_state *s, unsigned q, int final, const char *text, unsigned len)
{
    char line[208];
    if (q >= NBM_Q_COUNT || final == 2) return 0;
    if (q == NBM_Q_ICCID) {
        uint8_t sw1 = 0, sw2 = 0, ef[16];
        uint32_t n = 0;
        if (final == 0 && nbm_crsm(text, len, &sw1, &sw2, ef, sizeof ef, &n) && sw1 == 0x90 && n == 10) {
            __builtin_memcpy(s->iccid_ef, ef, 10);
            s->have_iccid = 1;
        }
    } else if (q >= NBM_Q_GID1_FCP && q <= NBM_Q_GID2) {
        /* 0277j: the GET RESPONSE sizes the file (the MT6893 answers P3 0 with 256 bytes), the READ BINARY that follows it in the
         * same refresh gives the bytes; a card without the file (6A82, 94xx) has no GID */
        const unsigned g = q >= NBM_Q_GID2_FCP;
        uint8_t sw1 = 0, sw2 = 0, d[64];
        uint32_t n = 0;
        if (final == 0 && nbm_crsm(text, len, &sw1, &sw2, d, sizeof d, &n)) {
            if (q == NBM_Q_GID1_FCP || q == NBM_Q_GID2_FCP) {
                struct nbm_fcp f;
                if ((sw1 == 0x90 || sw1 == 0x91 || sw1 == 0x92) && nbm_fcp_parse(d, n, g ? 0x6f3f : 0x6f3e, &f)) s->gid_size[g] = f.size;
                else if (sw1 == 0x6a || sw1 == 0x94) { s->gid_known[g] = 2; s->gid_len[g] = 0; }
            } else if (sw1 == 0x90 && s->gid_size[g] && n) {
                uint32_t k = s->gid_size[g] < n ? s->gid_size[g] : n;
                if (k > sizeof s->gid[g]) k = sizeof s->gid[g];
                __builtin_memcpy(s->gid[g], d, k);
                s->gid_len[g] = (uint8_t)k;
                s->gid_known[g] = 1;
            }
        }
    } else {
        if (q == NBM_Q_CPIN && final == 1) s->sim = NBM_SIM_ERROR;   /* refined by the +CME ERROR line below */
        if (q == NBM_Q_CEREG) { s->have_tac = s->have_eci = 0; }
        for (unsigned i = 0; i < len;) {
            unsigned k = 0;
            while (i < len && text[i] != '\n') { if (k + 1 < sizeof line) line[k++] = text[i]; i++; }
            i++;
            line[k] = 0;
            if (k) nbm_parse_line(s, q, line);
        }
    }
    s->answered |= 1u << q;
    return 1;
}

/* ------------------------------------------------------------------ derived values */
static inline uint8_t nbm_radio(int act)      /* 27.007 <AcT> -> QmiNasRadioInterface */
{
    switch (act) {
    case 0: case 1: case 3: return 4;          /* GSM */
    case 2: case 4: case 5: case 6: return 5;  /* UMTS */
    case 7: case 9: case 10: case 13: return 8; /* LTE (also connected to 5GC / EN-DC: Mav17 is LTE only) */
    default: return 0;
    }
}
static inline int nbm_stat_registered(int stat) { return stat == 1 || stat == 5; }
/* QMI registration state (0 not registered, 1 registered, 2 searching, 3 denied, 4 unknown) and its RAT/roaming */
static inline uint8_t nbm_reg(const struct nbm_state *s, uint8_t *radio, int *roaming)
{
    int stat = s->eps_stat, act = s->eps_act;
    if (!nbm_stat_registered(stat) && nbm_stat_registered(s->cs_stat)) { stat = s->cs_stat; act = s->cs_act; }
    if (stat == 1 && act < 0) act = 7;
    *radio = nbm_stat_registered(stat) ? nbm_radio(act) : 0;
    if (nbm_stat_registered(stat) && !*radio) *radio = 8;
    *roaming = stat == 5;
    switch (stat) {
    case 1: case 5: return 1;
    case 0: return 0;
    case 2: return 2;
    case 3: return 3;
    default: return 4;
    }
}
static inline int nbm_plmn(const struct nbm_state *s, uint16_t *mcc, uint16_t *mnc, unsigned *mnc_digits)
{
    const unsigned n = nbm_strlen(s->plmn);
    if (n != 5 && n != 6) return 0;
    *mcc = (uint16_t)((s->plmn[0] - '0') * 100 + (s->plmn[1] - '0') * 10 + (s->plmn[2] - '0'));
    *mnc = 0;
    for (unsigned i = 3; i < n; i++) *mnc = (uint16_t)(*mnc * 10 + (s->plmn[i] - '0'));
    *mnc_digits = n - 3;
    return 1;
}

/* ------------------------------------------------------------------ TLV writer */
static inline uint32_t nbm_tlv(uint8_t *p, uint32_t room, uint32_t type, const void *value, uint32_t len)
{
    const uint8_t *v = (const uint8_t *)value;
    if (room < 3u + len) return 0;
    p[0] = (uint8_t)type;
    nbq_wr16(p + 1, len);
    for (uint32_t i = 0; i < len; i++) p[3 + i] = v[i];
    return 3u + len;
}
static inline uint32_t nbm_tlv_str(uint8_t *p, uint32_t room, uint32_t type, const char *s)
{
    return nbm_tlv(p, room, type, s, nbm_strlen(s));
}
static inline uint32_t nbm_tlv_u8(uint8_t *p, uint32_t room, uint32_t type, uint32_t v)
{
    const uint8_t b = (uint8_t)v;
    return nbm_tlv(p, room, type, &b, 1);
}

/* ------------------------------------------------------------------ NAS */
/* 0277t: the <AcT> behind the registration nbm_reg reports (EPS first, else CS); -1 unknown */
static inline int nbm_reg_act(const struct nbm_state *s)
{
    if (nbm_stat_registered(s->eps_stat)) return s->eps_act;
    if (nbm_stat_registered(s->cs_stat)) return s->cs_act;
    return -1;
}
/* 0277t: QmiNasDataCapability list of the serving RAT (1 GPRS, 2 EDGE, 3 HSDPA, 4 HSUPA, 5 WCDMA, 11 LTE); the count */
static inline uint32_t nbm_data_caps(const struct nbm_state *s, uint8_t radio, uint8_t *c)
{
    const int act = nbm_reg_act(s);
    if (radio == 8) { c[0] = 11; return 1; }
    if (radio == 5) {
        if (act == 4) { c[0] = 5; c[1] = 3; return 2; }
        if (act == 5) { c[0] = 5; c[1] = 4; return 2; }
        if (act == 6) { c[0] = 5; c[1] = 3; c[2] = 4; return 3; }
        c[0] = 5; return 1;
    }
    if (radio == 4) { c[0] = act == 3 ? 2 : 1; return 1; }
    return 0;
}
/* Get Serving System response / Serving System indication: 0x01 serving system, 0x10 roaming, 0x11 data capabilities, 0x12 PLMN */
static inline uint32_t nbm_nas_serving_system(const struct nbm_state *s, uint8_t *t, uint32_t room)
{
    uint8_t radio = 0, b[40];
    int roaming = 0;
    uint16_t mcc, mnc;
    unsigned digits;
    const uint8_t reg = nbm_reg(s, &radio, &roaming);
    uint32_t w = 0;
    b[0] = reg;
    b[1] = nbm_stat_registered(s->cs_stat) ? 1 : s->cs_stat >= 0 ? 2 : 0;      /* CS attach: 1 attached, 2 detached, 0 unknown */
    b[2] = nbm_stat_registered(s->eps_stat) ? 1 : s->eps_stat >= 0 ? 2 : 0;    /* PS attach */
    b[3] = reg == 1 ? 2 : 0;                                                    /* selected network: 3GPP */
    b[4] = radio ? 1 : 0;
    b[5] = radio;
    w += nbm_tlv(t + w, room - w, 0x01, b, radio ? 6u : 5u);
    if (reg != 1) return w;
    w += nbm_tlv_u8(t + w, room - w, 0x10, roaming ? 0 : 1);                  /* QmiNasRoamingIndicatorStatus: 0 on, 1 off */
    {                                                                           /* 0277t: data capabilities of the serving RAT */
        const uint32_t k = nbm_data_caps(s, radio, b + 1);
        b[0] = (uint8_t)k;
        if (k) w += nbm_tlv(t + w, room - w, 0x11, b, 1u + k);
    }
    if (nbm_plmn(s, &mcc, &mnc, &digits)) {
        unsigned k = nbm_strlen(s->oper);
        if (k > 32) k = 32;
        nbq_wr16(b, mcc); nbq_wr16(b + 2, mnc); b[4] = (uint8_t)k;
        __builtin_memcpy(b + 5, s->oper, k);
        w += nbm_tlv(t + w, room - w, 0x12, b, 5u + k);
    }
    (void)digits;
    return w;
}

/* Get System Info response / System Info indication: per 3GPP RAT a service status (0x12 GSM, 0x13 WCDMA, 0x14 LTE: {status, true
 * status, preferred data path}) and, for the serving RAT, its system info (0x17 GSM v2 30 bytes, 0x18 WCDMA v2 33 bytes, 0x19 LTE v2
 * 29 bytes; libqmi's layouts). 0277t: GSM and WCDMA too (CS fallback calls move the modem to 3G/2G: q10cj showed "No service" for the
 * whole call with only LTE reported). */
static inline uint32_t nbm_nas_system_info_ex(const struct nbm_state *s, int get, uint8_t *t, uint32_t room);
static inline uint32_t nbm_nas_system_info(const struct nbm_state *s, uint8_t *t, uint32_t room) { return nbm_nas_system_info_ex(s, 0, t, room); }
/* 0277t: get = 1 for the Get System Info answer, whose TLVs after 0x23 differ from the indication's: LTE voice (both: 0x21 LTE
 * voice support u8), and only in the answer 0x29 IMS voice support u8 and 0x2A LTE voice domain u32 (3 = 3GPP CS fallback). q10cj:
 * dials never reached CommCenter; a voice-capable LTE registration is what CommCenter's call capabilities are built from. */
static inline uint32_t nbm_nas_system_info_ex(const struct nbm_state *s, int get, uint8_t *t, uint32_t room)
{
    uint8_t radio = 0, b[40];
    int roaming = 0;
    uint16_t mcc, mnc;
    unsigned digits;
    const uint8_t reg = nbm_reg(s, &radio, &roaming);
    const uint8_t serving = reg == 1 ? radio : 0;
    uint32_t w = 0;
    static const uint8_t rats[3][3] = { { 4, 0x12, 0x17 }, { 5, 0x13, 0x18 }, { 8, 0x14, 0x19 } };
    for (unsigned r = 0; r < 3; r++) {                  /* service status: 0 none, 1 limited, 2 available */
        const int on = serving == rats[r][0];
        b[0] = on ? 2 : (rats[r][0] == 8 && s->eps_stat == 3) ? 1 : 0;
        b[1] = b[0];
        b[2] = on ? 1 : 0;                               /* preferred data path */
        w += nbm_tlv(t + w, room - w, rats[r][1], b, 3);
    }
    if (!serving || (serving != 4 && serving != 5 && serving != 8)) return w;
    __builtin_memset(b, 0, sizeof b);
    const uint8_t domain = nbm_stat_registered(s->cs_stat) ? (nbm_stat_registered(s->eps_stat) || serving != 8 ? 3 : 1) : 2;  /* CS+PS, CS, PS */
    b[0] = 1; b[1] = domain;                             /* domain */
    b[2] = 1; b[3] = domain;                             /* service capability */
    b[4] = 1; b[5] = roaming ? 1 : 0;                    /* roaming status */
    b[6] = 1; b[7] = 0;                                  /* forbidden */
    b[8] = s->have_lac; nbq_wr16(b + 9, s->lac);         /* LAC */
    const int cs_cell = serving != 8;                    /* GSM/WCDMA: the +CREG cell; LTE: the +CEREG one */
    if (cs_cell) { b[11] = s->have_ci || s->have_eci; nbq_wr32(b + 12, s->have_ci ? s->ci : s->eci); }
    else { b[11] = s->have_eci; nbq_wr32(b + 12, s->eci); }
    b[16] = 0;                                           /* registration reject info: none (17, 18 zero) */
    if (nbm_plmn(s, &mcc, &mnc, &digits)) {
        b[19] = 1;
        b[20] = (uint8_t)('0' + mcc / 100); b[21] = (uint8_t)('0' + mcc / 10 % 10); b[22] = (uint8_t)('0' + mcc % 10);
        if (digits == 3) { b[23] = (uint8_t)('0' + mnc / 100); b[24] = (uint8_t)('0' + mnc / 10 % 10); b[25] = (uint8_t)('0' + mnc % 10); }
        else { b[23] = (uint8_t)('0' + mnc / 10 % 10); b[24] = (uint8_t)('0' + mnc % 10); b[25] = 0xff; }
    }
    if (serving == 8) {
        b[26] = s->have_tac; nbq_wr16(b + 27, s->tac);   /* TAC */
        w += nbm_tlv(t + w, room - w, 0x19, b, 29);
        const int csfb = nbm_stat_registered(s->cs_stat);    /* combined EPS/IMSI attach: voice by CS fallback */
        w += nbm_tlv_u8(t + w, room - w, 0x21, csfb ? 1 : 0);
        if (get) {
            uint8_t d[4];
            w += nbm_tlv_u8(t + w, room - w, 0x29, 0);         /* no IMS voice */
            nbq_wr32(d, csfb ? 3u : 0u);
            w += nbm_tlv(t + w, room - w, 0x2a, d, 4);
        }
    } else if (serving == 5) {
        const int act = nbm_reg_act(s);
        const uint8_t hs = act == 4 ? 1 : act == 5 ? 2 : act == 6 ? 3 : 0;   /* QmiNasWcdmaHsService: none, HSDPA, HSUPA, both */
        b[26] = 1; b[27] = hs;                           /* HS call status */
        b[28] = 1; b[29] = hs;                           /* HS service */
        b[30] = 0; nbq_wr16(b + 31, 0);                  /* primary scrambling code: unknown */
        w += nbm_tlv(t + w, room - w, 0x18, b, 33);
    } else {
        b[26] = 1; b[27] = nbm_reg_act(s) == 3;          /* EGPRS */
        b[28] = 1; b[29] = 0;                            /* DTM */
        w += nbm_tlv(t + w, room - w, 0x17, b, 30);
    }
    return w;
}

/* dBm from +CSQ <rssi> (0..31), 0 when unknown */
static inline int nbm_rssi_dbm(const struct nbm_state *s) { return s->rssi >= 0 && s->rssi <= 31 ? -113 + 2 * s->rssi : 0; }

/* Get Signal Info / Signal Info indication, the serving RAT only: 0x14 LTE {i8 rssi, i8 rsrq dB, i16 rsrp dBm, i16 snr 0.1 dB} (+CESQ
 * has no SNR: 0 dB), 0277t: 0x13 WCDMA {i8 rssi, i16 Ec/Io in -0.5 dB steps} + 0x19 RSCP i16 dBm, 0x12 GSM i8 rssi dBm. */
static inline uint32_t nbm_nas_signal_info(const struct nbm_state *s, uint8_t *t, uint32_t room)
{
    uint8_t radio = 0, b[6];
    int roaming = 0;
    if (nbm_reg(s, &radio, &roaming) != 1) return 0;
    int rssi = nbm_rssi_dbm(s);
    if (radio == 8) {
        if (!rssi) return 0;
        const int rsrq = s->rsrq >= 0 && s->rsrq <= 34 ? -20 + (s->rsrq + 1) / 2 : -10;
        const int rsrp = s->rsrp >= 0 && s->rsrp <= 97 ? -140 + s->rsrp : rssi - 20;
        b[0] = (uint8_t)(int8_t)rssi;
        b[1] = (uint8_t)(int8_t)rsrq;
        nbq_wr16(b + 2, (uint16_t)(int16_t)rsrp);
        nbq_wr16(b + 4, 0);
        return nbm_tlv(t, room, 0x14, b, 6);
    }
    if (radio == 5) {
        const int rscp = s->rscp >= 0 && s->rscp <= 96 ? -121 + s->rscp : 0;
        if (!rssi && rscp) rssi = rscp + 10;
        if (!rssi) return 0;
        const int ecio = s->ecno >= 0 && s->ecno <= 49 ? 49 - s->ecno : 16;      /* Ec/No dB = -24.5 + ecno/2: -0.5 dB steps */
        b[0] = (uint8_t)(int8_t)rssi;
        nbq_wr16(b + 1, (uint16_t)(int16_t)ecio);
        uint32_t w = nbm_tlv(t, room, 0x13, b, 3);
        if (rscp) { nbq_wr16(b, (uint16_t)(int16_t)rscp); w += nbm_tlv(t + w, room - w, 0x19, b, 2); }
        return w;
    }
    if (radio == 4) {
        if (!rssi && s->rxlev >= 0 && s->rxlev <= 63) rssi = -111 + s->rxlev;
        if (!rssi) return 0;
        b[0] = (uint8_t)(int8_t)rssi;
        return nbm_tlv(t, room, 0x12, b, 1);
    }
    return 0;
}
/* Get Signal Strength: 0x01 {i8 dBm, u8 radio interface} */
static inline uint32_t nbm_nas_signal_strength(const struct nbm_state *s, uint8_t *t, uint32_t room)
{
    uint8_t radio = 0, b[2];
    int roaming = 0;
    if (nbm_reg(s, &radio, &roaming) != 1 || !nbm_rssi_dbm(s)) return 0;
    b[0] = (uint8_t)(int8_t)nbm_rssi_dbm(s);
    b[1] = radio;
    return nbm_tlv(t, room, 0x01, b, 2);
}
/* Get Home Network: 0x01 {u16 mcc, u16 mnc, u8 len, name}, from the IMSI; the MNC length from the PLMN when registered at home,
 * otherwise 2 digits (3 for North American MCCs 302..316). */
static inline uint32_t nbm_nas_home_network(const struct nbm_state *s, uint8_t *t, uint32_t room)
{
    uint8_t b[40];
    uint16_t mcc, mnc;
    unsigned digits = 0, k = 0;
    if (nbm_strlen(s->imsi) < 6) return 0;
    mcc = (uint16_t)((s->imsi[0] - '0') * 100 + (s->imsi[1] - '0') * 10 + (s->imsi[2] - '0'));
    const int home = s->eps_stat == 1 || s->cs_stat == 1;
    uint16_t pmcc, pmnc;
    unsigned pdigits;
    if (home && nbm_plmn(s, &pmcc, &pmnc, &pdigits) && pmcc == mcc) digits = pdigits;
    if (!digits) digits = mcc >= 302 && mcc <= 316 ? 3 : 2;
    mnc = 0;
    for (unsigned i = 3; i < 3 + digits; i++) mnc = (uint16_t)(mnc * 10 + (s->imsi[i] - '0'));
    if (home) { k = nbm_strlen(s->oper); if (k > 32) k = 32; }
    nbq_wr16(b, mcc); nbq_wr16(b + 2, mnc); b[4] = (uint8_t)k;
    __builtin_memcpy(b + 5, s->oper, k);
    return nbm_tlv(t, room, 0x01, b, 5u + k);
}

/* ------------------------------------------------------------------ DMS */
/* Get Operating Mode 0x01 from +CFUN: 0 online (CFUN 1), 1 low power (CFUN 0/4, or not known yet) */
static inline uint8_t nbm_operating_mode(const struct nbm_state *s) { return s->cfun == 1 ? 0 : 1; }
/* Get Capabilities 0x01: max tx/rx rate, data service capability 3 (simultaneous CS+PS), SIM supported, GSM/UMTS/LTE */
static inline uint32_t nbm_dms_capabilities(uint8_t *t, uint32_t room)
{
    uint8_t b[14];
    nbq_wr32(b, 50000000u); nbq_wr32(b + 4, 150000000u);
    b[8] = 3; b[9] = 2; b[10] = 3; b[11] = 4; b[12] = 5; b[13] = 8;
    return nbm_tlv(t, room, 0x01, b, 14);
}
/* ICCID digits in reading order (EF_ICCID keeps each pair low nibble first; F ends it) */
static inline int nbm_iccid_digits(const struct nbm_state *s, char *out, unsigned room)
{
    unsigned k = 0;
    if (!s->have_iccid) return 0;
    for (unsigned i = 0; i < 10; i++) {
        const uint8_t lo = s->iccid_ef[i] & 0xfu, hi = s->iccid_ef[i] >> 4;
        if (lo > 9 || k + 1 >= room) break;
        out[k++] = (char)('0' + lo);
        if (hi > 9 || k + 1 >= room) break;
        out[k++] = (char)('0' + hi);
    }
    out[k] = 0;
    return k >= 18;
}
/* IMSI in reading-order BCD padded with F (the activation indication's TLV 0x43 form) */
static inline int nbm_imsi_bcd(const struct nbm_state *s, uint8_t out[8])
{
    const unsigned n = nbm_strlen(s->imsi);
    if (n < 6 || n > 15) return 0;
    for (unsigned i = 0; i < 8; i++) {
        const uint8_t a = 2u * i < n ? (uint8_t)(s->imsi[2u * i] - '0') : 0xfu, b = 2u * i + 1u < n ? (uint8_t)(s->imsi[2u * i + 1u] - '0') : 0xfu;
        out[i] = (uint8_t)(a << 4 | b);
    }
    return 1;
}
/* 0277m: the SIM state as CommCenter may see it. With the radio function off (CFUN 0, before the boot script's AT+CFUN=1) the MediaTek
 * modem has the card powered down and +CPIN? answers +CME ERROR: that is not a dead card, it is a card the modem has not read yet
 * (a Qualcomm baseband reads it in low power mode too). Shown as BUSY: present, USIM application "detected". */
static inline int nbm_sim_shown(const struct nbm_state *s)
{
    return s->sim == NBM_SIM_ERROR && s->cfun != 1 ? NBM_SIM_BUSY : s->sim;
}
/* 0277m: 1 when the modem's SIM state is final (radio function on and the card ready, locked or absent), 2 when the radio is on and
 * +CPIN? still fails (final only if it stays so: the caller times it), 0 while it can still change on its own (CFUN not yet 1,
 * unknown, busy). */
static inline int nbm_sim_settled(const struct nbm_state *s)
{
    if (s->cfun != 1) return 0;
    switch (s->sim) {
    case NBM_SIM_READY: case NBM_SIM_PIN: case NBM_SIM_PUK: case NBM_SIM_ABSENT: return 1;
    case NBM_SIM_ERROR: return 2;
    default: return 0;
    }
}

/* DMS UIM Get State 0x01: 0 initialization completed, 1 locked or failed, 2 not present, 0xff unavailable */
static inline uint8_t nbm_dms_uim_state(const struct nbm_state *s)
{
    switch (nbm_sim_shown(s)) {
    case NBM_SIM_READY: return 0;
    case NBM_SIM_PIN: case NBM_SIM_PUK: case NBM_SIM_ERROR: return 1;
    case NBM_SIM_ABSENT: return 2;
    default: return 0xff;
    }
}

/* ------------------------------------------------------------------ UIM */
/* Card Status (TLV 0x10 of Get Card Status and of the Card Status indication), one slot, one USIM application, no AID; then
 * Apple's TLV 0x21 (one eSIM capability byte per slot, CommCenter expects two; 0 none). */
static inline uint32_t nbm_uim_card_status(const struct nbm_state *s, uint8_t *t, uint32_t room)
{
    uint8_t b[40];
    uint32_t k = 0, w = 0;
    const int sim = nbm_sim_shown(s);
    const int present = sim != NBM_SIM_ABSENT && sim != NBM_SIM_UNKNOWN;
    const int app = present && sim != NBM_SIM_ERROR;
    nbq_wr16(b, app ? 0x0000u : 0xffffu);                 /* GW primary: slot 0, app 0 */
    nbq_wr16(b + 2, 0xffffu); nbq_wr16(b + 4, 0xffffu); nbq_wr16(b + 6, 0xffffu);
    k = 8;
    b[k++] = 1;                                          /* cards */
    b[k++] = sim == NBM_SIM_ABSENT ? 0 : sim == NBM_SIM_ERROR ? 2 : 1;   /* card state: absent, error, present */
    b[k++] = 0; b[k++] = 0; b[k++] = 0;                  /* UPIN state, retries, PUK retries */
    b[k++] = sim == NBM_SIM_ERROR ? 7 : 0;               /* error code (technical problem) */
    b[k++] = app ? 1 : 0;                                /* applications */
    if (app) {
        const uint8_t pin1 = sim == NBM_SIM_PIN ? 1 : sim == NBM_SIM_PUK ? 4 : s->pin_lock == 1 ? 2 : 3;
        b[k++] = 2;                                      /* USIM */
        b[k++] = sim == NBM_SIM_READY ? 7 : sim == NBM_SIM_PIN ? 2 : sim == NBM_SIM_PUK ? 3 : 1;   /* ready, PIN, PUK, detected */
        b[k++] = sim == NBM_SIM_READY ? 2 : 0;           /* personalization: ready */
        b[k++] = 0; b[k++] = 0; b[k++] = 0;              /* feature, retries, unblock retries */
        b[k++] = 0;                                      /* AID length (unknown: AT gives none) */
        b[k++] = 0;                                      /* UPIN replaces PIN1 */
        b[k++] = pin1; b[k++] = 3; b[k++] = 10;          /* PIN1 state (1 enabled not verified, 2 verified, 3 disabled, 4 blocked), retries */
        b[k++] = 2; b[k++] = 3; b[k++] = 10;             /* PIN2 enabled not verified... reported as verified, 3/10 retries */
    }
    w += nbm_tlv(t + w, room - w, 0x10, b, k);
    const uint8_t caps[2] = { 0, 0 };
    w += nbm_tlv(t + w, room - w, 0x21, caps, 2);
    return w;
}

/* A Read Transparent (0x20) or Read Record (0x21) request, as AT+CRSM can carry it. */
struct nbm_uim_read {
    uint8_t session, record_read;
    uint16_t file, offset, length, record;
    uint8_t path_len;                    /* bytes, as in the request (u16 LE elements) */
    uint8_t path[16];
    uint8_t indication_token;            /* TLV 0x10 "response in indication" present: not handled here */
    uint8_t attributes;                  /* 0277f: Get File Attributes (AT+CRSM GET RESPONSE) */
};
/* 0 parsed; otherwise the QMI error to answer with */
static inline uint32_t nbm_uim_read_parse(uint32_t message, const uint8_t *sdu, uint32_t n, struct nbm_uim_read *r)
{
    uint32_t len = 0, tl;
    const uint8_t *v;
    if (n < 7u || 7u + nbq_rd16(sdu + 5) > n) return NBQ_ERR_MALFORMED_MESSAGE;
    const uint8_t *tlvs = sdu + 7;
    tl = nbq_rd16(sdu + 5);
    __builtin_memset(r, 0, sizeof *r);
    r->record_read = message == NBM_UIM_READ_RECORD;
    r->attributes = message == NBM_UIM_GET_FILE_ATTRIBUTES;
    if (!(v = nbq_tlv(tlvs, tl, 0x01, &len)) || len < 2) return NBQ_ERR_MISSING_ARGUMENT;
    r->session = v[0];
    if (!(v = nbq_tlv(tlvs, tl, 0x02, &len)) || len < 3 || v[2] > sizeof r->path || 3u + v[2] > len || (v[2] & 1u)) return NBQ_ERR_MISSING_ARGUMENT;
    r->file = nbq_rd16(v);
    r->path_len = v[2];
    __builtin_memcpy(r->path, v + 3, v[2]);
    if (r->attributes) {                 /* no read information; the token TLV of this message is 0x10 as well */
        r->indication_token = nbq_tlv(tlvs, tl, 0x10, &len) != 0;
        return 0;
    }
    if (!(v = nbq_tlv(tlvs, tl, 0x03, &len)) || len < 4) return NBQ_ERR_MISSING_ARGUMENT;
    if (r->record_read) { r->record = nbq_rd16(v); r->length = nbq_rd16(v + 2); }
    else { r->offset = nbq_rd16(v); r->length = nbq_rd16(v + 2); }
    r->indication_token = nbq_tlv(tlvs, tl, 0x10, &len) != 0;
    return 0;
}
static inline unsigned nbm_put_uint(char *o, unsigned at, unsigned room, uint32_t v)
{
    char d[12];
    unsigned k = 0;
    do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < sizeof d);
    while (k && at + 1 < room) o[at++] = d[--k];
    return at;
}
static inline unsigned nbm_put_str(char *o, unsigned at, unsigned room, const char *s)
{
    while (*s && at + 1 < room) o[at++] = *s++;
    return at;
}
/* The AT+CRSM command for a read; 0 when the session is not one AT+CRSM reaches (slot one, provisioning or card session) */
static inline int nbm_crsm_read_cmd(const struct nbm_uim_read *r, char *o, unsigned room)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned at = 0;
    /* sessions: 0 primary GW provisioning, 1 primary 1X provisioning, 6 card on slot 1 */
    if (r->session != 0 && r->session != 1 && r->session != 6) return 0;
    if (r->length > 255 || (!r->record_read && r->offset > 0x7fff) || (r->record_read && (r->record < 1 || r->record > 255))) return 0;
    at = nbm_put_str(o, at, room, r->attributes ? "AT+CRSM=192," : r->record_read ? "AT+CRSM=178," : "AT+CRSM=176,");
    at = nbm_put_uint(o, at, room, r->file);
    o[at++] = ',';
    if (r->attributes) {
        at = nbm_put_str(o, at, room, "0,0,");             /* GET RESPONSE: P1 P2 0, P3 0 (the card's whole FCP) */
    } else if (r->record_read) {
        at = nbm_put_uint(o, at, room, r->record);
        at = nbm_put_str(o, at, room, ",4,");
    } else {
        at = nbm_put_uint(o, at, room, (uint32_t)r->offset >> 8);
        o[at++] = ',';
        at = nbm_put_uint(o, at, room, r->offset & 0xffu);
        o[at++] = ',';
    }
    at = nbm_put_uint(o, at, room, r->length);
    /* <pathid>: the path below the MF ("select by path from MF", 27.007), hex, the leading 3F00 dropped */
    unsigned first = 0;
    if (r->path_len >= 2 && nbq_rd16(r->path) == 0x3f00u) first = 2;
    if (r->path_len > first) {
        at = nbm_put_str(o, at, room, ",,\"");
        for (unsigned i = first; i + 1 < r->path_len && at + 5 < room; i += 2) {
            const uint16_t e = nbq_rd16(r->path + i);
            o[at++] = hex[e >> 12 & 0xf]; o[at++] = hex[e >> 8 & 0xf]; o[at++] = hex[e >> 4 & 0xf]; o[at++] = hex[e & 0xf];
        }
        if (at + 1 < room) o[at++] = '"';
    }
    o[at] = 0;
    return at + 1 < room;
}
/* The TLVs of a read's answer: 0x10 card result {sw1, sw2}, 0x11 read result {u16 length, content} */
static inline uint32_t nbm_uim_read_answer(uint8_t sw1, uint8_t sw2, const uint8_t *data, uint32_t n, uint8_t *t, uint32_t room)
{
    const uint8_t sw[2] = { sw1, sw2 };
    uint32_t w = nbm_tlv(t, room, 0x10, sw, 2);
    if (sw1 == 0x90 || sw1 == 0x91 || sw1 == 0x92) {
        if (room < w + 5u + n) return 0;
        t[w] = 0x11; nbq_wr16(t + w + 1, n + 2u); nbq_wr16(t + w + 3, n);
        for (uint32_t i = 0; i < n; i++) t[w + 5 + i] = data[i];
        w += 5u + n;
    }
    return w;
}

/* 0277i: QMI Read Transparent/Read Record with length 0 means "the whole file/record". AT+CRSM with P3 0 is an APDU Le of 256, and
 * the MT6893 modem answers 256 bytes for every file (q10bt-q10bv: ICCID, IMSI, GID1... of 256 bytes, CommCenter "empty ICCID for a
 * ready SIM!!" 538 times). The length to ask for comes from the file's FCP (a GET RESPONSE first): the size from the offset on,
 * at most 256 (P3 0), or the record size; failing that, the size of the few files whose size the specs fix. 0: not known. */
static inline uint32_t nbm_whole_read_length(const struct nbm_uim_read *r, const struct nbm_fcp *f, int have_fcp)
{
    uint32_t want = 0;
    if (have_fcp && r->record_read && f->rec_size) want = f->rec_size;
    else if (have_fcp && !r->record_read && f->size) want = f->size > r->offset ? (uint32_t)f->size - r->offset : 0;
    else if (!r->record_read && r->offset == 0 && r->file == 0x2fe2) want = 10;      /* EF ICCID */
    else if (!r->record_read && r->offset == 0 && r->file == 0x6f07) want = 9;       /* EF IMSI */
    return want > 256 ? 256 : want;
}

/* ------------------------------------------------------------------ 0277f builders */
/* UIM Get File Attributes from a GET RESPONSE (AT+CRSM=192): 0x10 card result {sw1, sw2}; on success 0x11 {u16 size, u16 file id,
 * u8 type (0 transparent, 1 cyclic, 2 linear fixed, 3 DF, 4 MF), u16 record size, u16 record count, 5 x {u8 logic, u16 attributes}
 * (read, write, increase, deactivate, activate: "always", the card enforces them), u16 raw length, the FCP as the card gave it}.
 * The FCP (TS 102 221 11.1.1.3): 62 L { 82 file descriptor, 83 file id, 80 size, 81 total size, ... }. */
static inline uint32_t nbm_uim_attributes_answer(uint8_t sw1, uint8_t sw2, uint16_t file, const uint8_t *fcp, uint32_t n, uint8_t *t,
                                                 uint32_t room)
{
    const uint8_t sw[2] = { sw1, sw2 };
    uint32_t w = nbm_tlv(t, room, 0x10, sw, 2);
    if (sw1 != 0x90 && sw1 != 0x91 && sw1 != 0x92) return w;
    struct nbm_fcp f;
    (void)nbm_fcp_parse(fcp, n, file, &f);
    if (n > 256) n = 256;
    if (room < w + 3u + 26u + n) return w;
    uint8_t *b = t + w + 3;
    uint32_t k = 0;
    nbq_wr16(b + k, f.size); k += 2; nbq_wr16(b + k, f.id); k += 2; b[k++] = f.type;
    nbq_wr16(b + k, f.rec_size); k += 2; nbq_wr16(b + k, f.rec_count); k += 2;
    for (unsigned i = 0; i < 5; i++) { b[k++] = 0; nbq_wr16(b + k, 0); k += 2; }
    nbq_wr16(b + k, n); k += 2;
    for (uint32_t i = 0; i < n; i++) b[k++] = fcp[i];
    t[w] = 0x11; nbq_wr16(t + w + 1, k);
    return w + 3u + k;
}

/* NAS Get System Selection Preference: emergency off, GSM+UMTS+LTE, any roaming, automatic selection, CS+PS */
static inline uint32_t nbm_nas_sys_sel_pref(uint8_t *t, uint32_t room)
{
    uint8_t b[4];
    uint32_t w = nbm_tlv_u8(t, room, 0x10, 0);
    nbq_wr16(b, 0x04u | 0x08u | 0x10u); w += nbm_tlv(t + w, room - w, 0x11, b, 2);
    nbq_wr16(b, 0xffu); w += nbm_tlv(t + w, room - w, 0x14, b, 2);
    w += nbm_tlv_u8(t + w, room - w, 0x16, 0);
    nbq_wr32(b, 2); w += nbm_tlv(t + w, room - w, 0x18, b, 4);
    return w;
}

/* NAS Get PLMN Name 0x10 (3GPP EONS PLMN name): no SPN, short and long name = the operator name +COPS gives for the registered PLMN.
 * 0277j: encoding 1 is UCS2 little endian (libqmi QmiNasPlmnEncodingScheme; 0 is packed GSM 7-bit): 0277f put ASCII bytes under it and
 * CommCenter showed "VERY" as two CJK characters ("Operator name changed from '' to '䕖奒'", q10bw). The names are UCS2-LE now.
 * 0 when the request is for another PLMN or the name is not known (the caller answers INFO_UNAVAILABLE). */
static inline uint32_t nbm_nas_plmn_name(const struct nbm_state *s, uint16_t want_mcc, uint16_t want_mnc, uint8_t *t, uint32_t room)
{
    uint16_t mcc, mnc;
    unsigned digits, k = nbm_strlen(s->oper);
    if (!k || !nbm_plmn(s, &mcc, &mnc, &digits) || mcc != want_mcc || mnc != want_mnc) return 0;
    if (k > 32) k = 32;
    uint8_t b[2 + 4 + 64 + 4 + 64];
    uint32_t j = 0;
    b[j++] = 1; b[j++] = 0;                                      /* SPN: UCS2-LE, empty */
    for (unsigned n = 0; n < 2; n++) {                           /* short name, long name: UCS2-LE, no country initials, no spare bits */
        b[j++] = 1; b[j++] = 0; b[j++] = 0; b[j++] = (uint8_t)(2u * k);
        for (unsigned i = 0; i < k; i++) { b[j++] = (uint8_t)s->oper[i]; b[j++] = 0; }
    }
    (void)digits;
    return nbm_tlv(t, room, 0x10, b, j);
}

/* DMS Get Band Capabilities: 0x01 GSM 900/1800, WCDMA 2100/900 (QmiDmsBandCapability bits 7, 8, 9, 22, 49), 0x10 LTE bands 1, 3, 7, 8,
 * 20, 28, 38, 40, 41 (bit n-1), the bands this MT6893 modem's carrier set uses in Italy. */
static inline uint32_t nbm_dms_band_capabilities(uint8_t *t, uint32_t room)
{
    uint8_t b[8];
    const uint64_t gw = (1ull << 7) | (1ull << 8) | (1ull << 9) | (1ull << 22) | (1ull << 49);
    const uint64_t lte = (1ull << 0) | (1ull << 2) | (1ull << 6) | (1ull << 7) | (1ull << 19) | (1ull << 27) | (1ull << 37) | (1ull << 39) |
                         (1ull << 40);
    nbq_wr32(b, (uint32_t)gw); nbq_wr32(b + 4, (uint32_t)(gw >> 32));
    uint32_t w = nbm_tlv(t, room, 0x01, b, 8);
    nbq_wr32(b, (uint32_t)lte); nbq_wr32(b + 4, (uint32_t)(lte >> 32));
    w += nbm_tlv(t + w, room - w, 0x10, b, 8);
    return w;
}

/* DMS Get Time 0x01 {6-byte count of 1.25 ms since the GPS epoch 1980-01-06, u16 source 0 (device)}, from Unix seconds */
static inline uint32_t nbm_dms_time(uint64_t unix_ms, uint8_t *t, uint32_t room)
{
    const uint64_t gps_ms = unix_ms - 315964800000ull, count = gps_ms * 4u / 5u;
    uint8_t b[8];
    for (unsigned i = 0; i < 6; i++) b[i] = (uint8_t)(count >> (8 * i));
    nbq_wr16(b + 6, 0);
    return nbm_tlv(t, room, 0x01, b, 8);
}

/* NAS Signal Info indication (0x51): the LTE TLV of Get Signal Info, sent when it changes */
static inline uint32_t nbm_nas_signal_ind(const struct nbm_state *s, uint8_t *t, uint32_t room) { return nbm_nas_signal_info(s, t, room); }

/* 0277i: PDS Get GPS Service State 0x01 {u8 service state (0 off, 1 on), u8 tracking session (1 inactive)}: locationd's
 * CLQmiPdsManager::parseGpsServiceState needs this TLV ("Service state mandatory TLV unavailable" -> "Invalid Data" -> a baseband
 * reset request) */
static inline uint32_t nbm_pds_gps_service_state(uint8_t on, uint8_t *t, uint32_t room)
{
    const uint8_t b[2] = { (uint8_t)(on ? 1 : 0), 1 };
    return nbm_tlv(t, room, 0x01, b, 2);
}

/* 0277i: MFSE RFS sync answer 0x10 {u32 sync token}. QMIRFSCommandDriver keeps the last token (0 after every start): the same token
 * makes it report {status: true, dirty: false}, FSModule logs "No new data to sync; skipping rfs sync" and the sync counts as done. */
static inline uint32_t nbm_mfse_sync_answer(uint32_t token, uint8_t *t, uint32_t room)
{
    uint8_t b[4];
    nbq_wr32(b, token);
    return nbm_tlv(t, room, 0x10, b, 4);
}

/* ------------------------------------------------------------------ 0277l: cellular data, operator name */
/* WDS Packet Service Status, the response's 0x01 and the indication's {u8 status (1 disconnected, 2 connected), u8 reconfiguration
 * required}, 0x10 call end reason (QmiWdsCallEndReason: 1 unspecified, 2 client end, 3 no service), 0x12 IP family (4). */
static inline uint32_t nbm_wds_pkt_status_ind(uint8_t connected, uint8_t reconfigure, uint16_t end_reason, uint8_t *t, uint32_t room)
{
    const uint8_t st[2] = { (uint8_t)(connected ? 2 : 1), reconfigure };
    uint32_t w = nbm_tlv(t, room, 0x01, st, 2);
    if (!connected && end_reason) { uint8_t b[2]; nbq_wr16(b, end_reason); w += nbm_tlv(t + w, room - w, 0x10, b, 2); }
    w += nbm_tlv_u8(t + w, room - w, 0x12, 4);
    return w;
}
/* WDS Start Network failure: 0x10 call end reason, 0x11 verbose {u16 type (6: 3GPP specification), u16 reason (TS 24.008 cause)} */
static inline uint32_t nbm_wds_call_failed(uint16_t end_reason, uint16_t verbose_type, uint16_t verbose_reason, uint8_t *t, uint32_t room)
{
    uint8_t b[4];
    nbq_wr16(b, end_reason);
    uint32_t w = nbm_tlv(t, room, 0x10, b, 2);
    nbq_wr16(b, verbose_type); nbq_wr16(b + 2, verbose_reason);
    w += nbm_tlv(t + w, room - w, 0x11, b, 4);
    return w;
}
/* The point-to-point peer a Qualcomm modem reports for an IPv4 PDN: the other host of the address's /30 */
static inline uint32_t nbm_wds_gateway(uint32_t ip) { return (ip & ~3u) | ((ip & 3u) == 1u ? 2u : 1u); }
/* WDS Get Current (Runtime) Settings of an IPv4 PDN, addresses host order as u32 LE: 0x14 APN, 0x15/0x16 DNS, 0x1E address, 0x20
 * gateway, 0x21 mask (/30), 0x29 MTU, 0x2B IP family 4 */
static inline uint32_t nbm_wds_runtime(uint32_t ip, uint32_t dns1, uint32_t dns2, uint32_t mtu, const char *apn, uint8_t *t, uint32_t room)
{
    uint8_t b[4];
    uint32_t w = 0;
    if (apn && *apn) w += nbm_tlv_str(t + w, room - w, 0x14, apn);
    if (dns1) { nbq_wr32(b, dns1); w += nbm_tlv(t + w, room - w, 0x15, b, 4); }
    if (dns2) { nbq_wr32(b, dns2); w += nbm_tlv(t + w, room - w, 0x16, b, 4); }
    nbq_wr32(b, ip); w += nbm_tlv(t + w, room - w, 0x1e, b, 4);
    nbq_wr32(b, nbm_wds_gateway(ip)); w += nbm_tlv(t + w, room - w, 0x20, b, 4);
    nbq_wr32(b, 0xfffffffcu); w += nbm_tlv(t + w, room - w, 0x21, b, 4);
    nbq_wr32(b, mtu ? mtu : 1500u); w += nbm_tlv(t + w, room - w, 0x29, b, 4);
    w += nbm_tlv_u8(t + w, room - w, 0x2b, 4);
    return w;
}
/* WDS Get LTE Attach Parameters: 0x10 APN, 0x11 IP support type (0 IPv4), 0x12 OTA attach performed */
static inline uint32_t nbm_wds_lte_attach(const char *apn, uint8_t *t, uint32_t room)
{
    uint32_t w = nbm_tlv_str(t, room, 0x10, apn ? apn : "");
    w += nbm_tlv_u8(t + w, room - w, 0x11, 0);
    w += nbm_tlv_u8(t + w, room - w, 0x12, 1);
    return w;
}
/* APN equality as networks treat it: ASCII case-insensitive */
static inline int nbm_apn_eq(const char *a, const uint8_t *b, uint32_t n)
{
    uint32_t i = 0;
    for (; i < n && a[i]; i++) {
        uint8_t x = (uint8_t)a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (uint8_t)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (uint8_t)(y + 32);
        if (x != y) return 0;
    }
    return i == n && !a[i];
}
/* 0277m: AT+CGDCONT=1,"IP","<apn>" for the PDN bring-up; the APN only of letters, digits, '.', '-' and '_' (3GPP 23.003 labels),
 * 1..62 characters; 0 when it does not fit or is not such a name */
static inline uint32_t nbm_pdn_define(char *out, uint32_t room, const char *apn)
{
    static const char head[] = "AT+CGDCONT=1,\"IP\",\"";
    uint32_t k = 0, n = 0;
    if (!apn || !out) return 0;
    for (; apn[n]; n++) {
        const char c = apn[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return 0;
        if (n >= 62) return 0;
    }
    if (!n || sizeof head - 1 + n + 2 > room) return 0;
    for (uint32_t i = 0; i + 1 < sizeof head; i++) out[k++] = head[i];
    for (uint32_t i = 0; i < n; i++) out[k++] = apn[i];
    out[k++] = '"';
    out[k] = 0;
    return k;
}
/* 0277x: AT+CGDCONT=<cid>,"IP","<apn>" for cid 1..9 (nbm_pdn_define with another cid) */
static inline uint32_t nbm_pdn_define_cid(char *out, uint32_t room, const char *apn, unsigned cid)
{
    if (cid < 1 || cid > 9) return 0;
    const uint32_t k = nbm_pdn_define(out, room, apn);
    if (k > 11) out[11] = (char)('0' + cid);                 /* "AT+CGDCONT=" is 11 characters */
    return k;
}
/* DSD System Status (Get 0x24 response and indication 0x26): 0x10 available systems [u8 n, {u32 network type (0 3GPP), u32 RAT,
 * u64 SO mask}]: LTE = RAT 3, SO 0x1000 (LTE FDD); 0277t: WCDMA = RAT 1, SO 0x01 WCDMA | 0x02 HSDPA | 0x04 HSUPA | 0x40 HSPA; GSM =
 * RAT 2, SO 0x80 GPRS / 0x100 EDGE | 0x200 GSM; none while not registered */
static inline uint32_t nbm_dsd_system_status(const struct nbm_state *s, uint8_t *t, uint32_t room)
{
    uint8_t radio = 0, b[1 + 16];
    int roaming = 0;
    const uint8_t reg = nbm_reg(s, &radio, &roaming);
    const int act = nbm_reg_act(s);
    uint32_t k = 0;
    if (reg == 1 && (radio == 8 || radio == 5 || radio == 4)) {
        const uint32_t rat = radio == 8 ? 3u : radio == 5 ? 1u : 2u;
        const uint32_t so = radio == 8 ? 0x1000u : radio == 5 ? (act == 4 ? 0x03u : act == 5 ? 0x05u : act == 6 ? 0x47u : 0x01u)
                                                              : (act == 3 ? 0x300u : 0x280u);
        b[k++] = 1;
        nbq_wr32(b + k, 0); k += 4;
        nbq_wr32(b + k, rat); k += 4;
        nbq_wr32(b + k, so); nbq_wr32(b + k + 4, 0); k += 8;
    } else {
        b[k++] = 0;
    }
    return nbm_tlv(t, room, 0x10, b, k);
}
/* NAS 0x61 current PLMN name indication as libCommCenterMCommandDrivers parses it: 0x11 SPN {u8 coding, u8 len, text}, 0x12 and 0x13
 * the long and short names {u8 coding, u8 country initials, u8 spare bits, u8 len, text} (coding 1 = UCS2 little endian), 0x1A u32
 * name source (NetworkNameSource: 0 kUnknown, 1 kOPL, 2 kCPHS, 3 kNITZ, 4 kTS25, 5 kMccMnc, 6 kSPN, 7 kERI): the operator name of
 * +COPS, as NITZ. 0 when not registered or no name. */
static inline uint32_t nbm_nas_current_plmn_name_ind(const struct nbm_state *s, uint8_t *t, uint32_t room)
{
    uint8_t radio = 0, b[4 + 64];
    int roaming = 0;
    unsigned k = nbm_strlen(s->oper);
    if (nbm_reg(s, &radio, &roaming) != 1 || !k) return 0;
    if (k > 32) k = 32;
    const uint8_t spn[2] = { 1, 0 };
    uint32_t w = nbm_tlv(t, room, 0x11, spn, 2);
    for (unsigned tl = 0x12; tl <= 0x13; tl++) {
        uint32_t j = 0;
        b[j++] = 1; b[j++] = 0; b[j++] = 0; b[j++] = (uint8_t)(2u * k);
        for (unsigned i = 0; i < k; i++) { b[j++] = (uint8_t)s->oper[i]; b[j++] = 0; }
        w += nbm_tlv(t + w, room - w, tl, b, j);
    }
    uint8_t src[4];
    nbq_wr32(src, 3);
    w += nbm_tlv(t + w, room - w, 0x1a, src, 4);
    return w;
}

/* ------------------------------------------------------------------ 0277r: SMS (WMS) and voice calls (VOICE)
 * Ids from Qualcomm's wireless_messaging_service / voice_service headers (libqmi data/qmi-service-{wms,voice}.json for the layouts).
 * SMS: 3GPP 23.040 PDUs carried as QMI "GW point-to-point" raw data (format 6): the SMSC address (24.008 form: length octet, TOA,
 * BCD digits; "00" = none) followed by the TPDU, as AT+CMGS/+CMT in PDU mode (27.005) carry them. Calls: 27.007 +CLCC and MediaTek's
 * +ECPI call progress. Nothing here prints; numbers and message bodies stay in the buffers the bridge hands over. */
#define NBM_WMS_EVENT_REPORT 0x0001u          /* Set Event Report, and the Event Report indication */
#define NBM_WMS_RAW_SEND 0x0020u
#define NBM_WMS_RAW_WRITE 0x0021u
#define NBM_WMS_RAW_READ 0x0022u
#define NBM_WMS_MODIFY_TAG 0x0023u
#define NBM_WMS_DELETE 0x0024u
#define NBM_WMS_GET_MESSAGE_PROTOCOL 0x0030u
#define NBM_WMS_LIST_MESSAGES 0x0031u
#define NBM_WMS_SET_ROUTES 0x0032u
#define NBM_WMS_GET_ROUTES 0x0033u
#define NBM_WMS_GET_SMSC_ADDRESS 0x0034u
#define NBM_WMS_SET_SMSC_ADDRESS 0x0035u
#define NBM_WMS_GET_STORE_MAX_SIZE 0x0036u
#define NBM_WMS_SEND_ACK 0x0037u
#define NBM_WMS_SET_MEMORY_STATUS 0x003bu
#define NBM_WMS_GET_BROADCAST_CONFIG 0x003eu
#define NBM_WMS_SET_PRIMARY_CLIENT 0x0045u
#define NBM_WMS_INDICATION_REGISTER 0x0047u
#define NBM_WMS_GET_TRANSPORT_LAYER_INFO 0x0048u
#define NBM_WMS_GET_TRANSPORT_NW_REG 0x004au
#define NBM_WMS_GET_INDICATION_REGISTER 0x004du
#define NBM_WMS_SERVICE_READY_IND 0x005du
#define NBM_WMS_SET_MESSAGE_WAITING 0x005fu
#define NBM_WMS_FORMAT_GW_PP 6u
#define NBM_WMS_MODE_GW 1u
#define NBM_WMS_TAG_MT_READ 0u
#define NBM_WMS_TAG_MT_NOT_READ 1u
#define NBM_WMS_TAG_MO_SENT 2u
#define NBM_WMS_TAG_MO_NOT_SENT 3u
#define NBM_QMI_ERR_WMS_DELIVERY_FAILURE 0x0038u
#define NBM_QMI_ERR_WMS_SMSC_ADDRESS 0x0049u
#define NBM_QMI_ERR_INVALID_INDEX 0x0031u
#define NBM_QMI_ERR_NO_ENTRY 0x0032u
#define NBM_QMI_ERR_STORAGE_FULL 0x0033u
#define NBM_QMI_ERR_NO_NETWORK_FOUND 0x000du
#define NBM_VOICE_INDICATION_REGISTER 0x0003u
#define NBM_VOICE_DIAL_CALL 0x0020u
#define NBM_VOICE_END_CALL 0x0021u
#define NBM_VOICE_ANSWER_CALL 0x0022u
#define NBM_VOICE_BURST_DTMF 0x0028u
#define NBM_VOICE_START_DTMF 0x0029u
#define NBM_VOICE_STOP_DTMF 0x002au
#define NBM_VOICE_ALL_CALL_STATUS_IND 0x002eu
#define NBM_VOICE_GET_ALL_CALL_INFO 0x002fu
#define NBM_VOICE_MANAGE_CALLS 0x0031u
#define NBM_VOICE_SET_CONFIG 0x0040u
#define NBM_VOICE_GET_CONFIG 0x0041u
enum { NBM_CALL_ORIGINATION = 1, NBM_CALL_INCOMING = 2, NBM_CALL_CONVERSATION = 3, NBM_CALL_ALERTING = 5, NBM_CALL_HOLD = 6,
       NBM_CALL_WAITING = 7, NBM_CALL_DISCONNECTING = 8, NBM_CALL_END = 9 };
enum { NBM_CALL_MODE_GSM = 2, NBM_CALL_MODE_UMTS = 3, NBM_CALL_MODE_LTE = 4 };

/* hex text (a PDU line) to bytes: the byte count, 0 for an odd length, a bad character or too little room */
static inline uint32_t nbm_hex_decode(const char *s, uint32_t n, uint8_t *out, uint32_t room)
{
    if (!n || (n & 1u) || n / 2u > room) return 0;
    for (uint32_t i = 0; i < n; i += 2) {
        const int h = nbm_hexdigit(s[i]), l = nbm_hexdigit(s[i + 1]);
        if (h < 0 || l < 0) return 0;
        out[i / 2u] = (uint8_t)(h << 4 | l);
    }
    return n / 2u;
}
/* bytes to upper-case hex, NUL terminated: the character count, 0 when it does not fit */
static inline uint32_t nbm_hex_encode(const uint8_t *b, uint32_t n, char *out, uint32_t room)
{
    static const char x[] = "0123456789ABCDEF";
    if (2u * n + 1u > room) return 0;
    for (uint32_t i = 0; i < n; i++) { out[2 * i] = x[b[i] >> 4]; out[2 * i + 1] = x[b[i] & 15u]; }
    out[2 * n] = 0;
    return 2u * n;
}
/* 23.038: does the data coding scheme pack the user data in 7-bit septets (UDL counts septets) */
static inline int nbm_sms_dcs_7bit(uint8_t dcs)
{
    if ((dcs & 0x80u) == 0) return (dcs & 0x20u) == 0 && (dcs & 0x0cu) == 0;    /* general / automatic deletion groups */
    if ((dcs & 0xf0u) == 0xf0u) return (dcs & 0x04u) == 0;                     /* data coding / message class */
    return (dcs & 0xf0u) == 0xc0u || (dcs & 0xf0u) == 0xd0u;                   /* message waiting, GSM 7 bit; 0xe0 UCS2 */
}
/* 23.040 SMS-SUBMIT: the TPDU's length from its own fields (callers want it == n), 0 when p[0..n) does not hold one */
static inline uint32_t nbm_sms_submit_len(const uint8_t *p, uint32_t n)
{
    uint32_t k = 0;
    if (n < 7) return 0;
    const uint8_t fo = p[k++];
    if ((fo & 3u) != 1u) return 0;                     /* TP-MTI SMS-SUBMIT */
    k++;                                               /* TP-MR */
    const uint32_t digits = p[k++];                    /* TP-DA: digit count, TOA, BCD */
    if (digits > 20u) return 0;
    k += 1u + (digits + 1u) / 2u;
    k += 2u;                                           /* TP-PID, TP-DCS */
    if (k > n) return 0;
    const uint8_t dcs = p[k - 1u];
    const uint32_t vpf = (uint32_t)(fo >> 3) & 3u;     /* TP-VP: none, enhanced (7), relative (1), absolute (7) */
    k += vpf == 2u ? 1u : vpf ? 7u : 0u;
    if (k >= n) return 0;
    const uint32_t udl = p[k++];
    const uint32_t octets = nbm_sms_dcs_7bit(dcs) ? (udl * 7u + 7u) / 8u : udl;
    if (octets > 140u || k + octets > n) return 0;
    return k + octets;
}
/* The raw data of a QMI Raw Send (GW PP): the length of its SMSC part (0 = none, the TPDU starts at once; Qualcomm and libqmi put the
 * SMSC first, "00" when the SIM's is meant), -1 when it parses neither way */
static inline int nbm_sms_smsc_part(const uint8_t *p, uint32_t n)
{
    if (n && nbm_sms_submit_len(p, n) == n) return 0;
    if (n >= 2u) {
        const uint32_t l = p[0];
        if (l <= 11u && 1u + l < n && (l == 0 || (p[1] & 0x80u)) && nbm_sms_submit_len(p + 1 + l, n - 1u - l) == n - 1u - l)
            return (int)(1u + l);
    }
    return -1;
}
/* AT+CMGS=<TPDU length> and its PDU text (the request's SMSC part, or "00": the SIM's SMSC). The TPDU length, 0 when the raw data
 * is no SMS-SUBMIT or does not fit */
static inline uint32_t nbm_sms_cmgs(const uint8_t *raw, uint32_t n, char *cmd, uint32_t cmd_room, char *pdu, uint32_t pdu_room)
{
    const int sp = nbm_sms_smsc_part(raw, n);
    if (sp < 0 || cmd_room < 16u) return 0;
    const uint32_t tpdu = n - (uint32_t)sp;
    uint32_t w = 0;
    if (!sp) { if (pdu_room < 3u) return 0; pdu[0] = pdu[1] = '0'; w = 2; }
    if (!nbm_hex_encode(raw, n, pdu + w, pdu_room - w)) return 0;
    static const char h[] = "AT+CMGS=";
    uint32_t k = 0;
    for (; h[k]; k++) cmd[k] = h[k];
    k = nbm_put_uint(cmd, k, cmd_room, tpdu);
    cmd[k] = 0;
    return tpdu;
}
/* 0277t: the TPDU of SMSC part + TPDU raw data (its offset), 0 when there is none. Transfer-route MT messages go to CommCenter
 * without the SMSC part (Qualcomm's layout, ModemManager's "transfer route" PDUs; q10cj: CommCenter "Decoding 3GPP SMS failed" for
 * every MT message sent with it); stored ones (Raw Read) keep it. */
static inline uint32_t nbm_sms_tpdu(const uint8_t *raw, uint32_t n, uint32_t *off)
{
    if (!n || 1u + raw[0] >= n || raw[0] > 11u) return 0;
    *off = 1u + raw[0];
    return n - *off;
}
/* the SMS-DELIVER's message class from its DCS (0..3, 4 none; status reports and anything unparsed: 4); raw = SMSC part + TPDU */
static inline unsigned nbm_sms_class(const uint8_t *raw, uint32_t n)
{
    if (!n || 1u + raw[0] >= n) return 4;
    const uint8_t *p = raw + 1 + raw[0];
    const uint32_t m = n - 1u - raw[0];
    if ((p[0] & 3u) != 0) return 4;                    /* not an SMS-DELIVER */
    const uint32_t k = 2u + 1u + (p[1] + 1u) / 2u + 1u; /* fo, OA (count, TOA, BCD), PID: the DCS is at k */
    if (k >= m) return 4;
    const uint8_t dcs = p[k];
    if ((dcs & 0xc0u) == 0 && (dcs & 0x10u)) return dcs & 3u;
    if ((dcs & 0xf0u) == 0xf0u) return dcs & 3u;
    return 4;
}
/* the last decimal number of an AT line ("+CMT: ,23" -> 23, "+CMTI: \"SM\",4" -> 4), -1 when it does not end with one */
static inline long nbm_last_number(const char *l)
{
    unsigned n = nbm_strlen(l);
    while (n && (l[n - 1] == ' ' || l[n - 1] == '\r')) n--;
    unsigned b = n;
    while (b && l[b - 1] >= '0' && l[b - 1] <= '9') b--;
    if (b == n || n - b > 9u) return -1;
    long v = 0;
    for (unsigned i = b; i < n; i++) v = v * 10 + (l[i] - '0');
    return v;
}
/* "+CMGS: <mr>" in an AT answer: the message reference, -1 without one */
static inline long nbm_cmgs_mr(const char *text, unsigned len)
{
    for (unsigned i = 0; i + 6u < len; i++)
        if ((i == 0 || text[i - 1] == '\n') && nbm_prefix(text + i, "+CMGS:")) {
            long v = 0;
            unsigned j = i + 6u, d = 0;
            while (j < len && text[j] == ' ') j++;
            for (; j < len && text[j] >= '0' && text[j] <= '9' && d < 6; j++, d++) v = v * 10 + (text[j] - '0');
            return d ? v : -1;
        }
    return -1;
}
/* the number of "+CMS ERROR: <n>" / "+CME ERROR: <n>" ending an AT answer, -1 for a bare ERROR or a text error */
static inline long nbm_at_error_code(const char *text, unsigned len)
{
    unsigned b = len;
    while (b && text[b - 1] == '\n') b--;
    unsigned s = b;
    while (s && text[s - 1] != '\n') s--;
    if (b - s < 12u || !(nbm_prefix(text + s, "+CMS ERROR:") || nbm_prefix(text + s, "+CME ERROR:"))) return -1;
    long v = 0;
    unsigned j = s + 11u, d = 0;
    while (j < b && text[j] == ' ') j++;
    for (; j < b && text[j] >= '0' && text[j] <= '9' && d < 6; j++, d++) v = v * 10 + (text[j] - '0');
    return d && j == b ? v : -1;
}
/* Raw Send failure TLVs from a +CMS ERROR (27.005 3.2.5: < 128 RP causes of 24.011, 128..255 TP-FCS of 23.040, >= 300 ME/network
 * errors, -1 bare ERROR or timeout): 0x12 GW cause {u16 RP cause, u8 TP cause}, 0x13 delivery failure type (0 temporary, 1 permanent);
 * *error the QMI error (WMS delivery failure, or SMSC address) */
static inline uint32_t nbm_wms_send_failure(long cms, uint8_t *t, uint32_t room, uint32_t *error)
{
    uint8_t g[3] = { 0, 0, 0 };
    uint32_t w = 0;
    int permanent = 0;
    *error = cms == 330 ? NBM_QMI_ERR_WMS_SMSC_ADDRESS : NBM_QMI_ERR_WMS_DELIVERY_FAILURE;
    if (cms >= 0 && cms < 128) {
        nbq_wr16(g, (uint32_t)cms);
        permanent = cms == 1 || cms == 8 || cms == 10 || cms == 21 || cms == 29 || cms == 30 || cms == 50 || cms == 69 || (cms >= 95 && cms <= 111);
        w += nbm_tlv(t + w, room - w, 0x12, g, 3);
    } else if (cms >= 128 && cms < 256) {
        g[2] = (uint8_t)cms;
        permanent = cms != 0xc0 && cms != 0xc1 && cms != 0xd3;
        w += nbm_tlv(t + w, room - w, 0x12, g, 3);
    } else if (cms == 300 || cms == 301 || cms == 302 || cms == 303 || cms == 304 || cms == 305 || cms == 330) {
        permanent = 1;                                  /* ME failure, not allowed, not supported, bad PDU, SMSC unknown */
    }
    w += nbm_tlv_u8(t + w, room - w, 0x13, (uint32_t)permanent);
    return w;
}
/* Event Report, transfer route: 0x11 {u8 ack indicator (0 the client acknowledges with Send Ack, 1 not needed), u32 transaction id,
 * u8 format, u16 length, raw data}, 0x12 message mode GW */
static inline uint32_t nbm_wms_mt_transfer(uint32_t ack_indicator, uint32_t txn, const uint8_t *raw, uint32_t n, uint8_t *t, uint32_t room)
{
    if (room < 11u + n + 4u || n > 0xffffu - 8u) return 0;
    t[0] = 0x11; nbq_wr16(t + 1, 8u + n);
    t[3] = (uint8_t)ack_indicator; nbq_wr32(t + 4, txn); t[8] = NBM_WMS_FORMAT_GW_PP; nbq_wr16(t + 9, n);
    for (uint32_t i = 0; i < n; i++) t[11 + i] = raw[i];
    uint32_t w = 11u + n;
    w += nbm_tlv_u8(t + w, room - w, 0x12, NBM_WMS_MODE_GW);
    return w;
}
/* Event Report, stored: 0x10 {u8 storage type (0 UIM, 1 NV), u32 index}, 0x12 message mode GW */
static inline uint32_t nbm_wms_mt_stored(uint32_t storage, uint32_t index, uint8_t *t, uint32_t room)
{
    uint8_t b[5];
    b[0] = (uint8_t)storage; nbq_wr32(b + 1, index);
    uint32_t w = nbm_tlv(t, room, 0x10, b, 5);
    w += nbm_tlv_u8(t + w, room - w, 0x12, NBM_WMS_MODE_GW);
    return w;
}
/* Raw Read answer: 0x01 {u8 tag, u8 format, u16 length, raw data} */
static inline uint32_t nbm_wms_raw_read_answer(uint32_t tag, const uint8_t *raw, uint32_t n, uint8_t *t, uint32_t room)
{
    if (room < 7u + n) return 0;
    t[0] = 0x01; nbq_wr16(t + 1, 4u + n); t[3] = (uint8_t)tag; t[4] = NBM_WMS_FORMAT_GW_PP; nbq_wr16(t + 5, n);
    for (uint32_t i = 0; i < n; i++) t[7 + i] = raw[i];
    return 7u + n;
}
/* "+CSCA: \"<address>\",<type>" -> the address without '+' and its type (145 international, 129 unknown); 0 without one */
static inline int nbm_csca(const char *text, unsigned len, char *digits, unsigned room, unsigned *type)
{
    for (unsigned i = 0; i + 6u < len; i++) {
        if (!((i == 0 || text[i - 1] == '\n') && nbm_prefix(text + i, "+CSCA:"))) continue;
        unsigned j = i + 6u, k = 0;
        int plus = 0;
        while (j < len && (text[j] == ' ' || text[j] == '"')) j++;
        if (j < len && text[j] == '+') { plus = 1; j++; }
        for (; j < len && text[j] >= '0' && text[j] <= '9'; j++) if (k + 1u < room) digits[k++] = text[j];
        digits[k] = 0;
        while (j < len && text[j] != ',' && text[j] != '\n') j++;
        unsigned t = 0;
        if (j < len && text[j] == ',') for (j++; j < len && text[j] >= '0' && text[j] <= '9' && t < 1000u; j++) t = t * 10u + (unsigned)(text[j] - '0');
        *type = plus ? 145u : t ? t : 129u;
        return k != 0;
    }
    return 0;
}
/* Get SMSC Address answer: 0x01 {char type[3] (decimal, "145"), u8 length, digits} */
static inline uint32_t nbm_wms_smsc_answer(const char *digits, unsigned type, uint8_t *t, uint32_t room)
{
    uint8_t b[4 + 24];
    const unsigned n = nbm_strlen(digits) < 21u ? nbm_strlen(digits) : 21u;
    b[0] = (uint8_t)('0' + type / 100u % 10u); b[1] = (uint8_t)('0' + type / 10u % 10u); b[2] = (uint8_t)('0' + type % 10u);
    b[3] = (uint8_t)n;
    for (unsigned i = 0; i < n; i++) b[4 + i] = (uint8_t)digits[i];
    return nbm_tlv(t, room, 0x01, b, 4u + n);
}
/* AT+CSCA="<+digits>",<type> from Set SMSC Address (0x01 the address, 0x10 optional type "145"/"129"); 0 when it is no number */
static inline uint32_t nbm_csca_set_cmd(const uint8_t *a, uint32_t n, const uint8_t *type, uint32_t tn, char *out, uint32_t room)
{
    static const char h[] = "AT+CSCA=\"";
    uint32_t k = 0, digits = 0, i = 0;
    int intl = tn == 3 && type[0] == '1' && type[1] == '4' && type[2] == '5';
    if (n && a[0] == '+') { intl = 1; i = 1; }
    if (room < sizeof h + 32u) return 0;
    for (; h[k]; k++) out[k] = h[k];
    if (intl) out[k++] = '+';
    for (; i < n; i++) {
        if (a[i] < '0' || a[i] > '9' || ++digits > 20u) return 0;
        out[k++] = (char)a[i];
    }
    if (!digits) return 0;
    out[k++] = '"'; out[k++] = ',';
    k = nbm_put_str(out, k, room, intl ? "145" : "129");
    out[k] = 0;
    return k;
}

/* ---- calls */
struct nbm_call {
    uint8_t id, state, dir, mode, mpty, pi;             /* QMI values; id = the modem's call index (1..7) */
    uint16_t end_reason;                                /* QMI call end reason for state END */
    char number[NBM_QLEN];                              /* as +CLCC gave it ("+39..." when international) */
};
/* "+CLCC: <id>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>]" -> a call (voice calls only: mode 0); 0 when the line is not one */
static inline int nbm_clcc(const char *l, struct nbm_call *c)
{
    static const uint8_t state[6] = { NBM_CALL_CONVERSATION, NBM_CALL_HOLD, NBM_CALL_ORIGINATION, NBM_CALL_ALERTING, NBM_CALL_INCOMING,
                                      NBM_CALL_WAITING };
    long v[NBM_FIELDS];
    char q[NBM_FIELDS][NBM_QLEN];
    if (!nbm_prefix(l, "+CLCC:")) return 0;
    const int n = nbm_fields(l + 6, v, q);
    if (n < 5 || v[0] < 1 || v[0] > 7 || v[1] < 0 || v[1] > 1 || v[2] < 0 || v[2] > 5 || v[3] != 0) return 0;
    __builtin_memset(c, 0, sizeof *c);
    c->id = (uint8_t)v[0];
    c->dir = v[1] ? 2 : 1;
    c->state = state[v[2]];
    c->mpty = v[4] == 1;
    if (n >= 6 && v[5] == -2) {
        unsigned k = 0;
        const int intl = n >= 7 && v[6] == 145 && q[5][0] != '+';
        if (intl && q[5][0]) c->number[k++] = '+';
        for (unsigned i = 0; q[5][i] && k + 1u < sizeof c->number; i++) {
            const char ch = q[5][i];
            if ((ch >= '0' && ch <= '9') || ch == '+' || ch == '*' || ch == '#') c->number[k++] = ch;
        }
        c->number[k] = 0;
    }
    c->pi = c->number[0] ? 0 : 2;                       /* allowed, or not available */
    return 1;
}
/* numeric fields of an AT line after its prefix ("+ECPI: 1,133,..."): numbers in v (-1 empty, -2 quoted), the count (at most max) */
static inline int nbm_numbers(const char *s, long *v, int max)
{
    int n = 0;
    while (*s && *s != ':') s++;
    if (*s) s++;
    while (n < max) {
        long val = -1;
        int neg = 0;
        while (*s == ' ') s++;
        if (*s == '"') {
            for (s++; *s && *s != '"'; s++) {}
            if (*s) s++;
            val = -2;
        } else {
            if (*s == '-') { neg = 1; s++; }
            if (*s >= '0' && *s <= '9')
                for (val = 0; *s >= '0' && *s <= '9' && val < 100000000; s++) val = val * 10 + (*s - '0');
            if (neg && val > 0) val = -val;
        }
        v[n++] = val;
        while (*s && *s != ',') s++;
        if (*s != ',') break;
        s++;
    }
    return n;
}
/* MediaTek "+ECPI: <id>,<msg>,<ibt>,<tch>,<dir>,<call mode>[,<number>,<toa>,<pau>[,<cause>]]": msg 0 setup (MT), 2 alert, 6 connected,
 * 129 all calls disconnected, 130 MO call id assigned, 131 held, 132 active, 133 disconnected (cause = 24.008 CC cause). 0: no ECPI */
static inline int nbm_ecpi(const char *l, int *id, int *msg, int *call_mode, int *cause)
{
    long v[12];
    if (!nbm_prefix(l, "+ECPI:")) return 0;
    const int n = nbm_numbers(l, v, 12);
    if (n < 2 || v[0] < 0 || v[1] < 0) return 0;
    *id = (int)v[0];
    *msg = (int)v[1];
    *call_mode = n >= 6 ? (int)v[5] : -1;
    *cause = n >= 10 && v[9] >= 0 ? (int)v[9] : -1;
    return 1;
}
/* 24.008 CC cause -> QMI call end reason (voice_service: 141 unassigned number ... the GW causes), default normal call clearing */
static inline uint16_t nbm_cc_cause_end_reason(int cause)
{
    static const uint8_t cc[] = { 1, 3, 6, 8, 16, 17, 18, 19, 21, 22, 25, 27, 28, 29, 30, 31, 34, 38, 41, 42, 43, 44, 47, 49, 50, 55, 57, 58,
                                  63, 65, 68, 69, 70, 79, 81, 87, 88, 91, 95, 96, 97, 98, 99, 100, 101, 102, 111, 127 };
    static const uint8_t qmi[] = { 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162,
                                   163, 164, 165, 166, 167, 168, 169, 171, 170, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183,
                                   184, 185, 186, 187, 188 };
    for (unsigned i = 0; i < sizeof cc; i++) if (cc[i] == cause) return qmi[i];
    return 145;
}
/* All Call Status indication (get_all 0) or Get All Call Info answer (get_all 1): call info {u8 n, n x {id, state, type 0 voice, dir,
 * mode, multiparty, ALS 0}} (0x01 / 0x10), remote party numbers {u8 n, n x {id, presentation, u8 len, digits}} (0x10 / 0x11), alerting
 * type {u8 n, n x {id, 1 local ringback}} (0x12 / 0x13) for alerting calls, call end reasons {u8 n, n x {id, u16 reason}} (indication
 * 0x14) for ended calls */
static inline uint32_t nbm_voice_calls(const struct nbm_call *c, unsigned n, int get_all, uint8_t *t, uint32_t room)
{
    uint8_t b[1 + 7 * 8 + 8 * (3 + NBM_QLEN)];
    uint32_t w = 0, k;
    unsigned m = 0;
    if (n > 8) n = 8;
    b[0] = (uint8_t)n; k = 1;
    for (unsigned i = 0; i < n; i++) {
        b[k++] = c[i].id; b[k++] = c[i].state; b[k++] = 0; b[k++] = c[i].dir; b[k++] = c[i].mode; b[k++] = c[i].mpty; b[k++] = 0;
    }
    w += nbm_tlv(t + w, room - w, get_all ? 0x10 : 0x01, b, k);
    if (!n) return w;
    b[0] = (uint8_t)n; k = 1;
    for (unsigned i = 0; i < n; i++) {
        const unsigned l = nbm_strlen(c[i].number);
        b[k++] = c[i].id; b[k++] = c[i].pi; b[k++] = (uint8_t)l;
        for (unsigned j = 0; j < l; j++) b[k++] = (uint8_t)c[i].number[j];
    }
    w += nbm_tlv(t + w, room - w, get_all ? 0x11 : 0x10, b, k);
    k = 1; m = 0;
    for (unsigned i = 0; i < n; i++) if (c[i].state == NBM_CALL_ALERTING) { b[k++] = c[i].id; b[k++] = 1; m++; }
    if (m) { b[0] = (uint8_t)m; w += nbm_tlv(t + w, room - w, get_all ? 0x13 : 0x12, b, k); }
    k = 1; m = 0;
    for (unsigned i = 0; i < n && !get_all; i++)
        if (c[i].state == NBM_CALL_END) { b[k++] = c[i].id; nbq_wr16(b + k, c[i].end_reason); k += 2; m++; }
    if (m) { b[0] = (uint8_t)m; w += nbm_tlv(t + w, room - w, 0x14, b, k); }
    return w;
}
/* a dialable number for ATD (digits, '+', '*', '#'; 1..40), the count; 0 when it is not one */
static inline uint32_t nbm_dial_number(const uint8_t *v, uint32_t n, char *out, uint32_t room)
{
    if (!n || n > 40u || n + 1u > room) return 0;
    for (uint32_t i = 0; i < n; i++) {
        const char c = (char)v[i];
        if (!((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#')) return 0;
        out[i] = c;
    }
    out[n] = 0;
    return n;
}
/* the last two digits of a number for the log ("..34"), never more */
static inline void nbm_tail2(const char *num, char out[3])
{
    const unsigned n = nbm_strlen(num);
    out[0] = n >= 2 ? num[n - 2] : '-';
    out[1] = n >= 1 ? num[n - 1] : '-';
    out[2] = 0;
}

#endif /* NORD2_BB_QMI_H */
