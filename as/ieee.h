#ifndef uint64_t
#   if SIZEOF_LONG == 8
	typedef unsigned long uint64_t;
#   elif SIZEOF_LONG_LONG == 8
	typedef unsigned long long uint64_t;
#   endif
#   define uint64_t uint64_t
#endif

uint64_t ieee_to_m20 (double d);
