////////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa_client.c
//  Description   : This is the client side of the SMSA communication protocol.
//
//   Author        : 
//   Last Modified : 
//


// Library Include Files
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
int sock;                        //file handle for the socket

//Functional Prototypes
int setupConnection ( int *socket );
int recievePacket ( int server, uint32_t *op, int16_t *ret, int *blkSize, unsigned char *block );
int readBytes ( int server, uint32_t len, unsigned char *block );
int sendPacket ( int server, uint32_t op, int16_t ret, unsigned char *block );
int sendBytes ( int server, uint32_t len, unsigned char *block );
int selectData ( int sock );
void signalHandler ( int signal );

//
// Functions

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


        int blkSize = SMSA_BLOCK_SIZE;     //number of bytes ia block
        int16_t ret;                       //


	//If we recieve a MOUNT command, then a connection needs to be established with
	//the server, so that we may send the disk commands
	if ( SMSA_OPCODE(op) == SMSA_MOUNT ) {
        	if ( setupConnection ( &sock ) ) {
                	logMessage ( LOG_ERROR_LEVEL, "_smsa_client_operation:Failed to properly set up the connection" );
                	return 1;
		}
		logMessage ( LOG_INFO_LEVEL, "Socket Successfully initialized. Socket File Handle [%d]", sock );
        }
	

        //send a request
        if ( sendPacket( sock, op, 0, (SMSA_OPCODE(op) == SMSA_DISK_WRITE) ? block: NULL ) == -1) {
        	logMessage ( LOG_ERROR_LEVEL, "_smsa_client_operation:Failed to send a request" );
                return 1;
        }

	logMessage ( LOG_INFO_LEVEL, "Packet Sent to the Server" );
 
       	//Wait for response to come in
       	if ( selectData ( sock ) ) {
       		logMessage ( LOG_ERROR_LEVEL, "_smsa_server:Failed to select data [%s]", strerror(errno) );
                return 1;
       	}

	logMessage( LOG_INFO_LEVEL, "Selected Data Sent From the Server. Processing Now..." );

       	//recieve data and process the packet
       	if ( recievePacket( sock, &op, &ret, &blkSize, block ) == 1 ) {
       		logMessage( LOG_ERROR_LEVEL, "_smsa_server:Failed to properly recieve a packet" );
                return 1;
       	}

	logMessage ( LOG_INFO_LEVEL, "Packet Successfully Processed" );
       
	
	//If the incoming command was an unmount command, then the disk, by now, has
	//been unmounted and will not be used again until mount is recieved. This means
	//that it is ok to close down the connection with the server	
	if ( SMSA_OPCODE(op) == SMSA_UNMOUNT ) {
		close( sock );
		logMessage ( LOG_INFO_LEVEL, "Sending UNMOUNT Command. Closing Connection with the Server");
	}
 	

       	return 0;
}



