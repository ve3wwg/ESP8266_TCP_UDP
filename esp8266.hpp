///////////////////////////////////////////////////////////////////////
// esp8266.hpp -- ESP8266 Class
// Date: Mon Oct 26 20:16:55 2015   (C) Warren Gay ve3wwg
///////////////////////////////////////////////////////////////////////

#ifndef ESP8266_HPP
#define ESP8266_HPP

#ifndef N_CONNECTION
#define N_CONNECTION	5
#endif

#ifdef USING_RTOS
extern "C" {
	void yield();
}
#define YIELD	yield
#else
#define YIELD	receive
#endif

class ESP8266 {
public:
	enum AP_Ecn {
		Open = 0,
		WPA_PSK = 1,
		WPA2_PSK = 2,
		WPA_WPA2_PSK = 3,
		Ecn_Undefined
	};	

	enum IpGwMask {		// IP Info Types
		IP_Addr=10,	// +CIPAP:ip:"192.168.4.1"
		Gateway,	// +CIPAP:gateway:"192.168.4.1"
		NetMask		// +CIPAP:netmask:"255.255.255.0"
	};

	// I/O Callbacks:
	typedef void (*idle_func_t)();			// Idle callback
	typedef void (*write_func_t)(char b);		// Writes a byte
	typedef char (*read_func_t)();			// Returns read byte
	typedef bool (*poll_func_t)();			// Returns true if data to be read

	// User Callbacks:
	typedef void (*recv_func_t)(int sock,int ch);		// Received data (1 byte)
	typedef void (*accept_t)(int sock);			// Accepted socket

	enum Error {
		Ok = 0,				// Success
		Fail,				// General failure
		Invalid,			// Invalid parameter(s)
		DNS_Fail,			// DNS lookup failed
		Disconnected,			// Disconnected
		Resource			// Resource limitation
	};

private:

	struct s_bufs {
		char	*buf;
		int	bufsiz;
	};

	write_func_t	writeb;			// Called to write 1 byte to ESP
	read_func_t	readb;			// Called to read 1 byte from ESP
	poll_func_t	rpoll;			// Called to poll if data to read from ESP
	idle_func_t	idle;			// Idle callback

	accept_t	accept_cb;		// Accept callback
	
	Error		error;			// Last error encountered

	struct s_state {
		recv_func_t	rxcallback;	// Receive callback
		unsigned	open : 1;	// 1 if this socket is in use 
		unsigned	connected : 1;	// 1 if this socket is connected
		unsigned	disconnected : 1; // 1 if this socket has seen a disconnect
		unsigned	udp : 1;	// This is a UDP socket
	};

	char		*version;		// Version info, else nullptr
	s_bufs		*bufsp;			// Temp ptr for collecting info

	s_state		state[N_CONNECTION];	// Sockets state

	short		first;			// First char after LF
	short		ipd_id;			// Session ID
	short		ipd_len;		// Byte length
	short		resp_id;		// Response id in 0,CONNECT
	short		s0;			// RX State
	short		ss;			// RX Substate
	short		channel;		// AP channel (CWJAP), when known (else -1)
	short		strength;		// Strength (CWJAP), when known (else -1)

	unsigned	ready : 1;		// Got "ready" after Reset
	unsigned	wifi_connected : 1;	// WiFi connected
	unsigned	wifi_got_ip : 1;	// WiFi has got IP
	unsigned	resp_ok : 1;		// When OK
	unsigned	resp_fail : 1;		// When FAIL
	unsigned	resp_connected : 1;	// When session CONNECTED
	unsigned	resp_closed : 1;	// When remote closed connection
	unsigned	resp_dnsfail : 1;	// When DNS Fail
	unsigned	resp_error : 1;		// When ERROR
	unsigned	send_ready : 1;		// When ready to accept send data
	unsigned	send_ok : 1;		// After successful SEND
	unsigned	send_fail : 1;		// After failed SEND

	void waitlf();				// Read bytes until LF
	s_state *lookup(int sock);		// Lookup socket, else nullptr
	bool waitokfail();			// Wait for OK or FAIL (or ERROR)
	char read_id();				// Read in an unsigned integer
	char read_buf(int bufx,char stop);	// Read into bufx until stop char
	char skip_until(char b,char stop);	// Skip until stop charactor (or \r)

	int socket(const char *socktype,const char *host,int port,recv_func_t rx_cb,int local_port=-1);

public:	ESP8266(write_func_t writeb,read_func_t readb,poll_func_t rpoll,idle_func_t idle);	// Non RTOS constructor
	~ESP8266();
	void clear(bool notify);		// Clear like the constructor (after reset)

