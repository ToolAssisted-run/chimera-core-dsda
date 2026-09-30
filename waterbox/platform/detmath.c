/* detmath.c - the engine's inexact libm calls, answered the same in both
 * builds (platform/detmath.h): the link wraps atan, asin, tan, pow and sincos
 * (gcc's pairing of a sin and a cos of one angle). sqrt, lround, lroundf and
 * roundf, the engine's other math, are exact everywhere. */
#define _GNU_SOURCE
#include "detmath.h"

double __wrap_atan(double x) { return dm_atan(x); }
double __wrap_asin(double x) { return dm_asin(x); }
double __wrap_tan(double x) { return dm_tan(x); }
double __wrap_pow(double x, double y) { return dm_pow(x, y); }
void __wrap_sincos(double x, double *s, double *c)
{
	*s = dm_sin(x);
	*c = dm_cos(x);
}
