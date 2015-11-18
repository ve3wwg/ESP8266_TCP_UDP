//////////////////////////////////////////////////////////////////////
// posntp.cpp -- POSIX test program : Get NTP Time
// Date: Tue Nov 17 22:12:45 2015  (C) Warren W. Gay VE3WWG 
//
// This program licensed under the GPL
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static void
utc_time(const char *host) {
	static const uint64_t ntp_offset = ((uint64_t(365)*70)+17)*24*60*60;
	static const unsigned char reqmsg[48] = {010,0,0,0,0,0,0,0,0};
	static const short port = 123;		// NTP
	const char *hostname = host ? host : "time.nrc.ca";
	struct hostent *hent = 0;
	struct sockaddr_in server_addr;
	uint32_t rxbuf[12];
	uint32_t ntp_time = 0;
	int s, rc;
	
	// Get UDP Socket
	s = socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if ( s == -1 ) {
		perror("socket(2)");
		exit(1);
	}

	// Looked up host name
	hent = gethostbyname2(hostname,AF_INET);
	if ( !hent || !hent->h_addr_list[0] ) {
		printf("Unknown host: %s\n",hostname);
		close(s);
		return;
	}

	// Initialize the server address:
	memset(&server_addr,0,sizeof server_addr);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = *(uint32_t *)hent->h_addr_list[0];

	// Loop until we get a time response
	do	{
		// Send request
		rc = sendto(s,reqmsg,sizeof reqmsg,0,(struct sockaddr *)&server_addr,sizeof server_addr);
		if ( rc == -1 ) {
			printf("%s: sendto(2) to %s\n",
				strerror(errno),
				hostname);
			close(s);
			return;
		}

		// Timeout if we wait too long:
		struct pollfd p = { s, POLLIN, 0 };
		rc = poll(&p,1,2000);
		if ( rc == 1 ) {
			// Got something to read:
			rc = recv(s,rxbuf,sizeof(rxbuf),0);
			if ( rc == -1 ) {
				printf("%s: recv(2) from %s\n",
					strerror(errno),
					hostname);
				close(s);
				return;

			}
			ntp_time = ntohl(rxbuf[10]);
		} else	{
			printf("Retrying %s..\n",hostname);
		}
	} while ( !ntp_time );

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

	close(s);
}

//////////////////////////////////////////////////////////////////////
// Provide the name(s) of time servers on the command line
//////////////////////////////////////////////////////////////////////

int
main(int argc,char **argv) {

	if ( argc <= 1 )
		utc_time(0);
	else	{
		for ( int x=1; x<argc; ++x )
			utc_time(argv[x]);
	}

	return 0;
}

// End posntp.cpp
