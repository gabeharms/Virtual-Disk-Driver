////////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa.c
//  Description   : This is the implementation of the SMSA simulator.
//
//   Author        : Patrick McDaniel
//   Last Modified : Fri Sep 13 06:55:42 EDT 2013
//

// Include files
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

// Project Include files
#include <smsa.h>
#include <smsa_internal.h>
#include <cmpsc311_log.h>
#include <cmpsc311_util.h>

//
// Defines
//#define SMSA_BLOCK_ADDRESS(drum,blk) ((smsa_disk_array[drum])+(blk*SMSA_BLOCK_SIZE))
#define SMSA_BLOCK_ADDRESS(drum,blk) &smsa_disk_array[drum][blk*SMSA_BLOCK_SIZE]
// #define SMSA_STORAGE_ENABLED
#define SMSA_ROW(x) ((int)x/4)
#define SMSA_COL(x) (x%4)
#define SMSA_DIFF(x,y) ((x>y) ? (x-y) : (y-x))

//
// Library global data

static uint8_t				smsa_library_initialized = 0;	// Flag indicating the library init occurred
static uint32_t				smsa_mount_state = 0;  			// Mount state (0=not mounted, 1=mounted)
SMSA_ERROR_LEVEL			smsa_error_number = 0;			// This is the current error number

// This is the disk array itself
static uint8_t				smsa_drum_head; // The current drum under eval
static uint32_t				smsa_read_head; // The current read position on the drum
static unsigned char		       *smsa_disk_array[SMSA_DISK_ARRAY_SIZE]; // The disk memory
static unsigned long                    smsa_cycle_count = 0; // This is the clock count for the SMSA

// This is the text associated with the SMSA operation (commands)
static const char *smsa_op_text[] = {
		"SMSA_MOUNT",  		// Mount the disk array
		"SMSA_UNMOUNT",  	// Unmount the disk array
		"SMSA_SEEK_DRUM",  	// See to a new drum
		"SMSA_SEEK_BLOCK",	// Seek to a block in the current drum
		"SMSA_DISK_READ",	// Read from the disk
		"SMSA_DISK_WRITE",	// Write to the disk
		"SMSA_GET_STATE",	// Get the current disk state (unimplemented)
		"SMSA_BLOCK_SIGN",  // Generate a signature for a block (and output to log)
		"SMSA_FORMAT_DRUM",	// Format the current drum (zeros)
};

// This is the text associated with the SMSA disk error
static const char *smsa_error_text[] = {
		"SMSA_NO_ERROR",				// No error has occurred
		"SMSA_UNMOUNTED_DISK",			// Operation attempted on unmounted disk
		"SMSA_ILLEGAL_DRUM",			// Operating with bad/illegal drum
		"SMSA_DISK_CACHELOAD_FAIL",		// Unable to load cache file
		"SMSA_DISK_CACHEWRITE_FAIL",	// Unable to load cache file
		"SMSA_BAD_OPCODE",				// Bad command id
		"SMSA_BAD_DRUM_ID", 			// Bad drum id
		"SMSA_BAD_BLOCK_ID",			// Bad block id
		"SMSA_BAD_READ",				// Performed an illegal read
		"SMSA_BAD_WRITE",				// Performed an illegal write
		"SMSA_SIG_FAIL",				// Signature generation failed
		"UNKNOW ERROR"					// Unknown error
};

// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_operation
// Description  : This is the external interface to the disk array.
//
// Inputs       : op - the operation encoded structure
//              : block - the block of data to operate on
// Outputs      : 0 if successful test, -1 if failure

