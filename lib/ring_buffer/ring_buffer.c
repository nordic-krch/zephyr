/* ring_buffer.c: Simple ring buffer API */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ring_buffer.h>

/**
 * Internal data structure for a buffer header.
 *
 * We want all of this to fit in a single u32_t. Every item stored in the
 * ring buffer will be one of these headers plus any extra data supplied
 */
struct ring_element {
	u32_t  type   :16; /**< Application-specific */
	u32_t  length :8;  /**< length in 32-bit chunks */
	u32_t  value  :8;  /**< Room for small integral values */
};

int sys_ring_buf_put(struct ring_buf *buf, u16_t type, u8_t value,
		     u32_t *data, u8_t size32)
{
	u32_t i, space, index, rc;

	space = sys_ring_buf_space_get(buf);
	if (space >= (size32 + 1)) {
		struct ring_element *header =
			(struct ring_element *)&buf->buf.buf32[buf->tail];
		header->type = type;
		header->length = size32;
		header->value = value;

		if (likely(buf->mask)) {
			for (i = 0; i < size32; ++i) {
				index = (i + buf->tail + 1) & buf->mask;
				buf->buf.buf32[index] = data[i];
			}
			buf->tail = (buf->tail + size32 + 1) & buf->mask;
		} else {
			for (i = 0; i < size32; ++i) {
				index = (i + buf->tail + 1) % buf->size;
				buf->buf.buf32[index] = data[i];
			}
			buf->tail = (buf->tail + size32 + 1) % buf->size;
		}
		rc = 0;
	} else {
		buf->dropped_put_count++;
		rc = -EMSGSIZE;
	}

	return rc;
}

int sys_ring_buf_get(struct ring_buf *buf, u16_t *type, u8_t *value,
		     u32_t *data, u8_t *size32)
{
	struct ring_element *header;
	u32_t i, index;

	if (sys_ring_buf_is_empty(buf)) {
		return -EAGAIN;
	}

	header = (struct ring_element *) &buf->buf.buf32[buf->head];

	if (header->length > *size32) {
		*size32 = header->length;
		return -EMSGSIZE;
	}

	*size32 = header->length;
	*type = header->type;
	*value = header->value;

	if (likely(buf->mask)) {
		for (i = 0; i < header->length; ++i) {
			index = (i + buf->head + 1) & buf->mask;
			data[i] = buf->buf.buf32[index];
		}
		buf->head = (buf->head + header->length + 1) & buf->mask;
	} else {
		for (i = 0; i < header->length; ++i) {
			index = (i + buf->head + 1) % buf->size;
			data[i] = buf->buf.buf32[index];
		}
		buf->head = (buf->head + header->length + 1) % buf->size;
	}

	return 0;
}

size_t sys_ring_buf_raw_put(struct ring_buf *buf, u8_t *data, size_t size)
{
	u32_t i, space, index;
	size_t cpy_size;

	space = sys_ring_buf_space_get(buf);
	cpy_size = size > space ? space : size;

	for (i = 0; i < cpy_size; i++) {
		index = (i + buf->tail) % buf->size;
		buf->buf.buf8[index] = data[i];
	}
	buf->tail = (buf->tail + cpy_size) % buf->size;

	return cpy_size;
}

size_t sys_ring_buf_raw_get(struct ring_buf *buf, u8_t *data, size_t size)
{
	u32_t i, index, space;
	size_t cpy_size;

	space = (buf->size - 1) - sys_ring_buf_space_get(buf);
	cpy_size = size > space ? space : size;

	for (i = 0; i < cpy_size; ++i) {
		index = (i + buf->head) % buf->size;
		data[i] = buf->buf.buf8[index];
	}
	buf->head = (buf->head + cpy_size) % buf->size;

	return cpy_size;
}
