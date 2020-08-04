#include <assert.h>
#include <stdlib.h>
#include "../incbin.h"

INCBIN(Lorem, "loremipsum.txt");
INCBIN(Onebyte, "onebyte.txt");
INCBIN(Sevenbytes, "sevenbytes.txt");

int main(int argc, char **argv)
{
	assert(gLoremSize==962);
	assert(&gLoremData[gLoremSize] == (const unsigned char*) &gLoremEnd);

	assert(gOnebyteSize == 1);
	assert(&gOnebyteData[gOnebyteSize] == (const unsigned char*) &gOnebyteEnd);

	assert(gSevenbytesSize==7);
	assert(&gSevenbytesData[gSevenbytesSize] == (const unsigned char*) &gSevenbytesEnd);
	exit(0);
}
