#include "LibVT_Internal.h"
#include "LibVT.h"

#if !defined(TARGET_GLES)
#include "dxt.h"
#endif
extern vtConfig c;

void * vtuCompressRGBA_DXT1(void *rgba)
{
#if defined(TARGET_GLES)
	return NULL;
#else

	int out_bytes = 0;
	uint8_t *out = (uint8_t *)malloc((c.pageDimension+3)*(c.pageDimension+3)/16*8);

	CompressImageDXT1((const byte*)rgba, out, c.pageDimension, c.pageDimension, out_bytes);

	return out;
#endif
}

void * vtuCompressRGBA_DXT5(void *rgba)
{
#if defined(TARGET_GLES)
	return NULL;
#else
	int out_bytes = 0;
	uint8_t *out = (uint8_t *)malloc((c.pageDimension+3)*(c.pageDimension+3)/16*16);

	CompressImageDXT5((const byte*)rgba, out, c.pageDimension, c.pageDimension, out_bytes);

	return out;
#endif
}
