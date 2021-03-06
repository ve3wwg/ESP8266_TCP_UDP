///////////////////////////////////////////////////////////////////////
// esp8266.cpp -- Implementation of ESP8266 Class
// Date: Mon Oct 26 20:20:22 2015  (C) Warren W. Gay VE3WWG 
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include "esp8266.hpp"

#define DBG 	0

#if DBG
#define RX(x) printf("RX(%c)\n",x)
#define CMD(s) puts(s)
#define CMDX(s) fputs(s,stdout);
#define CMDC(c) putchar(c)
#else
#define NDEBUG 1
#define RX(x)
#define CMD(x)
#define CMDX(s)
#define CMDC(c)
#endif

//////////////////////////////////////////////////////////////////////
// Constructor
//////////////////////////////////////////////////////////////////////

ESP8266::ESP8266(write_func_t writeb,read_func_t readb,poll_func_t rpoll,idle_func_t idle) : writeb(writeb), readb(readb), rpoll(rpoll), idle(idle)  {
	clear(false);
}

void
ESP8266::clear(bool notify) {

	if ( notify && accept_cb )
		accept_cb(-1);				// Notify server of closure

	for ( int sock=0; sock<N_CONNECTION; ++sock ) {
		s_state& s = state[sock];
		if ( notify && s.open && !s.disconnected && s.rxcallback ) {
			s.rxcallback(sock,-1);		// Notify app of closure
		}
		s.open = 0;
		s.connected = s.disconnected = 0;
		s.rxcallback = 0;
	}

	channel = -1;		// Unknown
	strength = -1;

	first = '\n';

	ready = 0;
	wifi_connected = 0;
	wifi_got_ip = 0;

	resp_ok = resp_fail = 0;
	resp_error = 0;

	send_ready = send_fail = 0;
	send_ok = 0;

	resp_connected = 0;
	resp_closed = 0;
	resp_dnsfail = 0;

	version = 0;
	error = Ok;

	accept_cb = 0;

	bufsp = 0;
}

//////////////////////////////////////////////////////////////////////
// Destructor
//////////////////////////////////////////////////////////////////////

ESP8266::~ESP8266() {
}

//////////////////////////////////////////////////////////////////////
// Lookup a socket
//////////////////////////////////////////////////////////////////////

ESP8266::s_state *
ESP8266::lookup(int sock) {

	if ( sock < 0 || sock >= N_CONNECTION ) {
		error = Invalid;
		return 0;
	}

	return &state[sock];
}

//////////////////////////////////////////////////////////////////////
// Read an unsigned integer into this->resp_id, returning stop char
//////////////////////////////////////////////////////////////////////

char
ESP8266::read_id() {
	char b;

	resp_id = 0;
	while ( (b = readb()) >= '0' && b <= '9' )
		resp_id = resp_id * 10 + (b & 0x0F);
	return b;
}

//////////////////////////////////////////////////////////////////////
// Read into selected buffer
//////////////////////////////////////////////////////////////////////

char
ESP8266::read_buf(int bufx,char stop) {
	char *buf = bufsp[bufx].buf;
	int x = 0, maxlen = bufsp[bufx].bufsiz;
	char b;

	while ( (b = readb()) != stop && b != '\r' ) {
		if ( buf && x + 1 >= maxlen )
			break;
		if ( buf )
			buf[x++] = b;
	}
	if ( buf )
		buf[x] = 0;

	return skip_until(b,stop);
}	

//////////////////////////////////////////////////////////////////////
// Skip until stop char is read, else stop if CR is reached
//////////////////////////////////////////////////////////////////////

char
ESP8266::skip_until(char b,char stop) {

	do	{
		if ( b == stop )
			return b;
		b = readb();
	} while ( b != '\r' );
	return b;
}

//////////////////////////////////////////////////////////////////////
// Perform receive functions
//////////////////////////////////////////////////////////////////////

