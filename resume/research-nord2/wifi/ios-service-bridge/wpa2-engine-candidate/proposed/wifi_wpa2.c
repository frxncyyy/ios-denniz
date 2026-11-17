/* Private adapted candidate: arithmetic from preserved upstream/lx_wpa.c.
 * Original provenance and licensing limitations: see REPORT.md and upstream/.
 * State engine below authored for retained-owner asynchronous integration. */
/*
 * The arithmetic of WPA2 with a passphrase (lx_wpa.h): SHA-1 and HMAC over it (FIPS 180, RFC
 * 2104), the key of a name and a passphrase (PBKDF2, RFC 8018), the function that makes the
 * handshake's keys out of that key, two addresses and two nonces (IEEE 802.11-2020, 12.7.1.2),
 * AES decryption and the unwrapping of a key with it (FIPS 197, RFC 3394), and the messages of
 * the four-way and the group-key handshake (12.7.6, 12.7.7; figure 12-35 is the layout).
 *
 * Nothing here is taken from another implementation; the tests in tools/wpa-test check it
 * against the standards' own examples and against Python's hashlib and "cryptography".
 * Nothing here is written for speed, and nothing here says a key in a message.
 */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#else
#include <stdint.h>
#include <string.h>
#endif

#include "wifi_wpa2.h"
static void wipe(void *v,size_t n){volatile uint8_t*p=v;while(n--)*p++=0;}

/* ---- SHA-1 ------------------------------------------------------------------------------------ */

struct sha1 {
	uint32_t h[5];
	uint64_t length;			/* bytes so far */
	uint8_t block[64];
};

static uint32_t
rol(uint32_t x, unsigned n)
{
	return (x << n | x >> (32 - n));
}

static void
sha1_block(uint32_t h[5], const uint8_t *p)
{
	uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

	for (unsigned t = 0; t < 16; t++) {
		w[t] = (uint32_t)p[4 * t] << 24 | (uint32_t)p[4 * t + 1] << 16 | (uint32_t)p[4 * t + 2] << 8 | p[4 * t + 3];
	}
	for (unsigned t = 16; t < 80; t++) {
		w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
	}
	for (unsigned t = 0; t < 80; t++) {
		uint32_t f = t < 20 ? ((b & c) | (~b & d)) + 0x5a827999 : t < 40 ? (b ^ c ^ d) + 0x6ed9eba1 : t < 60 ?
		    ((b & c) | (b & d) | (c & d)) + 0x8f1bbcdc : (b ^ c ^ d) + 0xca62c1d6;
		uint32_t next = rol(a, 5) + f + e + w[t];

		e = d;
		d = c;
		c = rol(b, 30);
		b = a;
		a = next;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
 wipe(w,sizeof(w));
}

static void
sha1_begin(struct sha1 *s)
{
	s->h[0] = 0x67452301;
	s->h[1] = 0xefcdab89;
	s->h[2] = 0x98badcfe;
	s->h[3] = 0x10325476;
	s->h[4] = 0xc3d2e1f0;
	s->length = 0;
}

static void
sha1_more(struct sha1 *s, const uint8_t *data, unsigned length)
{
	for (unsigned i = 0; i < length; i++) {
		s->block[s->length++ % 64] = data[i];
		if (s->length % 64 == 0) {
			sha1_block(s->h, s->block);
		}
	}
}

static void
sha1_end(struct sha1 *s, uint8_t digest[20])
{
	uint64_t bits = s->length * 8;
	uint8_t tail[8];

	sha1_more(s, (const uint8_t *)"\x80", 1);
	while (s->length % 64 != 56) {
		sha1_more(s, (const uint8_t *)"", 1);
	}
	for (unsigned i = 0; i < 8; i++) {
		tail[i] = (uint8_t)(bits >> (56 - 8 * i));
	}
	sha1_more(s, tail, 8);
	for (unsigned i = 0; i < 20; i++) {
		digest[i] = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));
	}
}

