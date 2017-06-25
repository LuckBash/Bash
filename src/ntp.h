
#ifndef	_NTP_H_
#define	_NTP_H_

/*#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef WIN32
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif */
#include "util.h"
#include "net.h"

using namespace std;
using namespace boost;

	// Structure that defines the 48 byte NTP packet protocol.
	typedef struct 
	{
		
		/*unsigned li   : 2;       // Only two bits. Leap indicator.
		unsigned vn   : 3;       // Only three bits. Version number of the protocol.
		unsigned mode : 3;       // Only three bits. Mode. Client will pick mode 3 for client. */
        uint8_t li_vn_mode;
		
		uint8_t stratum;         // Eight bits. Stratum level of the local clock.
		uint8_t poll;            // Eight bits. Maximum interval between successive messages.
		uint8_t precision;       // Eight bits. Precision of the local clock.
		
		uint32_t rootDelay;      // 32 bits. Total round trip delay time.
		uint32_t rootDispersion; // 32 bits. Max error aloud from primary clock source.
		uint32_t refId;          // 32 bits. Reference clock identifier.
		
		uint32_t refTm_s;        // 32 bits. Reference time-stamp seconds.
		uint32_t refTm_f;        // 32 bits. Reference time-stamp fraction of a second.
		
		uint32_t origTm_s;       // 32 bits. Originate time-stamp seconds.
		uint32_t origTm_f;       // 32 bits. Originate time-stamp fraction of a second.
		
		uint32_t rxTm_s;         // 32 bits. Received time-stamp seconds.
		uint32_t rxTm_f;         // 32 bits. Received time-stamp fraction of a second.
		
		uint32_t txTm_s;         // 32 bits and the most important field the client cares about. Transmit time-stamp seconds.
		uint32_t txTm_f;         // 32 bits. Transmit time-stamp fraction of a second.
		
	} ntp_packet;                 // Total: 384 bits or 48 bytes.

int64_t syncNtpTime(const std::string host);

#endif
