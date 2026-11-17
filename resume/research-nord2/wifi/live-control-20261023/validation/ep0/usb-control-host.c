
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_live_control.h"
#define DMB_SY 15
#define __builtin_arm_dmb(x) ((void)(x))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define NORD2_PMSG_SIZE 65536u
#define NORD2_RING_HDR 12u
#define NORD2_PMSG_SIG 0x43474244u
struct nord2_ring { uint32_t sig, start, size; uint8_t data[65524]; };
static struct nord2_ring ring;
static volatile struct nord2_ring *nord2_ring(void) { return &ring; }
static struct { uint32_t requests, resets, log_reads, stalls; uint8_t configuration; } nusb;
uint32_t
nord2_log_read(uint32_t offset, uint8_t *out, uint32_t max)
{
	volatile struct nord2_ring *ring = nord2_ring();
	uint32_t size = ring->size;
	/* Pair with publication in nord2_putc. Published bytes never change. */
	__builtin_arm_dmb(DMB_SY);
	if (ring->sig != NORD2_PMSG_SIG || size > NORD2_PMSG_SIZE - NORD2_RING_HDR ||
	    offset >= size) {
		return 0;
	}
	uint32_t count = size - offset < max ? size - offset : max;
	for (uint32_t i = 0; i < count; i++) {
		out[i] = (uint8_t)ring->data[offset + i];
	}
	return count;
}

#define NORD2_STREAM_CAPACITY (4u * 1024u * 1024u)
static uint8_t nord2_stream[NORD2_STREAM_CAPACITY];
static uint32_t nord2_stream_size;
static bool nord2_stream_ready;

static void
nord2_stream_init(void)
{
	uint32_t n = nord2_log_read(0, nord2_stream, NORD2_PMSG_SIZE - NORD2_RING_HDR);
	__atomic_store_n(&nord2_stream_size, n, __ATOMIC_RELEASE);
	nord2_stream_ready = true;
}

void
nord2_log_stream_info(uint32_t info[2])
{
	info[0] = NORD2_STREAM_CAPACITY;
	info[1] = __atomic_load_n(&nord2_stream_size, __ATOMIC_ACQUIRE);
}

uint32_t
nord2_log_stream_read(uint32_t offset, uint8_t *out, uint32_t max)
{
	uint32_t size = __atomic_load_n(&nord2_stream_size, __ATOMIC_ACQUIRE);
	if (offset >= size) { return 0; }
	uint32_t n = size - offset < max ? size - offset : max;
	for (uint32_t i = 0; i < n; i++) { out[i] = nord2_stream[offset + i]; }
	return n;
}

static const uint8_t nusb_device[] = {
	18, 1, 0, 2, 0, 0, 0, 64, 0xd1, 0x18, 1, 0xd0, 0, 1, 1, 2, 3, 1
};
static const uint8_t nusb_config[] = {
	9, 2, 18, 0, 1, 1, 0, 0x80, 250,
	9, 4, 0, 0, 0, 0xff, 0x53, 1, 0
};
static const uint8_t nusb_qualifier[] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};
static const char *const nusb_strings[] = {
	NULL, "ios-sweet", "Nord2 XNU console", "nord2-xnu"
};

static uint16_t
nusb_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t
nusb_string(unsigned index, uint8_t *out)
{
	if (index == 0) {
		out[0] = 4; out[1] = 3; out[2] = 9; out[3] = 4;
		return 4;
	}
	if (index >= sizeof(nusb_strings) / sizeof(nusb_strings[0])) {
		return 0;
	}
	uint32_t n = 2;
	for (const char *s = nusb_strings[index]; *s && n < 126; s++) {
		out[n++] = (uint8_t)*s;
		out[n++] = 0;
	}
	out[0] = (uint8_t)n; out[1] = 3;
	return n;
}

/*
 * Pure request decoder: 0=STALL, 1=IN data, 2=no-data ACK.
 * Descriptors/configuration, bounded log/status reads and optional fixed
 * Wi-Fi owner mailbox. No data-stage OUT, memory/disk access or code execution.
 */
