// seaparate implementation reduces runtime.cpp comilation time significantly

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"    // Enables Vorbis decoding.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// The stb_vorbis implementation must come after the implementation of miniaudio.
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"