////////////////////////////////////////////////////////////////////////////////
//
// Function     : recievePacket
// Description  : Recieve a packet from the client
//
// Inputs       : server - socket file handle
//                op - opcode for smsa_operation
//                ret - return of smsa_operation
//                blkSize - num of bytes to read into the block
//                block - the read of the block
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

    	//Read the packet into header
    	if ( readBytes ( server, SMSA_NET_HEADER_SIZE, header ) == -1 ) {
                logMessage( LOG_ERROR_LEVEL, "_recievePacket:Failure to read bytes properly [%s]", strerror(errno) );
                return 1;
    	}


    	//Put data into host byte order
    	//The first two bytes that have been placed in header is the length
    	//of the entire packet. The next 4 bytes is the opcode of the command
    	//to be used on the smsa_operation function ( size of uint32_t ). The
    	//following two bytes hold the return of the command. All of the remaining
    	//bytes ( len - index ), are the block ( 255 bytes ), and will be read into the block
    	memcpy( &len, header, twoBytes );           //LENGTH
    	headerIndex += twoBytes;

    	len = ntohs ( len );  //host byte order

    	memcpy( op, &header[headerIndex], 2*twoBytes );       //OPCODE
    	headerIndex += 2*twoBytes;

    	*op = ntohl ( *op ); //host byte order

    	memcpy( ret, &header[headerIndex], twoBytes );       //RETURN
    	headerIndex += twoBytes;

    	*ret = ntohs ( *ret ); //host byte order

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Packet Header Successfully Processed, len [%d], op [%d], ret [%d]", len, *op, *ret );
	
	//check return and make sure it is not an invalid return
	if ( *ret == 1 ) {
		logMessage ( LOG_INFO_LEVEL, "_readPacket:Return value is an error value" );
		return 1;
	}
		

    	//check to see if there is more data to read
	if ( len > SMSA_NET_HEADER_SIZE ) {

		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "Packet Contains Block. Reading Now..");

        	//Call readBytes to read the Length of the packet ( len ) minus the 
        	//size of the packet header ( SMSA_NET_HEADER_SIZE ), which is in fact
        	//reading the block portion of the packet 
        	if ( readBytes( server, len-SMSA_NET_HEADER_SIZE, block ) == -1 ) {
          		logMessage( LOG_ERROR_LEVEL, "_recievePacket:Failed to read bytes [%s]", strerror(errno) );
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
//                len - amount of bytes to read
//                block - the read of the block
// Outputs      : 0 if successful, -1 if failure

int readBytes ( int server, uint32_t len, unsigned char *block ) {

        int readBytes = 0;     //how many bytes have been read
        int rb;                //current byte that has been read

        //Run until all bytes have been read which is when the amount
        //of bytes that have been read ( readBytes ) is no longer less then
        //the amount of bytes total that need to be read ( len ).
        while ( readBytes < len ) {

                //Read byte into "block", at the "block" index of readBytes, from the socket ( server ). 
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
	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Successfully Read [%d] Bytes", readBytes );

        return 0;

}



////////////////////////////////////////////////////////////////////////////////
//
// Function     : sendPacket
// Description  : Send a packet to the client
//
// Inputs       : server - socket file handle
//                op - opcode for smsa_operation
//                ret - return of smsa_operation
//                blkSize - num of bytes to read into the block
//                block - the read of the block
// Outputs      : 0 if successful, -1 if failure

int sendPacket ( int server, uint32_t op, int16_t ret, unsigned char *block ) {

        uint32_t len;
        uint32_t bufIndex = 0;
        int twoBytes = sizeof ( uint16_t );
        unsigned char buf[SMSA_NET_HEADER_SIZE + SMSA_BLOCK_SIZE];

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
		logMessage ( LOG_INFO_LEVEL, "Putting Together Packet to be Sent of [%d] Bytes", len );

    //Put in network byte config
    len = htons(len);
    op = htonl(op);
    ret = htons(ret);


    //Put together a packet
    memcpy( &buf[bufIndex], &len, sizeof( len ) );      //LENGTH
    bufIndex += twoBytes;

    memcpy( &buf[bufIndex], &op, sizeof( op ) );        //OPCODE        
    bufIndex += 2*twoBytes;

    memcpy( &buf[bufIndex], &ret, sizeof( ret) );       //RETURN
    bufIndex += twoBytes;

    //If this is a read, add the block to the packet
    if ( block != NULL ) {
        memcpy( &buf[bufIndex], block, SMSA_BLOCK_SIZE );
        bufIndex += SMSA_BLOCK_SIZE;    //incrememnt the index the size of the block we just added to the packet
    }

    //call sendBytes to send the packet we have just constructed to the 
    //client over our socket ( server ).
    logMessage( LOG_INFO_LEVEL, "Sending %d bytes on handle %d", bufIndex, server );
    return( sendBytes( server, bufIndex, buf) );


}

//////////////////////////////////////////////////////////////////
// Function     : sendBytes
// Description  : Read a certain amount of bytes from the client
//
// Inputs       : server - socket file handle
//                len - amount of bytes to read
//                block - the read of the block
// Outputs      : 0 if successful, -1 if failure

int sendBytes ( int server, uint32_t len, unsigned char *buf ) {

        int sentBytes = 0;      //Amount of bytes that have been sent
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
                logMessage( LOG_ERROR_LEVEL, "_selectData:Failure to wait and select data [%s]", strerror(errno) );
                return 1;
        }

        //make sure we are selected on the read
        if ( FD_ISSET ( sock, &readEvent ) == 0 ) {
                logMessage( LOG_ERROR_LEVEL, "_selectData:Failure to select on the read [%s]", strerror(errno) );
                return 1;
        }

        return 0;

}



////////////////////////////////////////////////////////////////////////////////
//
// Function     : setUpServer
// Description  : Sets up the server by creating the socket, binding it to the right addresss
//                and starts the server listening. It then sets the server input equal to the
//                socket file handler.
//
// Inputs       : int - server file handler
// Outputs      : 0 if successful, -1 if failure

int setupConnection ( int *sock ) {


        struct sockaddr_in clientAddress;  //holds server addres
	char *ip = "127.0.0.1";


	//Set up teh client address settings
	clientAddress.sin_family = AF_INET;
	clientAddress.sin_port = htons(SMSA_DEFAULT_PORT);
	if ( inet_aton ( ip, &clientAddress.sin_addr ) == 0 ) {
		logMessage ( LOG_ERROR_LEVEL, "_setupConnection:Failed to map ip adderss to the clientAddress [%s]", strerror(errno) );
		return 1;
	}
 
	logMessage ( LOG_INFO_LEVEL, "Successfully mapped clientAddress to Port [%d], ip [%s]", SMSA_DEFAULT_PORT, ip );

        //Create the socket
        //Set up a socket using TCP protocol ( SOCK_STREAM ), and the address family 
        //version of inet, while setting the server variable to the file handle
        if ( ( *sock = socket ( PF_INET, SOCK_STREAM, 0 ) ) == -1 ) {
                logMessage( LOG_ERROR_LEVEL, "_setupConnection:Failed to set up the socket [%s}", strerror(errno) );
                return 1;
        }

	logMessage ( LOG_INFO_LEVEL, "Socket Successfully Initialized" );

        //Connect the socket to the clientAddress
        if ( connect ( *sock, (const struct sockaddr*)&clientAddress,  sizeof(struct sockaddr) ) == -1 ) {
                logMessage ( LOG_ERROR_LEVEL, "_setupConnection:Failed during the connect function [%s]", strerror(errno) );
                return 1;
        }

	logMessage ( LOG_INFO_LEVEL, "Connected socket to server" );

	return 0;
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : signalHandler
// Description  : Handles a SIGINT from the operating system and changes the serverShutdown
//                variable equal to zero.
//
// Inputs       : int - server file handler
// Outputs      : 0 if successful, -1 if failure

void signalHandler ( int signal ) {

        logMessage ( LOG_ERROR_LEVEL, "_signalHandler: Following Signal recieved %d. Shutting down Server", signal );
        serverShutdown = 1;


}

                                                                                                   
