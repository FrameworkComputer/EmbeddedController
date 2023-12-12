test: asserts
	./asserts

CFLAGS  += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

asserts: asserts.c ../incbin.h
	$(CC) $(CFLAGS) -o asserts asserts.c
