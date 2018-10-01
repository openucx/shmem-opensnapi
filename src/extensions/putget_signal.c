/* For license: see LICENSE file at top-level */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif /* HAVE_CONFIG_H */

#include "shmemc.h"
#include "shmem/api.h"

#include "putget_signal.h"

/*
 * Extension: put/get with signal
 *
 */

#ifdef ENABLE_PSHMEM
#pragma weak shmemx_ctx_float_put_signal = pshmemx_ctx_float_put_signal
#define shmemx_ctx_float_put_signal pshmemx_ctx_float_put_signal
#pragma weak shmemx_ctx_double_put_signal = pshmemx_ctx_double_put_signal
#define shmemx_ctx_double_put_signal pshmemx_ctx_double_put_signal
#pragma weak shmemx_ctx_longdouble_put_signal = pshmemx_ctx_longdouble_put_signal
#define shmemx_ctx_longdouble_put_signal pshmemx_ctx_longdouble_put_signal
#pragma weak shmemx_ctx_char_put_signal = pshmemx_ctx_char_put_signal
#define shmemx_ctx_char_put_signal pshmemx_ctx_char_put_signal
#pragma weak shmemx_ctx_schar_put_signal = pshmemx_ctx_schar_put_signal
#define shmemx_ctx_schar_put_signal pshmemx_ctx_schar_put_signal
#pragma weak shmemx_ctx_short_put_signal = pshmemx_ctx_short_put_signal
#define shmemx_ctx_short_put_signal pshmemx_ctx_short_put_signal
#pragma weak shmemx_ctx_int_put_signal = pshmemx_ctx_int_put_signal
#define shmemx_ctx_int_put_signal pshmemx_ctx_int_put_signal
#pragma weak shmemx_ctx_long_put_signal = pshmemx_ctx_long_put_signal
#define shmemx_ctx_long_put_signal pshmemx_ctx_long_put_signal
#pragma weak shmemx_ctx_longlong_put_signal = pshmemx_ctx_longlong_put_signal
#define shmemx_ctx_longlong_put_signal pshmemx_ctx_longlong_put_signal
#pragma weak shmemx_ctx_uchar_put_signal = pshmemx_ctx_uchar_put_signal
#define shmemx_ctx_uchar_put_signal pshmemx_ctx_uchar_put_signal
#pragma weak shmemx_ctx_ushort_put_signal = pshmemx_ctx_ushort_put_signal
#define shmemx_ctx_ushort_put_signal pshmemx_ctx_ushort_put_signal
#pragma weak shmemx_ctx_uint_put_signal = pshmemx_ctx_uint_put_signal
#define shmemx_ctx_uint_put_signal pshmemx_ctx_uint_put_signal
#pragma weak shmemx_ctx_ulong_put_signal = pshmemx_ctx_ulong_put_signal
#define shmemx_ctx_ulong_put_signal pshmemx_ctx_ulong_put_signal
#pragma weak shmemx_ctx_ulonglong_put_signal = pshmemx_ctx_ulonglong_put_signal
#define shmemx_ctx_ulonglong_put_signal pshmemx_ctx_ulonglong_put_signal
#pragma weak shmemx_ctx_int8_put_signal = pshmemx_ctx_int8_put_signal
#define shmemx_ctx_int8_put_signal pshmemx_ctx_int8_put_signal
#pragma weak shmemx_ctx_int16_put_signal = pshmemx_ctx_int16_put_signal
#define shmemx_ctx_int16_put_signal pshmemx_ctx_int16_put_signal
#pragma weak shmemx_ctx_int32_put_signal = pshmemx_ctx_int32_put_signal
#define shmemx_ctx_int32_put_signal pshmemx_ctx_int32_put_signal
#pragma weak shmemx_ctx_int64_put_signal = pshmemx_ctx_int64_put_signal
#define shmemx_ctx_int64_put_signal pshmemx_ctx_int64_put_signal
#pragma weak shmemx_ctx_uint8_put_signal = pshmemx_ctx_uint8_put_signal
#define shmemx_ctx_uint8_put_signal pshmemx_ctx_uint8_put_signal
#pragma weak shmemx_ctx_uint16_put_signal = pshmemx_ctx_uint16_put_signal
#define shmemx_ctx_uint16_put_signal pshmemx_ctx_uint16_put_signal
#pragma weak shmemx_ctx_uint32_put_signal = pshmemx_ctx_uint32_put_signal
#define shmemx_ctx_uint32_put_signal pshmemx_ctx_uint32_put_signal
#pragma weak shmemx_ctx_uint64_put_signal = pshmemx_ctx_uint64_put_signal
#define shmemx_ctx_uint64_put_signal pshmemx_ctx_uint64_put_signal
#endif /* ENABLE_PSHMEM */

