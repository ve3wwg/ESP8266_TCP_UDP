include Makefile.incl

.PHONYT: all clean clobber

all:	posix posntp cmdesp

posix:	posix.o esp8266.o
	$(GXX) posix.o esp8266.o -o posix

posntp:	posntp.o
	$(GXX) posntp.o -o posntp

cmdesp:	cmdesp.o
	$(GXX) cmdesp.o -o cmdesp -lreadline

clean:
	rm -f *.o

clobber: clean
	rm -f posix posntp cmdesp .errs.t

# End
