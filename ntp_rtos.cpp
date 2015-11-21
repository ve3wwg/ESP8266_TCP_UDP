//////////////////////////////////////////////////////////////////////
// esp_rtos.cpp -- ESP8266 NTP Code (under an RTOS)
// Date: Fri Nov 20 19:46:55 2015  (C) Warren W. Gay VE3WWG 
///////////////////////////////////////////////////////////////////////
// 
// This test program is essentially the same as espntp.cpp, except that
// we assume that we are operating under different threads for the
// ESP8266::receive() calls and the main calling thread.
// 
// The PCoroutine package simulates coroutines under POSIX (even
// for Mac OSX). This allows identification of problems before it
// makes it to mbed + RTOS etc.
// 
// It is _critical_ that the ESP8266.o module be compiled with the
// macro USING_RTOS defined (notice that the Makefile creates a
// special object esp8266_rtos.o for this build. You should also
// declare USING_RTOS ahead of the #include "esp8266.hpp" also
// in the calling program.
// 
// Built this way, both the ESP8266::receive() thread and the main
// thread should periodically call yield() to give up the CPU.
// Notice in this case that the idle proc is "yield()".
// 
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <termios.h>
#include <assert.h>

#include "PCoroutine/pcoroutine.hpp"	// Simulates coroutine scheduling

#define USING_RTOS	1
#include "esp8266.hpp"

CR_Mutex cr_mutex(false);		// Non-preemptive scheduling

static void writeb(char b);
static char readb();
static bool rpoll();
void yield();				// The new idle procedure

static ESP8266 esp(writeb,readb,rpoll,yield);
static int fd = -1;
static bool opt_verbose = false;
static int opt_baudrate = 115200;
static const char *opt_device = "/dev/cu.usbserial-A50285BI";

//////////////////////////////////////////////////////////////////////
// Write byte callback
//////////////////////////////////////////////////////////////////////

static void
writeb(char b) {
	int rc;

	do	{
		rc = write(fd,&b,1);
	} while ( rc == -1 && errno == EINTR );
	assert(rc==1);
}

//////////////////////////////////////////////////////////////////////
// Read byte callback
//////////////////////////////////////////////////////////////////////

static char
readb() {
	char b;
	int rc;

	do	{
		rc = read(fd,&b,1);
	} while ( rc == -1 && errno == EINTR );
	assert(rc==1);

	return b;
}	

//////////////////////////////////////////////////////////////////////
// Poll for a readable byte
//////////////////////////////////////////////////////////////////////

static bool
rpoll() {
	struct pollfd p;
	int rc;

	p.fd = fd;
	p.events = POLLIN;
	p.revents = 0;

	do	{
		rc = poll(&p,1,0);
	} while ( rc == -1 && errno == EINTR );

	return rc == 1;
}

//////////////////////////////////////////////////////////////////////
// Idle processing
//////////////////////////////////////////////////////////////////////

void
yield() {
	cr_mutex.yield();	// Pass the CPU to the other thread
	usleep(100);		// Under POSIX, don't heat the CPU
}

//////////////////////////////////////////////////////////////////////
// UDP Receiving
//////////////////////////////////////////////////////////////////////

static uint32_t rxbuf[12];	// NTP receiving buffer
static char *rxp = 0;		// rx_cb byte pointer within rxbuf
static unsigned rx = 0;		// Index for rxp[rx]
static bool rx_done = false;	// End of datagram seen

static void
rx_cb(int s,int ch) {

	if ( ch == -1 ) {
		rx_done = true;
	} else	{
		if ( rx < sizeof rxbuf )
			rxp[rx++] = ch;
	}
}

//////////////////////////////////////////////////////////////////////
// Query NTP time server 
//////////////////////////////////////////////////////////////////////

static time_t
ntp_time(const char *hostname) {
	static const uint64_t ntp_offset = ((uint64_t(365)*70)+17)*24*60*60;
	static const unsigned char reqmsg[48] = {010,0,0,0,0,0,0,0,0};
	static const short port = 123;		// NTP
	uint32_t ntp_time = 0;
	int s, rc;

	cr_mutex.yield();

	// Get a socket
	s = esp.udp_socket(hostname,port,rx_cb);
	if ( s < 0 )
		return 0;			// No socket

	cr_mutex.yield();

	// Initialize RX
	rxp = (char *)rxbuf;
	rx = 0;
	rx_done = false;

	// Write request datagram
	rc = esp.write(s,(const char *)reqmsg,sizeof reqmsg);
	assert(rc == sizeof reqmsg);

	// Wait for the response
	{
		time_t t0 = time(0);			// This is valid only for POSIX systems

		while ( !rx_done && time(0) - t0 < 5 )
			cr_mutex.yield();		// Yield until a response
		esp.close(s);

		cr_mutex.yield();

		if ( !rx_done )
			return 0;			// No response
	}

	ntp_time = ntohl(rxbuf[10]);

	// Convert to Unix epoch time:
	time_t uxtime = uint64_t(ntp_time) - ntp_offset;

	// Simple UTC time calculation:
	unsigned hour = uxtime % 86400ul / 3600ul;
	unsigned min  = uxtime % 3600ul / 60ul;
	unsigned secs = uxtime % 60;

	printf("%02d:%02d:%02d UTC from %s\n",hour,min,secs,hostname);

	// Compute system time difference
	time_t td;
	td = time(0);
	long sdiff = int64_t(td) - int64_t(uxtime);
	printf("%+ld seconds off: %s\n",long(sdiff),ctime(&uxtime));

	cr_mutex.yield();

	return uxtime;
}

