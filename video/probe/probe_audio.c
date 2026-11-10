/*
 * probe_audio.c -- the probe's AudioToolbox measurement.
 *
 *   T07 audio.aac : LPCM 44.1 kHz mono 16-bit -> AAC ('aac ') at 64 kbit/s -> LPCM again, and the RMS of the
 *                   decoded signal against the input's (pass: within 3 dB).
 *
 * SPDX-License-Identifier: MIT
 */
#include "probe_core.h"
#include "probe_apple.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_RATE   44100
#define AUDIO_SECONDS 1
#define AUDIO_FRAMES (AUDIO_RATE * AUDIO_SECONDS)
#define AUDIO_AMP    12000
#define MAX_PACKETS  256

struct pcm_in {
	const int16_t *pcm;
	size_t frames;
	size_t pos;
};

static OSStatus pcm_input(AudioConverterRef converter, uint32_t *ioNumberDataPackets, AudioBufferList *ioData,
                          AudioStreamPacketDescription **outDesc, void *userData)
{
	struct pcm_in *in = (struct pcm_in *)userData;
	size_t want, avail;

	(void)converter;
	if (outDesc)
		*outDesc = NULL;
	if (!in || !ioData || !ioNumberDataPackets)
		return -50;                     /* kAudio_ParamError */
	want = *ioNumberDataPackets;
	avail = in->pos < in->frames ? in->frames - in->pos : 0;
	if (want > avail)
		want = avail;
	ioData->mNumberBuffers = 1;
	ioData->mBuffers[0].mNumberChannels = 1;
	ioData->mBuffers[0].mData = (void *)(in->pcm + in->pos);
	ioData->mBuffers[0].mDataByteSize = (uint32_t)(want * sizeof(int16_t));
	*ioNumberDataPackets = (uint32_t)want;
	in->pos += want;
	return 0;
}

struct aac_in {
	const uint8_t *bytes;
	const uint32_t *sizes;
	unsigned count;
	unsigned next;
};

static OSStatus aac_input(AudioConverterRef converter, uint32_t *ioNumberDataPackets, AudioBufferList *ioData,
                          AudioStreamPacketDescription **outDesc, void *userData)
{
	struct aac_in *in = (struct aac_in *)userData;
	static AudioStreamPacketDescription desc;

	(void)converter;
	if (!in || !ioData || !ioNumberDataPackets)
		return -50;
	if (in->next >= in->count) {
		ioData->mNumberBuffers = 0;
		*ioNumberDataPackets = 0;
		return 0;
	}
	ioData->mNumberBuffers = 1;
	ioData->mBuffers[0].mNumberChannels = 1;
	ioData->mBuffers[0].mData = (void *)(in->bytes);
	ioData->mBuffers[0].mDataByteSize = in->sizes[in->next];
	desc.mStartOffset = 0;
	desc.mVariableFramesInPacket = 0;
	desc.mDataByteSize = in->sizes[in->next];
	if (outDesc)
		*outDesc = &desc;
	in->bytes += in->sizes[in->next];
	in->next++;
	*ioNumberDataPackets = 1;
	return 0;
}

static double rms_db(const int16_t *p, size_t n)
{
	double sum = 0.0;
	size_t i;
	if (!n)
		return -999.0;
	for (i = 0; i < n; i++)
		sum += (double)p[i] * (double)p[i];
	return 10.0 * __builtin_log10(sum / (double)n + 1e-9);
}

