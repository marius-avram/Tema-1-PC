CFLAGS=-g

all: send recv

send: send.o lib.o crc.o
	gcc $(CFLAGS) send.o lib.o crc.o -o send

recv: recv.o lib.o crc.o
	gcc $(CFLAGS) -g recv.o lib.o crc.o -o recv

.c.o: 
	gcc $(CFLAGS) -Wall -c $? 

clean:
	-rm send.o recv.o crc.o send recv 
