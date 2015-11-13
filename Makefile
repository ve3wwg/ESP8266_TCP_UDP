include Makefile.incl

.PHONYT: all clean clobber

all:	posix cmdesp

posix:	posix.o esp8266.o
	$(GXX) posix.o esp8266.o -o posix

cmdesp:	cmdesp.o
	$(GXX) cmdesp.o -o cmdesp -lreadline

clean:
	rm -f *.o

clobber: clean
	rm -f posix cmdesp .errs.t

# End
