/* Not part of the rnnoise 0.2 release, which includes it without shipping it.
   Added for slim-m's vendored copy: the generic (non-SSE, non-NEON) path in
   vec.h needs only OPUS_CLEAR from it. */
#ifndef OS_SUPPORT_H
#define OS_SUPPORT_H

#include <string.h>

#define OPUS_CLEAR(dst, n) (memset((dst), 0, (n) * sizeof(*(dst))))

#endif
