///////////////////////////////////////////////////////////////////////
// cmdesp.cpp -- POSIX Command Line Editor for the ESP8266
// Date: Thu Nov 12 20:40:10 2015  (C) Warren W. Gay VE3WWG 
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <assert.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <readline/readline.h>
#include <readline/history.h>

static bool opt_verbose = false;
static const char *opt_device = "/dev/cu.usbserial-A50285BI";
static int opt_timeout = 2000;
static int opt_baudrate = 115200;

static void
usage(const char *cmd) {
	const char *cp = strrchr(cmd,'/');

	if ( cp )
		cmd = cp + 1;

	fprintf(stderr,
		"Usage: %s [-options..] [-v] [-h]\n"
		"where options include:\n"
		"\t-d device\tSerial device pathname\n"
		"\t-b baudrate\tBaud rate for device\n"
		"\t-v\t\tVerbose output mode\n"
		"\t-h\t\tThis help info.\n\n"
		"Use environment variables ESP8266_DEV to default device path\n"
		"and ESP8266_BAUD for baud rate.\n",
		cmd);
	exit(0);
}

static void
receive(int fd) {
	int rc;
	
	for (;;) {
		struct pollfd fds[] = {
			{ 0, POLLIN, 0 },
			{ fd, POLLIN, 0 }
		};

		do	{
			rc = poll(fds,2,opt_timeout);
		} while ( rc == -1 && errno == EINTR );

		if ( rc > 0 && (fds[1].revents & POLLIN) ) {
			char buf[128];

			rc = read(fd,buf,sizeof buf);
			if ( rc > 0 )
				write(1,buf,rc);
		}

		if ( fds[0].revents & POLLIN )
			return;
	}
}

int
main(int argc,char **argv) {
	static const char options[] = ":d:b:vh";
	int optch, fd=-1, rc;

	if ( getenv("ESP8266_DEV") )
		opt_device = getenv("ESP8266_DEV");

	if ( getenv("ESP8266_BAUD") )
		opt_baudrate = atoi(getenv("ESP8266_BAUD"));

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
			exit(1);
			break;
		default:
			fprintf(stderr,"Invalid option -%c\n",optopt);
			exit(1);
		}
	}

	fd = open(opt_device,O_RDWR);
	if ( fd == -1 ) {
		fprintf(stderr,"%s: Opening serial device %s for r/w\n",
			strerror(errno),
			opt_device);
		exit(3);
	} else	{
		struct termios ios;

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
	}

	printf(	"\nAt the prompt, enter your ESP8266 command. Upon pressing\n"
		"return, your command will be sent to the device with a CR LF\n"
		"appended. This command will continue to echo the device's \n"
		"response, until you hit enter to input a new command.\n\n"
		"Don't forget there is command line history available also.\n"
		"Use EOF to exit (^D) for most users.\n");
	

	static const char crlf[] = "\r\n";
	char *line;

	while ( ( line = readline("> ")) != 0 ) {
		if ( !*line ) {
			free(line);
			continue;
		}
		add_history(line);

		if ( line[0] != 0 ) {
			write(fd,line,strlen(line));
			write(fd,crlf,2);
		}

		free(line);
		receive(fd);
	}

	close(fd);

	putchar('\n');

	return 0;
}

// End cmdesp.cpp
