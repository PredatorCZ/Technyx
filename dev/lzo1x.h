
#pragma once

#define LZO_E_OK 0
#define LZO_E_ERROR (-1)
#define LZO_E_OUT_OF_MEMORY (-2)
#define LZO_E_NOT_COMPRESSIBLE (-3)
#define LZO_E_INPUT_OVERRUN (-4)
#define LZO_E_OUTPUT_OVERRUN (-5)
#define LZO_E_LOOKBEHIND_OVERRUN (-6)
#define LZO_E_EOF_NOT_FOUND (-7)
#define LZO_E_INPUT_NOT_CONSUMED (-8)
#define LZO_E_NOT_YET_IMPLEMENTED (-9)
#define LZO_E_INVALID_ARGUMENT (-10)

#ifdef __cplusplus
extern "C" {
#endif

int lzo1x_decompress_safe(const unsigned char *in, size_t in_len,
                          unsigned char *out, size_t *out_len);

#ifdef __cplusplus
}
#endif
