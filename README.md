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

The ESP8266 class can be made to operate in single-thread or 
as mbed RTOS threads. For an RTOS example, run under POSIX, see
the program ntp_rtos.cpp.

WIKI
----

Wiki documentation is now online. It will likely be refined further
over the course of time:

   https://github.com/ve3wwg/ESP8266_TCP_UDP/wiki


*** AP WARNING ***
------------------

At the present time, this module assumes Access Point (AP) is enabled in
order to allow TCP connections. 

  By default, the ESP device is configured for open access. So if
  your ESP joins your own WIFI, you are exposing yourself to 
  outside traffic.

The best solution is to simply manually change the AP parameters using a
terminal program (suggest you use cmdesp below). In this manner your AP will
be secured when enabled:

    AT+CWSAP="ssid","pw",chan,ecn 

where ecn is 0=open/2=WPA_PSK/3=WPA2_PSK/4=WPA_WPA2_PSK.


POSIX TEST
----------

A POSIX test program named posix.cpp can be used to try out the library
before involving the MCU. First make the project:

    $ make 

Use target clobber, if you made changes and you need to rebuild:

    $ make clobber

to clobber and rebuild:

    $ make clobber all

You can get help from the test program:

    $ ./posix -h
    Usage: posix [-options..] [-v] [-h]
    where options include:
        -R              Begin with ESP8266 reset
        -W              Wait for WIFI CONNECT + GOT IP (with -R)
        -c host         TCP host to connect to
        -u host         UDP host to send/recv with
        -U port         Local UDP port (else assigned)
        -Z secs         Wait seconds for a UDP response
        -p port         Default is port 80
        -d device       Serial device pathname
        -j wifi_name    WIFI network to join
        -P password     WIFI passord (for -j)
        -r              Resume connection to last used WIFI
        -o file         Send received output to file (default is stdout)
        -D {0|1}        Disable/Enable DHCP
        -A ipaddr       Set AP IP Address
        -T secs         Set new timeout
        -L port         Listen on port
        -v              Verbose output mode
        -h              This help info.

    Options -c (TCP) and -u (UDP) are mutually exclusive.
    When neither -j or -r used, -r is assumed.

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
        TCP:

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

        UDP:

        -u host -p port Create a UDP socket for communication to
                        host and port. 
        -U port         Specifies the local port for UDP, so that
                        UDP packets can be back to the ESP at a
                        known port.
        -Z seconds      Specifies how long the UDP test should
                        wait for incoming datagrams before exiting
                        the test.

HARDWARE:
---------

There seems to be a lot of people struggling with loss of data on
receive from the ESP8266. You MUST have hardware flow control in place
to make this communicate reliably. The loss of a single byte of data,
will lockup the interface because it may wait forever for a response.

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

--
