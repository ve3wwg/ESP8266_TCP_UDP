include Makefile.incl

.PHONYT: all clean clobber

all:	posix

posix:	posix.o esp8266.o
	$(GXX) posix.o esp8266.o -o posix

clean:
	rm -f *.o

clobber: clean
	rm -f posix .errs.t

# End