int smsa_operation( uint32_t op, unsigned char *block ) {

	// Local variables
	int retcode = 0;
	SMSA_OPERATION dop;

	// Decode the command and log it if verbose
	if ( decode_SMSA_operation(&dop, op, block) ) {
		logMessage( LOG_ERROR_LEVEL, "Unable to decode SMSA operation [%lu]", op );
	}
	logMessage( LOG_INFO_LEVEL, "SMSA Array received operation [%s/did=%d,blk=%d]",
			smsa_op_text[dop.cmd], dop.did, dop.bid );


	// Check to see if this is the first time we have called the library
	if ( smsa_library_initialized == 0 ) {

		// Setup the virtual hardware initial state
		smsa_mount_state  = 0;
		smsa_error_number = SMSA_NO_ERROR;
		memset( smsa_disk_array, 0x0, sizeof(smsa_disk_array) );
		smsa_library_initialized = 1;
	}

	// Count the cycles the operation will take
	smsa_cycle_count += operation_cycle_cost( dop.cmd, dop.did, dop.bid );

	// Perform the disk operation
	switch (dop.cmd) {

		case SMSA_MOUNT: // Mount the disk array
			retcode = SMSAMountArray();
			break;

		case SMSA_UNMOUNT: // Unmount the disk array
			retcode = SMSAUnmountArray();
			break;

		case SMSA_SEEK_DRUM: // See to a new drum
			retcode = SMSASeekDrum( dop.did );
			break;

		case SMSA_SEEK_BLOCK: // Seek to a disk address in the current drum
			retcode = SMSASeekBlock( dop.bid );
			break;

		case SMSA_DISK_READ: // Read from the disk
			retcode = SMSAReadBlock( block );
			break;

		case SMSA_DISK_WRITE: // Write to the disk
			retcode = SMSAWriteBlock( block );
			break;

		case SMSA_GET_STATE: // Get the current disk state (unimplemented)
			logMessage( LOG_ERROR_LEVEL, "Get state UNIMPLEMENTED, ignoring" );
			retcode = 0;
			break;

		case SMSA_FORMAT_DRUM: // Format the current drum (zeros)
			retcode = SMSAFormatDrum();
			break;

		case SMSA_BLOCK_SIGN: // Generate a signature for a block (and output to log)
			retcode = SMSABlockSign( dop.did, dop.bid );
			break;

		default: logMessage( LOG_ERROR_LEVEL, "OP Illegal disk command [%u]", dop.cmd );
			retcode = -1;
			break;
	}

	// Return successfully
	return( retcode );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_error_string
// Description  : This returns a constant string detailing the meaning of
//                an SMSA error
//
// Inputs       : eno - error number
// Outputs      : a pointer to the text associated with the error

const char * smsa_error_string( int eno ) {

	// Check for an in range error number
	if ( eno < 0 || eno >= SMSA_MAX_ERRNO ) {
		return( smsa_error_text[SMSA_MAX_ERRNO] );
	}

	// Just return the error string from the library array
	return( smsa_error_text[eno] );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSABlockSign
// Description  : Write a block to the current read head positions
//
// Inputs       : drum - the drum to sign the block from
//                block - the block to generate the signature for
// Outputs      : 0 if successful test, -1 if failure

int SMSABlockSign( SMSA_DRUM_ID drum, SMSA_BLOCK_ID block ) {

	// Local variables
	unsigned char sig[CMPSC311_HASH_LENGTH], sigstr[CMPSC311_HASH_LENGTH*4];
	uint32_t slen = CMPSC311_HASH_LENGTH*4;

	// Check for sane signature address
	if ( drum >= SMSA_DISK_ARRAY_SIZE ) {
		logMessage( LOG_ERROR_LEVEL, "Illegal signature drum [%u/%u]",	smsa_drum_head, smsa_read_head );
		smsa_error_number =	SMSA_BAD_DRUM_ID;
		return( -1 );
	}
	if ( block >= SMSA_DISK_SIZE ) {
		logMessage( LOG_ERROR_LEVEL, "Illegal signature block [%u/%u]",	smsa_drum_head, smsa_read_head );
		smsa_error_number =	SMSA_BAD_BLOCK_ID;
		return( -1 );
	}

	// Now do the signature and check the result
	if ( generate_md5_signature( block_address(drum,block), SMSA_BLOCK_SIZE, sig, &slen) ) {
		logMessage( LOG_ERROR_LEVEL, "Signature failed (%d/%d]", drum, block );
		smsa_error_number =	SMSA_SIG_FAIL;
		return( -1 );
	}

	// Log the string byte for the message
	bufToString( sig, slen, sigstr, CMPSC311_HASH_LENGTH*4 );
	logMessage( LOG_OUTPUT_LEVEL, "SIG(drum,block) %2d %3d : %s", drum, block, sigstr );

	// Return successfully
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_get_cycle_count
// Description  : Return the current number of cycles expended
//
// Inputs       : none
// Outputs      : the cycle count

unsigned long smsa_get_cycle_count( void ) {

	// Return the cycle count
	return( smsa_cycle_count );
}

//
// Internal Disk Interfaces

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSAMountArray
// Description  : Mount the array (load from disk or init)
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int SMSAMountArray( void ) {

	// Local variables
	int i;

	// See if already mounted
	if ( smsa_mount_state ) {
		logMessage( LOG_INFO_LEVEL, "Trying to mount already mounted disk array, ignoring." );
		return( 0 );
	}

	// Mounting operation begin
	logMessage( LOG_INFO_LEVEL, "Mounting the disk array ..." );

	// Allocate the data for the array, set pointer to the beginning of the array
	for ( i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
		smsa_disk_array[i] = malloc( SMSA_DISK_SIZE );
		memset( smsa_disk_array[i], 0x0, SMSA_DISK_SIZE );
	}
	smsa_drum_head = 0;
	smsa_read_head = 0;

	// Mounting operation finished, set appropriate flag
	logMessage( LOG_INFO_LEVEL, "Mounted the disk array successfully." );
	smsa_mount_state  = 1;

#if SMSA_STORAGE_ENABLED
	// Try to load the disk array from disk file or format disks if not available
	if ( SMSALoadArray() != 0 ) {
		logMessage( LOG_INFO_LEVEL, "No mount data or failed, resetting disk data." );

		// Initialize the disk array data
		for (  i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
			SMSASeekDrum( i );
			SMSAFormatDrum();
		}
	}
#endif

	// Return successfully
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSAUmountArray
// Description  : Unmount the array, saving to disk if possible
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int SMSAUnmountArray( void ) {

	// Local variables
	int i;

	// See if already mounted
	if ( ! smsa_mount_state ) {
		logMessage( LOG_INFO_LEVEL, "Trying to unmount unmounted disk array, ignoring." );
		return( 0 );
	}

	// Mounting operation begin
	logMessage( LOG_INFO_LEVEL, "Unmounting the disk array ..." );

	// Store contents, deallocate the data from the array, reset disk heads
#if SMSA_STORAGE_ENABLED
	SMSAStoreArray();
#endif
	for ( i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
		free( smsa_disk_array[i] );
		smsa_disk_array[i] = NULL;
	}
	smsa_drum_head = 0;
	smsa_read_head = 0;
	smsa_mount_state = 0;

	// Return successfully
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSASeekDrum
// Description  : Seek to another drum in the array
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int SMSASeekDrum( SMSA_DRUM_ID did ) {

	// Check to see if the disk array has been mounted
	if ( ! smsa_mount_state ) {
		logMessage( LOG_ERROR_LEVEL, "Trying to seek on unmounted array." );
			smsa_error_number = SMSA_UNMOUNTED_DISK;
		return( -1 );
	}

	// Storing operation begin
	logMessage( LOG_INFO_LEVEL, "Seeking new drum [%u]", did );

	// Check for legal disk
	if ( did >= SMSA_DISK_ARRAY_SIZE ) {
		logMessage( LOG_ERROR_LEVEL, "Seek illegal drum id [%u]", did );
		smsa_error_number = SMSA_BAD_DRUM_ID;
		return( -1 );
	}

	// Move to the drum and return successfully
	smsa_drum_head = did;
	smsa_read_head = 0;
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSASeekBlock
// Description  : Seek to a block in the array
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int SMSASeekBlock( SMSA_BLOCK_ID blk ) {

	// Check to see if the disk array has been mounted
	if ( ! smsa_mount_state ) {
		logMessage( LOG_ERROR_LEVEL, "Trying to seek on unmounted array." );
			smsa_error_number = SMSA_UNMOUNTED_DISK;
		return( -1 );
	}

	// Storing operation begin
	logMessage( LOG_INFO_LEVEL, "Seeking new block [%u] on current disk [%d]", blk, smsa_drum_head );

	// Check for legal disk
	if ( blk >= SMSA_MAX_BLOCK_ID ) {
		logMessage( LOG_ERROR_LEVEL, "Seek illegal block id [%u]", blk );
		smsa_error_number = SMSA_BAD_BLOCK_ID;
		return( -1 );
	}

	// Move to the drum and return successfully
	smsa_read_head = blk;
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSAReadBlock
// Description  : Read a block from the current read head positions
//
// Inputs       : block - the buffer to place the data in
// Outputs      : 0 if successful test, -1 if failure

int SMSAReadBlock( unsigned char *block ) {

	// Storing operation begin
	logMessage( LOG_INFO_LEVEL, "Reading drum/block [%u/%u]", smsa_drum_head, smsa_read_head );
	assert( smsa_drum_head < SMSA_DISK_ARRAY_SIZE );
	assert( smsa_read_head < SMSA_MAX_BLOCK_ID );

	// Check to see if the disk array has been mounted
	if ( ! smsa_mount_state ) {
		logMessage( LOG_ERROR_LEVEL, "Trying to read on unmounted array." );
			smsa_error_number = SMSA_UNMOUNTED_DISK;
		return( -1 );
	}

	// Check to make sure that we are in a good read place
	if ( (smsa_drum_head >= SMSA_DISK_ARRAY_SIZE) || (smsa_read_head >= SMSA_MAX_BLOCK_ID) ) {
		logMessage( LOG_ERROR_LEVEL, "Illegal read drum/block [%u/%u]",
				smsa_drum_head, smsa_read_head );
		smsa_error_number = SMSA_BAD_READ;
		return( -1 );
	}

	// Now do the read and return successfully
	memcpy( block, SMSA_BLOCK_ADDRESS(smsa_drum_head,smsa_read_head), SMSA_BLOCK_SIZE );
	smsa_read_head ++;
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSAWriteBlock
// Description  : Write a block to the current read head positions
//
// Inputs       : block - the buffer to obtain data to write
// Outputs      : 0 if successful test, -1 if failure

int SMSAWriteBlock( unsigned char *block ) {

	// Log the write, check to see if current position sane
	logMessage( LOG_INFO_LEVEL, "Write drum/block [%u/%u]", smsa_drum_head, smsa_read_head );
	assert( smsa_drum_head < SMSA_DISK_ARRAY_SIZE );
	assert( smsa_read_head < SMSA_MAX_BLOCK_ID );

	// Check to see if the disk array has been mounted
	if ( ! smsa_mount_state ) {
		logMessage( LOG_ERROR_LEVEL, "Trying to write on unmounted array." );
			smsa_error_number = SMSA_UNMOUNTED_DISK;
		return( -1 );
	}

	// Check the write for sanity
	if ( (smsa_drum_head >= SMSA_DISK_ARRAY_SIZE) || (smsa_read_head >= SMSA_MAX_BLOCK_ID) ) {
		logMessage( LOG_ERROR_LEVEL, "Illegal write drum/block [%u/%u]",
				smsa_drum_head, smsa_read_head );
		smsa_error_number = SMSA_BAD_WRITE;
		return( -1 );
	}

	// Now do the read and return successfully
	memcpy( SMSA_BLOCK_ADDRESS(smsa_drum_head,smsa_read_head), block, SMSA_BLOCK_SIZE );
	smsa_read_head ++;
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSAFormatDrum
// Description  : Format the drum at the current head location
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int SMSAFormatDrum( void ) {

	// Log the format
	logMessage( LOG_INFO_LEVEL, "Formatting drum [%u] ...", smsa_drum_head );

	// Check if the drum array has been mounted
	if ( ! smsa_mount_state ) {
		smsa_error_number = SMSA_UNMOUNTED_DISK;
		return( -1 );
	}

	// Check if we are on a legal drum
	if ( smsa_drum_head >= SMSA_DISK_ARRAY_SIZE ) {
		smsa_error_number = SMSA_ILLEGAL_DRUM;
		return( -1 );
	}

	// Zero the disk contents, reset the read head
	memset( smsa_disk_array[smsa_drum_head], 0x0, SMSA_DISK_SIZE );
	smsa_drum_head = 0;
	smsa_read_head = 0;

	// Log the format completion
	logMessage( LOG_INFO_LEVEL, "Formatting drum [%u] completed successfully.", smsa_drum_head );

	// Return successfully
	return( 0 );
}

//
// Utility functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSAStoreArray
// Description  : Push the contents of the array to disk file.
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int SMSAStoreArray( void ) {

	// Local variables
	int fh, bytes, i;

	// Storing operation begin
	logMessage( LOG_INFO_LEVEL, "Storing the disk array contents ..." );

	// Open the disk file, check for error
	if ( (fh=open(SMSA_DISK_FILE, O_CREAT|O_WRONLY|O_TRUNC,S_IRWXU)) == -1 ) {
		logMessage( LOG_ERROR_LEVEL, "Failure opening array data for store [%s], error=[%s]",
				SMSA_DISK_FILE, strerror(errno) );
		smsa_error_number = SMSA_DISK_CACHEWRITE_FAIL;
		return( -1 );
	}

	// Now read the disk data
	for ( i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
		// Keep reading disk by disk
		bytes = write( fh, smsa_disk_array[i], SMSA_DISK_SIZE );
		if ( bytes != SMSA_DISK_SIZE ) {
			logMessage( LOG_ERROR_LEVEL, "Failure writing array data [%s], error=[%s]",
							SMSA_DISK_FILE, strerror(errno) );
			smsa_error_number = SMSA_DISK_CACHEWRITE_FAIL;
			return( -1 );
		}
		logMessage( LOG_INFO_LEVEL, "Wrote disk (%d) contents successfully", i );
	}

	// Now close the file and log results
	close( fh );
	logMessage( LOG_INFO_LEVEL, "Stored the disk array contents successfully." );

	// Return successfully
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : SMSALoadArray
// Description  : Read the contents of the array from a disk file.
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int SMSALoadArray( void ) {

	// Local variables
	int fh, bytes, i;

	// Storing operation begin
	logMessage( LOG_INFO_LEVEL, "Loading the disk array contents ..." );

	// Open the disk file, check for error
	if ( (fh=open(SMSA_DISK_FILE, O_RDONLY)) == -1 ) {
		logMessage( LOG_ERROR_LEVEL, "Failure opening array data for load [%s], error=[%s]",
				SMSA_DISK_FILE, strerror(errno) );
		smsa_error_number = SMSA_DISK_CACHELOAD_FAIL;
		return( -1 );
	}

	// Now read the disk data
	for ( i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
		// Keep reading disk by disk
		bytes = read( fh, smsa_disk_array[i], SMSA_DISK_SIZE );
		if ( bytes != SMSA_DISK_SIZE ) {
			logMessage( LOG_ERROR_LEVEL, "Failure reading array data [%s], error=[%s]",
							SMSA_DISK_FILE, strerror(errno) );
			smsa_error_number = SMSA_DISK_CACHELOAD_FAIL;
			return( -1 );
		}
		logMessage( LOG_INFO_LEVEL, "Loaded disk (%d) contents successfully", i );
	}

	// Now close the file and log results
	close( fh );
	logMessage( LOG_INFO_LEVEL, "Loaded the disk array contents successfully." );

	// Return successfully
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : decode_SMSA_operation
// Description  : This function decodes a disk operation from a buffer for
//                receiving from the disk layer.
//
// Inputs       : dop - structure to place operation contents on
//                op - the operation structure
//                block - the disk block
// Outputs      : 0 if successful test, -1 if failure

int decode_SMSA_operation( SMSA_OPERATION *dop, uint32_t op, unsigned char *block ) {

	/*
	 * SMSA operation bit layout
	 *
	 * 	0-5		- command number (6-bits)
	 * 	6-9		- drum identifier (4-bits)
	 * 	10-23	- RESERVED (unused)
	 * 	24-31	- block address (8-bits)
	 *
	 */

	// Do the bit manipulations
	dop->cmd = (op>>26);		// The type of operation being performed
	dop->did = (op>>22)&0xf;	// This is the drum to be written to/read from
	dop->bid = (op&0xff);		// This is the block address to read/write

	// Check for legal values
	if ( dop->cmd >= SMSA_MAX_COMMAND ) {
		logMessage( LOG_ERROR_LEVEL, "Decoded operation illegal [%lu->%u]", op, dop->cmd );
		smsa_error_number = SMSA_BAD_OPCODE;
		return( -1 );
	}

	// Check for legal disk
	if ( dop->did >= SMSA_DISK_ARRAY_SIZE ) {
		logMessage( LOG_ERROR_LEVEL, "Decoded drum id illegal [%lu->%u]", op, dop->did );
		smsa_error_number = SMSA_BAD_DRUM_ID;
		return( -1 );
	}

	// Check for legal block address
	if ( dop->bid >= SMSA_MAX_BLOCK_ID ) {
		logMessage( LOG_ERROR_LEVEL, "Decoded block id illegal [%lu->%u]", op, dop->bid );
		smsa_error_number = SMSA_BAD_BLOCK_ID;
		return( -1 );
	}

	// Set the block for processing
	if ( block != NULL ) {
		dop->len = SMSA_BLOCK_SIZE;
		dop->blk = block;
	} else {
		dop->len = 0;
		dop->blk = NULL;
	}

	// Return successfully
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : encode_SMSA_operation
// Description  : This function encodes a disk operation in a buffer for
//                passing to the disk layer.
//
// Inputs       : cmd - the command to be created
//                did - the drum identifier
//                bid - the block identifier
// Outputs      : the encoded operation value or 0 on failure

uint32_t encode_SMSA_operation( SMSA_DISK_COMMAND cmd, SMSA_DRUM_ID did, SMSA_BLOCK_ID bid ) {

	// Check for legal command
	if ( cmd >= SMSA_MAX_COMMAND ) {
		logMessage( LOG_ERROR_LEVEL, "Encoding illegal operation  [%u]", cmd );
		smsa_error_number = SMSA_BAD_OPCODE;
		return( 0 );
	}

	// Check for legal disk
	if ( did >= SMSA_DISK_ARRAY_SIZE ) {
		logMessage( LOG_ERROR_LEVEL, "Encoding illegal drum id [%u]", did );
		smsa_error_number = SMSA_BAD_DRUM_ID;
		return( 0 );
	}

	// Check for legal block address
	if ( bid >= SMSA_MAX_BLOCK_ID ) {
		logMessage( LOG_ERROR_LEVEL, "Encoding illegal block id [%u]", bid );
		smsa_error_number = SMSA_BAD_BLOCK_ID;
		return( 0 );
	}

	// Do the bit operations and return
	uint32_t op = 0;
	op |= (cmd<<26);
	op |= (did<<22);
	op |= bid;
	return( op );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_address
// Description  : This function calculates the address of a block
//
// Inputs       : did - the drum identifier
//                bid - the block identifier
// Outputs      : the pointer to the block in memory

unsigned char * block_address( SMSA_DRUM_ID did, SMSA_BLOCK_ID bid ) {

	// Get drum address, then add offset of
	unsigned char *ptr = smsa_disk_array[did];
	uint32_t offset = (bid*SMSA_BLOCK_SIZE);
	ptr += offset;
	return( ptr );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : opperation_cycle_cost
// Description  : This function calculates the cycle cost of an operation
//
// Inputs       : cmd - the operatio to perform
//                did - the drum identifier
//                bid - the block identifier
// Outputs      : the pointer to the block in memory

int operation_cycle_cost( SMSA_DISK_COMMAND cmd, SMSA_DRUM_ID did, SMSA_BLOCK_ID bid ) {

    // Local variables
    int cost = 0;

    // Perform the disk operation
    switch (cmd) {

	case SMSA_MOUNT: // Mount the disk array
	    cost = 10000;
	    break;

	case SMSA_UNMOUNT: // Unmount the disk array
	    cost = 10000;
	    break;

	case SMSA_SEEK_DRUM: // See to a new drum
	    cost = SMSA_DIFF(SMSA_ROW(smsa_drum_head),SMSA_ROW(did));
            cost = SMSA_DIFF(SMSA_COL(smsa_drum_head),SMSA_COL(did));
	    cost *= 1000;
	    break;
    
	case SMSA_SEEK_BLOCK: // Seek to a disk address in the current drum
	    cost = SMSA_DIFF(smsa_read_head,bid)*10;
	    break;

	case SMSA_DISK_READ: // Read from the disk
	    cost = 50;
	    break;

	case SMSA_DISK_WRITE: // Write to the disk
	    cost = 200;
	    break;

	case SMSA_GET_STATE: // Get the current disk state (unimplemented)
	    logMessage( LOG_ERROR_LEVEL, "Get state UNIMPLEMENTED, ignoring" );
	    cost = 0;
	    break;

	case SMSA_FORMAT_DRUM: // Format the current drum (zeros)
	    cost = 0;
	    break;

	case SMSA_BLOCK_SIGN: // Generate a signature for a block (and output to log)
	    cost = 0;
	    break;

	default: logMessage( LOG_ERROR_LEVEL, "OP Illegal disk command (cost) [%u]", cmd );
	    cost = -1;
	    break;
    }

    // Return successfully
    return( cost );
}