void
ESP8266::receive() {
	struct s_rxstate;
	static struct s_rxstate {
		const char	*pattern;
		short		start;
		short		stateno;
	} rxstate[] = {
		{ "+IPD,", 		0,	0x0100 },
		{ "+CWAUTOCONN:", 	1,	0x0101 },
		{ "+CWJAP:\"",		3,	0x0111 },
		{ "+CWSAP:\"",		3,	0x0134 },
		{ "+CIPAP:ip:\"", 	2,	0x0102 },
		{ "+CIPAP:gateway:\"",	7,	0x0112 },
		{ "+CIPAP:netmask:\"",	7,	0x0122 },
		{ "+CIPAPMAC:\"", 	6,	0x0103 },
		{ "+CIPSTA:ip:\"",	4,	0x0104 },
		{ "+CIPMODE:",		4,	0x0107 },
		{ "+CIPMUX",		5,	0x0108 },
		{ "+CIPSTA:gateway:\"",	8,	0x0114 },
		{ "+CIPSTA:netmask:\"",	8,	0x0124 },
		{ "+CIPSTAMAC:\"", 	7,	0x0105 },
		{ "+CIPSTO:",		6,	0x0106 },
		{ "OK", 		0,	0x0200 },
		{ "FAIL", 		0,	0x0201 },
		{ "ERROR", 		0,	0x0202 },
		{ "SEND OK", 		0,	0x0300 },
		{ ",CONNECT", 		0,	0x0400 },
		{ ",CLOSED", 		2,	0x0500 },
		{ "DNS Fail", 		0,	0x0600 },
		{ "WIFI DISCONNECT", 	0,	0x0700 },
		{ "WIFI CONNECT", 	5,	0x0701 },
		{ "WIFI GOT IP", 	5,	0x0702 },
		{ "AT version:", 	0,	0x0800 },
		{ "No AP",		0,	0x0900 },
		{ "ready\r", 		0,	0x7F00 },
		{ 0, 			0, 	0x0000 }
	};
	char b;

	while ( rpoll() ) {
		b = readb();
#if DBG >= 3
		printf("rx b='%c' %02X (first=%02X, s0=%d, ss=%d)\n",b,b,first,s0,ss);
#endif
		if ( b == '\n' ) {
			first = '\n';
			s0 = ss = 0;
			continue;
		}

		if ( first == '\n' ) {
			first = b;
			if ( first >= '0' && first <= '9' ) {
				first = '9';
				resp_id = 0;
			} else if ( first == '>') {
				send_ready = 1;
				first = 0;
#if DBG >= 2
				puts("))) SENDING>");
#endif
				continue;
			}
		} else if ( !first ) {
			continue;
		}
		
// printf("first=%04X, b='%c' %02X, s0=%d, ss=%d\n",first,b,b,s0,ss);

		if ( first == '9' ) {
			if ( b == ',' ) {
				first = b;
				s0 = ss = 0;
			} else	{
				resp_id = resp_id * 10 + ( b & 0x0F );
				continue;
			}
		}

		if ( !ss && first < 0x0100 ) {
			// Locate matching first character
			for ( s0 = 0; rxstate[s0].pattern && rxstate[s0].pattern[0] != first; ++s0 )
				;
			if ( !rxstate[s0].pattern ) {
				first = 0;
				continue;
			}
		}

		if ( b == rxstate[s0].pattern[ss] ) {
			if ( !rxstate[s0].pattern[++ss] ) {
#if DBG >= 2
				printf("STATE = 0x%04X (%s)\n",rxstate[s0].stateno,rxstate[s0].pattern);
#endif
				switch ( rxstate[s0].stateno ) {
				case 0x0100:	// "+IPD,",
					{
						b = read_id();
						ipd_id = resp_id;
						b = read_id();
						ipd_len = resp_id;
						// Stops on b=':'
						// Read session data
#if DBG
						printf("))) +IPD,%d,%d:\n",ipd_id,ipd_len);
#endif
						s_state *statep = lookup(ipd_id);
						recv_func_t rx_cb = statep ? statep->rxcallback : 0;

						while ( ipd_len > 0 ) {
							b = readb();
							--ipd_len;
							if ( rx_cb )
								rx_cb(ipd_id,b);
#if DBG
							else	printf(" +IPD(%d,ch='%c' %02X) bytes remaining %d\n",ipd_id,b,b,ipd_len);
#endif
						}
						if ( statep->udp && rx_cb )	// Is this a UDP socket?
							rx_cb(ipd_id,-1);	// yes, send -1 to indicate end of datagram
						first = '\n';
						ipd_id = ipd_len = 0;
						resp_id = 0;
						s0 = ss = 0;
					}
					continue;
				case 0x0101:	// "+CWAUTOCONN:",
					b = readb();
					resp_id = b == '0' ? 0 : 1;
					break;
				case 0x0111:	// +CWJAP:"NETGEAR67","c0:ff:d4:95:80:04",7,-66
					wifi_connected = 1;
					b = read_buf(0,'"');
					b = skip_until(0,'"');
					b = read_buf(1,'"');
					b = skip_until(b,',');
					b = read_buf(2,',');
					b = read_buf(3,'\r');
					break;
				case 0x0102:	// "+CIPAP:ip:\""
					b = read_buf(0,'"');
					if ( !wifi_got_ip ) {
						// Invoked by is_wifi(bool got_ip=true)
						if ( bufsp[0].buf )
							wifi_got_ip = strcmp(bufsp[0].buf,"0.0.0.0") != 0;
					}
					break;
				case 0x0112:	// "+CIPAP:gateway:\""
					b = read_buf(1,'"');
					break;
				case 0x0122:	// "+CIPAP:netmask:\""
					b = read_buf(2,'"');
					break;
				case 0x0103:	// "+CIPAPMAC:\"",
					b = read_buf(0,'"');
					break;
				case 0x0104:	// "+CIPSTA:ip:\"",
					b = read_buf(0,'"');
					break;
				case 0x0114:	// "+CIPSTA:gateway:\"",
					b = read_buf(1,'"');
					break;
				case 0x0124:	// "+CIPSTA:netmask:\"",
					b = read_buf(2,'"');
					break;
				case 0x0134:	// +CWSAP:"AI-THINKER_FA205E","",11,0
					assert(bufsp);
					b = read_buf(0,'"');
					b = skip_until(b,',');
					b = skip_until(b,'"');
					b = read_buf(1,'"');
					b = skip_until(b,',');
					b = read_buf(2,',');
					b = read_buf(3,'\r');
					first = 0;
					break;
				case 0x0105:	// "+CIPSTAMAC:\"",
					b = read_buf(0,'"');
					break;
				case 0x0106:	// +CIPSTO:
					b = read_id();
					break;
				case 0x0107:	// +CIPMODE:0
				case 0x0108:	// +CIPMUX:1
					b = read_id();
					break;
				case 0x0200:	// "OK",
					resp_ok = 1;
					break;
				case 0x0201:	// "FAIL",
					resp_fail = 1;
					break;
				case 0x0202:	// "ERROR",
					resp_error = 1;
					break;
				case 0x0300:	// "SEND OK",
					send_ok = 1;
					break;
				case 0x0400:	// ",CONNECT",
					{
						s_state *statep = lookup(resp_id);
						if ( statep && !statep->open ) {
							statep->open = 1;
							statep->connected = 1;
							statep->disconnected = 0;
							if ( accept_cb )
								accept_cb(resp_id);
						}
					}
					break;
				case 0x0500:	// ",CLOSED",
					{
						resp_closed = 1;
						s_state *statep = lookup(resp_id);
						if ( statep && statep->open ) {
							statep->connected = 0;
							if ( statep->rxcallback )
								statep->rxcallback(resp_id,-1);
							statep->disconnected = 1;
						}
					}
					break;
				case 0x0600:	// "DNS Fail",
					resp_dnsfail = 1;
					break;
				case 0x0700:	// "WIFI DISCONNECT",
					wifi_connected = 0;
					wifi_got_ip = 0;
					break;
				case 0x0701:	// "WIFI CONNECT",
					wifi_connected = 1;
					break;
				case 0x0702:	// "WIFI GOT IP",
					wifi_got_ip = 1;
					break;
				case 0x0800:	// "AT version:",
					b = read_buf(0,'\r');
					first = 0;
					break;
				case 0x0900:	// No AP
					wifi_connected = 0;
					wifi_got_ip = 0;
					break;
				case 0x7F00:	// "ready\r",
					clear(true);
					ready = 1;
					break;
				}
				first = 0;
			}
		} else	{
			bool matched = false;
			const char *cur = rxstate[s0].pattern;

			while ( rxstate[++s0].pattern && !strncmp(cur,rxstate[s0].pattern,ss) ) {
				if ( ss == rxstate[s0].start && rxstate[s0].pattern[ss] == b ) {
					matched = true;
					break;
				}
			}
			
			if ( !matched )
				first = 0;
			else	++ss;
		}
	}
	if ( idle )
		idle();
}

