/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic LC3 fixture generator (host tool, NOT part of the
 * firmware and never run by the test suites).
 *
 * Produces, for each supported receiver shape, a checked-in .lc3 input
 * (one LC3 frame per channel, 60 bytes each) and the expected
 * interleaved stereo int16 little-endian PCM the production decoder
 * must output:
 *
 *   mono_48k_7p5ms_60b   mono,  48 kHz, 7.5 ms, [L 60 B]           -> 720 samples
 *   mono_48k_10ms_60b    mono,  48 kHz, 10 ms,  [L 60 B]           -> 960 samples
 *   modeb_48k_7p5ms_60b  Mode B, 48 kHz, 7.5 ms, [L 60 B][R 60 B]  -> 720 samples
 *   modeb_48k_10ms_60b   Mode B, 48 kHz, 10 ms,  [L 60 B][R 60 B]  -> 960 samples
 *
 * Source PCM is deterministic integer-generated with distinct left and
 * right patterns:
 *
 *   L(i) = (int16_t)((uint32_t)(i * 1103515245 + 12345) >> 12)
 *   R(i) = (int16_t)((uint32_t)(i * 2654435761 + 67890) >> 12)
 *
 * Mono encodes L only and the expected output duplicates every decoded
 * sample into both channels.  Mode B encodes L and R with independent
 * encoder instances and the expected output interleaves the outputs of
 * two independent decoder instances.
 *
 * The bitstream is written as raw bytes; the PCM is written explicitly
 * little-endian.  The build must use the same relevant flags as the
 * Zephyr liblc3 module (-O3 -std=c11 -ffast-math) so the golden PCM is
 * bit-exact against the native_sim production decoder.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "lc3.h"

#define SR_HZ       48000
#define FRAME_BYTES 60
#define MAX_SAMPLES 480

static void write_le16_to_buf(uint8_t *buf, int16_t v)
{
	buf[0] = (uint8_t)(v & 0xFFu);
	buf[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static int16_t gen_l(int i)
{
	return (int16_t)((uint32_t)(i * 1103515245 + 12345) >> 12);
}

static int16_t gen_r(int i)
{
	return (int16_t)((uint32_t)(i * 2654435761 + 67890) >> 12);
}

/* CRC-32 / IEEE 802.3 (reflected poly 0xEDB88320, init/xorout
 * 0xFFFFFFFF) — identical to Zephyr crc32_ieee() so the test suites can
 * assert the recorded values with the production library. */
static uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
		}
	}
	return ~crc;
}

struct fixture {
	const char *name;
	int dt_us; /* frame duration in microseconds */
	int chans; /* 1 = mono, 2 = Mode B */
};

