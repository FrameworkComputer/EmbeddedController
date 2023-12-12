#include "board_util.h"

void find_best_divisor(uint32_t desired_freq, const uint32_t base_frequencies[],
		       size_t num_base_frequencies, uint8_t *best_divisor,
		       size_t *best_base_frequency_index)
{
	*best_divisor = 255;
	*best_base_frequency_index = 0;
	uint32_t best_freq = base_frequencies[0] / 256;
	for (size_t i = 0; i < num_base_frequencies; i++) {
		uint32_t divisor = base_frequencies[i] / (desired_freq + 1);
		if (divisor >= 256)
			continue;
		uint32_t freq = base_frequencies[i] / (divisor + 1);
		if (freq > best_freq) {
			*best_divisor = divisor;
			*best_base_frequency_index = i;
			best_freq = freq;
		}
	}
}
