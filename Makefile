CFLAGS=-I/usr/include/libdrm -Os
CC=gcc

all: qrdrm drmtouch

qrdrm: qrdrm.o common.o
	$(CC) -o $@ $^ $(LDFLAGS) -lqrencode -ldrm

drmtouch: drmtouch.o common.o
	$(CC) -o $@ $^ $(LDFLAGS) -ldrm

clean:
	rm -f *.o qrdrm drmtouch
.PHONY: all clean
