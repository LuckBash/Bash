/*
 * 
 * David Lettier (C) 2014.
 * 
 * http://www.lettier.com/
 * 
 * NTP client.
 * 
 * Compiled with gcc version 4.7.2 20121109 (Red Hat 4.7.2-8) (GCC).
 * 
 * Tested on Linux 3.8.11-200.fc18.x86_64 #1 SMP Wed May 1 19:44:27 UTC 2013 x86_64 x86_64 x86_64 GNU/Linux.
 * 
 * To compile: $ gcc main.c -o ntpClient.out
 * 
 * Usage: $ ./ntpClient.out
 * 
 */

#include <algorithm>
#include <boost/algorithm/string/replace.hpp>
#include <string>
#include "bitbet.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
/*#ifdef WIN32
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
#include "ntp.h"

using namespace std;
using namespace boost;

#define NTP_TIMESTAMP_DELTA 2208988800ull

/*void error2( char* msg )
{
    //perror( msg ); // Print the error message to stderr.  
    //exit( 0 ); // Quit the process.
}*/

void setSocketTimeOut(SOCKET sockfd, int tmo)
{
#ifdef WIN32
    int timeout = tmo; //5s
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval timeout={tmo/1000, (tmo % 1000) * 1000};
    setsockopt(sockfd, SOL_SOCKET,SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET,SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#endif
}

int64_t syncNtpTime(const std::string host)
{
	int64_t rzt=0;

	SOCKET sockfd; // Socket file descriptor and the n return result from writing/reading from the socket.
	
	int n, portno = 123; // NTP UDP port number.
	
    //char* host_name = "us.pool.ntp.org";
	char* host_name = (char*)host.c_str();  //"us.pool.ntp.org"; // NTP server host-name.
	
	// Create and zero out the packet. All 48 bytes worth.
	
	ntp_packet packet = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };  //ntp_packet packet = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	
	memset( &packet, 0, sizeof( ntp_packet ) );
	
	// Set the first byte's bits to 00,011,011 for li = 0, vn = 3, and mode = 3. The rest will be left set to zero.
	
	packet.li_vn_mode = 0x1b;  //*( ( char * ) &packet + 0 ) = 0x1b; // Represents 27 in base 10 or 00011011 in base 2.
	
	// Create a UDP socket, convert the host-name to an IP address, set the port number,
	// connect to the server, send the packet, and then read in the return packet.

	struct sockaddr_in serv_addr; // Server address data structure.
	struct hostent *server;	     // Server data structure.
	
	sockfd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ); // Create a UDP socket.
	
	if ( sockfd < 0 )
	{
		error( "ERROR opening socket" );   return rzt;
	}
	
	server = gethostbyname( host_name ); // Convert URL to IP.
	
	if ( server == NULL )
	{
		closesocket(sockfd);   error( "ERROR, no such host" );   return rzt;
	}
	
	// Zero out the server address structure.
	
	memset( ( char* ) &serv_addr, 0, sizeof( serv_addr ) );  //bzero( ( char* ) &serv_addr, sizeof( serv_addr ) );
	
	serv_addr.sin_family = AF_INET;
	
	// Copy the server's IP address to the server address structure.
	
	//bcopy( ( char* )server->h_addr, ( char* ) &serv_addr.sin_addr.s_addr, server->h_length );
	memcpy( ( char* ) &serv_addr.sin_addr.s_addr, ( char* )server->h_addr, server->h_length );
	
	// Convert the port number integer to network big-endian style and save it to the server address structure.
	
	serv_addr.sin_port = htons( portno );
	
	// Call up the server using its IP address and port number.
	
	if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr) ) < 0 ) 
	{
		closesocket(sockfd);   error( "ERROR connecting" );   return rzt;
	}
	
    //struct timeval tv;   tv.tv_sec  = 10;   tv.tv_usec=0;
    //fd_set fdsetRecv;   fd_set fdsetSend;   fd_set fdsetError;
	//FD_ZERO(&fdsetRecv);   FD_ZERO(&fdsetSend);  FD_ZERO(&fdsetError);  FD_SET(sockfd, &fdsetRecv);

    setSocketTimeOut(sockfd, 5000);
#ifdef WIN32
    sendto(sockfd, ( char* ) &packet, sizeof(ntp_packet), 0, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr));

    int len = sizeof(struct sockaddr);
	//int ir = recvfrom(sockfd, (char*)&packet, sizeof(ntp_packet), 0, (struct sockaddr*)&serv_addr, &len);
	int ir = recv(sockfd, (char*)&packet, sizeof(ntp_packet), MSG_DONTWAIT);  // MSG_DONTWAIT, MSG_WAITALL
	//if( fDebug ){ printf("recv [%d] data \n", ir); }
	if( ir <= 0 )  // SOCKET_ERROR
    {
        closesocket(sockfd);   printf("recv error! \n");   return rzt;
    }
    /*if( select(sockfd+1, &fdsetRecv, &fdsetSend, &fdsetError, &tv) != SOCKET_ERROR )
    {
        closesocket(sockfd);   printf("select error!\n");   return rzt;
    }
    else
    {
        printf("OK 001 \n");
        if( FD_ISSET(sockfd, &fdsetRecv) )
        {
            printf("OK 002 \n");
            //if( recv(sockfd, ( char* ) &packet, sizeof(ntp_packet), 0) < 0 )
			if( recvfrom(sockfd, (char*)&packet, sizeof(ntp_packet), 0, (struct sockaddr*)&serv_addr, &len) != SOCKET_ERROR )
            {
                closesocket(sockfd);   printf("recv error! \n");   return rzt;
            }
        }
    }*/

#else
	// Send it the NTP packet it wants. If n == -1, it failed.
	n = write( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );
	if ( n < 0 )
	{
		closesocket(sockfd);   error( "writing error!" );   return rzt;
	}

	// Wait and receive the packet back from the server. If n == -1, it failed.
	n = read( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );
	if ( n < 0 )
	{
		closesocket(sockfd);   error( "recv error!" );   return rzt;
	}
#endif
  closesocket(sockfd);
	
	// These two fields contain the time-stamp seconds as the packet left the NTP server.
	// The number of seconds correspond to the seconds passed since 1900.
	// ntohl() converts the bit/byte order from the network's to host's "endianness".

	packet.txTm_s = ntohl( packet.txTm_s ); // Time-stamp seconds.
	packet.txTm_f = ntohl( packet.txTm_f ); // Time-stamp fraction of a second.
	
	// Extract the 32 bits that represent the time-stamp seconds (since NTP epoch) from when the packet left the server.
	// Subtract 70 years worth of seconds from the seconds since 1900.
	// This leaves the seconds since the UNIX epoch of 1970.
	// (1900)------------------(1970)**************************************(Time Packet Left the Server)
	
	time_t txTm = ( time_t ) ( packet.txTm_s - NTP_TIMESTAMP_DELTA );    rzt = txTm;
	
	// Print the time we got from the server, accounting for local timezone and conversion from UTC time.
		
	if( fDebug ){ printf( "Time: %s", ctime( ( const time_t* ) &txTm ) ); }

	return rzt;
}
