/*
 * SQ21 Simplified Encoder - Header
 */

#ifndef SQ21_SIMPLE_H
#define SQ21_SIMPLE_H

#include <stdint.h>

int sq21_compress_888(const uint8_t *rgb_data, int width, int height, int dpi,
                      uint8_t **out_data, size_t *out_size);
int sq21_compress_8(const uint8_t *data, int line_size, int height, int dpi,
                    uint8_t **out_data, size_t *out_size);

#endif /* SQ21_SIMPLE_H */
