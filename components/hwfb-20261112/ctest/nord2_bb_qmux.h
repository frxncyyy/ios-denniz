/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * NORD2 BB: the QMUX side of the virtual Mav17 baseband. ABM speaks QMUX on the qmux1/qmux2 channels of what it takes for a PCIe
 * Qualcomm modem; there is no Qualcomm modem behind it, every service is local and every request is answered in the kernel
 * (Nord2Baseband.cpp, from the MediaTek modem's AT state where it can be, "unhandled" with an error otherwise).
 *
 * Derived from QcomBaseband baseband/coachz_bb_qmux.h (HoshinoYumeka @chiba233, Trogdor-ipadOS 58b5bc8b, BSD-2-Clause): the QMUX
 * framing, the CTL answers (GetClientId per channel and service, ReleaseClientId, Sync, GetVersionInfo, anything else accepted) and
 * the client-id rules are kept; the QRTR server table is replaced by a fixed list of "announced" services. Portable: kernel and the
 * host test (test/nord2_bb_host_test.c).
 *
 * Framing: I/F type 1, length (excluding the I/F byte, LE), control flags (0x00 from the host, 0x80 from a service), service, client
 * id, then the SDU. CTL (service 0): flags, 8-bit transaction, message, length. Every other service: flags, 16-bit transaction,
 * message, length, TLVs (type u8, length u16 LE).
 */
#ifndef NORD2_BB_QMUX_H
#define NORD2_BB_QMUX_H

#include <stdint.h>

#define NBQ_IF_TYPE 0x01u
#define NBQ_HDR 6u
#define NBQ_FLAG_SERVICE 0x80u
#define NBQ_CTL_HDR 6u
#define NBQ_SDU_HDR 7u
#define NBQ_CTL_RESPONSE 0x01u
#define NBQ_CTL_INDICATION 0x02u
#define NBQ_QMI_REQUEST 0x00u
#define NBQ_QMI_RESPONSE 0x02u
#define NBQ_QMI_INDICATION 0x04u

#define NBQ_CTL_SET_INSTANCE_ID 0x0020u
#define NBQ_CTL_GET_VERSION_INFO 0x0021u
#define NBQ_CTL_GET_CLIENT_ID 0x0022u
#define NBQ_CTL_RELEASE_CLIENT_ID 0x0023u
#define NBQ_CTL_SYNC 0x0027u

/* libqmi qmi-errors.h (QmiProtocolError) */
#define NBQ_ERR_MALFORMED_MESSAGE 1u
#define NBQ_ERR_INTERNAL 3u
#define NBQ_ERR_CLIENT_IDS_EXHAUSTED 5u
#define NBQ_ERR_INVALID_CLIENT_ID 7u
#define NBQ_ERR_MISSING_ARGUMENT 17u
#define NBQ_ERR_INVALID_SERVICE_TYPE 31u
#define NBQ_ERR_INVALID_ARG 48u
#define NBQ_ERR_DEVICE_NOT_READY 52u
#define NBQ_ERR_INVALID_QMI_CMD 71u          /* what a Qualcomm firmware answers for a message it does not know */
#define NBQ_ERR_INFO_UNAVAILABLE 74u
#define NBQ_ERR_SIM_FILE_NOT_FOUND 80u
#define NBQ_ERR_NOT_SUPPORTED 94u

/* QMI service numbers (libqmi QmiService); Apple's private services sit at 0xe1 and up (libQMIParser: AUDIO 0xe1, BSP 0xe2,
 * VINYL 0xe5, ... SFT 0xea; iOS 16 also knows CIQ, AWD, MFSE, Mav 5WI, ELQM, Persistent Device, Mav P2P, BSP APPS, MSE, DFS, Stewie). */
#define NBQ_SVC_CTL 0x00u
#define NBQ_SVC_WDS 0x01u
#define NBQ_SVC_DMS 0x02u
#define NBQ_SVC_NAS 0x03u
#define NBQ_SVC_QOS 0x04u
#define NBQ_SVC_WMS 0x05u
#define NBQ_SVC_VOICE 0x09u
#define NBQ_SVC_CAT2 0x0au
#define NBQ_SVC_UIM 0x0bu
#define NBQ_SVC_PBM 0x0cu
#define NBQ_SVC_WDA 0x1au
#define NBQ_SVC_BSP 0xe2u
#define NBQ_SVC_VINYL 0xe5u

#define NBQ_CHANNELS 2u                     /* qmux1, qmux2 */
#define NBQ_MAX_CLIENTS 96u                 /* the reference saw 53 clients at boot */

static inline int nbq_private_service(uint32_t service) { return service >= 0xe0u && service <= 0xfeu; }

/* The services this "modem" announces (GetVersionInfo) and gives clients for in the core-only mode (n2bb bit 1). Versions are
 * plausible for an MDM9x55-class firmware; nothing is known yet about what ABM checks. */
struct nbq_service { uint8_t service; uint16_t major, minor; const char *name; };
static const struct nbq_service nbq_services[] = {
    { 0x00, 1, 5, "CTL" },   { 0x01, 1, 67, "WDS" },  { 0x02, 1, 14, "DMS" },  { 0x03, 1, 25, "NAS" },   { 0x04, 1, 12, "QOS" },
    { 0x05, 1, 10, "WMS" },  { 0x06, 1, 13, "PDS" },  { 0x07, 1, 3, "AUTH" },  { 0x08, 1, 2, "AT" },     { 0x09, 2, 1, "VOICE" },
    { 0x0a, 2, 24, "CAT2" }, { 0x0b, 1, 46, "UIM" },  { 0x0c, 1, 4, "PBM" },   { 0x10, 2, 0, "LOC" },    { 0x11, 1, 0, "SAR" },
    { 0x12, 1, 0, "IMSS" },  { 0x14, 1, 0, "CSD" },   { 0x1a, 1, 16, "WDA" },  { 0x21, 1, 0, "IMSA" },   { 0x22, 1, 0, "COEX" },
    { 0x24, 1, 0, "PDC" },   { 0x2a, 1, 0, "DSD" },   { 0x2b, 1, 0, "SSCTL" }, { 0x2f, 1, 0, "DPM" },
};
#define NBQ_SERVICES (sizeof nbq_services / sizeof nbq_services[0])

static inline const char *nbq_service_name(uint32_t service)
{
    for (unsigned i = 0; i < NBQ_SERVICES; i++)
        if (nbq_services[i].service == service) return nbq_services[i].name;
    switch (service) {
    case 0x2c: return "MFSE";      /* 0277h: names as CommCenter's qmux log prints them */
    case 0xe1: return "AUDIO";
    case 0xe2: return "BSP";
    case 0xe4: return "AWD";
    case 0xe5: return "VINYL";
    case 0xe7: return "ELQM";
    case 0xe8: return "P2P";
    case 0xea: return "SFT";
    default: return nbq_private_service(service) ? "PRIV" : "?";
    }
}

struct nbq_client { uint8_t used, channel, service, cid; };
struct nbq_bridge {
    struct nbq_client client[NBQ_MAX_CLIENTS];
    uint8_t last_cid[NBQ_CHANNELS][256];
    uint8_t grant_all;                      /* 1: a client id for any service 1..0xfe (default); 0: announced + private only */
    uint32_t ctl_other, dropped, refused, last_refused, last_ctl_other;
};

enum nbq_kind {
    NBQ_DROP,       /* nothing to send (malformed, unknown client, not a request); counted in dropped */
    NBQ_TO_HOST,    /* `out` holds `len` bytes: the CTL answer for `channel` */
    NBQ_LOCAL,      /* a request for client `cid` of `service`: the SDU is the input frame from NBQ_HDR, `len` bytes */
};
struct nbq_result { enum nbq_kind kind; uint32_t len, ctl_message; uint8_t channel, service, cid; };

static inline void nbq_init(struct nbq_bridge *b, int grant_all)
{
    __builtin_memset(b, 0, sizeof *b);
    b->grant_all = (uint8_t)(grant_all != 0);
}

static inline uint16_t nbq_rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t nbq_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void nbq_wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void nbq_wr32(uint8_t *p, uint32_t v) { nbq_wr16(p, v); nbq_wr16(p + 2, v >> 16); }

static inline const struct nbq_service *nbq_service_find(uint32_t service)
{
    for (unsigned i = 0; i < NBQ_SERVICES; i++)
        if (nbq_services[i].service == service) return &nbq_services[i];
    return 0;
}
static inline int nbq_service_grantable(const struct nbq_bridge *b, uint32_t service)
{
    if (!service || service > 0xfeu) return 0;
    return b->grant_all || nbq_private_service(service) || nbq_service_find(service) != 0;
}

/* ------------------------------------------------------------------ clients */
static inline int nbq_client_find(const struct nbq_bridge *b, uint32_t channel, uint32_t service, uint32_t cid)
{
    for (unsigned i = 0; i < NBQ_MAX_CLIENTS; i++) {
        const struct nbq_client *c = &b->client[i];
        if (c->used && c->channel == channel && c->service == service && c->cid == cid) return (int)i;
    }
    return -1;
}
/* client ids 1..254 per channel and service, round robin after the last one given (0 is CTL's, 255 broadcast) */
static inline int nbq_client_alloc(struct nbq_bridge *b, uint32_t channel, uint32_t service)
{
    int slot = -1;
    for (unsigned i = 0; i < NBQ_MAX_CLIENTS; i++)
        if (!b->client[i].used) { slot = (int)i; break; }
    if (slot < 0 || channel >= NBQ_CHANNELS) return -1;
    uint32_t cid = b->last_cid[channel][service];
    for (unsigned tries = 0; tries < 254u; tries++) {
        cid = cid % 254u + 1u;
        if (nbq_client_find(b, channel, service, cid) < 0) {
            b->last_cid[channel][service] = (uint8_t)cid;
            b->client[slot].used = 1;
            b->client[slot].channel = (uint8_t)channel;
            b->client[slot].service = (uint8_t)service;
            b->client[slot].cid = (uint8_t)cid;
            return slot;
        }
    }
    return -1;
}
static inline unsigned nbq_clients(const struct nbq_bridge *b, uint32_t service)
{
    unsigned n = 0;
    for (unsigned i = 0; i < NBQ_MAX_CLIENTS; i++) n += b->client[i].used && (service == 0xffffffffu || b->client[i].service == service);
    return n;
}

/* ------------------------------------------------------------------ frames */
static inline uint32_t nbq_frame_header(uint8_t *out, uint32_t sdu_len, uint32_t service, uint32_t cid)
{
    out[0] = NBQ_IF_TYPE;
    nbq_wr16(out + 1, NBQ_HDR - 1u + sdu_len);
    out[3] = NBQ_FLAG_SERVICE;
    out[4] = (uint8_t)service;
    out[5] = (uint8_t)cid;
    return NBQ_HDR;
}
static inline uint32_t nbq_tlv_result(uint8_t *p, uint32_t error)
{
    p[0] = 0x02; nbq_wr16(p + 1, 4);
    nbq_wr16(p + 3, error ? 1u : 0u);
    nbq_wr16(p + 5, error);
    return 7;
}

/* CTL answer: [QMUX][flags 1, txn, id, len][result][optional TLV 0x01 of `n` bytes] */
static inline uint32_t nbq_ctl_answer(uint8_t *out, uint32_t room, uint32_t txn, uint32_t id, uint32_t error, const uint8_t *tlv1, uint32_t n)
{
    const uint32_t tlvs = 7u + (tlv1 ? 3u + n : 0u), total = NBQ_HDR + NBQ_CTL_HDR + tlvs;
    if (total > room) return 0;
    nbq_frame_header(out, NBQ_CTL_HDR + tlvs, 0, 0);
    uint8_t *s = out + NBQ_HDR;
    s[0] = NBQ_CTL_RESPONSE;
    s[1] = (uint8_t)txn;
    nbq_wr16(s + 2, id);
    nbq_wr16(s + 4, tlvs);
    uint8_t *t = s + NBQ_CTL_HDR;
    t += nbq_tlv_result(t, error);
    if (tlv1) {
        t[0] = 0x01; nbq_wr16(t + 1, n);
        for (uint32_t i = 0; i < n; i++) t[3 + i] = tlv1[i];
    }
    return total;
}

/* find TLV `type` in a TLV area; returns its value and length, or 0 */
static inline const uint8_t *nbq_tlv(const uint8_t *p, uint32_t n, uint32_t type, uint32_t *len)
{
    while (n >= 3) {
        const uint32_t l = nbq_rd16(p + 1);
        if (3u + l > n) return 0;
        if (p[0] == type) { *len = l; return p + 3; }
        p += 3u + l; n -= 3u + l;
    }
    return 0;
}

static inline struct nbq_result nbq_ctl(struct nbq_bridge *b, uint32_t channel, const uint8_t *s, uint32_t n, uint8_t *out, uint32_t room)
{
    struct nbq_result r = { NBQ_TO_HOST, 0, 0, (uint8_t)channel, 0, 0 };
    const uint32_t txn = s[1], id = nbq_rd16(s + 2), tlen = nbq_rd16(s + 4);
    const uint8_t *tlvs = s + NBQ_CTL_HDR, *v;
    uint32_t vlen = 0;
    r.ctl_message = id;
    if (s[0] != 0 || NBQ_CTL_HDR + tlen > n) { b->dropped++; r.kind = NBQ_DROP; return r; }
    switch (id) {
    case NBQ_CTL_GET_CLIENT_ID: {
        v = nbq_tlv(tlvs, tlen, 0x01, &vlen);
        if (!v || vlen < 1) { r.len = nbq_ctl_answer(out, room, txn, id, NBQ_ERR_MISSING_ARGUMENT, 0, 0); break; }
        const uint32_t service = v[0];
        r.service = (uint8_t)service;
        if (!nbq_service_grantable(b, service)) {
            b->refused++; b->last_refused = service;
            r.len = nbq_ctl_answer(out, room, txn, id, NBQ_ERR_INVALID_SERVICE_TYPE, 0, 0);
            break;
        }
        const int slot = nbq_client_alloc(b, channel, service);
        if (slot < 0) { r.len = nbq_ctl_answer(out, room, txn, id, NBQ_ERR_CLIENT_IDS_EXHAUSTED, 0, 0); break; }
        const uint8_t info[2] = { (uint8_t)service, b->client[slot].cid };
        r.cid = info[1];
        r.len = nbq_ctl_answer(out, room, txn, id, 0, info, 2);
        break;
    }
    case NBQ_CTL_RELEASE_CLIENT_ID: {
        v = nbq_tlv(tlvs, tlen, 0x01, &vlen);
        if (!v || vlen < 2) { r.len = nbq_ctl_answer(out, room, txn, id, NBQ_ERR_MISSING_ARGUMENT, 0, 0); break; }
        const int slot = nbq_client_find(b, channel, v[0], v[1]);
        r.service = v[0]; r.cid = v[1];
        if (slot < 0) { r.len = nbq_ctl_answer(out, room, txn, id, NBQ_ERR_INVALID_CLIENT_ID, 0, 0); break; }
        b->client[slot].used = 0;
        r.len = nbq_ctl_answer(out, room, txn, id, 0, v, 2);
        break;
    }
    case NBQ_CTL_SYNC:
        for (unsigned i = 0; i < NBQ_MAX_CLIENTS; i++)
            if (b->client[i].used && b->client[i].channel == channel) b->client[i].used = 0;
        r.len = nbq_ctl_answer(out, room, txn, id, 0, 0, 0);
        break;
    case NBQ_CTL_GET_VERSION_INFO: {
        uint8_t list[1 + 5 * NBQ_SERVICES];
        uint32_t count = 0;
        for (unsigned i = 0; i < NBQ_SERVICES; i++) {
            uint8_t *e = list + 1 + 5 * count++;
            e[0] = nbq_services[i].service; nbq_wr16(e + 1, nbq_services[i].major); nbq_wr16(e + 3, nbq_services[i].minor);
        }
        list[0] = (uint8_t)count;
        r.len = nbq_ctl_answer(out, room, txn, id, 0, list, 1 + 5 * count);
        break;
    }
    default:   /* SetInstanceId, ConfigPowerSaveMode(Ext), SetDataFormat ... accepted, nothing here sleeps */
        b->ctl_other++; b->last_ctl_other = id;
        r.len = nbq_ctl_answer(out, room, txn, id, 0, 0, 0);
        break;
    }
    if (!r.len) { b->dropped++; r.kind = NBQ_DROP; }
    return r;
}

/* One write from ABM on a QMUX channel (one frame per write). */
static inline struct nbq_result nbq_from_host(struct nbq_bridge *b, uint32_t channel, const uint8_t *f, uint32_t n, uint8_t *out, uint32_t room)
{
    struct nbq_result r = { NBQ_DROP, 0, 0, (uint8_t)channel, 0, 0 };
    if (channel >= NBQ_CHANNELS || n < NBQ_HDR + NBQ_CTL_HDR || f[0] != NBQ_IF_TYPE || nbq_rd16(f + 1) + 1u != n || f[3] != 0) {
        b->dropped++;
        return r;
    }
    const uint32_t service = f[4], cid = f[5];
    if (!service) return nbq_ctl(b, channel, f + NBQ_HDR, n - NBQ_HDR, out, room);
    const uint8_t *s = f + NBQ_HDR;
    r.service = (uint8_t)service; r.cid = (uint8_t)cid;
    if (nbq_client_find(b, channel, service, cid) < 0 || n < NBQ_HDR + NBQ_SDU_HDR ||
        NBQ_SDU_HDR + nbq_rd16(s + 5) > n - NBQ_HDR || s[0] != NBQ_QMI_REQUEST) {
        b->dropped++;
        return r;
    }
    r.kind = NBQ_LOCAL;
    r.len = n - NBQ_HDR;
    return r;
}

/* A QMI message from this "modem": `type` NBQ_QMI_RESPONSE (result `error`, then `tlvs`) or NBQ_QMI_INDICATION (`tlvs` only,
   transaction 0). */
static inline uint32_t nbq_message(uint32_t type, uint32_t txn, uint32_t message, uint32_t service, uint32_t cid, uint32_t error,
                                   const uint8_t *tlvs, uint32_t tlv_len, uint8_t *out, uint32_t room)
{
    const uint32_t body = (type == NBQ_QMI_RESPONSE ? 7u : 0u) + tlv_len, total = NBQ_HDR + NBQ_SDU_HDR + body;
    if (room < total || body > 0xffffu - NBQ_HDR - NBQ_SDU_HDR) return 0;
    nbq_frame_header(out, NBQ_SDU_HDR + body, service, cid);
    uint8_t *s = out + NBQ_HDR, *t = s + NBQ_SDU_HDR;
    s[0] = (uint8_t)type;
    nbq_wr16(s + 1, txn);
    nbq_wr16(s + 3, message);
    nbq_wr16(s + 5, body);
    if (type == NBQ_QMI_RESPONSE) t += nbq_tlv_result(t, error);
    for (uint32_t i = 0; i < tlv_len; i++) t[i] = tlvs[i];
    return total;
}
/* The answer to a request: same message id and transaction. */
static inline uint32_t nbq_answer(const uint8_t *request_sdu, uint32_t service, uint32_t cid, uint32_t error, const uint8_t *tlvs,
                                  uint32_t tlv_len, uint8_t *out, uint32_t room)
{
    return nbq_message(NBQ_QMI_RESPONSE, nbq_rd16(request_sdu + 1), nbq_rd16(request_sdu + 3), service, cid, error, tlvs, tlv_len, out, room);
}
static inline uint32_t nbq_indication(uint32_t message, uint32_t service, uint32_t cid, const uint8_t *tlvs, uint32_t tlv_len,
                                      uint8_t *out, uint32_t room)
{
    return nbq_message(NBQ_QMI_INDICATION, 0, message, service, cid, 0, tlvs, tlv_len, out, room);
}

#endif /* NORD2_BB_QMUX_H */
