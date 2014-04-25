////////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa_server.c
//  Description   : This is the server side of the SMSA communication protocol.
//
//   Author        : Gabe Harms
//   Last Modified : Mon Oct 28 06:58:31 EDT 2013
//


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

/* DEBUG */
#define DEBUG 0


// Global Variables
int serverShutdown;


//Functional Prototypes
int setupServer ( int *server ); 
int recievePacket ( int server, uint32_t *op, int16_t *ret, int *blkSize, unsigned char *block );
int readBytes ( int server, uint32_t len, unsigned char *block );
int sendPacket ( int server, uint32_t op, int16_t ret, unsigned char *block );
int sendBytes ( int server, uint32_t len, unsigned char *block );
int selectData ( int sock );
void signalHandler ( int signal );


////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_server
// Description  : The main function SMSA server processing loop.
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int smsa_server ( void ) {


	
	struct sockaddr_in clientAddress;  //holds client address
	int server;			   //file handle for the socket
	int client;			   //file handle for the client
	unsigned int inet_len;		
	
	int blkSize = SMSA_BLOCK_SIZE;     //number of bytes ia block
	int recieving = 1;		   //true if were still recieving data
	unsigned char block[SMSA_BLOCK_SIZE];
	uint32_t op;				   //opcode for the smsa_operatoin_t
	int16_t ret;			   //

	if ( setupServer ( &server ) ) {
		logMessage ( LOG_ERROR_LEVEL, "_smsa_server:Failed to properly set up the server" );
		return 1;
	}


	
	//Loop until server needs to shutdown
	serverShutdown = 0;
	while ( !serverShutdown ) {
	
		logMessage ( LOG_INFO_LEVEL, "Now Waiting for Data to Come In..." );
		
		//Wait for data to come in
		if ( selectData ( server ) ) {
			logMessage ( LOG_ERROR_LEVEL, "_smsa_server:Failed to select data [%s]", strerror(errno) );
			return 1;
		}

		logMessage ( LOG_INFO_LEVEL, "Selected Data. Connecting to the Client..." );
		
		//Accept the connection
		inet_len = sizeof( clientAddress );
		if ( (client = accept ( server, (struct sockaddr*)&clientAddress, &inet_len)) == -1 ) {
			logMessage( LOG_ERROR_LEVEL, "_smsa_server:Failed to accept connection [%s]", strerror(errno) );
			return 1;
		}

		logMessage ( LOG_INFO_LEVEL, "New Client Connection Recieved [%s/%d]", inet_ntoa(clientAddress.sin_addr), clientAddress.sin_port ); 
		
		recieving = 1;
		//Read until all data has been processed
		while ( recieving && !serverShutdown ) {

			//recieve data and process the packet
			if ( recievePacket( client, &op, &ret, &blkSize, block ) == 1 ) {
				logMessage( LOG_ERROR_LEVEL, "_smsa_server:Failed to properly recieve a packet" );
				return 1;
			}
	
			logMessage ( LOG_INFO_LEVEL, "Processed Incoming Packet. Now Sending Response Packet...");
				
			//call the neccessary function
			ret = smsa_operation ( op, block );
			
			//send a response
			if ( sendPacket( client, op, ret, (SMSA_OPCODE(op) == SMSA_DISK_READ) ? block: NULL ) == -1) {
				logMessage ( LOG_ERROR_LEVEL, "_smsa_server:Failed to properly send a response" );
				return 1;
			}
			
			//If an UNMOUNT command has been recieved, it is now ok to close down the connection 
			//with the client and wait for other connections to come in
			if ( SMSA_OPCODE(op) == SMSA_UNMOUNT)
				recieving = 0;
			
		}
		
		//Done with connection, now close it
		logMessage( LOG_INFO_LEVEL, "Closing client connection [%s/%d]", inet_ntoa(clientAddress.sin_addr), clientAddress.sin_port );
        	close( client );

	}

	//Shutting down the server
	logMessage ( LOG_INFO_LEVEL, "Shutting Down the Server..." );
	close ( server );
	return 0;
}



////////////////////////////////////////////////////////////////////////////////
//
// Function     : recievePacket
// Description  : Recieve a packet from the client
//
// Inputs       : server - socket file handle
//		  op - opcode for smsa_operation
//		  ret - return of smsa_operation
// 		  blkSize - num of bytes to read into the block
//		  block - the read of the block
// Outputs      : 0 if successful, -1 if failure