//////////////////////////////////////////////////////////////////////
// Ignore data until LF is read
//////////////////////////////////////////////////////////////////////

void
ESP8266::waitlf() {
	while ( readb() != '\n' )
		;
	first = '\n';
}

//////////////////////////////////////////////////////////////////////
// (Software) Reset the ESP8266
//////////////////////////////////////////////////////////////////////

bool
ESP8266::reset() {

	YIELD();

	// Reset
	ready = 0;
	first = '\n';
	CMD("AT+RST");
	command("AT+RST");

	while ( !ready )
		YIELD();

	return start();
}

//////////////////////////////////////////////////////////////////////
// Here we assume that the ESP8266 pin has been activated and now
// must wait for the reception of the "ready" message. Currently
// this method waits forever if the ready message does not arrive.
//////////////////////////////////////////////////////////////////////

bool
ESP8266::wait_reset() {

	ready = 0;
	while ( !ready )
		YIELD();
	return start();
}

//////////////////////////////////////////////////////////////////////
// Set operational parameters:
//
// This method simply turns off echo (ATE0), sets AT+CIPMODE=0 and
// makes certain that we have AT+CIPMUX=1 mode established for TCP/UDP.
//////////////////////////////////////////////////////////////////////

bool
ESP8266::start() {

	// Disable echo
	CMD("ATE0");
	command("ATE0");
	if ( !waitokfail() )
		return false;

	if ( !set_cipmode(0) )	// Check/set AT+CIPMODE=0
		return false;

	if ( !set_cipmux(1) )	// Check/set AT+CIPMUX=1
		return false;

	close_all();

	return true;		// WIFI connected
}