SHMEMX_CTX_TYPED_PUT_SIGNAL(float, float)
SHMEMX_CTX_TYPED_PUT_SIGNAL(double, double)
SHMEMX_CTX_TYPED_PUT_SIGNAL(longdouble, long double)
SHMEMX_CTX_TYPED_PUT_SIGNAL(char, char)
SHMEMX_CTX_TYPED_PUT_SIGNAL(schar, signed char)
SHMEMX_CTX_TYPED_PUT_SIGNAL(short, short)
SHMEMX_CTX_TYPED_PUT_SIGNAL(int, int)
SHMEMX_CTX_TYPED_PUT_SIGNAL(long, long)
SHMEMX_CTX_TYPED_PUT_SIGNAL(longlong, long long)
SHMEMX_CTX_TYPED_PUT_SIGNAL(uchar, unsigned char)
SHMEMX_CTX_TYPED_PUT_SIGNAL(ushort, unsigned short)
SHMEMX_CTX_TYPED_PUT_SIGNAL(uint, unsigned int)
SHMEMX_CTX_TYPED_PUT_SIGNAL(ulong, unsigned long)
SHMEMX_CTX_TYPED_PUT_SIGNAL(ulonglong, unsigned long long)
SHMEMX_CTX_TYPED_PUT_SIGNAL(int8, int8_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(int16, int16_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(int32, int32_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(int64, int64_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(uint8, uint8_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(uint16, uint16_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(uint32, uint32_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(uint64, uint64_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(size, size_t)
SHMEMX_CTX_TYPED_PUT_SIGNAL(ptrdiff, ptrdiff_t)

#ifdef ENABLE_PSHMEM
#pragma weak shmemx_float_put_signal = pshmemx_float_put_signal
#define shmemx_float_put_signal pshmemx_float_put_signal
#pragma weak shmemx_double_put_signal = pshmemx_double_put_signal
#define shmemx_double_put_signal pshmemx_double_put_signal
#pragma weak shmemx_longdouble_put_signal = pshmemx_longdouble_put_signal
#define shmemx_longdouble_put_signal pshmemx_longdouble_put_signal
#pragma weak shmemx_char_put_signal = pshmemx_char_put_signal
#define shmemx_char_put_signal pshmemx_char_put_signal
#pragma weak shmemx_schar_put_signal = pshmemx_schar_put_signal
#define shmemx_schar_put_signal pshmemx_schar_put_signal
#pragma weak shmemx_short_put_signal = pshmemx_short_put_signal
#define shmemx_short_put_signal pshmemx_short_put_signal
#pragma weak shmemx_int_put_signal = pshmemx_int_put_signal
#define shmemx_int_put_signal pshmemx_int_put_signal
#pragma weak shmemx_long_put_signal = pshmemx_long_put_signal
#define shmemx_long_put_signal pshmemx_long_put_signal
#pragma weak shmemx_longlong_put_signal = pshmemx_longlong_put_signal
#define shmemx_longlong_put_signal pshmemx_longlong_put_signal
#pragma weak shmemx_uchar_put_signal = pshmemx_uchar_put_signal
#define shmemx_uchar_put_signal pshmemx_uchar_put_signal
#pragma weak shmemx_ushort_put_signal = pshmemx_ushort_put_signal
#define shmemx_ushort_put_signal pshmemx_ushort_put_signal
#pragma weak shmemx_uint_put_signal = pshmemx_uint_put_signal
#define shmemx_uint_put_signal pshmemx_uint_put_signal
#pragma weak shmemx_ulong_put_signal = pshmemx_ulong_put_signal
#define shmemx_ulong_put_signal pshmemx_ulong_put_signal
#pragma weak shmemx_ulonglong_put_signal = pshmemx_ulonglong_put_signal
#define shmemx_ulonglong_put_signal pshmemx_ulonglong_put_signal
#pragma weak shmemx_int8_put_signal = pshmemx_int8_put_signal
#define shmemx_int8_put_signal pshmemx_int8_put_signal
#pragma weak shmemx_int16_put_signal = pshmemx_int16_put_signal
#define shmemx_int16_put_signal pshmemx_int16_put_signal
#pragma weak shmemx_int32_put_signal = pshmemx_int32_put_signal
#define shmemx_int32_put_signal pshmemx_int32_put_signal
#pragma weak shmemx_int64_put_signal = pshmemx_int64_put_signal
#define shmemx_int64_put_signal pshmemx_int64_put_signal
#pragma weak shmemx_uint8_put_signal = pshmemx_uint8_put_signal
#define shmemx_uint8_put_signal pshmemx_uint8_put_signal
#pragma weak shmemx_uint16_put_signal = pshmemx_uint16_put_signal
#define shmemx_uint16_put_signal pshmemx_uint16_put_signal
#pragma weak shmemx_uint32_put_signal = pshmemx_uint32_put_signal
#define shmemx_uint32_put_signal pshmemx_uint32_put_signal
#pragma weak shmemx_uint64_put_signal = pshmemx_uint64_put_signal
#define shmemx_uint64_put_signal pshmemx_uint64_put_signal
#pragma weak shmemx_size_put_signal = pshmemx_size_put_signal
#define shmemx_size_put_signal pshmemx_size_put_signal
#pragma weak shmemx_ptrdiff_put_signal = pshmemx_ptrdiff_put_signal
#define shmemx_ptrdiff_put_signal pshmemx_ptrdiff_put_signal
#endif /* ENABLE_PSHMEM */

APIX_DECL_TYPED_PUT_SIGNAL(float, float)
APIX_DECL_TYPED_PUT_SIGNAL(double, double)
APIX_DECL_TYPED_PUT_SIGNAL(longdouble, long double)
APIX_DECL_TYPED_PUT_SIGNAL(schar, signed char)
APIX_DECL_TYPED_PUT_SIGNAL(char, char)
APIX_DECL_TYPED_PUT_SIGNAL(short, short)
APIX_DECL_TYPED_PUT_SIGNAL(int, int)
APIX_DECL_TYPED_PUT_SIGNAL(long, long)
APIX_DECL_TYPED_PUT_SIGNAL(longlong, long long)
APIX_DECL_TYPED_PUT_SIGNAL(uchar, unsigned char)
APIX_DECL_TYPED_PUT_SIGNAL(ushort, unsigned short)
APIX_DECL_TYPED_PUT_SIGNAL(uint, unsigned int)
APIX_DECL_TYPED_PUT_SIGNAL(ulong, unsigned long)
APIX_DECL_TYPED_PUT_SIGNAL(ulonglong, unsigned long long)
APIX_DECL_TYPED_PUT_SIGNAL(int8, int8_t)
APIX_DECL_TYPED_PUT_SIGNAL(int16, int16_t)
APIX_DECL_TYPED_PUT_SIGNAL(int32, int32_t)
APIX_DECL_TYPED_PUT_SIGNAL(int64, int64_t)
APIX_DECL_TYPED_PUT_SIGNAL(uint8, uint8_t)
APIX_DECL_TYPED_PUT_SIGNAL(uint16, uint16_t)
APIX_DECL_TYPED_PUT_SIGNAL(uint32, uint32_t)
APIX_DECL_TYPED_PUT_SIGNAL(uint64, uint64_t)
APIX_DECL_TYPED_PUT_SIGNAL(size, size_t)
APIX_DECL_TYPED_PUT_SIGNAL(ptrdiff, ptrdiff_t)

#ifdef ENABLE_PSHMEM
#pragma weak shmemx_ctx_float_get_signal = pshmemx_ctx_float_get_signal
#define shmemx_ctx_float_get_signal pshmemx_ctx_float_get_signal
#pragma weak shmemx_ctx_double_get_signal = pshmemx_ctx_double_get_signal
#define shmemx_ctx_double_get_signal pshmemx_ctx_double_get_signal
#pragma weak shmemx_ctx_longdouble_get_signal = pshmemx_ctx_longdouble_get_signal
#define shmemx_ctx_longdouble_get_signal pshmemx_ctx_longdouble_get_signal
#pragma weak shmemx_ctx_char_get_signal = pshmemx_ctx_char_get_signal
#define shmemx_ctx_char_get_signal pshmemx_ctx_char_get_signal
#pragma weak shmemx_ctx_schar_get_signal = pshmemx_ctx_schar_get_signal
#define shmemx_ctx_schar_get_signal pshmemx_ctx_schar_get_signal
#pragma weak shmemx_ctx_short_get_signal = pshmemx_ctx_short_get_signal
#define shmemx_ctx_short_get_signal pshmemx_ctx_short_get_signal
#pragma weak shmemx_ctx_int_get_signal = pshmemx_ctx_int_get_signal
#define shmemx_ctx_int_get_signal pshmemx_ctx_int_get_signal
#pragma weak shmemx_ctx_long_get_signal = pshmemx_ctx_long_get_signal
#define shmemx_ctx_long_get_signal pshmemx_ctx_long_get_signal
#pragma weak shmemx_ctx_longlong_get_signal = pshmemx_ctx_longlong_get_signal
#define shmemx_ctx_longlong_get_signal pshmemx_ctx_longlong_get_signal
#pragma weak shmemx_ctx_uchar_get_signal = pshmemx_ctx_uchar_get_signal
#define shmemx_ctx_uchar_get_signal pshmemx_ctx_uchar_get_signal
#pragma weak shmemx_ctx_ushort_get_signal = pshmemx_ctx_ushort_get_signal
#define shmemx_ctx_ushort_get_signal pshmemx_ctx_ushort_get_signal
#pragma weak shmemx_ctx_uint_get_signal = pshmemx_ctx_uint_get_signal
#define shmemx_ctx_uint_get_signal pshmemx_ctx_uint_get_signal
#pragma weak shmemx_ctx_ulong_get_signal = pshmemx_ctx_ulong_get_signal
#define shmemx_ctx_ulong_get_signal pshmemx_ctx_ulong_get_signal
#pragma weak shmemx_ctx_ulonglong_get_signal = pshmemx_ctx_ulonglong_get_signal
#define shmemx_ctx_ulonglong_get_signal pshmemx_ctx_ulonglong_get_signal
#pragma weak shmemx_ctx_int8_get_signal = pshmemx_ctx_int8_get_signal
#define shmemx_ctx_int8_get_signal pshmemx_ctx_int8_get_signal
#pragma weak shmemx_ctx_int16_get_signal = pshmemx_ctx_int16_get_signal
#define shmemx_ctx_int16_get_signal pshmemx_ctx_int16_get_signal
#pragma weak shmemx_ctx_int32_get_signal = pshmemx_ctx_int32_get_signal
#define shmemx_ctx_int32_get_signal pshmemx_ctx_int32_get_signal
#pragma weak shmemx_ctx_int64_get_signal = pshmemx_ctx_int64_get_signal
#define shmemx_ctx_int64_get_signal pshmemx_ctx_int64_get_signal
#pragma weak shmemx_ctx_uint8_get_signal = pshmemx_ctx_uint8_get_signal
#define shmemx_ctx_uint8_get_signal pshmemx_ctx_uint8_get_signal
#pragma weak shmemx_ctx_uint16_get_signal = pshmemx_ctx_uint16_get_signal
#define shmemx_ctx_uint16_get_signal pshmemx_ctx_uint16_get_signal
#pragma weak shmemx_ctx_uint32_get_signal = pshmemx_ctx_uint32_get_signal
#define shmemx_ctx_uint32_get_signal pshmemx_ctx_uint32_get_signal
#pragma weak shmemx_ctx_uint64_get_signal = pshmemx_ctx_uint64_get_signal
#define shmemx_ctx_uint64_get_signal pshmemx_ctx_uint64_get_signal
#endif /* ENABLE_PSHMEM */

SHMEMX_CTX_TYPED_GET_SIGNAL(float, float)
SHMEMX_CTX_TYPED_GET_SIGNAL(double, double)
SHMEMX_CTX_TYPED_GET_SIGNAL(longdouble, long double)
SHMEMX_CTX_TYPED_GET_SIGNAL(char, char)
SHMEMX_CTX_TYPED_GET_SIGNAL(schar, signed char)
SHMEMX_CTX_TYPED_GET_SIGNAL(short, short)
SHMEMX_CTX_TYPED_GET_SIGNAL(int, int)
SHMEMX_CTX_TYPED_GET_SIGNAL(long, long)
SHMEMX_CTX_TYPED_GET_SIGNAL(longlong, long long)
SHMEMX_CTX_TYPED_GET_SIGNAL(uchar, unsigned char)
SHMEMX_CTX_TYPED_GET_SIGNAL(ushort, unsigned short)
SHMEMX_CTX_TYPED_GET_SIGNAL(uint, unsigned int)
SHMEMX_CTX_TYPED_GET_SIGNAL(ulong, unsigned long)
SHMEMX_CTX_TYPED_GET_SIGNAL(ulonglong, unsigned long long)
SHMEMX_CTX_TYPED_GET_SIGNAL(int8, int8_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(int16, int16_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(int32, int32_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(int64, int64_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(uint8, uint8_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(uint16, uint16_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(uint32, uint32_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(uint64, uint64_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(size, size_t)
SHMEMX_CTX_TYPED_GET_SIGNAL(ptrdiff, ptrdiff_t)

#ifdef ENABLE_PSHMEM
#pragma weak shmemx_float_get_signal = pshmemx_float_get_signal
#define shmemx_float_get_signal pshmemx_float_get_signal
#pragma weak shmemx_double_get_signal = pshmemx_double_get_signal
#define shmemx_double_get_signal pshmemx_double_get_signal
#pragma weak shmemx_longdouble_get_signal = pshmemx_longdouble_get_signal
#define shmemx_longdouble_get_signal pshmemx_longdouble_get_signal
#pragma weak shmemx_char_get_signal = pshmemx_char_get_signal
#define shmemx_char_get_signal pshmemx_char_get_signal
#pragma weak shmemx_schar_get_signal = pshmemx_schar_get_signal
#define shmemx_schar_get_signal pshmemx_schar_get_signal
#pragma weak shmemx_short_get_signal = pshmemx_short_get_signal
#define shmemx_short_get_signal pshmemx_short_get_signal
#pragma weak shmemx_int_get_signal = pshmemx_int_get_signal
#define shmemx_int_get_signal pshmemx_int_get_signal
#pragma weak shmemx_long_get_signal = pshmemx_long_get_signal
#define shmemx_long_get_signal pshmemx_long_get_signal
#pragma weak shmemx_longlong_get_signal = pshmemx_longlong_get_signal
#define shmemx_longlong_get_signal pshmemx_longlong_get_signal
#pragma weak shmemx_uchar_get_signal = pshmemx_uchar_get_signal
#define shmemx_uchar_get_signal pshmemx_uchar_get_signal
#pragma weak shmemx_ushort_get_signal = pshmemx_ushort_get_signal
#define shmemx_ushort_get_signal pshmemx_ushort_get_signal
#pragma weak shmemx_uint_get_signal = pshmemx_uint_get_signal
#define shmemx_uint_get_signal pshmemx_uint_get_signal
#pragma weak shmemx_ulong_get_signal = pshmemx_ulong_get_signal
#define shmemx_ulong_get_signal pshmemx_ulong_get_signal
#pragma weak shmemx_ulonglong_get_signal = pshmemx_ulonglong_get_signal
#define shmemx_ulonglong_get_signal pshmemx_ulonglong_get_signal
#pragma weak shmemx_int8_get_signal = pshmemx_int8_get_signal
#define shmemx_int8_get_signal pshmemx_int8_get_signal
#pragma weak shmemx_int16_get_signal = pshmemx_int16_get_signal
#define shmemx_int16_get_signal pshmemx_int16_get_signal
#pragma weak shmemx_int32_get_signal = pshmemx_int32_get_signal
#define shmemx_int32_get_signal pshmemx_int32_get_signal
#pragma weak shmemx_int64_get_signal = pshmemx_int64_get_signal
#define shmemx_int64_get_signal pshmemx_int64_get_signal
#pragma weak shmemx_uint8_get_signal = pshmemx_uint8_get_signal
#define shmemx_uint8_get_signal pshmemx_uint8_get_signal
#pragma weak shmemx_uint16_get_signal = pshmemx_uint16_get_signal
#define shmemx_uint16_get_signal pshmemx_uint16_get_signal
#pragma weak shmemx_uint32_get_signal = pshmemx_uint32_get_signal
#define shmemx_uint32_get_signal pshmemx_uint32_get_signal
#pragma weak shmemx_uint64_get_signal = pshmemx_uint64_get_signal
#define shmemx_uint64_get_signal pshmemx_uint64_get_signal
#pragma weak shmemx_size_get_signal = pshmemx_size_get_signal
#define shmemx_size_get_signal pshmemx_size_get_signal
#pragma weak shmemx_ptrdiff_get_signal = pshmemx_ptrdiff_get_signal
#define shmemx_ptrdiff_get_signal pshmemx_ptrdiff_get_signal
#endif /* ENABLE_PSHMEM */

APIX_DECL_TYPED_GET_SIGNAL(float, float)
APIX_DECL_TYPED_GET_SIGNAL(double, double)
APIX_DECL_TYPED_GET_SIGNAL(longdouble, long double)
APIX_DECL_TYPED_GET_SIGNAL(schar, signed char)
APIX_DECL_TYPED_GET_SIGNAL(char, char)
APIX_DECL_TYPED_GET_SIGNAL(short, short)
APIX_DECL_TYPED_GET_SIGNAL(int, int)
APIX_DECL_TYPED_GET_SIGNAL(long, long)
APIX_DECL_TYPED_GET_SIGNAL(longlong, long long)
APIX_DECL_TYPED_GET_SIGNAL(uchar, unsigned char)
APIX_DECL_TYPED_GET_SIGNAL(ushort, unsigned short)
APIX_DECL_TYPED_GET_SIGNAL(uint, unsigned int)
APIX_DECL_TYPED_GET_SIGNAL(ulong, unsigned long)
APIX_DECL_TYPED_GET_SIGNAL(ulonglong, unsigned long long)
APIX_DECL_TYPED_GET_SIGNAL(int8, int8_t)
APIX_DECL_TYPED_GET_SIGNAL(int16, int16_t)
APIX_DECL_TYPED_GET_SIGNAL(int32, int32_t)
APIX_DECL_TYPED_GET_SIGNAL(int64, int64_t)
APIX_DECL_TYPED_GET_SIGNAL(uint8, uint8_t)
APIX_DECL_TYPED_GET_SIGNAL(uint16, uint16_t)
APIX_DECL_TYPED_GET_SIGNAL(uint32, uint32_t)
APIX_DECL_TYPED_GET_SIGNAL(uint64, uint64_t)
APIX_DECL_TYPED_GET_SIGNAL(size, size_t)
APIX_DECL_TYPED_GET_SIGNAL(ptrdiff, ptrdiff_t)
