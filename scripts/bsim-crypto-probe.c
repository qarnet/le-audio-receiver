/* ABI-matched probe of the installed library, not linked-in crypto code. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef void (*aes_t)(const uint8_t *, const uint8_t *, uint8_t *);
typedef void (*ecb_t)(const uint8_t *, size_t, const uint8_t *, uint8_t *);
typedef void (*enc_t)(uint8_t, uint8_t, const uint8_t *, const uint8_t *, const uint8_t *,
		      uint8_t *);
typedef int (*dec_t)(uint8_t, uint8_t, const uint8_t *, const uint8_t *, const uint8_t *, int,
		     uint8_t *);
typedef void (*enc3_t)(uint8_t *, int, int, int, int, const uint8_t *, const uint8_t *,
		       const uint8_t *, uint8_t *);
typedef int (*dec3_t)(uint8_t *, int, int, int, int, const uint8_t *, const uint8_t *,
		      const uint8_t *, int, uint8_t *);

#define REQUIRE(test, why)                                                                         \
	do {                                                                                       \
		if (!(test)) {                                                                     \
			fprintf(stderr, "crypto preflight: %s\n", why);                            \
			return 1;                                                                  \
		}                                                                                  \
	} while (0)
#define LOAD(var, name)                                                                            \
	do {                                                                                       \
		*(void **)(&(var)) = dlsym(handle, name);                                          \
		REQUIRE((var) != NULL, name);                                                      \
	} while (0)

int main(void)
{
	void *handle = dlopen("../lib/libCryptov1.so", RTLD_NOW);
	if (!handle) {
		fprintf(stderr, "crypto preflight: %s\n", dlerror());
		return 1;
	}
	aes_t aes;
	ecb_t ecb;
	enc_t enc;
	dec_t dec;
	enc3_t enc3;
	dec3_t dec3;
	LOAD(aes, "blecrypt_aes_128");
	LOAD(ecb, "blecrypt_aes_ecb");
	LOAD(enc, "blecrypt_packet_encrypt");
	LOAD(dec, "blecrypt_packet_decrypt");
	LOAD(enc3, "blecrypt_packet_encrypt_v3");
	LOAD(dec3, "blecrypt_packet_decrypt_v3");
	const uint8_t key[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	const uint8_t pt[16] = {0,    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
				0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	const uint8_t ct[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 4,    0x30,
				0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
	uint8_t out[32] = {0};
	aes(key, pt, out);
	REQUIRE(!memcmp(out, ct, 16), "AES-128 known answer");
	ecb(key, 128, pt, out);
	REQUIRE(!memcmp(out, ct, 16), "AES ECB known answer");
	/* Bluetooth Core v4.2 Vol6 PartC section1 LL_START_ENC_RSP vector. */
	const uint8_t sk[16] = {0x99, 0xad, 0x1b, 0x52, 0x26, 0xa3, 0x7e, 0x3e,
				5,    0x8e, 0x3b, 0x8e, 0x27, 0xc2, 0xc6, 0x66};
	const uint8_t nonce[13] = {0,    0,    0,    0,    0x80, 0x24, 0xab,
				   0xdc, 0xba, 0xbe, 0xba, 0xaf, 0xde};
	const uint8_t payload[1] = {6}, expected[5] = {0x9f, 0xcd, 0xa7, 0xf4, 0x48};
	uint8_t aad = 3, encrypted[5], decoded[4];
	enc(0x0f, 1, payload, sk, nonce, encrypted);
	REQUIRE(!memcmp(encrypted, expected, 5), "legacy CCM known answer");
	REQUIRE(dec(0x0f, 1, encrypted, sk, nonce, 0, decoded) == 1 && decoded[0] == 6,
		"legacy CCM decrypt");
	encrypted[4] ^= 1;
	REQUIRE(dec(0x0f, 1, encrypted, sk, nonce, 0, decoded) == 0,
		"legacy CCM invalid MIC accepted");
	enc3(&aad, 1, 1, 4, 13, payload, sk, nonce, encrypted);
	REQUIRE(!memcmp(encrypted, expected, 5), "v3 CCM known answer");
	REQUIRE(dec3(&aad, 1, 1, 4, 13, encrypted, sk, nonce, 0, decoded) == 1 && decoded[0] == 6,
		"v3 CCM decrypt");
	encrypted[4] ^= 1;
	REQUIRE(dec3(&aad, 1, 1, 4, 13, encrypted, sk, nonce, 0, decoded) == 0,
		"v3 CCM invalid MIC accepted");
	REQUIRE(dec3(&aad, 1, 1, 4, 13, expected, sk, nonce, 1, decoded) == 1 && decoded[0] == 6,
		"v3 CCM MIC-less decrypt");
	dlclose(handle);
	puts("crypto ready: six APIs, AES/CCM known answers, MIC validation");
	return 0;
}