//////////////////////////////////////////////////////////////////////
// Wait until WIFI CONNECTED occurs.
//////////////////////////////////////////////////////////////////////

void
ESP8266::wait_wifi(bool got_ip) {

	do	{
		YIELD();
	} while ( !wifi_connected );

	if ( got_ip ) {
		do	{
			YIELD();
		} while ( !wifi_got_ip );
	}
}

//////////////////////////////////////////////////////////////////////
// Return true if we have WIFI (AP), optionally with IP
//////////////////////////////////////////////////////////////////////

bool
ESP8266::is_wifi(bool got_ip) {
	int ch, db;

	if ( !get_ap_ssid(0,0,0,0,ch,db) )
		return false;

	if ( !got_ip )
		return wifi_connected;

	//////////////////////////////////////////////////////////////
	// AT+CIPAP?
	// +CIPSTA:ip:"192.168.0.73"
	// +CIPSTA:gateway:"192.168.0.1"
	// +CIPSTA:netmask:"255.255.255.0"
	// 
	// OK
	//////////////////////////////////////////////////////////////

	char ip[32];

	if ( !get_ap_info(ip,sizeof ip,0,0,0,0) )
		return false;

	return wifi_got_ip;
}

//////////////////////////////////////////////////////////////////////
// Access point connect
//////////////////////////////////////////////////////////////////////

bool
ESP8266::ap_join(const char *ap,const char *passwd) {
	bool bf = true;

	resp_id = 0;
	resp_connected = 0;
	resp_closed = 0;
	resp_dnsfail = 0;
	resp_error = 0;

	// Connect to WIFI AP:
	// AT+CWJAP="ssid","password"
	write("AT+CWJAP=\"");
	write(ap);
	write("\",\"");
	if ( passwd )
		write(passwd);
	write("\"");
	crlf();

	bf = waitokfail();
	if ( !bf )
		error = Fail;
	return bf;
}

//////////////////////////////////////////////////////////////////////
// Write CRLF
//////////////////////////////////////////////////////////////////////

void
ESP8266::crlf() {
	writeb('\r');
	writeb('\n');
}

//////////////////////////////////////////////////////////////////////
// Write a nul terminated string
//////////////////////////////////////////////////////////////////////

void
ESP8266::write(const char *str) {
	while ( *str )
		writeb(*str++);
}

//////////////////////////////////////////////////////////////////////
// Write a command
//////////////////////////////////////////////////////////////////////

void
ESP8266::command(const char *cmd) {
	
	resp_ok = resp_fail = resp_error = 0;
	ESP8266::write(cmd);
	crlf();
}

//////////////////////////////////////////////////////////////////////
// Issue ESP command and wait for OK/FAIL/ERROR (returns true for OK)
//////////////////////////////////////////////////////////////////////

bool
ESP8266::commandok(const char *cmd) {
	command(cmd);
	return waitokfail();
}

//////////////////////////////////////////////////////////////////////
// Read until we get OK/FAIL
//////////////////////////////////////////////////////////////////////

bool
ESP8266::waitokfail() {
	resp_ok = resp_fail = resp_error = 0;

	do	{
		YIELD();
	} while ( !resp_fail && !resp_ok && !resp_error );

	return resp_ok;
}

//////////////////////////////////////////////////////////////////////
// Start a TCP or UDP socket
//////////////////////////////////////////////////////////////////////

