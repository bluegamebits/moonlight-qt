/* Build configuration for the bundled SpeexDSP echo canceller and preprocessor */
#pragma once

#define FLOATING_POINT
#define USE_SMALLFT
#define EXPORT

#if defined(_MSC_VER) && !defined(__cplusplus)
#define inline __inline
#endif