int recievePacket ( int server, uint32_t *op, int16_t *ret, int *blkSize, unsigned char *block ) {

	uint32_t len;
	uint32_t headerIndex = 0;
	int twoBytes = sizeof ( uint16_t );
	unsigned char header[SMSA_NET_HEADER_SIZE];

    // SMSA Packet definition
    //
    //  Bytes 0-1   : length - how many total bytes in packet
    //  Bytes 2-6   : opcode - the opcode for the command
    //  Bytes 7-8   : return - return code of comamnd 
    //  Bytes 6-261 : block - as needed, SMSA_BLOCK
    //

    ///Read the packet into header
    if ( readBytes ( server, SMSA_NET_HEADER_SIZE, header ) == 1 ) {
		logMessage( LOG_ERROR_LEVEL, "_recievePacket:Failure to read bytes properly" );
		return 1;
    }

 
    //Put data into host byte order
    //The first two bytes that have been placed in header is the length
    //of the entire packet. The next 4 bytes is the opcode of the command
    //to be used on the smsa_operation function ( size of uint32_t ). The
    //following two bytes hold the return of the command. All of the remaining
    //bytes ( len - index ), are the block ( 255 bytes ), and will be read into the block
    memcpy( &len, header, twoBytes );   	//LENGTH
    headerIndex += twoBytes;

    len = ntohs ( len );  //host byte order

    memcpy( op, &header[headerIndex], 2*twoBytes );	  //OPCODE
    headerIndex += 2*twoBytes;
 
    *op = ntohl ( *op ); //host byte order
 
    memcpy( ret, &header[headerIndex], twoBytes );  	 //RETURN
    headerIndex += twoBytes;

    *ret = ntohs ( *ret ); //host byte order

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Packet Head Processed. length [%d], op [%d], return [%d]", len, *op, *ret );

    //check to see if there is more data to read
    if ( len > SMSA_NET_HEADER_SIZE ) {
	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Packet has Block to be Read. Reading Now" );

	//Call readBytes to read the Length of the packet ( len ) minus the 
	//size of the packet header ( SMSA_NET_HEADER_SIZE ), which is in fact
	//reading the block portion of the packet 
	if ( readBytes( server, len-SMSA_NET_HEADER_SIZE, block ) == -1 ) {
		logMessage( LOG_ERROR_LEVEL, "_recievePacket:Failed to read bytes");
		return 1;
	}
	//Adjust the blkSize for the use of other functions. The blockSize, in bytes, 
	//is the length of the packet minus the packet header. The only reason that we 
	//are not hard coding 255 here is because we might in the future want to use a 
	//different SMSA_BLOCK_SIZE
	*blkSize = len-SMSA_NET_HEADER_SIZE;
    }
    else 
 	*blkSize = 0;  //No block in this packet.


    logMessage( LOG_INFO_LEVEL, "Received %d bytes on handle %d", len, server );
    return 0;
 	
}



////////////////////////////////////////////////////////////////////////////////
//
// Function     : readBytes
// Description  : Read a certain amount of bytes from the client
//
// Inputs       : server - socket file handle
//		  len - amount of bytes to read
//		  block - the read of the block
// Outputs      : 0 if successful, -1 if failure

int readBytes ( int server, uint32_t len, unsigned char *block ) {

	int readBytes = 0;     //how many bytes have been read
	int rb;		       //current byte that has been read

	//Run until all bytes have been read which is when the amount
	//of bytes that have been read ( readBytes ) is no longer less then
	//the amount of bytes total that need to be read ( len ).
	while ( readBytes < len ) {

		//Read byte into "block", at the index of readBytes, from the socket ( server ). 
		//The readBytes allows us to move across the "block" so that we don't read over stuff
		//on the "block". rb will contain the amount of bytes that were able to be read. We wish for
		//this to be the entire len variable, but it might not be ready for us to read the first time.
		//This loop will allow us to continue reading until we have read the amount of bytes in len.
		if ( (rb = read( server, &block[readBytes], len-readBytes )) < 0 ) {
			logMessage( LOG_ERROR_LEVEL, "_readBytes:Failed to read a byte [%s]", strerror(errno) );
			return 1;
		}
		else if ( rb == 0 ) {
			//This means the file was closed
			logMessage( LOG_ERROR_LEVEL, "_readBytes:File was closed" );
			return 1;
		}

		//Increment the amount of Bytes that have been read ( readBytes ), by the amount
		//of bytes that were just read in this iteration of the loop.
		readBytes =+ rb;
	}
		
	return 0;

}