/* HMAC-SHA1 of several pieces in a row. */
static void
hmac_sha1(const uint8_t *key, unsigned key_length, const uint8_t *const pieces[], const unsigned lengths[], unsigned count,
    uint8_t digest[20])
{
	uint8_t pad[64], hashed[20], inner[20];
	struct sha1 s;

	if (key_length > 64) {
		sha1_begin(&s);
		sha1_more(&s, key, key_length);
		sha1_end(&s, hashed);
		key = hashed;
		key_length = 20;
	}
	for (unsigned i = 0; i < 64; i++) {
		pad[i] = (i < key_length ? key[i] : 0) ^ 0x36;
	}
	sha1_begin(&s);
	sha1_more(&s, pad, 64);
	for (unsigned i = 0; i < count; i++) {
		sha1_more(&s, pieces[i], lengths[i]);
	}
	sha1_end(&s, inner);
	for (unsigned i = 0; i < 64; i++) {
		pad[i] ^= 0x36 ^ 0x5c;
	}
	sha1_begin(&s);
	sha1_more(&s, pad, 64);
	sha1_more(&s, inner, 20);
	sha1_end(&s, digest);
	wipe(pad,sizeof(pad));
 wipe(hashed,sizeof(hashed));
 wipe(&s,sizeof(s));
	wipe(inner,sizeof(inner));
}

/* ---- the key of a name and a passphrase -------------------------------------------------------- */

/* PBKDF2 with HMAC-SHA1, 4096 rounds, 32 bytes: two blocks of 20, of the second the first 12. */
static void
raw_pmk(const unsigned char *passphrase, unsigned passphrase_length, const unsigned char *name, unsigned name_length,
    unsigned char pmk[32])
{
	for (unsigned block = 1; block <= 2; block++) {
		const uint8_t number[4] = { 0, 0, 0, (uint8_t)block };
		const uint8_t *first[2] = { name, number };
		const unsigned first_lengths[2] = { name_length, 4 };
		uint8_t u[20], sum[20];

		hmac_sha1(passphrase, passphrase_length, first, first_lengths, 2, u);
		memcpy(sum, u, 20);
		for (unsigned round = 1; round < 4096; round++) {
			const uint8_t *again[1] = { u };
			const unsigned again_lengths[1] = { 20 };

			hmac_sha1(passphrase, passphrase_length, again, again_lengths, 1, u);
			for (unsigned i = 0; i < 20; i++) {
				sum[i] ^= u[i];
			}
		}
		memcpy(pmk + 20 * (block - 1), sum, block == 1 ? 20 : 12);
		wipe(u,sizeof(u));
		wipe(sum,sizeof(sum));
	}
}

/* The standard's PRF: HMAC-SHA1 of (label, a zero byte, data, a counter byte), block after block. */
static void
prf(const uint8_t *key, unsigned key_length, const char *label, const uint8_t *data, unsigned data_length, uint8_t *out,
    unsigned out_length)
{
	uint8_t zero = 0, counter = 0, digest[20];
	const uint8_t *pieces[4] = { (const uint8_t *)label, &zero, data, &counter };
	const unsigned lengths[4] = { (unsigned)strlen(label), 1, data_length, 1 };

	for (unsigned done = 0; done < out_length; done += 20, counter++) {
		hmac_sha1(key, key_length, pieces, lengths, 4, digest);
		memcpy(out + done, digest, out_length - done < 20 ? out_length - done : 20);
	}
	wipe(digest,sizeof(digest));
}

/* ---- AES-128, decryption only, and the unwrapping of a key --------------------------------------- */

