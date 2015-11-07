//////////////////////////////////////////////////////////////////////
// posix.cpp -- POSIX Test Program using serial to ESP8266
// Date: Mon Oct 26 20:22:30 2015  (C) Warren W. Gay VE3WWG 
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <termios.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>

#include "esp8266.hpp"

static ESP8266 *esp_ptr = 0;

static bool opt_verbose = false;
static const char *opt_device = "/dev/cu.usbserial-A50285BI";
static const char *opt_join = 0;
static const char *opt_password = 0;
static bool opt_resume = false;
static int opt_baudrate = 115200;
static const char *opt_connect = 0;
static int opt_port = 80;
static const char *opt_output = 0;
static const char *opt_dhcp = 0;
static const char *opt_ap_address = 0;
static const char *opt_station_address = 0;
static int opt_timeout = -1;
static int opt_listen = -1;
static bool opt_reset = false;
static bool opt_wait_wifi = false;

static int fd = -1;
static struct termios ios;
static FILE *output = 0;		// For opt_output

//////////////////////////////////////////////////////////////////////
// Write one byte to the usb serial adapter
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
// Read one byte from the usb serial adapter
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
// Poll for a byte to be read from the serial adapter. Returns true,
// if there is at least one byte to be read.
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
// Idle callback
//////////////////////////////////////////////////////////////////////

static void
idle() {
	usleep(100);
}

//////////////////////////////////////////////////////////////////////
// Used to receive response from tcp_connect() socket
//////////////////////////////////////////////////////////////////////

static void
rx_callback(int sock,int byte) {

	if ( byte == -1 ) {
		printf("Remote closed socket %d\n",sock);
	} else	{
		fputc(byte,output);
		fflush(output);
	}
}

//////////////////////////////////////////////////////////////////////
// Receive IP/Gateway/Netmask info
//////////////////////////////////////////////////////////////////////

static void
ip_info(ESP8266::IpGwMask itype,const char *info) {
	printf("itype=%d, info='%s'\n",itype,info);
}

//////////////////////////////////////////////////////////////////////
// Print AP/STA MAC Address
//////////////////////////////////////////////////////////////////////

static void
recv_mac(const char *mac_addr) {
	printf("MAC address = '%s'\n",mac_addr);
}

static void
server_recv(int sock,int byte) {

	if ( byte == -1 ) {
		printf("\nREMOTE CLOSED server socket %d\n",sock);
		close(sock);
	} else	{
		printf("Server byte = '%c' %02X\n",byte,byte);
	}
}

static void
accept_cb(int sock) {

	if ( sock >= 0 ) {
		printf("ACCEPTED server connect on sock = %d\n",sock);
		esp_ptr->accept(sock,server_recv);
	} else	{
		printf("SERVER has CLOSED due to reset.\n");
	}
}

//////////////////////////////////////////////////////////////////////
// Return usage information about this test program.
//////////////////////////////////////////////////////////////////////

static void
usage(const char *cmd) {
	const char *cp = strrchr(cmd,'/');

	if ( cp )
		cmd = cp + 1;

	fprintf(stderr,
		"Usage: %s [-R [-W]] [-c:] [-p:] [-b:] [-d:] [-j:] [-P:] [-r] [-o:] [-D:] [-A:] [-v] [-h]\n"
		"where\n"
		"\t-R\t\tBegin with ESP8266 reset\n"
		"\t-W\t\tWait for WIFI CONNECT + GOT IP\n"
		"\t-c host\t\tHost to connect to\n"
		"\t-p port\t\tDefault is port 80\n"
		"\t-d device\tSerial device pathname\n"
		"\t-j wifi_name\tWIFI network to join\n"
		"\t-P password\tWIFI passord (for -j)\n"
		"\t-r\t\tResume connection to last used WIFI\n"
		"\t-o file\t\tSend received output to file (default is stdout)\n"
		"\t-D {0|1}\tDisable/Enable DHCP\n"
		"\t-A ipaddr\tSet AP IP Address\n"
		"\t-T secs\t\tSet new timeout\n"
		"\t-L port\t\tListen on port\n"
		"\t-v\t\tVerbose output mode\n"
		"\t-h\t\tThis help info.\n"
		"\n"
		"When neither -j or -r used, -r is assumed.\n",
		cmd);
	exit(0);
}

