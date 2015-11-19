include Makefile.incl

.PHONYT: all clean clobber

all:	posix posntp espntp cmdesp

posix:	posix.o esp8266.o
	$(GXX) posix.o esp8266.o -o posix

posntp:	posntp.o
	$(GXX) posntp.o -o posntp

espntp: espntp.o esp8266.o
	$(GXX) espntp.o esp8266.o -o espntp

cmdesp:	cmdesp.o
	$(GXX) cmdesp.o -o cmdesp -lreadline

clean:
	rm -f *.o

clobber: clean
	rm -f posix posntp espntp cmdesp .errs.t

# End