static const uint8_t sbox[256]={99,124,119,123,242,107,111,197,48,1,103,43,254,215,171,118,202,130,201,125,250,89,71,240,173,212,162,175,156,164,114,192,183,253,147,38,54,63,247,204,52,165,229,241,113,216,49,21,4,199,35,195,24,150,5,154,7,18,128,226,235,39,178,117,9,131,44,26,27,110,90,160,82,59,214,179,41,227,47,132,83,209,0,237,32,252,177,91,106,203,190,57,74,76,88,207,208,239,170,251,67,77,51,133,69,249,2,127,80,60,159,168,81,163,64,143,146,157,56,245,188,182,218,33,16,255,243,210,205,12,19,236,95,151,68,23,196,167,126,61,100,93,25,115,96,129,79,220,34,42,144,136,70,238,184,20,222,94,11,219,224,50,58,10,73,6,36,92,194,211,172,98,145,149,228,121,231,200,55,109,141,213,78,169,108,86,244,234,101,122,174,8,186,120,37,46,28,166,180,198,232,221,116,31,75,189,139,138,112,62,181,102,72,3,246,14,97,53,87,185,134,193,29,158,225,248,152,17,105,217,142,148,155,30,135,233,206,85,40,223,140,161,137,13,191,230,66,104,65,153,45,15,176,84,187,22};
static const uint8_t inverse_sbox[256]={82,9,106,213,48,54,165,56,191,64,163,158,129,243,215,251,124,227,57,130,155,47,255,135,52,142,67,68,196,222,233,203,84,123,148,50,166,194,35,61,238,76,149,11,66,250,195,78,8,46,161,102,40,217,36,178,118,91,162,73,109,139,209,37,114,248,246,100,134,104,152,22,212,164,92,204,93,101,182,146,108,112,72,80,253,237,185,218,94,21,70,87,167,141,157,132,144,216,171,0,140,188,211,10,247,228,88,5,184,179,69,6,208,44,30,143,202,63,15,2,193,175,189,3,1,19,138,107,58,145,17,65,79,103,220,234,151,242,207,206,240,180,230,115,150,172,116,34,231,173,53,133,226,249,55,232,28,117,223,110,71,241,26,113,29,41,197,137,111,183,98,14,170,24,190,27,252,86,62,75,198,210,121,32,154,219,192,254,120,205,90,244,31,221,168,51,136,7,199,49,177,18,16,89,39,128,236,95,96,81,127,169,25,181,74,13,45,229,122,159,147,201,156,239,160,224,59,77,174,42,245,176,200,235,187,60,131,83,153,97,23,43,4,126,186,119,214,38,225,105,20,99,85,33,12,125};

static uint8_t
times(uint8_t a, uint8_t b)			/* in GF(2^8) modulo x^8 + x^4 + x^3 + x + 1 */
{
	uint8_t product = 0;

	for (; b != 0; b >>= 1) {
		if (b & 1) {
			product ^= a;
		}
		a = (uint8_t)(a << 1 ^ (a & 0x80 ? 0x1b : 0));
	}
	return (product);
}

static void
aes_keys(const uint8_t key[16], uint8_t round_keys[176])
{
	uint8_t constant = 1;

	memcpy(round_keys, key, 16);
	for (unsigned i = 16; i < 176; i += 4) {
		uint8_t word[4];

		memcpy(word, round_keys + i - 4, 4);
		if (i % 16 == 0) {
			uint8_t first = word[0];

			word[0] = sbox[word[1]] ^ constant;
			word[1] = sbox[word[2]];
			word[2] = sbox[word[3]];
			word[3] = sbox[first];
			constant = times(constant, 2);
		}
		for (unsigned j = 0; j < 4; j++) {
			round_keys[i + j] = round_keys[i - 16 + j] ^ word[j];
		}
  wipe(word,sizeof(word));
	}
}

