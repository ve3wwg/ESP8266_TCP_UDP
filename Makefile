include Makefile.incl

.PHONY: all clean clobber

all:	posix posntp espntp cmdesp
	@if [ -f PCoroutine/Makefile ] ; then \
		$(MAKE) -$(MAKEFLAGS) ntp_rtos ; \
	else \
		echo "git clone git@github.com:ve3wwg/PCoroutine.git" ; \
		echo "If you want to try ntp_rtos." ; \
	fi

posix:	posix.o esp8266.o
	$(GXX) posix.o esp8266.o -o posix

posntp:	posntp.o
	$(GXX) posntp.o -o posntp

espntp: espntp.o esp8266.o
	$(GXX) espntp.o esp8266.o -o espntp

ntp_rtos: ntp_rtos.o esp8266_rtos.o
	$(GXX) ntp_rtos.o esp8266_rtos.o -o ntp_rtos -LPCoroutine -lpcoroutine

cmdesp:	cmdesp.o
	$(GXX) cmdesp.o -o cmdesp -lreadline

clean:
	rm -f *.o

esp8266_rtos.o:
	$(GXX) -c $(CXXOPTS) -DUSE_RTOS esp8266.cpp -o esp8266_rtos.o

clobber: clean
	rm -f posix posntp espntp cmdesp .errs.t

# End