////////////////////////////////////////////////////////////////////////////////
//
// Function     : sendPacket
// Description  : Send a packet to the client
//
// Inputs       : server - socket file handle
//		  op - opcode for smsa_operation
//		  ret - return of smsa_operation
// 		  blkSize - num of bytes to read into the block
//		  block - the read of the block
// Outputs      : 0 if successful, -1 if failure

int sendPacket ( int server, uint32_t op, int16_t ret, unsigned char *block ) {

	uint32_t len;
	uint32_t bufIndex = 0;
	int twoBytes = sizeof ( uint16_t );
	unsigned char buf[SMSA_NET_HEADER_SIZE+SMSA_BLOCK_SIZE];

    // SMSA Packet definition
    //
    //  Bytes 0-1   : length - how many total bytes in packet
    //  Bytes 2-6   : opcode - the opcode for the command
    //  Bytes 7-8   : return - return code of comamnd 
    //  Bytes 6-261 : block - as needed, SMSA_BLOCK
    //

    //Set the size of the packet. Which is the size of the packet header. Unless
    //there is a block to be read, then it is the size of a packet header plus
    //size of the block.
    len = SMSA_NET_HEADER_SIZE;
    if ( block != NULL ) {
	len += SMSA_BLOCK_SIZE;
    }
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Putting Together a Packet to Send of [%d] Bytes", len );

    //Put in network byte config
    len = htons(len);
    op = htonl(op);
    ret = htons(ret);

 
    //Put together a packet
    memcpy( &buf[bufIndex], &len, sizeof( len ) );	//LENGTH
    bufIndex += twoBytes;

    memcpy( &buf[bufIndex], &op, sizeof( op ) ); 	//OPCODE	
    bufIndex += 2*twoBytes;
 
    memcpy( &buf[bufIndex], &ret, sizeof( ret) );       //RETURN
    bufIndex += twoBytes;

    //If this is a read, add the block to the packet
    if ( block != NULL ) {
	memcpy( &buf[bufIndex], block, SMSA_BLOCK_SIZE );
	bufIndex += SMSA_BLOCK_SIZE; 	//incrememnt the index the size of the block we just added to the packet
    }
	

    //call sendBytes to send the packet we have just constructed to the 
    //client over our socket ( server ).
    logMessage( LOG_INFO_LEVEL, "Sending %d bytes on handle %d", bufIndex, server );
    return( sendBytes( server, bufIndex, buf) );

 	
}



////////////////////////////////////////////////////////////////////////////////
//
// Function     : sendBytes
// Description  : Read a certain amount of bytes from the client
//
// Inputs       : server - socket file handle
//		  len - amount of bytes to read
//		  block - the read of the block
// Outputs      : 0 if successful, -1 if failure

int sendBytes ( int server, uint32_t len, unsigned char *buf ) {

	int sentBytes = 0;   	//Amount of bytes that have been sent
	int sb;
	
	//Run until all bytes ( len ) have been sent which is when the 
	//amount of sentBytes no longer less than len
	while ( sentBytes < len ) {

		//Write bytes from buf onto the socket ( server ). It will attempt to write all 
		//bytes ( len ), but might not be able to. So it will store the amount of bytes that
		//were able to be sent into sb.
		if ( (sb = write( server, &buf[sentBytes], len-sentBytes )) < 0 ) {
			logMessage( LOG_ERROR_LEVEL, "_sendBytes:Failed to write a byte [%s]", strerror(errno) );
			return 1;
		}
		else if ( sb == 0 ) {
			//This means the file was closed
			logMessage( LOG_ERROR_LEVEL, "_sendBytes:File was closed" );
			return 1;
		}

		//Increment the amount of Bytes that have been sent ( sentBytes ), by the amount of
		//bytes that were able to be sent during this iteration of the loop ( sb ). 
		sentBytes =+ sb;
	}

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Successfully Sent [%d] Bytes", sentBytes );
		
	return 0;

}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : selectData
// Description  : Waits for data and then selects of it to be processed
//
// Inputs       : int - server file handler
// Outputs      : 0 if successful, -1 if failure