int
ESP8266::socket(const char *socktype,const char *host,int port,recv_func_t rx_cb,int local_port) {
	int sock = -1;

	// Allocate a socket
	for ( int x=0; x<N_CONNECTION; ++x ) {
		if ( !state[x].open ) {
			sock = x;
			break;
		}
	}

	if ( sock == -1 ) {
		error = Resource;
		return -1;		// No free sessions
	}

	YIELD();

	s_state& s = state[sock];

	s.open = 1;	// Mark it as allocated (for now)
	s.udp = socktype[0] == 'U';
	s.disconnected = 0;

	resp_id = 0;
	resp_connected = 0;
	resp_closed = 0;
	resp_dnsfail = 0;
	resp_error = 0;
	resp_ok = 0;

	// Try to connect
	CMDX("AT+CIPSTART=");
	write("AT+CIPSTART=");

	CMDC('0' + sock);
	writeb('0' + sock);
	
	CMDX(",\"");
	write(",\"");

	CMDX(socktype);
	write(socktype);

	CMDX("\",\"");
	write("\",\"");
	CMDX(host);
	write(host);
	CMDX("\",");
	write("\",");

	// Convert port to string
	{
		char portbuf[16];
		const char *portstr = int2str(port,portbuf,sizeof portbuf);
		CMDX(portstr);
		write(portstr);
	}

	if ( local_port >= 0 ) {
		char lportbuf[16];
		const char *lportstr = int2str(local_port,lportbuf,sizeof lportbuf);
		CMDC(',');
		writeb(',');
		CMDX(lportstr);
		write(lportstr);
		CMDX(",2");
		write(",2");
	}

	CMDC('\n');
	crlf();

	do	{
		YIELD();
		if ( resp_error ) {
			if ( resp_dnsfail )
				error = DNS_Fail;
			else	error = Fail;
			s.open = 0;
			return -1;
		}
	} while ( !resp_ok );

	s.connected = 1;
	s.rxcallback = rx_cb;
	return sock;
}

//////////////////////////////////////////////////////////////////////
// Start TCP connect to host, port. Returns socket if successful,
// else -1 if an error occurred.
//////////////////////////////////////////////////////////////////////

int
ESP8266::tcp_connect(const char *host,int port,recv_func_t rx_cb) {
	return socket("TCP",host,port,rx_cb,-1);
}

//////////////////////////////////////////////////////////////////////
// Open a UDP socket for sending to host and port
//////////////////////////////////////////////////////////////////////

int
ESP8266::udp_socket(const char *host,int port,recv_func_t rx_cb,int local_port) {
	return socket("UDP",host,port,rx_cb,local_port);
}

//////////////////////////////////////////////////////////////////////
// Close a socket.
//////////////////////////////////////////////////////////////////////

bool
ESP8266::close(int sock) {
	s_state *statep = lookup(sock);
	bool ok;

	if ( !statep || !statep->open ) {
		error = Invalid;
		return false;
	}

	statep->open = 0;
	if ( !statep->connected )
		return true;
	{
		char sockbuf[16];
		const char *sockstr = int2str(sock,sockbuf,sizeof sockbuf);

		write("AT+CIPCLOSE=");
		write(sockstr);
		crlf();
	}

	state[sock].connected = 0;
	ok = waitokfail();
	if ( !ok )
		error = Fail;
	else	statep->open = 0;	// Closed

	return ok;
}

//////////////////////////////////////////////////////////////////////
// Close all sockets (ignoring errors)
//////////////////////////////////////////////////////////////////////

void
ESP8266::close_all() {
	for ( int s=0; s<N_CONNECTION; ++s ) {
		close(s);			// Attempt to close on ESP side
		state[s].open = 0;		// Force close on our side
	}
}

//////////////////////////////////////////////////////////////////////
// Write to a socket.
//////////////////////////////////////////////////////////////////////

int
ESP8266::write(int sock,const char *data,int bytes,const char *udp_address) {
	char session;
	int wlen, tlen = 0;
	bool bf;

	s_state *statep = lookup(sock);

	if ( !statep || !data || bytes < 0 ) {
		error = Invalid;
		return -1;
	}

	if ( statep->disconnected ) {
		error = Disconnected;
		return -1;
	} else if ( udp_address && !statep->udp ) {
		error = Invalid;
		return -1;
	} else if ( bytes == 0 )
		return 0;

	while ( bytes > 0 ) {
		if ( (wlen = bytes) > 1500 )
			wlen = 1500;

		send_ready = 0;
		send_ok = 0;
		send_fail = 0;

		session = '0' + sock;
		write("AT+CIPSEND=");
		writeb(session);
		writeb(',');

		if ( udp_address ) {
			// Not supported on all ESP devices
			writeb('"');
			write(udp_address);
			write("\",");
		}

		{
			char buf[16];
			const char *bytestr = int2str(bytes,buf,sizeof buf);
			
			write(bytestr);
			crlf();
		}

		bf = waitokfail();
		if ( !bf ) {
			error = Fail;
			return -1;
		}

		do	{
			YIELD();
		} while ( !send_ready );

		first = 0;
		int count = bytes;
		while ( count-- > 0 )
			writeb(*data++);

		do	{
			YIELD();
		} while ( !(send_ok || send_fail) );

		if ( send_fail )
			break;

		tlen += wlen;
		bytes -= wlen;
	}

	if ( !send_ok )
		error = Fail;

	return send_ok ? tlen : -1;
}