/* FIPS 197, 5.3: the inverse cipher on one block (bytes in column order: byte r of column c is s[4c + r]). */
static void
aes_decrypt(const uint8_t round_keys[176], uint8_t s[16])
{
	for (unsigned i = 0; i < 16; i++) {
		s[i] ^= round_keys[160 + i];
	}
	for (int round = 9; round >= 0; round--) {
		uint8_t t[16];

		for (unsigned c = 0; c < 4; c++) {		/* the rows shifted back, and the bytes substituted back */
			for (unsigned r = 0; r < 4; r++) {
				t[4 * ((c + r) % 4) + r] = inverse_sbox[s[4 * c + r]];
			}
		}
		for (unsigned i = 0; i < 16; i++) {
			t[i] ^= round_keys[16 * round + i];
		}
		if (round == 0) {
			memcpy(s, t, 16);
   wipe(t,sizeof(t));
			break;
		}
		for (unsigned c = 0; c < 4; c++) {		/* the columns mixed back */
			const uint8_t *a = t + 4 * c;

			s[4 * c + 0] = times(a[0], 14) ^ times(a[1], 11) ^ times(a[2], 13) ^ times(a[3], 9);
			s[4 * c + 1] = times(a[0], 9) ^ times(a[1], 14) ^ times(a[2], 11) ^ times(a[3], 13);
			s[4 * c + 2] = times(a[0], 13) ^ times(a[1], 9) ^ times(a[2], 14) ^ times(a[3], 11);
			s[4 * c + 3] = times(a[0], 11) ^ times(a[1], 13) ^ times(a[2], 9) ^ times(a[3], 14);
		}
  wipe(t,sizeof(t));
	}
}

/*
 * RFC 3394, 2.2.2: `wrapped` is 8 bytes longer than what comes out, which goes to `plain`. 0, or -1
 * if the length is not one a wrapped key has or the check value does not come out.
 */
static int
aes_unwrap(const uint8_t kek[16], const uint8_t *wrapped, unsigned length, uint8_t *plain)
{
	unsigned n = length / 8 - 1;
	uint8_t round_keys[176], a[8], block[16];
	int good = 0;

	if (length % 8 != 0 || length < 24) {
		return (-1);
	}
	aes_keys(kek, round_keys);
	memcpy(a, wrapped, 8);
	memcpy(plain, wrapped + 8, 8 * n);
	for (int j = 5; j >= 0; j--) {
		for (unsigned i = n; i >= 1; i--) {
			uint64_t t = (uint64_t)n * (unsigned)j + i;

			memcpy(block, a, 8);
			for (unsigned k = 0; k < 8; k++) {
				block[7 - k] ^= (uint8_t)(t >> (8 * k));
			}
			memcpy(block + 8, plain + 8 * (i - 1), 8);
			aes_decrypt(round_keys, block);
			memcpy(a, block, 8);
			memcpy(plain + 8 * (i - 1), block + 8, 8);
		}
	}
	for (unsigned i = 0; i < 8; i++) {
		good |= a[i] ^ 0xa6;
	}
	wipe(round_keys,sizeof(round_keys));
 wipe(a,sizeof(a));
	wipe(block,sizeof(block));
	return (good == 0 ? 0 : -1);
}