static int gen_fixture(const struct fixture *fx)
{
	char path[128];
	int ns = (fx->dt_us * SR_HZ) / 1000000; /* 360 or 480 samples/channel */
	int16_t pcm_in[2][MAX_SAMPLES];
	int16_t pcm_dec[2][MAX_SAMPLES];
	uint8_t frame[2][FRAME_BYTES];
	lc3_encoder_mem_48k_t enc_mem[2];
	lc3_encoder_t enc[2];
	lc3_decoder_mem_48k_t dec_mem[2];
	lc3_decoder_t dec[2];
	int16_t stereo[2 * MAX_SAMPLES];
	uint8_t pcm_bytes[4 * MAX_SAMPLES]; /* interleaved stereo, LE */
	uint8_t l_bytes[2 * MAX_SAMPLES];   /* left channel samples, LE */
	uint8_t r_bytes[2 * MAX_SAMPLES];   /* right channel samples, LE */

	for (int i = 0; i < ns; i++) {
		pcm_in[0][i] = gen_l(i);
		pcm_in[1][i] = gen_r(i);
	}

	/* Independent encoder instance per channel. */
	for (int ch = 0; ch < fx->chans; ch++) {
		enc[ch] = lc3_setup_encoder(fx->dt_us, SR_HZ, 0, &enc_mem[ch]);
		if (!enc[ch]) {
			fprintf(stderr, "%s: encoder setup failed\n", fx->name);
			return 1;
		}
		if (lc3_encode(enc[ch], LC3_PCM_FORMAT_S16, pcm_in[ch], 1, FRAME_BYTES,
			       frame[ch]) != 0) {
			fprintf(stderr, "%s: encode failed\n", fx->name);
			return 1;
		}
	}

	snprintf(path, sizeof(path), "%s.lc3", fx->name);
	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "%s: cannot open for write\n", path);
		return 1;
	}
	for (int ch = 0; ch < fx->chans; ch++) {
		if (fwrite(frame[ch], 1, FRAME_BYTES, f) != FRAME_BYTES) {
			fprintf(stderr, "%s: frame write failed\n", path);
			fclose(f);
			return 1;
		}
	}
	fclose(f);

	/* Independent decoder instance per channel. */
	for (int ch = 0; ch < fx->chans; ch++) {
		dec[ch] = lc3_setup_decoder(fx->dt_us, SR_HZ, 0, &dec_mem[ch]);
		if (!dec[ch]) {
			fprintf(stderr, "%s: decoder setup failed\n", fx->name);
			return 1;
		}
		if (lc3_decode(dec[ch], frame[ch], FRAME_BYTES, LC3_PCM_FORMAT_S16, pcm_dec[ch],
			       1) != 0) {
			fprintf(stderr, "%s: decode of own output failed\n", fx->name);
			return 1;
		}
	}

	/* Expected interleaved stereo output. */
	for (int i = 0; i < ns; i++) {
		stereo[2 * i] = pcm_dec[0][i];
		stereo[2 * i + 1] = (fx->chans == 2) ? pcm_dec[1][i] : pcm_dec[0][i];
	}

	snprintf(path, sizeof(path), "%s.pcm", fx->name);
	f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "%s: cannot open for write\n", path);
		return 1;
	}
	for (int i = 0; i < ns; i++) {
		uint8_t le[2];

		write_le16_to_buf(le, stereo[2 * i]);
		if (fwrite(le, 1, 2, f) != 2) {
			fprintf(stderr, "%s: pcm write failed\n", path);
			fclose(f);
			return 1;
		}
		write_le16_to_buf(le, stereo[2 * i + 1]);
		if (fwrite(le, 1, 2, f) != 2) {
			fprintf(stderr, "%s: pcm write failed\n", path);
			fclose(f);
			return 1;
		}
	}
	fclose(f);

	/* Report the hashes the tests assert against. */
	for (int i = 0; i < ns; i++) {
		write_le16_to_buf(pcm_bytes + 4 * i, stereo[2 * i]);
		write_le16_to_buf(pcm_bytes + 4 * i + 2, stereo[2 * i + 1]);
		write_le16_to_buf(l_bytes + 2 * i, stereo[2 * i]);
		write_le16_to_buf(r_bytes + 2 * i, stereo[2 * i + 1]);
	}

	printf("%-24s dt=%6d us chans=%d ns=%3d  lc3=%3d B  pcm=%4d B  "
	       "crc_full=%08X crc_l=%08X crc_r=%08X\n",
	       fx->name, fx->dt_us, fx->chans, ns, fx->chans * FRAME_BYTES, 4 * ns,
	       crc32_ieee(pcm_bytes, 4 * ns), crc32_ieee(l_bytes, 2 * ns),
	       crc32_ieee(r_bytes, 2 * ns));

	if (fx->chans == 2 && crc32_ieee(l_bytes, 2 * ns) == crc32_ieee(r_bytes, 2 * ns)) {
		fprintf(stderr, "%s: Mode B left/right channel hashes must differ\n", fx->name);
		return 1;
	}
	return 0;
}

int main(void)
{
	static const struct fixture fixtures[] = {
		{"mono_48k_7p5ms_60b", 7500, 1},
		{"mono_48k_10ms_60b", 10000, 1},
		{"modeb_48k_7p5ms_60b", 7500, 2},
		{"modeb_48k_10ms_60b", 10000, 2},
	};
	int rc = 0;

	for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
		rc |= gen_fixture(&fixtures[i]);
	}
	return rc;
}
