# ESP8266
Embedded ESP8266 C++ Class Library
----------------------------------

This library is designed to be used in embedded C++ scenarios, where an
AVR or ARM MCU is used with limited resources. While the ESP8266 support
is provided here as a C++ class, it is by no means inferior to a C only
language library in efficiency. No STL components are used.

This project is currently laid out to interface with an ESP8266 module
using a FTDI (or similar) usb serial cable from Linux/*BSD/Mac OSX. You
can easily take the esp8266.hpp/cpp source code and plunk it into your
AVR or ARM MCU project.

POSIX TESTING
-------------

There is a POSIX test program named posix.cpp that you can use to try
out the library before involving the MCU. First make the project:

$ make 

and then invoke the test program:

$ ./posix -h
Usage: posix [-R [-W]] [-c:] [-p:] [-b:] [-d:] [-j:] [-P:] [-r] [-o:] [-D:] [-A:] [-v] [-h]
where
	-R		Begin with ESP8266 reset
	-W		Wait for WIFI CONNECT + GOT IP
	-c host		Host to connect to
	-p port		Default is port 80
	-d device	Serial device pathname
	-j wifi_name	WIFI network to join
	-P password	WIFI passord (for -j)
	-r		Resume connection to last used WIFI
	-o file		Send received output to file (default is stdout)
	-D {0|1}	Disable/Enable DHCP
	-A ipaddr	Set AP IP Address
	-T secs		Set new timeout
	-L port		Listen on port
	-v		Verbose output mode
	-h		This help info.

When neither -j or -r used, -r is assumed. The option order is
insignificant, but the order that they are acted upon is as follows:

    1. STARTUP. These options are critical:
        -R [-W]         Reset the ESP8266 and optionally wait for the
                        WIFI CONNECTED and WIFI GOT IP (option -W)
        -r              This alternative does not do a reset, and
                        attempts to use the ESP8266 in its current
                        state (overrides -j).
        -j wifi -p pw   Join an WIFI access point with the given 
                        name and password (overrides -r).

    2. CONFIGURATION
        -d devie        This specifies your usb serial device path
        -D 0 or -D 1    Test disable or enable DHCP
        -A x.x.x.x      Set your ESP8266 access point to IP address
        -T secs         Set your ESP8266 timeout in seconds (note
                        that setting the timeout did not work for
                        my ESP-07 device)

    3. COMMUNICATION TESTS
        -c host -p port Connect to host host, at port port, and 
                        send a "GET /CRLF" command to it, saving
                        the received text to file given by the 
                        -o option (else stdout by default).
                        Example: -c google.com -p 80
        -L port         Create a listening server on your ESP8266
                        at port "port". For example, if it is 
                        listening at 192.168.0.73 port 80, you can
                        telnet to it or use netcat:

                        pwd | nc 192.168.0.73 80

HARDWARE:
---------

There seems to be a lot of people struggling with loss of data on
receive from the ESP8266. You MUST have hardware flow control in place
to make this communicate reliably. The loss of a single byte of data,
will lockup the interface because it may wait forever for a response
that does not arrive. 

Use a 3.3V FTDI adapter only (you can cheat if you use a voltage 
divider). The ESP8299 will not tolerate 5V signals.

MCU/USB             ESP8266
--------            -----------------
TX  --------------> RX
RX  <-------------- TX
RTS(1)------------> GPIO15 (UART0_RTS)
CTS(2) <----------- GPIO13 (UART0_CTS)
        OTHER
        -----
        +3.3V ----> CH_PD (or "EN")
        +3.3V ----> RST (low to high to reset)
Notes:
------
    1. Some usb serial adapters may label this connection as DTR.
    2. If your serial adapter runs on either 5V 3.3V by jumper,
       you may find that you need a 1K pull-down resistor between 
       CTS and ground (this was necessary on mine to bring the
       CTS low voltage below 0.6V).

Class Design:
-------------

This class makes extensive use of callbacks, for the utmost in
application flexibility. Everything is handled through the ESP8266 class
and is instantiated as follows:

    #include "esp8266.hpp"

    ESP8266 esp(writeb,readb,rpoll,idle); 

The four parameters are callbacks:

    writeb  Write callback, for writing to the ESP device.
    readb   Read callback, used for reading from the ESP device.
    rpoll   Read poll callback, to determine if there is more
            data to be read.
    idle    The idle callback is invoked when there is nothing
            productive to do.

    CALLING SIGNATURES:

        void writeb(char b);
        char readb();
        bool rpoll();
        void idle();

RECEIVING:

When the ESP8266 has information to be sent, it must be received
immediately whether the application is ready for it or not. For this
reason, the ESP8266 class uses a receiving callback to deliver received
information. Otherwise it would be necessary to buffer the data, which
is not practical in a MCU short of RAM. 

MCU applications that are web scraping may only need to save a fraction
of the received data. This approach permits the callback to filter out
what it needs to keep. An example will make this easier to explain:

To connect to a website, you would use the class method tcp_connect(),
as shown below:

int ESP8266::tcp_connect(const char *host,int port,recv_func_t rx_cb);

The callback recv_func_t is defined as:

    void rx_cb(int sock,int ch) {
        ...
    }

If the connect is successful, a socket number (0 to 4) is returned. The
value -1 is returned if this fails. When data is received, the callback
rx_cb is called for each character (ch) for the socket (sock). The
special value of ch=-1 means that the remote end has closed the socket
(you will need to close the socket also to free that resource). An
example of this is found in the posix.cpp example program.

WRITING TO A SOCKET:

Once your have connected to your destination, your calling application
can write data to the socket using:

    int bytes = esp.write(int sock,const char *data,int bytes); 

if the return value is -1, there has been an error.

CLOSING A SOCKET:

    esp.close(sock);

THE ESP::receive() METHOD:

ESP8266 methods like tcp_connect() automatically invoke the receive()
class method at strategic places. Whenever your application is idle, it
too should invoke:

    esp.receive();

so that the necessary receiving functions are carried out (if there is
data to be received, the data is passed to the registered callbacks).
It returns when there is no more data to be read from the device (this
depends upon the rpoll callback that you provided).

ESP STARTUP:

    esp.reset(true);        // Resets ESP and waits for WIFI AP & IP
    esp.reset(false);       // Resets ESP (and does not wait for AP IP)

otherwise use:

    esp.start();            // Turns off echo etc. and otherwise initializes

JOINING AN AP:

    bool ok = esp.ap_join(ap_name,password);

VERSION INFO:

    printf("Version: %s\n",esp.get_version());

GET STATION (SERVER) ADDRESS:

    bool ok = esp.get_station_info(ip_info);

START A SERVER (STATION):

    bool ok = esp.listen(int port,accept_t accp_cb);

The accept callback is invoked when a client has connected to your
server. It is declared as:

    void accept_cb(int sock);

This callback must accept the connection by calling:

    esp.accept(sock,server_recv);

This registers the receiver for the socket:

    void server_recv(int sock,int byte) {
        ...
    }

The value byte will be -1, if the client disconnects (closes). Once this
connection is established, you can receive client data via the callback,
and write back using esp.write(), as before.

SHUTTING DOWN THE SERVER:

    esp.unlisten();

will terminate the server.

OTHER CLASS METHODS:

See the class definition in esp8266.hpp for other class methods that are
available for retreiving IP and MAC address info etc.

RESOURCES USED:

The class definition is frugal in its use of RAM. There is very little
dynamic memory used, but malloc() is used to allocate a temporary buffer
for version information and other information (like IP number info).
When the full size of the buffer is known, it is reduced in size using a
call to realloc(). The buffer allocated never exceeds 80 bytes.

Any other temporary buffers needed are created on the stack and released
upon return. These are small and less than 32 bytes in size.

To avoid the need to pull in sprintf/snprintf library code, the esp8266.cpp
module provides its own efficient integer to string conversion routine:

    const char *int2str(int v,char *buf,int bufsiz);

To use it, define your own buffer and use the returned pointer:

    char temp[16];
    int x = 23;
    const char *cp = int2str(x,temp, sizeof temp);

Upon return cp points to "23". Do not use the buffer name "temp", since
the conversion starts at the end of the buffer (temp[15] holds the null
byte) and works backwards. 

When compiled under Mac OSX, the esp8266.o module is about 17k in size.
I expect a similar code size for AVR or ARM use. The class was designed
to use flash over RAM, whenever there was a choice.

UNFINISHED BUSINESS:

    - Currently lacking UDP support
    - Ability to receive a list of WIFI stations
    - and sundry

FINAL NOTE:

Anything not explained sufficiently in this README can be found in the
class definition (esp8266.hpp) and the POSIX test program (posix.cpp).

--
