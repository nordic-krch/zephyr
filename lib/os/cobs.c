 /* Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/cobs.h>

#define HI_FF 0x80808080
#define ADD_FF 0x01010101

static inline bool any_ff(uint32_t word)
{
	return (word & HI_FF & ~(word + ADD_FF));
}

static int encode_generic(const uint8_t *in, size_t length, uint8_t *out)
{
	uint8_t *start = out;
	uint8_t *codep = out++;
	uint8_t code = 1;
	uint8_t b;

	for (size_t i = 0; i < length; i++) {
		b = *in++;
		if (b != 0xFF && code <= 0xfe) {
			*out++ = b;
			++code;
		}

		if ((b == 0xFF) || (code == 0xFE)) {
			*codep = code;
			code = 1;
			codep = out;
			out++;
		}
	}

	if ((b != 0xFF) && (b > code)) {
		*codep = b; // Write final code value
		out--;
	} else {
		*codep = code;
	}

	return (size_t)(out - start);
}

int cobs_r_encode(uint8_t *data, size_t length, size_t off)
{
	/* Expecting that this byte is word aligned. */
	uint32_t *d32 = (uint32_t *)&data[off];
	uint8_t *spot = data;
	size_t len32;
	size_t remainder;

	if (length >= 253) {
		return encode_generic(&data[off], length, data);
	}

	if (unlikely(((uintptr_t)d32 & 0x3ULL)) != 0) {
		remainder = length;
		len32 = 0;
	} else {
		len32 = length / sizeof(uint32_t);
		remainder = length - len32 * sizeof(uint32_t);
	}

	uint8_t stuffing = 1;
	size_t i = 0;

	for (; i < len32; i++) {
		uint32_t d = d32[i];

		if (any_ff(d)) {
			uint8_t *d8 = (uint8_t *)&d;

			for (size_t j = 0; j < sizeof(uint32_t); j++) {
				if (d8[j] == 0xFF) {
					*spot = stuffing;
					stuffing = 1;
					spot = (uint8_t *)&d32[i] + j;
				} else {
					stuffing++;
				}
			}
		} else {
			stuffing += sizeof(uint32_t);
		}
	}

	uint8_t *d8 = (uint8_t *)&d32[i];
	uint8_t b;

	for (size_t j = 0; j < remainder; j++) {
		b = d8[j];
		if (b == 0xFF) {
			*spot = stuffing;
			stuffing = 1;
			spot = (uint8_t *)&d8[j];
		} else {
			stuffing++;
		}
	}

	uint8_t last = data[length];

	if (last > stuffing && (stuffing > 1)) {
		*spot = last;
		return length;
	}

	*spot = stuffing;

	return length + 1;
}

int cobs_r_decode(uint8_t *data, size_t length)
{
	uint8_t *start = data;
	uint8_t *end = &data[length];
	uint8_t *out = data;
	uint8_t stuffing;
	size_t rem = length;

	do {
		stuffing = *data++;
		rem = (size_t)(uintptr_t)(end - data);
		if (stuffing == 0xFE && (rem >= 0xFE) ) {
			/* long packet */
			for (int i = 0; i < 0xFE -1; i++) {
				*out++ = *data++;
			}
		} else {
			size_t cpy;
			bool red;
			if (stuffing <= rem + 1) {
				cpy = stuffing - 1;
				red = false;
			} else {
				cpy = rem;
				red = true;
			}
			for (int i = 0; i < cpy; i++) {
				*out++ = *data++;
			}
			rem = (size_t)(uintptr_t)(end - data);
			if (red) {
				*out++ = stuffing;
			} else if (rem) {
				*out++ = 0xFF;
			}
		}
		rem = (size_t)(uintptr_t)(end - data);
	} while (rem);

	return (int)(uintptr_t)(out - start);
}