static int
nusb_reply(const uint8_t setup[8], uint8_t data[1024], uint32_t *length,
    int *address, int *configuration)
{
	uint8_t type = setup[0], request = setup[1];
	uint16_t value = nusb_le16(setup + 2), index = nusb_le16(setup + 4);
	uint16_t wanted = nusb_le16(setup + 6);
	uint32_t n = 0;
	*address = *configuration = -1;
	*length = 0;
	int live_response = nwl_usb(setup, data, 1024, length);
	if (live_response >= 0) { return live_response; }
	if (type == 0xc0 && request == 0x51 && index == 0) {
		n = nord2_log_read(value, data, MIN((uint32_t)wanted, 1024u));
		nusb.log_reads++;
	} else if (type == 0xc0 && request == 0x52 && value == 0 && index == 0) {
		const uint32_t status[] = {0x4253554e, 2, nusb.requests,
		    nusb.resets, nusb.log_reads, nusb.stalls, nusb.configuration};
		memcpy(data, status, sizeof(status));
		n = sizeof(status);
	} else if (type == 0xc0 && request == 0x53 && value == 0 && index == 0) {
		uint32_t info[2];
		nord2_log_stream_info(info);
		memcpy(data, info, sizeof(info));
		n = sizeof(info);
	} else if (type == 0xc0 && request == 0x54) {
		n = nord2_log_stream_read((uint32_t)value | (uint32_t)index << 16,
		    data, MIN((uint32_t)wanted, 1024u));
		nusb.log_reads++;
	} else if (type == 0x80 && request == 6) {
		unsigned kind = value >> 8, number = value & 0xff;
		if (kind != 3 && (index || number)) {
			return 0;
		}
		switch (kind) {
		case 1: n = sizeof(nusb_device); memcpy(data, nusb_device, n); break;
		case 2: n = sizeof(nusb_config); memcpy(data, nusb_config, n); break;
		case 3:
			if (index != 0 && index != 0x409) { return 0; }
			n = nusb_string(number, data);
			if (!n) { return 0; }
			break;
		case 6: n = sizeof(nusb_qualifier); memcpy(data, nusb_qualifier, n); break;
		case 7: n = sizeof(nusb_config); memcpy(data, nusb_config, n); data[1] = 7; break;
		default: return 0;
		}
	} else if (type == 0 && request == 5 && value <= 127 && !index && !wanted) {
		*address = value; return 2;
	} else if (type == 0 && request == 9 && value <= 1 && !index && !wanted) {
		*configuration = value; return 2;
	} else if (type == 0x80 && request == 8 && !value && !index && wanted == 1) {
		data[0] = nusb.configuration; n = 1;
	} else if ((type == 0x80 || type == 0x81 || type == 0x82) && request == 0 &&
	    !value && wanted == 2 &&
	    (index == 0 || (type == 0x82 && index == 0x80))) {
		data[0] = data[1] = 0; n = 2;
	} else if (type == 0x81 && request == 10 && !value && !index && wanted == 1 &&
	    nusb.configuration == 1) {
		data[0] = 0; n = 1;
	} else if (type == 1 && request == 11 && !value && !index && !wanted &&
	    nusb.configuration == 1) {
		return 2;
	} else {
		return 0;
	}
	*length = MIN(n, (uint32_t)wanted);
	return wanted ? 1 : 2;
}


