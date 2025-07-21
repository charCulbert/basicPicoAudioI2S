/**
 * Fixed Point Arithmetic Library
 * Based on V. Hunter Adams' ECE 4760 course materials
 * Cornell University
 *
 * Uses 16.15 signed fixed point format:
 * - 1 sign bit
 * - 16 integer bits
 * - 15 fractional bits
 * - Range: ±65536 with resolution of ~0.00003
 */

#ifndef FIX15_H
#define FIX15_H

#include <stdlib.h>
#include <math.h>
#include <cstdint>

// Fixed point type definition (16.15 format)
typedef signed int fix15;

// ===== CONVERSION MACROS =====

#define int2fix15(a) ((fix15)((a) << 15))
#define fix152int(a) ((int)((a) >> 15))
#define float2fix15(a) ((fix15)((a) * 32768.0))
#define fix152float(a) ((float)(a) / 32768.0)
#define fix152int16(a) ((int16_t)((a) >> 15))

// ===== ARITHMETIC MACROS =====

// Fixed point multiplication
#define multfix15(a,b) ((fix15)(((signed long long)(a) * (signed long long)(b)) >> 15))

// Fixed point division (avoid if possible - very slow!)
#define divfix15(a,b) ((fix15)(((signed long long)(a) << 15) / (b)))

// ===== OTHER OPERATIONS =====

// Absolute value (just works with standard abs())
// Usage: fix15 result = abs(my_fix15_var);

// Square root (converts to float, does sqrt, converts back)
#define sqrtfix15(a) (float2fix15(sqrt(fix152float(a))))

// ===== RANDOM NUMBER GENERATION =====

// Random fixed point between 0 and 1
#define randfix15_0_1() ((fix15)(rand() & 0xffff) >> 16)

// Random fixed point between -1 and 1
#define randfix15_n1_1() (((fix15)(rand() & 0xffff) >> 15) - int2fix15(1))

// Random fixed point between -2 and 2
#define randfix15_n2_2() (((fix15)(rand() & 0xffff) >> 14) - int2fix15(2))

// ===== UTILITY CONSTANTS =====

// Common fixed point constants
#define FIX15_ZERO     (0)
#define FIX15_ONE      (32768)         // 1.0 in 16.15 format
#define FIX15_HALF     (16384)         // 0.5 in 16.15 format
#define FIX15_TWO      (65536)         // 2.0 in 16.15 format
#define FIX15_PI       (102944)        // π ≈ 3.14159 in 16.15 format
#define FIX15_2PI      (205888)        // 2π ≈ 6.28318 in 16.15 format

// ===== RANGE/BOUNDARY MACROS =====

// Clamp fixed point value to range [min, max]
#define clampfix15(val, min_val, max_val) \
    ((val) < (min_val) ? (min_val) : ((val) > (max_val) ? (max_val) : (val)))


#endif // FIX15_H