/* ---- strict first WPA2-PSK/CCMP transaction, no rekey/roam/PMF ---- */
static unsigned be16(const uint8_t*p){return (unsigned)p[0]<<8|p[1];}
static void put16(uint8_t*p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static int equal(const uint8_t*a,const uint8_t*b,size_t n){unsigned d=0;for(size_t i=0;i<n;i++)d|=a[i]^b[i];return !d;}
static int nonzero(const uint8_t*a,size_t n){unsigned d=0;for(size_t i=0;i<n;i++)d|=a[i];return !!d;}
static int rsn_valid(const uint8_t*p){
 static const uint8_t rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
 return equal(p,rsn,sizeof rsn); /* exactly one CCMP pairwise/group, PSK; no PMF */
}
void nwp_cancel(struct nwp*w){if(w)wipe(w,sizeof(*w));}
static int fail(struct nwp*w,struct nwp_result*r){nwp_cancel(w);if(r)wipe(r,sizeof(*r));return NWP_FAIL;}
int nwp_pmk(const uint8_t*p,size_t pn,const uint8_t*s,size_t sn,uint8_t out[32]){
 if(!out)return -1;
 if(!p||!s||pn<8||pn>63||sn<1||sn>32){wipe(out,32);return -1;}
 raw_pmk(p,(unsigned)pn,s,(unsigned)sn,out);return 0;
}
int nwp_begin(struct nwp*w,uint64_t gen,const uint8_t pmk[32],const uint8_t own[6],const uint8_t peer[6],const uint8_t rsn[22],const uint8_t ap[22],nwp_rng rng,void*ctx){
 if(!w)return -1;
 nwp_cancel(w);
 if(!gen||!pmk||!own||!peer||!rsn||!ap||!rng||!rsn_valid(rsn)||!rsn_valid(ap)||
    !nonzero(own,6)||!nonzero(peer,6)||(own[0]&1)||(peer[0]&1)||equal(own,peer,6))return -1;
 w->generation=gen;memcpy(w->pmk,pmk,32);memcpy(w->own,own,6);memcpy(w->peer,peer,6);memcpy(w->rsn,rsn,22);memcpy(w->ap_rsn,ap,22);
 if(rng(ctx,w->snonce,32)||!nonzero(w->snonce,32)){nwp_cancel(w);return -1;}
 w->state=NWP_M1;return 0;
}
static void derive_keys(struct nwp*w){
 uint8_t data[76],keys[48];int m=memcmp(w->own,w->peer,6)<0,n=memcmp(w->snonce,w->anonce,32)<0;
 memcpy(data,m?w->own:w->peer,6);memcpy(data+6,m?w->peer:w->own,6);
 memcpy(data+12,n?w->snonce:w->anonce,32);memcpy(data+44,n?w->anonce:w->snonce,32);
 prf(w->pmk,32,"Pairwise key expansion",data,76,keys,48);
 memcpy(w->kck,keys,16);memcpy(w->kek,keys+16,16);memcpy(w->tk,keys+32,16);wipe(keys,48);wipe(data,76);
}
static void mic(const struct nwp*w,const uint8_t*p,unsigned n,uint8_t*out){
 const uint8_t z[16]={0},*pieces[3]={p,z,p+97};unsigned sizes[3]={81,16,n-97};uint8_t d[20];
 hmac_sha1(w->kck,16,pieces,sizes,3,d);memcpy(out,d,16);wipe(d,20);
}
static int reply(struct nwp*w,const uint8_t*p,unsigned kind,struct nwp_result*r){
 if(w->serial==UINT64_MAX)return fail(w,r);
 unsigned n=kind==1?121:99;memset(r,0,sizeof(*r));r->frame[0]=p[0];r->frame[1]=3;put16(r->frame+2,n-4);r->frame[4]=2;
 put16(r->frame+5,kind==1?0x010a:0x030a);memcpy(r->frame+9,p+9,8);
 if(kind==1){memcpy(r->frame+17,w->snonce,32);put16(r->frame+97,22);memcpy(r->frame+99,w->rsn,22);}
 mic(w,r->frame,n,r->frame+81);r->action=NWP_TX;r->bytes=n;r->ticket=++w->serial;w->pending=r->ticket;w->tx_kind=kind;return NWP_TX;
}
static int kde(struct nwp*w,const uint8_t*p,unsigned n,const uint8_t*rsc){
 unsigned at=0,have_rsn=0,have_gtk=0;
 while(at<n){
  if(p[at]==0xdd&&(at+1==n||p[at+1]==0)){
   for(unsigned j=at+1;j<n;j++)if(p[j])return -1;
   break;
  }
  if(n-at<2||p[at+1]>n-at-2)return -1;
  unsigned len=p[at+1]+2;const uint8_t*e=p+at;
  if(e[0]==48){if(have_rsn++||len!=22||!equal(e,w->ap_rsn,22))return -1;}
  else if(e[0]==221){
   static const uint8_t oui[4]={0,15,172,1};
   if(have_gtk++||len!=24||!equal(e+2,oui,4)||(e[6]&~7u)||!(e[6]&3)||e[7])return -1;
   w->gtk_index=e[6]&3;memcpy(w->gtk,e+8,16);memcpy(w->rsc,rsc,8);
  }else return -1;
  at+=len;
 }
 return have_rsn==1&&have_gtk==1?0:-1;
}
int nwp_receive(struct nwp*w,const uint8_t*p,size_t n,struct nwp_result*r){
 if(!r)return NWP_FAIL;
 memset(r,0,sizeof(*r));
 if(!w)return NWP_FAIL;
 if(w->state==NWP_OFF)return NWP_FAIL;
 if(!p||n<99||n>NWP_FRAME||p[0]<1||p[0]>2||p[1]!=3||be16(p+2)!=n-4||p[4]!=2||be16(p+97)!=n-99)return fail(w,r);
 unsigned info=be16(p+5);
 if(be16(p+7)!=16)return fail(w,r);
 if(info==0x008a){
  if(!nonzero(p+17,32)||be16(p+97))return fail(w,r);
  if(w->state!=NWP_M1){
   if(w->state!=NWP_M3||memcmp(p+9,w->m1_replay,8)<0||!equal(p+17,w->anonce,32))return fail(w,r);
   if(w->pending)return NWP_BUSY;
   memcpy(w->m1_replay,p+9,8);
   return reply(w,p,1,r); /* Same ANonce, same SNonce/PTK; monotonic retransmit counter. */
  }
  if(w->pending)return NWP_BUSY;
  memcpy(w->anonce,p+17,32);memcpy(w->m1_replay,p+9,8);derive_keys(w);w->state=NWP_M3;
  return reply(w,p,1,r);
 }
 if(info!=0x13ca||w->state<NWP_M3)return fail(w,r);
 uint8_t signature[16];mic(w,p,(unsigned)n,signature);int ok=equal(signature,p+81,16);wipe(signature,16);
 if(!ok||!equal(p+17,w->anonce,32))return fail(w,r);
 if(w->m3_bytes){
  /* AP retries may use a higher replay counter. All authenticated key/RSN/RSC
   * contents must remain identical; never reinstall/reset any packet number. */
  if(n!=w->m3_bytes||memcmp(p+9,w->m3+9,8)<0)return fail(w,r);
  for(unsigned i=0;i<n;i++)if(!((i>=9&&i<17)||(i>=81&&i<97))&&p[i]!=w->m3[i])return fail(w,r);
  if(w->pending)return NWP_BUSY;
  memcpy(w->m3,p,n);
  return reply(w,p,3,r);
 }
 if(w->state!=NWP_M3||memcmp(p+9,w->m1_replay,8)<=0)return fail(w,r);
 if(w->pending)return NWP_BUSY;
 uint8_t plain[NWP_FRAME];unsigned wrapped=be16(p+97);
 if(aes_unwrap(w->kek,p+99,wrapped,plain)||kde(w,plain,wrapped-8,p+65)){wipe(plain,sizeof plain);return fail(w,r);}
 wipe(plain,sizeof plain);memcpy(w->m3,p,n);w->m3_bytes=(unsigned)n;w->state=NWP_M4_TX;
 return reply(w,p,2,r);
}
int nwp_tx_done(struct nwp*w,uint64_t gen,uint64_t ticket,int success){
 if(!w||!ticket||gen!=w->generation||ticket!=w->pending)return NWP_DROP;
 unsigned kind=w->tx_kind;w->pending=0;w->tx_kind=0;
 if(!success){nwp_cancel(w);return NWP_FAIL;}
 if((kind==2||kind==3)&&w->state==NWP_M4_TX&&!w->installed){w->installed=1;w->install_ticket=ticket;w->state=NWP_KEYS;return NWP_INSTALL;}
 return NWP_DROP;
}
int nwp_keys_ready(struct nwp*w,uint64_t gen,uint64_t ticket,unsigned mask,int success){
 if(!w||gen!=w->generation||!ticket||ticket!=w->install_ticket||w->state!=NWP_KEYS||!w->installed)return NWP_DROP;
 if(!mask||(mask&~7u)||success!=1){nwp_cancel(w);return NWP_FAIL;}
 w->ready_mask|=mask;
 if(w->ready_mask!=7)return NWP_DROP;
 w->state=NWP_AUTHORIZED;return NWP_AUTH;
}
