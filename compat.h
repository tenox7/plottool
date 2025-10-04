#ifndef COMPAT_H
#define COMPAT_H

/* C89 compatibility definitions */

/* Include system headers first to get their definitions */
#include <stdint.h>

/* Integer types - only define if not already defined by system */
/* Note: On systems with stdint.h, these types are already defined */

/* Atomic operations - use regular types for maximum compatibility */
#ifndef atomic_uint_fast32_t
#define atomic_uint_fast32_t uint32_t
#endif

#ifndef atomic_load
#define atomic_load(ptr) (*(ptr))
#endif

#ifndef atomic_store
#define atomic_store(ptr, val) (*(ptr) = (val))
#endif

#endif /* COMPAT_H */