//////////////////////////////////////////////////////////////////////
// Return ESP8266 Version Info (only the AT version line is returned)
//
// AT+GMR
// AT version:0.25.0.0(Jun  5 2015 16:27:16)
// SDK version:1.1.1
// Ai-Thinker Technology Co. Ltd.
// Jun 23 2015 23:23:50
// OK
//////////////////////////////////////////////////////////////////////

bool
ESP8266::get_version(char *buf,int bufsiz) {
	s_bufs bufs[] = {
		{ buf, bufsiz }
	};

	this->bufsp = bufs;

	CMD("AT+GMR");	
	command("AT+GMR");
	if ( !waitokfail() ) {
		*buf = 0;
		return false;
	}

	this->bufsp = 0;

	return true;
}

//////////////////////////////////////////////////////////////////////
// This method returns the name (if any) of the joined Access Point.
// Optionally, the mac address is also returned (set mac=0 to not
// allocate a buffer). Finally, the channel and signal strength is
// returned.
//////////////////////////////////////////////////////////////////////

bool
ESP8266::get_ap_ssid(char *ssid,int ssid_size,char *mac,int mac_size,int& chan,int& db) {
	// +CWJAP:"NETGEAR67","c0:ff:d4:95:80:04",7,-66
	char chbuf[6], dbbuf[8];
	s_bufs bufs[] = {
		{ ssid, ssid_size },
		{ mac, mac_size },
		{ chbuf, sizeof chbuf },
		{ dbbuf, sizeof dbbuf }
	};

	this->bufsp = bufs;

	CMD("AT+CWJAP?");
	command("AT+CWJAP?");
	
	if ( !waitokfail() ) {
		this->bufsp = 0;
		return false;
	}

	this->bufsp = 0;
	this->channel = chan = str2int(chbuf);
	this->strength = db = str2int(dbbuf);
	return true;
}

//////////////////////////////////////////////////////////////////////
// Request IP/Gateway and Netmask Info
//////////////////////////////////////////////////////////////////////

bool
ESP8266::get_ap_info(char *ip,int ipsiz,char *gw,int gwsiz,char *nm,int nmsiz) {
	s_bufs bufs[] = {
		{ ip, gwsiz },
		{ gw, gwsiz },
		{ nm, nmsiz }
	};
	bool ok;

	this->bufsp = bufs;

	CMD("AT+CIPAP?");
	command("AT+CIPAP?");
	ok = waitokfail();

	if ( !ok ) {
		if ( ip )
			*ip = 0;
		if ( gw )
			*gw = 0;
		if ( nm )
			*nm = 0;
	}

	return ok;
}

//////////////////////////////////////////////////////////////////////
// Request Station IP/Gateway and Netmask Info
//////////////////////////////////////////////////////////////////////

bool
ESP8266::get_station_info(char *ip,int ipsiz,char *gw,int gwsiz,char *nmask,int nmasksiz) {
	s_bufs bufs[3] = {
		{ ip, ipsiz },
		{ gw, gwsiz },
		{ nmask, nmasksiz }
	};
	bool ok;

	this->bufsp = bufs;

	CMD("AT+CIPSTA?");
	command("AT+CIPSTA?");

	ok = waitokfail();
	if ( !ok ) {
		error = Fail;
		if ( ip )
			*ip = 0;
		if ( gw )
			*gw = 0;
		if ( nmask )
			*nmask = 0;
	}
	return ok;
}

//////////////////////////////////////////////////////////////////////
// Set the Access Point IP Address
//////////////////////////////////////////////////////////////////////

bool
ESP8266::set_ap_addr(const char *ip_addr) {

	CMD("AT+CIPAP=...");
	write("AT+CIPAP=\"");
	write(ip_addr);
	write("\"\r\n");

	return waitokfail();
}

//////////////////////////////////////////////////////////////////////
// Set the Access Point IP Address
//////////////////////////////////////////////////////////////////////