static int t_aac(struct probe_ctx *c)
{
	AudioStreamBasicDescription src, dst, enc_out;
	AudioConverterRef enc = NULL, dec = NULL;
	struct pcm_in pin;
	struct aac_in ain;
	int16_t *pcm = NULL;
	int16_t *decoded = NULL;
	uint8_t **packets = NULL;
	uint32_t *sizes = NULL;
	unsigned n_packets = 0;
	size_t enc_bytes = 0, decoded_frames = 0, i;
	OSStatus st;
	int pass = 1;
	double rms_in, rms_out, diff;

	pcm = malloc((size_t)AUDIO_FRAMES * sizeof(int16_t));
	decoded = malloc((size_t)AUDIO_FRAMES * 2 * sizeof(int16_t));
	packets = calloc(MAX_PACKETS, sizeof(*packets));
	sizes = calloc(MAX_PACKETS, sizeof(*sizes));
	if (!pcm || !decoded || !packets || !sizes) {
		probe_say(c, "alloc", "failed");
		pass = 0;
		goto out;
	}
	for (i = 0; i < AUDIO_FRAMES; i++) {
		double t = (double)i / (double)AUDIO_RATE;
		double s = __builtin_sin(2.0 * 3.14159265358979323846 * 440.0 * t);
		pcm[i] = (int16_t)(s * (double)AUDIO_AMP);
	}

	memset(&src, 0, sizeof src);
	src.mSampleRate = (double)AUDIO_RATE;
	src.mFormatID = N2_kAudioFormatLinearPCM;
	src.mFormatFlags = N2_kAudioFormatFlagIsSignedInteger | N2_kAudioFormatFlagIsPacked;
	src.mBytesPerPacket = 2;
	src.mFramesPerPacket = 1;
	src.mBytesPerFrame = 2;
	src.mChannelsPerFrame = 1;
	src.mBitsPerChannel = 16;

	memset(&dst, 0, sizeof dst);
	dst.mSampleRate = (double)AUDIO_RATE;
	dst.mFormatID = N2_kAudioFormatMPEG4AAC;
	dst.mFormatFlags = 0;
	dst.mBytesPerPacket = 0;
	dst.mFramesPerPacket = 1024;
	dst.mBytesPerFrame = 0;
	dst.mChannelsPerFrame = 1;
	dst.mBitsPerChannel = 0;

	st = AudioConverterNew(&src, &dst, &enc);
	probe_say(c, "create_enc", "%d", (int)st);
	if (st != 0 || !enc) {
		pass = 0;
		goto out;
	}
	{
		uint32_t bitrate = 64000;
		st = AudioConverterSetProperty(enc, N2_kAudioConverterEncodeBitRate, sizeof bitrate, &bitrate);
		probe_say(c, "set_bitrate", "%d", (int)st);
	}
	{
		uint32_t sz = sizeof enc_out;
		memset(&enc_out, 0, sizeof enc_out);
		st = AudioConverterGetProperty(enc, N2_kAudioConverterCurrentOutputStreamDescription, &sz, &enc_out);
		probe_say(c, "enc_asbd", "%d", (int)st);
	}
	pin.pcm = pcm;
	pin.frames = AUDIO_FRAMES;
	pin.pos = 0;
	{
		unsigned calls;
		for (calls = 0; calls < MAX_PACKETS * 4; calls++) {
			AudioBufferList out;
			uint32_t np = 1;
			uint8_t *buf;

			if (n_packets >= MAX_PACKETS)
				break;
			buf = malloc(8192);
			if (!buf) {
				pass = 0;
				break;
			}
			memset(&out, 0, sizeof out);
			out.mNumberBuffers = 1;
			out.mBuffers[0].mNumberChannels = 1;
			out.mBuffers[0].mDataByteSize = 8192;
			out.mBuffers[0].mData = buf;
			st = AudioConverterFillComplexBuffer(enc, pcm_input, &pin, &np, &out, NULL);
			if (st != 0) {
				/* the end-of-stream convention of the input proc is an error after the last frame */
				if (pin.pos >= pin.frames && n_packets > 0) {
					free(buf);
					break;
				}
				probe_say(c, "encode", "call %u status %d", n_packets, (int)st);
				free(buf);
				pass = 0;
				break;
			}
			if (np == 0 || out.mBuffers[0].mDataByteSize == 0) {
				free(buf);
				if (pin.pos >= pin.frames)
					break;      /* nothing left to encode */
				continue;       /* priming: the converter wants more input */
			}
			packets[n_packets] = buf;
			sizes[n_packets] = out.mBuffers[0].mDataByteSize;
			enc_bytes += out.mBuffers[0].mDataByteSize;
			n_packets++;
		}
	}
	probe_say(c, "packets", "%u", n_packets);
	probe_say(c, "bytes", "%zu", enc_bytes);
	if (n_packets == 0 || enc_bytes == 0)
		pass = 0;

	/* decode the packets again */
	if (n_packets) {
		uint32_t sz = sizeof enc_out;
		AudioStreamBasicDescription dec_src;
		memset(&dec_src, 0, sizeof dec_src);
		st = AudioConverterGetProperty(enc, N2_kAudioConverterCurrentOutputStreamDescription, &sz, &enc_out);
		if (st != 0 || enc_out.mFormatID == 0) {
			/* fall back to the ASBD we asked for */
			enc_out = dst;
		}
		dec_src = enc_out;
		st = AudioConverterNew(&dec_src, &src, &dec);
		probe_say(c, "create_dec", "%d", (int)st);
		if (st != 0 || !dec) {
			pass = 0;
		}
	}
	if (dec) {
		int16_t *all = decoded;
		size_t room = (size_t)AUDIO_FRAMES * 2;
		ain.bytes = NULL;
		ain.sizes = sizes;
		ain.count = n_packets;
		ain.next = 0;
		/* aac_input walks one contiguous buffer whose packet sizes are in `sizes`; build the offsets */
		{
			uint8_t *flat = malloc(enc_bytes ? enc_bytes : 1);
			size_t off = 0;
			if (!flat) {
				probe_say(c, "alloc", "flat failed");
				pass = 0;
			} else {
				for (i = 0; i < n_packets; i++) {
					memcpy(flat + off, packets[i], sizes[i]);
					off += sizes[i];
				}
				ain.bytes = flat;
			}
			if (ain.bytes) {
				uint8_t *outbuf = malloc(16384);
				unsigned calls = 0;
				while (outbuf && calls++ < 10000) {
					AudioBufferList out;
					uint32_t np = 1024;
					memset(&out, 0, sizeof out);
					out.mNumberBuffers = 1;
					out.mBuffers[0].mNumberChannels = 1;
					out.mBuffers[0].mDataByteSize = 16384;
					out.mBuffers[0].mData = outbuf;
					st = AudioConverterFillComplexBuffer(dec, aac_input, &ain, &np, &out, NULL);
					if (st != 0) {
						if (ain.next >= ain.count && decoded_frames > 0)
							break;
						probe_say(c, "decode", "status %d", (int)st);
						pass = 0;
						break;
					}
					if (np == 0 || out.mBuffers[0].mDataByteSize == 0) {
						if (ain.next >= ain.count)
							break;
						continue;
					}
					{
						size_t frames = out.mBuffers[0].mDataByteSize / sizeof(int16_t);
						if (frames > room)
							frames = room;
						memcpy((uint8_t *)all, out.mBuffers[0].mData, frames * sizeof(int16_t));
						all += frames;
						room -= frames;
						decoded_frames += frames;
					}
					if (room == 0)
						break;
					/* one more call flushes what is still buffered inside the decoder */
				}
				free(outbuf);
			}
			free(flat);    /* (lead) not ain.bytes: aac_input advances it (run 1 aborted in free here) */
			if (0) {
			}
		}
		probe_say(c, "decoded_frames", "%zu", decoded_frames);
	}

	rms_in = rms_db(pcm, AUDIO_FRAMES);
	/* skip the codec's priming (first 2048 samples) and the padding at the end */
	if (decoded_frames > 4096)
		rms_out = rms_db(decoded + 2048, decoded_frames - 3072);
	else
		rms_out = decoded_frames ? rms_db(decoded, decoded_frames) : -999.0;
	diff = rms_out - rms_in;
	probe_say(c, "rms_in_db", "%.2f", rms_in);
	probe_say(c, "rms_out_db", "%.2f", rms_out);
	probe_say(c, "rms_diff_db", "%.2f", diff);
	if (!(diff > -3.0 && diff < 3.0))
		pass = 0;
	if (decoded_frames < (size_t)AUDIO_FRAMES / 2)
		pass = 0;

out:
	if (enc)
		AudioConverterDispose(enc);
	if (dec)
		AudioConverterDispose(dec);
	if (packets) {
		for (i = 0; i < n_packets; i++)
			free(packets[i]);
		free(packets);
	}
	free(sizes);
	free(pcm);
	free(decoded);
	return pass ? 0 : 1;
}

const struct probe_test probe_audio_tests[] = {
	{ "audio.aac", t_aac, 30 },
};
const unsigned probe_audio_tests_count = sizeof probe_audio_tests / sizeof probe_audio_tests[0];