	inline Error get_error() const		{ return error; }
	inline const char *strerror() const	{ return strerror(error); }
	const char *strerror(Error err) const;		// Return text for error code

	inline int get_softap_channel() const	{ return channel; }
	inline int get_softap_strength() const	{ return strength; }

	bool reset();					// Reset the ESP device (and optionally await wifi connect)
	bool wait_reset();				// Wait for "ready" message after hardware reset
	bool start();					// Set operational parameters (required if no reset)
	void wait_wifi(bool got_ip);			// Wait for "WIFI CONNECTED" (optionally WIFI GOT IP)
	bool is_wifi(bool got_ip);			// Return true if we have AP (optionally and IP)

	bool query_softap(char *ssid,int ssidsiz,char *pw,int pwsiz,int& ch,AP_Ecn& ecn);

	bool get_version(char *buf,int bufsiz);		// Return ESP version

	void crlf();					// Write CR LF to ESP device
	void write(const char *str);			// Write string to ESP device
	void command(const char *cmd);			// Write string + CR LF to ESP device

	bool dhcp(bool on);				// Enable/disable DHCP

	bool ap_join(const char *ap,const char *passwd); // Join Access Point

	bool get_ap_ssid(char *ssid,int ssid_size,char *mac,int mac_size,int& chan,int& db);
	bool get_ap_info(char *ip,int ipsiz,char *gw,int gwsiz,char *nm,int nmsiz);
	bool set_ap_addr(const char *ip_addr);		// Change access point IP address
	bool get_ap_mac(char *mac,int macsiz);		// Get access point MAC address
	bool set_ap_mac(const char *mac_addr);		// Set access point MAC address

	int get_autoconn();				// Get access point auto-connect setting
	bool set_autoconn(bool on);			// Set access point auto-connect setting

	bool get_station_info(char *ip,int ipsiz,char *gw,int gwsiz,char *nm,int nmsiz);
	bool set_station_addr(const char *ip_addr);	// Set station IP address
	bool get_station_mac(char *mac,int macsiz);	// Get station MAC address
	bool set_station_mac(const char *mac_addr);	// Set station MAC address
	int get_timeout();				// Get station timeout
	bool set_timeout(int seconds);			// Set station timeout

	bool listen(int port,accept_t accp_cb);		// Station listen port & accept callback
	void accept(int socket,recv_func_t recv_cb);	// Accept a connection, set recv callback
	bool unlisten();				// Close station listening port

	int tcp_connect(const char *host,int port,recv_func_t rx_cb);	// Connect to TCP destination with recv callback
	int udp_socket(const char *host,int port,recv_func_t rx_cb,int local_port=-1);	// Create UDP socket to send to host at port, with recv callback
	int write(int sock,const char *data,int bytes,const char *udp_address=0); // Write to TCP/UDP connection (optionally to a different UDP address)
	bool close(int sock);						// Close TCP connection
	void close_all();

	void receive();					// Receiving state machine

	//////////////////////////////////////////////////////////////
	// Intermediate API
	//////////////////////////////////////////////////////////////

	int get_cipmode();				// AT+CIPMODE?
	bool set_cipmode(int mode);			// AT+CIPMODE={0|1}
	int get_cipmux();				// AT+CIPMUX?
	bool set_cipmux(int mode);			// AT+CIPMUX={0|1}

	//////////////////////////////////////////////////////////////
	// The "manual" API
	//////////////////////////////////////////////////////////////

	bool commandok(const char *cmd);		// Issue command + CR LF and wait for OK/FAIL/ERROR

	inline void clear_flag_ready()			{ ready = 0; }
	inline void clear_flag_wifi_connected()		{ wifi_connected = 0; }
	inline void clear_flag_got_ip()			{ wifi_got_ip = 0; }
	inline void clear_flag_ok()			{ resp_ok = 0; }
	inline void clear_flag_fail()			{ resp_fail = 0; }
	inline void clear_flag_dnsfail()		{ resp_dnsfail = 0; }
	inline void clear_flag_error()			{ resp_error = 0; }

	inline bool get_flag_ready() const		{ return ready; }
	inline bool get_flag_wifi_connected() const	{ return wifi_connected; }
	inline bool get_flag_got_ip() const		{ return wifi_got_ip; }
	inline bool get_flag_ok() const			{ return resp_ok; }
	inline bool get_flag_fail() const		{ return resp_fail; }
	inline bool get_flag_dnsfail() const		{ return resp_dnsfail; }
	inline bool get_flag_error() const		{ return resp_error; }
};

const char *int2str(int v,char *buf,int bufsiz);
int str2int(const char *s);

#endif // ESP8266_HPP

// End esp8266.hpp