bool
ESP8266::set_station_addr(const char *ip_addr) {

	CMD("AT+CIPSTA=...");
	write("AT+CIPSTA=\"");
	write(ip_addr);
	write("\"\r\n");

	return waitokfail();
}

bool
ESP8266::get_ap_mac(char *mac,int macsiz) {
	s_bufs bufs[] = {
		{ mac, macsiz }
	};

	this->bufsp = bufs;

	CMD("AT+CIPAPMAC?");
	command("AT+CIPAPMAC?");
	return waitokfail();
}

bool
ESP8266::set_ap_mac(const char *mac_addr) {

	CMD("AT+CIPAPMAC=...");
	write("AT+CIPAPMAC=\"");
	write(mac_addr);
	write("\"\r\n");

	return waitokfail();
}

bool
ESP8266::get_station_mac(char *mac,int macsiz) {
	s_bufs bufs[] = {
		{ mac, macsiz }
	};

	this->bufsp = bufs;

	CMD("AT+CIPSTAMAC?");
	command("AT+CIPSTAMAC?");
	return waitokfail();
}

bool
ESP8266::set_station_mac(const char *mac_addr) {

	CMD("AT+CIPSTAMAC=...");
	write("AT+CIPSTAMAC=\"");
	write(mac_addr);
	write("\"\r\n");

	return waitokfail();
}

int
ESP8266::get_timeout() {
	
	CMD("AT+CIPSTO?");
	command("AT+CIPSTO?");

	if ( !waitokfail() )
		return -1;
	return resp_id;
}

bool
ESP8266::set_timeout(int seconds) {
	char buf[16];
	const char *timeoutstr = int2str(seconds,buf,sizeof buf);

	CMD("AT+CIPSTO=...");
	write("AT+CIPSTO=");
	write(timeoutstr);
	crlf();

	return waitokfail();
}

int
ESP8266::get_autoconn() {
	bool rf;

	CMD("AT+CWAUTOCONN?");
	resp_id = 0;
	command("AT+CWAUTOCONN?");
	rf = waitokfail();
	if ( !rf ) {
		error = Fail;
		return -1;
	}
		
	return resp_id;
}

bool
ESP8266::set_autoconn(bool on) {

	CMD("AT+CWAUTOCONN=...");
	write("AT+CWAUTOCONN=");
	write(on ? "1" : "0");
	crlf();
	return waitokfail();
}

bool
ESP8266::listen(int port,accept_t accp_cb) {
	char buf[16];
	const char *portstr = int2str(port,buf,sizeof buf);

	this->accept_cb = accp_cb;

	CMD("AT+CIPSERVER=1,..");
	write("AT+CIPSERVER=1,");
	write(portstr);
	crlf();
	return waitokfail();
}

void
ESP8266::accept(int sock,recv_func_t recv_cb) {
	s_state *sockp = lookup(sock);

	if ( sockp ) {
		sockp->rxcallback = recv_cb;
	}
}

bool
ESP8266::unlisten() {

	CMD("AT+CIPSERVER=0");
	command("AT+CIPSERVER=0");
	return waitokfail();
}

//////////////////////////////////////////////////////////////////////
// Enable/Disable DHCP
//////////////////////////////////////////////////////////////////////

bool
ESP8266::dhcp(bool on) {

	CMD("AT+CWDHCP=2,..");
	write("AT+CWDHCP=2,");
	write(on ? "1" : "0");
	crlf();

	return waitokfail();
}

//////////////////////////////////////////////////////////////////////
// Get the response to AT+CIPMODE?
//////////////////////////////////////////////////////////////////////

int
ESP8266::get_cipmode() {

	CMD("AT+CIPMODE?");
	command("AT+CIPMODE?");
	if ( !waitokfail() ) {
		error = Fail;
		return -1;
	}
	return resp_id;
}

//////////////////////////////////////////////////////////////////////
// Check/set the AT+CIPMODE=n (0 == normal, 1 == "unvarnished")
//
// Note:
//	Setting this option can only be done in certain "modes".
//	To avoid getting the message:
//
//	CIPMUX and CIPSERVER must be 0
//	ERROR
//
//	we check it first. If the mode agrees, we can return ok.
//	If it differs, then we must make the attempt to set it.
//////////////////////////////////////////////////////////////////////

bool
ESP8266::set_cipmode(int mode) {

	if ( get_cipmode() == mode )
		return true;

	char buf[12];
	const char *cp = int2str(mode,buf,sizeof buf);
	CMDX("AT+CIPMODE=");
	CMD(cp);
	write("AT+CIPMODE=");
	command(cp);
	return waitokfail();
}

