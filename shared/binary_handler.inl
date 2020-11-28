#include <foundation/api_types.h>

static inline uint32_t tm_binary_handler_read_bits(const char *src, uint64_t *bit_offset, uint32_t count)
{
	uint32_t result = 0;
	const uint8_t *data = (const uint8_t *)src;
	const uint64_t bit_end = *bit_offset + count;

	for (uint64_t bit_start = *bit_offset & 7, byte_offset = *bit_offset >> 3, offset = 0; *bit_offset < bit_end; ++byte_offset) {
		const uint8_t bit_count = (uint8_t)tm_min(count, 8 - bit_start);
		const uint8_t mask = (0xFF >> (8 - bit_count)) << bit_start;
		const uint8_t byte = (data[byte_offset] & mask) >> bit_start;
		result |= byte << offset;

		bit_start = (bit_start + bit_count) & 7;
		*bit_offset += bit_count;
		offset += bit_count;
		count -= bit_count;
	}

	return result;
}

static inline bool tm_binary_handler_read_single_bit(const char *src, uint64_t *bit_offset)
{
	const uint8_t *data = (const uint8_t *)src;
	const uint8_t mask = 1 << (*bit_offset & 7);
	const bool result = data[*bit_offset >> 3] & mask;

	++(*bit_offset);
	return result;
}

static inline void tm_binary_handler_write_bits(char *dst, uint64_t *bit_offset, uint32_t bits, uint32_t count)
{
	uint8_t *data = (uint8_t *)dst;
	for (uint64_t bit_start = *bit_offset & 7, byte_offset = *bit_offset >> 3; count > 0; ++byte_offset) {
		const uint8_t bit_count = (uint8_t)tm_min(count, 8 - bit_start);
		data[byte_offset] |= (bits & (0xFF >> (8 - bit_count))) << bit_start;

		bit_start = (bit_start + bit_count) & 7;
		*bit_offset += bit_count;
		bits >>= bit_count;
		count -= bit_count;
	}
}