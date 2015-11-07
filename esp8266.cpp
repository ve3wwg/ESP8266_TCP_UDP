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
#else
#define RX(x)
#define CMD(x)
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

	first = 0;

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
	temp = 0;
	error = Ok;

	recv_ipinfo = 0;
	recv_mac = 0;
	accept_cb = 0;
}

//////////////////////////////////////////////////////////////////////
// Destructor
//////////////////////////////////////////////////////////////////////

ESP8266::~ESP8266() {
	release();
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
// Read into temp buffer of maxlen chars, returning stop char
//////////////////////////////////////////////////////////////////////

char
ESP8266::read_temp(int maxlen,char stop) {
	int x = 0;
	char b;

	temp = (char *)realloc(temp,maxlen+1);
	while ( (b = readb()) != stop && b != '\r' ) {
		if ( x < maxlen )
			temp[x++] = b;
	}
	temp[x] = 0;

	if ( x + 5 < maxlen+1 )
		temp = (char *)realloc(temp,x+1);
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
		{ "+CIPAP:ip:\"", 	2,	0x0102 },
		{ "+CIPAP:gateway:\"",	7,	0x0112 },
		{ "+CIPAP:netmask:\"",	7,	0x0122 },
		{ "+CIPAPMAC:\"", 	6,	0x0103 },
		{ "+CIPSTA:ip:\"",	4,	0x0104 },
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
				case 0x0111:	// +CWJAP:"...
					wifi_connected = 1;
					break;
				case 0x0102:	// "+CIPAP:ip:\""
					b = read_temp(40,'"');
					if ( recv_ipinfo ) {
						recv_ipinfo(IP_Addr,temp);
					} else	{
						// Invoked by is_wifi(bool got_ip=true)
						wifi_got_ip = strcmp(temp,"0.0.0.0") != 0;
					}
					break;
				case 0x0112:	// "+CIPAP:gateway:\""
					b = read_temp(40,'"');
					if ( recv_ipinfo )
						recv_ipinfo(Gateway,temp);
					break;
				case 0x0122:	// "+CIPAP:netmask:\""
					b = read_temp(40,'"');
					if ( recv_ipinfo )
						recv_ipinfo(NetMask,temp);
					break;
				case 0x0103:	// "+CIPAPMAC:\"",
					b = read_temp(40,'"');
					if ( recv_mac )
						recv_mac(temp);
					break;
				case 0x0104:	// "+CIPSTA:ip:\"",
					b = read_temp(40,'"');
					recv_ipinfo(IP_Addr,temp);
					break;
				case 0x0114:	// "+CIPSTA:gateway:\"",
					b = read_temp(40,'"');
					recv_ipinfo(Gateway,temp);
					break;
				case 0x0124:	// "+CIPSTA:netmask:\"",
					b = read_temp(40,'"');
					recv_ipinfo(NetMask,temp);
					break;
				case 0x0105:	// "+CIPSTAMAC:\"",
					b = read_temp(40,'"');
					if ( recv_mac )
						recv_mac(temp);
					break;
				case 0x0106:	// +CIPSTO:
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
					if ( version )
						free(version);
					version = (char *)malloc(80);
					for ( ss=0; ss + 1 < 80; ++ss ) {
						b = readb();
						if ( b == '\r' )
							break;
						version[ss] = b;
					}
					version[ss] = 0;
					// Now read until we get OK
					for (;;) {
						while ( (b = readb()) != '\n' );
						if ( readb() != 'O' )
							continue;
						if ( readb() != 'K' )
							continue;
						if ( readb() == '\r' )
							break;
					}
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
// Reset the ESP8266
//////////////////////////////////////////////////////////////////////

void
ESP8266::reset(bool wait_wifi_connect) {

	receive();

	// Reset
	ready = 0;
	first = '\n';
	CMD("AT+RST");
	command("AT+RST");

	while ( !ready ) {
		receive();
		idle();		
	}

	// Disable echo
	CMD("ATE0");
	do	{
		command("ATE0");
	} while ( !waitokfail() );

	if ( wait_wifi_connect )
		wait_wifi(true);	// Wait until WIFI CONNECT; WIFI GOT IP messages appear

	CMD("AT+CIPMODE=0");
	command("AT+CIPMODE=0");
	waitokfail();			// Ignore status for now

	CMD("AT+CIPMUX=1");
	command("AT+CIPMUX=1");
	waitokfail();
}

//////////////////////////////////////////////////////////////////////
// Set operational parameters
//////////////////////////////////////////////////////////////////////

bool
ESP8266::start() {

	// Disable echo
	CMD("ATE0");
	do	{
		command("ATE0");
	} while ( !waitokfail() );

	get_version();

	CMD("AT+CWJAP?");
	// This will set wifi_connected = 0 if "No AP" message is seen
	command("AT+CWJAP?");
	if ( !waitokfail() )
		return false;

	if ( wifi_connected ) {
		CMD("AT+CIPMODE=0");
		command("AT+CIPMODE=0");
		return waitokfail();
	}

	return true;		// WIFI connected
}

//////////////////////////////////////////////////////////////////////
// Wait until WIFI CONNECTED occurs.
//////////////////////////////////////////////////////////////////////

void
ESP8266::wait_wifi(bool got_ip) {

	do	{
		receive();
		idle();
	} while ( !wifi_connected );

	if ( got_ip ) {
		do	{
			receive();
			idle();
		} while ( !wifi_got_ip );
	}
}

//////////////////////////////////////////////////////////////////////
// Return true if we have WIFI (AP), optionally with IP
//////////////////////////////////////////////////////////////////////

bool
ESP8266::is_wifi(bool got_ip) {

	// This will set wifi_connected = 0 if "No AP" message is seen

	CMD("AT+CWJAP?");
	command("AT+CWJAP?");
	if ( !waitokfail() )
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

	recv_ipinfo_t save_recv_ipinfo = recv_ipinfo;	// Save callback
	recv_ipinfo = 0;				// Disable it for now

	CMD("AT+CIPAP?");
	command("AT+CIPAP?");
	waitokfail();

	recv_ipinfo = save_recv_ipinfo;			// Restore cb, if any

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
// Read until we get OK/FAIL
//////////////////////////////////////////////////////////////////////

bool
ESP8266::waitokfail() {
	resp_ok = resp_fail = resp_error = 0;

	do	{
		receive();
		if ( !resp_fail && !resp_ok && !resp_error )
			idle();
	} while ( !resp_fail && !resp_ok && !resp_error );

	return resp_ok;
}

//////////////////////////////////////////////////////////////////////
// Start TCP connect to host, port. Returns socket if successful,
// else -1 if an error occurred.
//////////////////////////////////////////////////////////////////////

int
ESP8266::tcp_connect(const char *host,int port,recv_func_t rx_cb) {
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

	receive();

	s_state& s = state[sock];

	s.open = 1;	// Mark it as allocated (for now)
	s.disconnected = 0;

	resp_id = 0;
	resp_connected = 0;
	resp_closed = 0;
	resp_dnsfail = 0;
	resp_error = 0;
	resp_ok = 0;

	// Try to connect
	write("AT+CIPSTART=");
	writeb('0' + sock);
	write(",\"TCP\",\"");
	write(host);
	write("\",");

	// Convert port to string
	{
		char portbuf[16];
		const char *portstr = int2str(port,portbuf,sizeof portbuf);
		write(portstr);
		crlf();
	}

	do	{
		receive();
		if ( resp_error ) {
			if ( resp_dnsfail )
				error = DNS_Fail;
			else	error = Fail;
			s.open = 0;
			return -1;
		}
		if ( !resp_ok )
			idle();
	} while ( !resp_ok );

	s.connected = 1;
	s.rxcallback = rx_cb;
	return sock;
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
// Write to a socket.
//////////////////////////////////////////////////////////////////////

int
ESP8266::write(int sock,const char *data,int bytes) {
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
			receive();
			if ( !send_ready )
				idle();
		} while ( !send_ready );

		first = 0;
		int count = bytes;
		while ( count-- > 0 )
			writeb(*data++);

		do	{
			receive();
			if ( !(send_ok || send_fail) )
				idle();
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
// Return ESP8266 Version Info
//////////////////////////////////////////////////////////////////////

const char *
ESP8266::get_version() {

	if ( version ) {
		free(version);
		version = 0;
	}

	CMD("AT+GMR");	
	command("AT+GMR");
	do	{
		receive();
		if ( !version )
			idle();
	} while ( !version );

	return version;
}

//////////////////////////////////////////////////////////////////////
// Request IP/Gateway and Netmask Info
//////////////////////////////////////////////////////////////////////

bool
ESP8266::get_ap_info(recv_ipinfo_t callback) {

	this->recv_ipinfo = callback;

	CMD("AT+CIPAP?");
	command("AT+CIPAP?");
	return waitokfail();
}

//////////////////////////////////////////////////////////////////////
// Request Station IP/Gateway and Netmask Info
//////////////////////////////////////////////////////////////////////

bool
ESP8266::get_station_info(recv_ipinfo_t callback) {
	this->recv_ipinfo = callback;

	CMD("AT+CIPSTA?");
	command("AT+CIPSTA?");
	return waitokfail();
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
ESP8266::get_ap_mac(recv_mac_t callback) {

	this->recv_mac = callback;

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
ESP8266::get_station_mac(recv_mac_t callback) {

	this->recv_mac = callback;

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
// Release any unessential storage
//////////////////////////////////////////////////////////////////////

void
ESP8266::release() {
	if ( version ) {
		free(version);
		version = 0;
	}
}

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

// End esp8266.cpp