//////////////////////////////////////////////////////////////////////
// Get the AT+CIPMUX? mode
// 
// AT+CIPMUX?
// +CIPMUX:1
// OK
//////////////////////////////////////////////////////////////////////

int
ESP8266::get_cipmux() {

	CMD("AT+CIPMUX?");
	command("AT+CIPMUX?");
	if ( !waitokfail() ) {
		error = Fail;
		return -1;
	}
	return resp_id;
}

//////////////////////////////////////////////////////////////////////
// AT+CIPMUX=<mode>
//////////////////////////////////////////////////////////////////////

bool
ESP8266::set_cipmux(int mode) {

	if ( get_cipmux() == mode )	// Avoid setting, if state matches
		return true;

	char buf[12];
	const char *cp = int2str(mode,buf,sizeof buf);
	CMDX("AT+CIPMUX=");
	CMD(cp);
	write("AT+CIPMUX=");
	command(cp);
	return waitokfail();
}

//////////////////////////////////////////////////////////////////////
// Query AP Parameters:
// AT+CWSAP?
// +CWSAP:"AI-THINKER_FA205E","",11,0
// 
// OK
// Set:
// "ssid","pwd",ch,ecn
// ecn:
//	0 - Open
//	1 - WPA_PSK
//	2 - WPA2_PSK
//	3 - WPA_WPA2_PSK
// OK
//////////////////////////////////////////////////////////////////////

bool
ESP8266::query_softap(char *ssid,int ssidsiz,char *pw,int pwsiz,int& ch,AP_Ecn& ecn) {
	char chbuf[8], ecnbuf[8];
	struct s_bufs bufs[4] = {
		{ ssid, ssidsiz },
		{ pw, pwsiz },
		{ chbuf, sizeof chbuf },
		{ ecnbuf, sizeof ecnbuf }
	};
	bool ok;

	this->bufsp = bufs;

	CMD("AT+CWSAP?");
	command("AT+CWSAP?");

	ok = waitokfail();
	if ( ok ) {
		ch = str2int(chbuf);
		ecn = AP_Ecn(str2int(ecnbuf));
	} else	{
		if ( ssid )
			*ssid = 0;
		if ( pw )
			*pw = 0;
		ch = -1;
		ecn = Ecn_Undefined;
		error = Fail;
	}
	this->bufsp = 0;
	return ok;
}

//////////////////////////////////////////////////////////////////////
// List AP's:
// AT+CWLAP
// +CWLAP:(2,"england",-74,"d8:eb:97:13:c6:9d",6)
// +CWLAP:(3,"largeshark2.4",-72,"b4:75:0e:fe:4b:bb",6)
// +CWLAP:(0,"largeshark-guest",-75,"b6:75:0e:fe:4b:bc",6)
// +CWLAP:(3,"The Room",-82,"24:a0:74:78:39:9c",11)
// +CWLAP:(4,"BrownTiger",-77,"c8:d7:19:01:68:53",7)
// +CWLAP:(3,"NETGEAR67",-57,"c0:ff:d4:95:80:04",11)
// 
// OK
// WIFI DISCONNECT
// WIFI CONNECTED
// WIFI GOT IP
//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////
// Quit AP:
// AT+CWQAP
//
// OK
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
// Return text for Error code
//////////////////////////////////////////////////////////////////////

const char *
ESP8266::strerror(Error err) const {
	static const char *serrors[] = {
		"Ok",
		"Fail",
		"Invalid",
		"DNS Fail",
		"Disconnected",
		"Resource"
	};

	return serrors[int(err)];
}

//////////////////////////////////////////////////////////////////////
// Replacement for sprintf() : Convert int v into string
//////////////////////////////////////////////////////////////////////

const char *
int2str(int v,char *buf,int bufsiz) {
	char *cp = buf + bufsiz - 1;
	char sgn;

	if ( v < 0 ) {
		sgn = '-';
		v = -v;
	} else	sgn = 0;

	*cp = 0;
	do	{
		*--cp = (v % 10) + '0';
		v /= 10;
	} while ( v != 0 && cp > buf );

	if ( sgn && cp > buf )
		*--cp = sgn;
	return cp;
}

//////////////////////////////////////////////////////////////////////
// Convert text s to string (no error check)
//////////////////////////////////////////////////////////////////////

int
str2int(const char *s) {
	int v = 0;
	bool neg = *s == '-' ? true : false;

	if ( neg )
		++s;

	while ( *s >= '0' && *s <= '9' )
		v = v * 10 + ((*s++) & 0x0F);

	if ( neg )
		return -v;
	return v;
}

// End esp8266.cpp