//////////////////////////////////////////////////////////////////////
// Command line usage
//////////////////////////////////////////////////////////////////////

static void
usage(const char *cmd) {
	const char *cp = strrchr(cmd,'/');

	if ( cp )
		cmd = cp + 1;

	fprintf(stderr,
		"Usage: %s [-b baudrate] [-d /dev/usbserial] [-v] [-h] [ntpserver1...]\n"
		"where options include:\n"
		"\t-b baudrate\tSerial baud rate (115200)\n"
		"\t-d device\tSerial device pathname\n"
		"\t-v\t\tVerbose output mode\n"
		"\t-h\t\tThis help info.\n",
		cmd);
	exit(0);
}

//////////////////////////////////////////////////////////////////////
// The other thread, used for receiving from the ESP device
//////////////////////////////////////////////////////////////////////

static volatile bool stop = false;

static void *
receiver(void *arg) {
	ESP8266& esp = *(ESP8266 *)arg;

	while ( !stop )
		esp.receive();
	return 0;
}

//////////////////////////////////////////////////////////////////////
// Provide the name(s) of time servers on the command line
//////////////////////////////////////////////////////////////////////

int
main(int argc,char **argv) {
	static const char options[] = ":b:d:vh";
	termios ios, svios;
	int rc, optch, er = 0;

	//////////////////////////////////////////////////////////////
	// Parse command line options
	//////////////////////////////////////////////////////////////

	while ( (optch = getopt(argc,argv,options)) != -1 ) {
		switch ( optch ) {
		case 'b':
			opt_baudrate = atoi(optarg);
			break;
		case 'd':
			opt_device = optarg;
			break;
		case 'v':
			opt_verbose = true;
			break;
		case 'h':	
			usage(argv[0]);
			break;
		case ':':
			fprintf(stderr,"Missing argument for -%c\n",optopt);
			++er;
			break;
		default:
			fprintf(stderr,"Invalid option -%c\n",optopt);
			++er;
		}
	}

	if ( er > 0 ) {
		fprintf(stderr,"Use option -h for more information.\n");
		exit(1);	// Command line option error(s)
	}

	if ( opt_baudrate < 300 || opt_baudrate > 115200 ) {
		fprintf(stderr,"Invalid baud rate -b %d\n",opt_baudrate);
		exit(2);
	}

	//////////////////////////////////////////////////////////////
	// Open serial device
	//////////////////////////////////////////////////////////////

	fd = open(opt_device,O_RDWR);
	if ( fd == -1 ) {
		fprintf(stderr,"%s: Opening serial device %s for r/w\n",
			strerror(errno),
			opt_device);
		exit(3);
	}

	//////////////////////////////////////////////////////////////
	// Setup device for raw I/O
	//////////////////////////////////////////////////////////////

	rc = tcgetattr(fd,&ios);
	svios = ios;
	assert(!rc);
	cfmakeraw(&ios);
	cfsetspeed(&ios,opt_baudrate);
	ios.c_cflag |= CRTSCTS;		// Hardware flow control on

	rc = tcsetattr(fd,TCSADRAIN,&ios);
	if ( rc == -1 ) {
		fprintf(stderr,"%s: setting raw device %s to baud_rate %d\n",
			strerror(errno),
			opt_device,
			opt_baudrate);
		exit(2);
	}

	//////////////////////////////////////////////////////////////
	// Start execution
	//////////////////////////////////////////////////////////////

	PCoroutine rx(cr_mutex,receiver,&esp);	// Start rx thread

	if ( !esp.start() ) {
		fprintf(stderr,"Unable to start ESP8266\n");
		exit(3);
	}

	if ( optind < argc ) {
		for ( ; optind < argc; ++optind ) {
			while ( !ntp_time(argv[optind]) ) {
				sleep(2);
				printf("Retrying %s\n",argv[optind]);
			}
		}
	} else	{
		const char *srv = "0.ca.pool.ntp.org";

		while ( !ntp_time(srv) ) {
			sleep(2);
			printf("Retrying %s\n",srv);
		}
	}

	//////////////////////////////////////////////////////////////
	// Stop the receiving thread
	//////////////////////////////////////////////////////////////

	stop = true;
	for ( int x=0; x<256; ++x )
		yield();		// Give rx several chances to see stop

	rx.join();			// Wait for rx thread to return

	//////////////////////////////////////////////////////////////
	// Restore terminal parameters
	//////////////////////////////////////////////////////////////

	rc = tcsetattr(fd,TCSADRAIN,&svios);
	close(fd);
	return 0;
}

// End ntp_rots.cpp