static uint8_t data[1024];
static uint32_t length;
static int address, configuration;
static int req(unsigned type, unsigned request, unsigned value, unsigned index, unsigned count) {
    uint8_t s[8] = {type,request,value,value>>8,index,index>>8,count,count>>8};
    int result=nusb_reply(s,data,&length,&address,&configuration);
    assert(result>=0 && result<=2 && length<=sizeof(data) && length<=count);
    assert(address>=-1 && address<=127 && configuration>=-1 && configuration<=1);
    return result;
}
int main(void) {
    ring.sig=NORD2_PMSG_SIG; ring.size=65524; ring.start=0;
    for (unsigned i=0;i<ring.size;i++) ring.data[i]=(uint8_t)i;
    assert(nord2_log_read(65524,data,1024)==0);
    assert(nord2_log_read(UINT32_MAX,data,1024)==0);
    assert(nord2_log_read(65520,data,1024)==4 && data[3]==(uint8_t)65523);
    assert(nord2_log_read(0,data,0)==0);
    ring.size=65525; assert(nord2_log_read(0,data,1024)==0);
    ring.size=65524; ring.sig=0; assert(nord2_log_read(0,data,1024)==0);
    ring.sig=NORD2_PMSG_SIG;
    assert(req(0x80,6,0x100,0,8)==1 && length==8 && data[0]==18);
    assert(req(0x80,6,0x100,0,65535)==1 && length==18 && data[7]==64);
    assert(req(0x80,6,0x200,0,65535)==1 && length==18 && data[13]==0);
    assert(req(0x80,6,0x600,0,65535)==1 && length==10 && data[1]==6);
    assert(req(0x80,6,0x700,0,65535)==1 && length==18 && data[1]==7);
    assert(req(0x80,6,0x300,0,255)==1 && length==4);
    assert(req(0x80,6,0x302,0x409,255)==1 && data[2]=='N' && length==36);
    assert(req(0x80,6,0x304,0x409,255)==0);
    assert(req(0x80,6,0x302,0x410,255)==0);
    assert(req(0x80,6,0x101,0,18)==0);
    assert(req(0,5,127,0,0)==2 && address==127 && configuration==-1);
    assert(req(0,5,128,0,0)==0);
    assert(req(0,5,1,0,1)==0);
    assert(req(0,9,1,0,0)==2 && configuration==1 && address==-1);
    assert(req(0,9,2,0,0)==0);
    nusb.configuration=1;
    assert(req(0x80,8,0,0,1)==1 && data[0]==1);
    assert(req(0x81,10,0,0,1)==1 && data[0]==0);
    assert(req(1,11,0,0,0)==2);
    assert(req(1,11,1,0,0)==0);
    assert(req(0x82,0,0,0x80,2)==1 && data[0]==0 && length==2);
    assert(req(0x82,0,0,0x81,2)==0);
    assert(req(0xc0,0x51,100,0,65535)==1 && length==1024 && data[0]==100);
    assert(req(0xc0,0x51,65520,0,1024)==1 && length==4);
    assert(req(0xc0,0x51,65535,0,1024)==1 && length==0);
    assert(req(0xc0,0x51,0,1,1024)==0);
    assert(req(0x40,0x51,0,0,1024)==0); /* Reject host-to-device data. */
    assert(req(0xc0,0x52,0,0,65535)==1 && length==28 && !memcmp(data,"NUSB",4));
    assert(req(0xc0,0x52,0,0,28)==1 && data[4]==2);
    nord2_stream_init();
    assert(nord2_stream_ready && nord2_stream_size==65524);
    assert(nord2_log_stream_read(65520,data,1024)==4);
    assert(nord2_log_stream_read(UINT32_MAX,data,1024)==0);
    for (uint32_t i=65524;i<NORD2_STREAM_CAPACITY;i++) nord2_stream[i]=(uint8_t)(i*7);
    __atomic_store_n(&nord2_stream_size,NORD2_STREAM_CAPACITY,__ATOMIC_RELEASE);
    assert(req(0xc0,0x53,0,0,65535)==1 && length==8);
    uint32_t cap,size; memcpy(&cap,data,4); memcpy(&size,data+4,4);
    assert(cap==NORD2_STREAM_CAPACITY && size==cap);
    assert(req(0xc0,0x53,1,0,8)==0 && req(0xc0,0x53,0,1,8)==0);
    assert(req(0x40,0x54,0,0,1024)==0);
    assert(req(0xc0,0x54,17,1,65535)==1 && length==1024 && data[0]==119);
    unsigned tail=NORD2_STREAM_CAPACITY-3;
    assert(req(0xc0,0x54,tail&65535,tail>>16,1024)==1 && length==3);
    assert(req(0xc0,0x54,0,64,1024)==1 && length==0);
    assert(req(0xc0,0x54,65535,65535,1024)==1 && length==0);
    /* Actual optional live mailbox shares EP0, but cannot introduce a data
     * OUT stage or bypass the opt-in owner initialization. */
    assert(req(0xc0,NWL_USB_CAP,0,0,64)==1 && length==64);
    assert(req(0x40,NWL_USB_COMMAND,0x1001,0,0)==0);
    assert(nwl_initialize(0x12345678)==0);
    assert(req(0x40,NWL_USB_COMMAND,0x1001,0,0)==2);
    assert(req(0x40,NWL_USB_COMMAND,0x1001,0,1)==0);
    assert(req(0xc0,NWL_USB_COMMAND,0x1001,0,1)==0);
    struct nwl_request live_request;struct nwl_snapshot live_snapshot={0};
    assert(nwl_take(&live_request)==1);
    assert(nwl_complete(&live_request,NWL_OK,&live_snapshot)==0);
    assert(req(0xc0,NWL_USB_REPLY,0,0,NWL_BYTES)==1 && length==NWL_BYTES);
    assert(req(0xc0,NWL_USB_REPLY,1,0,NWL_BYTES)==0);
    assert(req(0x40,NWL_USB_REPLY,0,0,0)==0);
    uint32_t rng=1;
    for (unsigned i=0;i<100000;i++) {
        uint8_t s[8];
        for (unsigned j=0;j<8;j++) {
            rng=rng*1664525u+1013904223u; s[j]=(uint8_t)(rng>>24);
        }
        int result=nusb_reply(s,data,&length,&address,&configuration);
        assert(result>=0 && result<=2 && length<=1024 && length<=nusb_le16(s+6));
        assert(address>=-1 && address<=127 && configuration>=-1 && configuration<=1);
        if (address>=0) assert(s[0]==0 && s[1]==5 && nusb_le16(s+6)==0);
        if (configuration>=0) assert(s[0]==0 && s[1]==9 && nusb_le16(s+6)==0);
    }
    puts("PASS: native USB descriptors, strict request decoding, read-only log bounds,"
         " invalid pmsg headers, 100000 malformed requests under ASan/UBSan");
}
