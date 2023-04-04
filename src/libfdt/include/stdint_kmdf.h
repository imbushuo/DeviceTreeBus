#pragma once
#ifndef __STDINT_KMDF__
#define __STDINT_KMDF__

#include <ntddk.h>

// Type forwarder to WDF types
typedef UINT8  uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;

typedef INT8  int8_t;
typedef INT16 int16_t;
typedef INT32 int32_t;
typedef INT64 int64_t;

/* 7.18.2.1  Limits of exact-width integer types */
#define INT8_MIN (-128) 
#define INT16_MIN (-32768)
#define INT32_MIN (-2147483647 - 1)
#define INT64_MIN  (-9223372036854775807LL - 1)

#define INT8_MAX 127
#define INT16_MAX 32767
#define INT32_MAX 2147483647
#define INT64_MAX 9223372036854775807LL

#define UINT8_MAX 0xff /* 255U */
#define UINT16_MAX 0xffff /* 65535U */
#define UINT32_MAX 0xffffffff  /* 4294967295U */
#define UINT64_MAX 0xffffffffffffffffULL /* 18446744073709551615ULL */

#endif