//////////////////////////////////////////////////////////////////////
// Test main program
//////////////////////////////////////////////////////////////////////

int
main(int argc,char **argv) {
	static const char options[] = ":RWc:P:b:d:j:p:ro:D:A:S:T:L:vh";
	int rc, optch, er = 0;

	//////////////////////////////////////////////////////////////
	// Parse command line options
	//////////////////////////////////////////////////////////////

	while ( (optch = getopt(argc,argv,options)) != -1 ) {
		switch ( optch ) {
		case 'R':
			opt_reset = true;
			break;
		case 'W':
			opt_wait_wifi = true;
			break;
		case 'b':
			opt_baudrate = atoi(optarg);
			break;
		case 'c':
			opt_connect = optarg;
			break;
		case 'p':
			opt_port = atoi(optarg);
			break;
		case 'd':
			opt_device = optarg;
			break;
		case 'j':
			opt_resume = false;
			opt_join = optarg;
			break;
		case 'P':
			opt_resume = false;
			opt_password = optarg;
			break;
		case 'r':
			opt_resume = true;
			opt_join = 0;
			opt_password = 0;
			break;
		case 'o':
			opt_output = optarg;
			break;
		case 'D':
			opt_dhcp = optarg;
			break;
		case 'A':
			opt_ap_address = optarg;
			break;
		case 'S':
			opt_station_address = optarg;
			break;
		case 'T':
			opt_timeout = atoi(optarg);
			break;
		case 'L':
			opt_listen = atoi(optarg);
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

	if ( er > 0 || argc <= 1 ) {
		fprintf(stderr,"Use option -h for more information.\n");
		exit(1);	// Command line option error(s)
	}

	if ( opt_baudrate < 300 || opt_baudrate > 115200 ) {
		fprintf(stderr,"Invalid baud rate -b %d\n",opt_baudrate);
		exit(2);
	}

	if ( optind < argc ) {
		fprintf(stderr,"Dangling command line arguments. Use -h for more info.\n");
		exit(4);
	}

	if ( !(opt_join || opt_resume) )
		opt_resume = true;

	//////////////////////////////////////////////////////////////
	// Open output file (if any)
	//////////////////////////////////////////////////////////////

	if ( opt_output ) {
		output = fopen(opt_output,"w");
		if ( !output ) {
			fprintf(stderr,"%s: opening %s for write\n",
				strerror(errno),
				opt_output);
			exit(5);
		}
	} else	output = stdout;

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
	// Begin the test
	//////////////////////////////////////////////////////////////

	if ( opt_verbose )
		fprintf(stderr,"Opened %s for I/O at %d baud\n",
			opt_device,opt_baudrate);

	ESP8266 esp(writeb,readb,rpoll,idle);
	esp_ptr = &esp;
	bool ok;

	//////////////////////////////////////////////////////////////
	// Initialize the device
	//////////////////////////////////////////////////////////////

	if ( opt_reset ) {
		esp.reset(opt_wait_wifi);
		if ( opt_wait_wifi )
			esp.wait_wifi(true);
	} else	esp.start();

	if ( opt_resume ) {
		if ( !esp.is_wifi(false) ) {
			fprintf(stderr,"No Access Point established (-r)\n");
			exit(13);
		}
		if ( !esp.is_wifi(true) ) {
			fprintf(stderr,"No IP number for AP (-r)\n");
			exit(13);
		}
	} else if ( opt_join && opt_password ) {
		if ( opt_verbose )
			printf("Joining WIFI network -j %s\n",opt_join);
		ok = esp.ap_join(opt_join,opt_password);
		if ( opt_verbose || !ok )
			fprintf(stderr,"WIFI %s (-j)\n",
				ok ? "ok" : "failed");
		if ( !ok )
			exit(13);
	}

	printf("Version: %s\n",esp.get_version());
	esp.release();

	ok = esp.get_ap_info(ip_info);
	if ( !ok )
		fprintf(stderr,"Get AP Address Info failed.\n");

	if ( opt_ap_address ) {
		ok = esp.set_ap_addr(opt_ap_address);
		if ( !ok )
			fprintf(stderr,"Set AP Address failed.\n");
	}

	if ( opt_station_address ) {
		ok = esp.set_station_addr(opt_station_address);
		if ( !ok )
			fprintf(stderr,"Set Station Address failed.\n");
	}

	ok = esp.get_station_info(ip_info);
	if ( !ok )
		fprintf(stderr,"Get Station Address Info failed.\n");

	ok = esp.get_ap_mac(recv_mac);
	if ( !ok )
		fprintf(stderr,"Get AP Mac address failed.\n");

	ok = esp.get_station_mac(recv_mac);
	if ( !ok )
		fprintf(stderr,"Get Station Mac address failed.\n");

	int timeout = esp.get_timeout();
	printf("Timeout = %d\n",timeout);

	if ( opt_timeout >= 0 ) {
puts("Setting TIMEOUT");
		ok = esp.set_timeout(opt_timeout);
		if ( !ok )
			fprintf(stderr,"Setting timeout -T %d failed.\n",opt_timeout);
		timeout = esp.get_timeout();
		printf("Timeout now = %d\n",timeout);
	}

	int a = esp.get_autoconn();
	if ( a >= 0 )
		printf("Auto Connect = %s\n",a ? "ON" : "OFF");
	else	printf("Fail: get Auto Connect mode.\n");

	if ( opt_dhcp ) {
		bool on = *opt_dhcp != '0';

		ok = esp.dhcp(on);
		fprintf(stderr,"DHCP %s, %s\n",
			on ? "on" : "off",
			ok ? "ok" : "FAILED");
	}

	//////////////////////////////////////////////////////////////
	// If -c option given, connect to that host
	//////////////////////////////////////////////////////////////

	if ( opt_connect ) {
		if ( opt_verbose )
			printf("Connecting to %s\n",opt_connect);

		int sock = esp.tcp_connect(opt_connect,opt_port,rx_callback);
		if ( sock < 0 ) {
			fprintf(stderr,"%s: Connecting to %s port %d\n",
				esp.strerror(),
				opt_connect,opt_port);
			exit(13);
		}

		if ( opt_verbose )
			printf("Opened socket %d\n",sock);

		int sent = esp.write(sock,"GET /\r\n",7);
		if ( opt_verbose )
			printf("Sent %d bytes\n",sent);

		ok = esp.close(sock);
		if ( !ok ) {
			fprintf(stderr,"%s: close socket %d\n",
				esp.strerror(),
				sock);
			exit(13);
		} else if ( opt_verbose ) {
			printf("Closed sock %d ok\n",sock);
		}
	}

	//////////////////////////////////////////////////////////////
	// Listen on a port, if requested
	//////////////////////////////////////////////////////////////

	if ( opt_listen >= 0 ) {
		ok = esp.listen(opt_listen,accept_cb);
		if ( !ok )
			fprintf(stderr,"Listen failed.\n");
		else if ( opt_verbose )
			printf("Listening on port %d..\n",opt_listen);

		for (;;) {
			esp.receive();
			usleep(10);
		}
	}

	//////////////////////////////////////////////////////////////
	// Close serial device
	//////////////////////////////////////////////////////////////

	fflush(output);
	if ( opt_output )
		fclose(output);

	close(fd);
	fd = -1;
	return 0;
}

// End posix.cpp
