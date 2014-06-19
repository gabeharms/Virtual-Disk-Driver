////////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa_server.c
//  Description   : This is the server side of the SMSA communication protocol.
//
//   Author        : Patrick McDaniel
//   Last Modified : Mon Oct 28 06:58:31 EDT 2013
//

// Include Files
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

// Project Include Files
#include <smsa.h>
#include <smsa_network.h>
#include <cmpsc311_log.h>

// Global variables
int smsa_server_shutdown    = 0;
int client_socket	    = -1;
unsigned char *client_ip    = NULL;
unsigned short client_port  = 0;

// Functional Prototypes
int smsa_server_handle_connection( int sock );
int smsa_client_connect( unsigned char *ip, uint16_t port );
int smsa_recieve_packet( int sock, uint32_t *op, int16_t *ret, int *blkbytes, unsigned char *block ); 
int smsa_send_packet( int sock, uint32_t op, int16_t ret, unsigned char *block );
int smsa_read_bytes( int sock, int len, unsigned char *block );
int smsa_send_bytes( int sock, int len, unsigned char *block );
int smsa_wait_read( int sock );
void smsa_signal_handler( int no );

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_server
// Description  : The main function SMSA server processing loop.
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int smsa_server( void ) {

    // Local variables
    struct sigaction new_action;
    struct sockaddr_in saddr, caddr;
    int server, client, optval;
    unsigned int inet_len;

    // Set the signal handler
    new_action.sa_handler = smsa_signal_handler;
    new_action.sa_flags = SA_NODEFER | SA_ONSTACK;
    sigaction( SIGINT, &new_action, NULL );

    // Create the socket
    if ( (server=socket(AF_INET, SOCK_STREAM, 0)) == -1 ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA socket() create failed : [%s]", strerror(errno) );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Setup so we can reuse the address
    optval = 1;
    if ( setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA set socket option create failed : [%s]", strerror(errno) );
	smsa_error_number = SMSA_NET_ERROR;
	close( server );
	return( -1 );
    }

    // Setup address and bind the server to a particular port 
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SMSA_DEFAULT_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Now bind to the server socket
    if ( bind(server, (struct sockaddr *)&saddr, sizeof(struct sockaddr)) == -1 ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA bind() create failed : [%s]", strerror(errno) );
	smsa_error_number = SMSA_NET_ERROR;
	close( server );
	server = -1;
	return( -1 );
    }
    logMessage( LOG_INFO_LEVEL, "Server bound and listening on port [%d]", SMSA_DEFAULT_PORT );

    // Listen for incoming connection
    if ( listen( server, SMSA_MAX_BACKLOG ) == -1 ) {
	logMessage( LOG_ERROR_LEVEL, "SMSA listen() create failed : [%s]", strerror(errno) );
	smsa_error_number = SMSA_NET_ERROR;
	close( server );
	server = -1;
	return( -1 );
    }

    // Wait until server is complete
    smsa_server_shutdown = 0;
    while ( ! smsa_server_shutdown ) {

	// Select waiting for data
	if ( smsa_wait_read( server ) == -1 ) {
	    // error out
	    logMessage( LOG_ERROR_LEVEL, "SMSA server wait failued, aborting." );
	    smsa_error_number = SMSA_NET_ERROR;
	    close( server );
	    server = -1;
	    return( -1 );
	}

	// Accept the connection
	inet_len = sizeof(caddr);
	if ( (client = accept( server, (struct sockaddr *)&caddr, &inet_len )) == -1 ) {
	    // error out
	    logMessage( LOG_ERROR_LEVEL, "SMSA server accept failued, aborting." );
	    smsa_error_number = SMSA_NET_ERROR;
	    close( server );
	    server = -1;
	    return( -1 );
	}

	// Log the creation of the new connection, process the input data
	logMessage( LOG_INFO_LEVEL, "Server new client connection [%s/%d]", inet_ntoa(caddr.sin_addr), caddr.sin_port );
	smsa_server_handle_connection( client );
	logMessage( LOG_INFO_LEVEL, "Closing client connection [%s/%d]", inet_ntoa(caddr.sin_addr), caddr.sin_port );
	close( client );
    }

    // Log and shutdowmn, return
    logMessage( LOG_INFO_LEVEL, "Shutting down SMSA server ..." );
    close( server );
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_client_operation
// Description  : This the client operation that sends a reques to the SMSA
//                server.   It will:
//
//                1) if mounting make a connection to the server 
//                2) send any request to the server, returning results
//                3) if unmounting, will close the connection
//
// Inputs       : op - the operation code for the command
//                block - the block to be read/writen from (READ/WRITE)
// Outputs      : 0 if successful, -1 if failure

int smsa_client_operation( uint32_t op, unsigned char *block ) {

    // Local variables
    unsigned char *ip;
    uint16_t pt;
    int blkbytes;
    int16_t ret;
    uint32_t rop;

    // Check if this is a mount command
    if ( SMSA_OPCODE(op) == SMSA_MOUNT ) {

	// Setup and call the connect
	ip = (client_ip == NULL) ? (unsigned char *)SMSA_DEFAULT_IP : client_ip;
	pt = (client_port == 0) ? SMSA_DEFAULT_PORT : client_port;
	if ( (client_socket = smsa_client_connect( ip, pt )) == -1 ) {
	    // Error out
	    logMessage( LOG_ERROR_LEVEL, "SMSA op failed. [%x]", op );
	    smsa_error_number = SMSA_NET_ERROR;
	    return( -1 );
	}

    } 
    
    // Check to see if connected
    if ( client_socket == -1 ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA client op failed, no connection." );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Now perform the operation 
    if ( smsa_send_packet( client_socket, op, 0, block ) == -1 ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA client send packet failed." );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Select waiting for data
    if ( smsa_wait_read( client_socket ) == -1 ) {
	// error out
	logMessage( LOG_ERROR_LEVEL, "SMSA client wait failued, aborting." );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Now receive the response 
    if ( smsa_recieve_packet( client_socket, &rop, &ret, &blkbytes, block ) == -1 ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA client send packet failed." );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Now check the op code
    if ( op != rop ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA client receive op mismatch (%x != %x).", op, rop );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // If unmounting, then disconnect
    if ( SMSA_OPCODE(op) == SMSA_UNMOUNT ) {
	// Log unmount disconnect
	logMessage( LOG_INFO_LEVEL, "Disconnecting socket." );
	close( client_socket );
	client_socket = -1;
    }

    // Return what was returned by the SMSA operation
    return( ret );
}

//
// Local Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_server_handle_connection
// Description  : Handle a connection from a client process.
//
// Inputs       : sock - the socket filehandle of the clinet connection
// Outputs      : 0 if successful, -1 if failure

int smsa_server_handle_connection( int sock ) {

    // Local variables
    int receiving = 1, blkbytes;
    unsigned char block[SMSA_BLOCK_SIZE];
    uint32_t op;
    int16_t ret;

    // Keep receiving requests until done
    while ( (receiving) && (! smsa_server_shutdown) ) {

	// Keep receiving until we have enough data
	blkbytes = SMSA_BLOCK_SIZE;
	if ( smsa_recieve_packet( sock, &op, &ret, &blkbytes, block ) == -1 ) {
	    logMessage( LOG_ERROR_LEVEL, "SMSA receive failed : [%s]", strerror(errno) );
	    smsa_error_number = SMSA_NET_ERROR;
	    return( -1 );
	}
	assert( (blkbytes == 0) || (blkbytes == SMSA_BLOCK_SIZE) );

	// Now process the received  data, send the response
	ret = smsa_operation( op, block );
	if ( smsa_send_packet(sock, op, ret, (SMSA_OPCODE(op) == SMSA_DISK_READ) ? block : NULL) == -1 ) {
	    logMessage( LOG_ERROR_LEVEL, "SMSA send failed : [%s]", strerror(errno) );
	    smsa_error_number = SMSA_NET_ERROR;
	    return( -1 );
	}
    }

    // Close the socket and return sucessfully
    close( sock );
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_client_connect
// Description  : The main function SMSA server processing loop.
//
// Inputs       : ip - the IP address string for the client
//                port - the port number of the service
// Outputs      : socket file handle if successful, -1 if failure

int smsa_client_connect( unsigned char *ip, uint16_t port ) {

    // Local variables
    int sock;
    struct sockaddr_in caddr;
    
    // Check to make sure you have a good IP address
    caddr.sin_family = AF_INET;
    caddr.sin_port = htons(port);
    if ( inet_aton((char *)ip, &caddr.sin_addr) == 0 ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA client unable to interpret ip address [%s]", ip );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Create the socket
    if ( (sock=socket(AF_INET, SOCK_STREAM, 0)) == -1 ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA client socket() failed : [%s]", strerror(errno) );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Now connect to the server
    if ( connect(sock, (const struct sockaddr *)&caddr, sizeof(struct sockaddr)) == -1 ) {
	// Error out
	logMessage( LOG_ERROR_LEVEL, "SMSA client connect() failed : [%s]", strerror(errno) );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Return the socket
    return( sock );
} 

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_recieve_packet
// Description  : Recevie a packet from the other side
//
// Inputs       : sock - the socket filehandle of the clinet connection
//                op - the opcode that was read
//                ret - the return value from the operation (as needed)
//                blkbytes - the number of bytes read into the block (0 if none)
//                block - the read block
// Outputs      : 0 if successful, -1 if failure

int smsa_recieve_packet( int sock, uint32_t *op, int16_t *ret, int *blkbytes, unsigned char *block ) {

    // Local variables
    uint16_t  len, idx;
    unsigned char hdr[SMSA_NET_HEADER_SIZE];

    // SMSA Packet definition
    //
    //	Bytes 0-1   : length - how many total bytes in packet
    //	Bytes 2-6   : opcode - the opcode for the command
    //  Bytes 7-8   : return - return code of comamnd 
    //	Bytes 6-261 : block - as needed, SMSA_BLOCK
    //

    // Read the header
    if ( smsa_read_bytes( sock, SMSA_NET_HEADER_SIZE, hdr ) == -1 ) {
	logMessage( LOG_ERROR_LEVEL, "SMSA receive packet failed : [%s]", strerror(errno) );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Now get the header and other data, convert to host byte order
    idx = 0;
    memcpy( &len, hdr, sizeof(uint16_t) );
    idx += sizeof(uint16_t);
    len = ntohs( len );
    memcpy( op, &hdr[idx], sizeof(uint32_t) );
    idx += sizeof(uint32_t);
    *op = ntohl( *op );
    memcpy( ret, &hdr[idx], sizeof(int16_t) );
    idx += sizeof(int16_t);
    *ret = ntohs( *ret );

    // Now see if there is more data to read
    if ( len > SMSA_NET_HEADER_SIZE ) {
	// Now read the remainig data for the packet
	if ( smsa_read_bytes( sock, len-SMSA_NET_HEADER_SIZE, block ) == -1 ) {
	    logMessage( LOG_ERROR_LEVEL, "SMSA receive packet failed : [%s]", strerror(errno) );
	    smsa_error_number = SMSA_NET_ERROR;
	    return( -1 );
	}
	*blkbytes = len-SMSA_NET_HEADER_SIZE;
    } else {
	// Set the block bytes to none
	*blkbytes = 0;
    }

    // Return successfully
    logMessage( LOG_INFO_LEVEL, "Received %d bytes on handle %d", len, sock );
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
// Function     : smsa_send_packet
// Description  : Send a packet to the other side
//
// Inputs       : sock - the socket filehandle of the clinet connection
//                op - the opcode that was readi
//                ret - return value to return
//                block - the read block (NULL if not sent)
// Outputs      : 0 if successful, -1 if failure

int smsa_send_packet( int sock, uint32_t op, int16_t ret, unsigned char *block ) {

    // Local varibles
    uint16_t len, idx;
    unsigned char sndbuf[SMSA_NET_HEADER_SIZE+SMSA_BLOCK_SIZE];

    // Read is the only time we send back a block
    len = SMSA_NET_HEADER_SIZE;
    if ( block != NULL ) {
	len += SMSA_BLOCK_SIZE;
    }
    len = htons(len);
    op = htonl(op);
    ret = htons(ret);

    // Assemble the packet
    idx = 0;
    memcpy( &sndbuf[idx], &len, sizeof(len) ); // Length
    idx += sizeof(uint16_t);
    memcpy( &sndbuf[idx], &op, sizeof(op) ); // Opcode
    idx += sizeof(uint32_t);
    memcpy( &sndbuf[idx], &ret, sizeof(ret) ); // Result
    idx += sizeof(uint16_t);

    // If reading, add block to packet
    if ( block != NULL ) {
	memcpy( &sndbuf[idx], block, SMSA_BLOCK_SIZE ); // Result
	idx += SMSA_BLOCK_SIZE;
    }
    
    // Send the packetx, return the return value
    logMessage( LOG_INFO_LEVEL, "Sending %d bytes on handle %d", idx, sock );
    return( smsa_send_bytes(sock, idx, sndbuf) );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_send_bytes
// Description  : Send a specific length of bytes to socket
//
// Inputs       : sock - the socket filehandle of the clinet connection
//                len - the number of bytes to send
//                buf - the buffer to send 
// Outputs      : 0 if successful, -1 if failure

int smsa_send_bytes( int sock, int len, unsigned char *buf ) {

    // Local variables
    int sentBytes = 0, sb;

    // Loop until you have read all the bytes
    while ( sentBytes < len ) {

	// Read the bytes and check for error
	if ( (sb = write(sock, &buf[sentBytes], len-sentBytes)) < 0 ) {
	    logMessage( LOG_ERROR_LEVEL, "SMSA send bytes failed : [%s]", strerror(errno) );
	    smsa_error_number = SMSA_NET_ERROR;
	    return( -1 );
	}

	// Check for closed file
	else if ( sb == 0 ) {
	    // Close file, not an error
	    logMessage( LOG_ERROR_LEVEL, "SMSA client socket closed on snd : [%s]", strerror(errno) );
	    return( -1 );
	}

	// Now process what we read
	sentBytes += sb;
    }

    // Return successsfully
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_read_bytes
// Description  : Recevie a specific length of bytes form socket
//
// Inputs       : sock - the socket filehandle of the clinet connection
//                len - the number of bytes to be read
//                buf - the buffer to read into 
// Outputs      : 0 if successful, -1 if failure

int smsa_read_bytes( int sock, int len, unsigned char *buf ) {

    // Local variables
    int readBytes = 0, rb;

    // Loop until you have read all the bytes
    while ( readBytes < len ) {

	// Read the bytes and check for error
	if ( (rb = read(sock, &buf[readBytes], len-readBytes)) < 0 ) {
	    // Check for client error on read
	    logMessage( LOG_ERROR_LEVEL, "SMSA read bytes failed : [%s]", strerror(errno) );
	    smsa_error_number = SMSA_NET_ERROR;
	    return( -1 );
	}

	// Check for closed file
	else if ( rb == 0 ) {
	    // Close file, not an error
	    logMessage( LOG_ERROR_LEVEL, "SMSA client socket closed on rd : [%s]", strerror(errno) );
	    return( -1 );
	}

	// Now process what we read
	readBytes += rb;
    }

    // Return successsfully
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_wait_read
// Description  : Wait for user input on the socket
//
// Inputs       : sock - the socket to write on
// Outputs      : 0 if successful, -1 if failure

int smsa_wait_read( int sock ) {

    // Local variables
    fd_set rfds;
    int nfds, ret;

    // Setup and perform the select
    nfds = sock + 1;
    FD_ZERO( &rfds );
    FD_SET( sock, &rfds );
    ret = select( nfds, &rfds, NULL, NULL, NULL );

    // Check the return value
    if ( ret == -1 ) {
	logMessage( LOG_ERROR_LEVEL, "SMSA select() failed : [%s]", strerror(errno) );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // check to make sure we are selected on the read
    if ( FD_ISSET( sock, &rfds ) == 0 ) {
	logMessage( LOG_ERROR_LEVEL, "SMSA select() returned without selecting FD : [%d]", sock );
	smsa_error_number = SMSA_NET_ERROR;
	return( -1 );
    }

    // Return successsfully
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_signal_handler
// Description  : This is the user defined signal handler
//
// Inputs       : no - the signal number
// Outputs      : 0 if successful, -1 if failure

void smsa_signal_handler( int no ) {

    // Log the signal and set process to shut down
    logMessage( LOG_WARNING_LEVEL, "SMSA signal received (%d), shutting down.", no );
    smsa_server_shutdown = 1;

    // Return, no return code
    return;
}
