#ifndef KRC_FLOAT_H
#define KRC_FLOAT_H

/* FLT_* describe the target's own float, so they come from the generated bits
   header: a profile whose float is 64 bits gets the double limits, and a profile
   without floating point has none. DBL_* are the same everywhere this compiler
   targets, whose double is 64-bit binary. */
#include <bits/float.h>

#define DBL_MANT_DIG 53
#define DBL_DIG 15
#define DBL_MIN_EXP (-1021)
#define DBL_MAX_EXP 1024
#define DBL_MAX 1.7976931348623157e+308
#define DBL_EPSILON 2.2204460492503131e-16

#endif