int selectData ( int sock ) {

	fd_set readEvent;
	int maxSocketChecks = sock + 1;

	//Initialize and set the file descriptors
	FD_ZERO( &readEvent );
	FD_SET( sock, &readEvent );

	//select and wait
	if ( select( maxSocketChecks, &readEvent, NULL, NULL, NULL ) == -1 ) {
		logMessage( LOG_ERROR_LEVEL, "_selectData:Failure to wait and select data" );
		return 1;
	}

	//make sure we are selected on the read
	if ( FD_ISSET ( sock, &readEvent ) == 0 ) {
		logMessage( LOG_ERROR_LEVEL, "_selectData:Failure to select on the read");
		return 1;
	}
	
	return 0;

}



////////////////////////////////////////////////////////////////////////////////
//
// Function     : setUpServer
// Description  : Sets up the server by creating the socket, binding it to the right addresss
//		  and starts the server listening. It then sets the server input equal to the
//		  socket file handler.
//
// Inputs       : int - server file handler
// Outputs      : 0 if successful, -1 if failure

int setupServer ( int *server ) {


	struct sigaction sigINT;   	   //holds the sigINT signal handler
	struct sockaddr_in serverAddress;  //holds server addres
	int optionValue = 1;		   //holds the value for setsocketopt function call


	//Set signal handler
	//This sets changes the current SIGINT signal handler to operate in the way
	//that we would like by having our smsa_signal_handler be called on reception
	//of a SIGINT from the OS, rather than the default handler
	sigINT.sa_handler = signalHandler;
	sigINT.sa_flags = SA_NODEFER | SA_ONSTACK;
	sigaction ( SIGINT, &sigINT, NULL );


	//Create the socket
	//Set up a socket using TCP protocol ( SOCK_STREAM ), and the address family 
	//version of inet, while setting the server variable to the file handle
	if ( ( *server = socket ( AF_INET, SOCK_STREAM, 0 ) ) == -1 ) {
		logMessage( LOG_ERROR_LEVEL, "_setUpServer:Failed to set up the socket [%s]", strerror(errno) );
		return 1;
	}

	logMessage( LOG_INFO_LEVEL, "Socket Successfully Initialized. Socket File Handle = %d", *server );
	
	//Set the socket to be reusable. 
	//The setsockopt will allow us to change options of our socket which is 
	//specified with our file handle, server.
	if ( setsockopt ( *server, SOL_SOCKET, SO_REUSEADDR, &optionValue, sizeof(optionValue) ) != 0 ) {
		logMessage ( LOG_ERROR_LEVEL, "_setUpServer:setsockopt failed to make the local address reusable [%s]", strerror(errno) );
		return 1;
	}

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Socket Set Up To Reuse Addresses" );

	//Set up server address
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons( SMSA_DEFAULT_PORT );
	serverAddress.sin_addr.s_addr = htonl( INADDR_ANY );


	//bind the server to the socket. bind the server file handle to 
	//the address we have initialized
	if ( bind ( *server, (struct sockaddr*)&serverAddress, sizeof( struct sockaddr) ) == -1 ) {
		logMessage ( LOG_ERROR_LEVEL, "_setUpServer:Failure to bind the server to the socket [%s]", strerror(errno) );
		return 1;
	}
	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Socket Is Now Bound To Any Address");

	//listen for connections
	if ( listen ( *server, SMSA_MAX_BACKLOG ) == -1 ) {
		logMessage ( LOG_ERROR_LEVEL, "_setUpServer:Failure to properly listen [%s]", strerror(errno) );
		return 1;
	}
	
	logMessage ( LOG_INFO_LEVEL, "Socket Is Now Listening Queueing %d Connections", SMSA_MAX_BACKLOG );


	logMessage ( LOG_INFO_LEVEL, "Server Has Now Been Successfully Setup" );
	return 0;

}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : signalHandler
// Description  : Handles a SIGINT from the operating system and changes the serverShutdown
//		  variable equal to zero.
//
// Inputs       : int - server file handler
// Outputs      : 0 if successful, -1 if failure

void signalHandler ( int signal ) {

	logMessage ( LOG_ERROR_LEVEL, "_signalHandler: Following Signal recieved %d. Shutting down Server", signal );
	serverShutdown = 1;


}

