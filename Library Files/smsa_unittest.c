////////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa_unittest.c
//  Description   : This is the UNIT TEST implementation of the SASA
//
//   Author : Patrick McDaniel
//   Last Modified : Sun Sep 22 16:34:32 EDT 2013
//

// Include Files
#include <string.h>
#include <assert.h>

// Project Includes
#include <smsa.h>
#include <smsa_internal.h>
#include <cmpsc311_log.h>
#include <cmpsc311_util.h>

// Defines

//
// Global Data

//
// Functional Prototypes
unsigned char * test_disk_block( SMSA_DRUM_ID did, SMSA_BLOCK_ID bid, unsigned char *blk );
int doVread( uint32_t addr, uint32_t len );
int translateVAddress( uint32_t addr, SMSA_DRUM_ID *drm, SMSA_BLOCK_ID *blk, uint32_t *offset ); // From implementation

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_unit_test
// Description  : This is the implementation of the UNIT test for the program.
//
// Inputs       : none
// Outputs      : 0 if successful, -1 otherwise

int smsa_unit_test( void ) {

	// Local variables
	int i, j;
	unsigned char blk[SMSA_BLOCK_SIZE], blk2[SMSA_BLOCK_SIZE];

	// Turn on the log level need to see everything happen, then announcing beginning
	enableLogLevels( LOG_ERROR_LEVEL|LOG_WARNING_LEVEL|LOG_INFO_LEVEL|LOG_OUTPUT_LEVEL );
	logMessage( LOG_INFO_LEVEL, "UNIT TEST Beginning ..." );

	//
	// PHASE 1 - FORMAT AND WRITE CONTENTS

	// Start by mounting the drive and formatting each of the disks
	smsa_operation( encode_SMSA_operation(SMSA_MOUNT, 0, 0), NULL );
	for ( i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
		smsa_operation( encode_SMSA_operation(SMSA_SEEK_DRUM, i, 0), NULL );
	}

	// Now write blocks to each of the disks
	for ( i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
		smsa_operation( encode_SMSA_operation(SMSA_SEEK_DRUM, i, 0), NULL ); // reset read head
		for ( j=0; j<SMSA_MAX_BLOCK_ID; j++ ) {
			smsa_operation( encode_SMSA_operation(SMSA_DISK_WRITE, 0, 0), test_disk_block(i,j,blk) );
		}
	}

	// Unmount (and save to disk)
	smsa_operation( encode_SMSA_operation(SMSA_UNMOUNT, 0, 0), NULL );

	//
	// PHASE 2 - READ CONTENTS

	// Remount the array
	smsa_operation( encode_SMSA_operation(SMSA_MOUNT, 0, 0), NULL );

	// Now write blocks to each of the disks
	for ( i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
		smsa_operation( encode_SMSA_operation(SMSA_SEEK_DRUM, i, 0), NULL ); // reset read head
		for ( j=SMSA_MAX_BLOCK_ID-1; j>=0; j-- ) {

			// Seek to specific disk location and execute
			smsa_operation( encode_SMSA_operation(SMSA_SEEK_BLOCK, 0, j), NULL ); // reset read head
			smsa_operation( encode_SMSA_operation(SMSA_DISK_READ, 0, 0), blk2 );

			// Now generate the disk block expected and compare
			test_disk_block( i, j, blk );
			if ( memcmp(blk, blk2, SMSA_BLOCK_SIZE) != 0 ) {
				logMessage( LOG_ERROR_LEVEL, "UNIT TEST FAILED DISK BLOCK COMPARE [drum=%d,block=%d]", i, j );
				return( -1 );
			} else {
				// Log the successful unit test operation
				logMessage( LOG_INFO_LEVEL, "Drum/Block [%d,%d] compare correct (%0x == %0x)\n",
						i, j, (uint32_t)blk[0], (uint32_t)blk2[0] );
			}
		}
	}

	// Now just test the disk block signature generation
	for ( i=0; i<SMSA_DISK_ARRAY_SIZE; i++ ) {
		for ( j=0; j<SMSA_MAX_BLOCK_ID; j++ ) {
			smsa_operation( encode_SMSA_operation(SMSA_BLOCK_SIGN, 0, j), NULL );
		}
	}

	//
	// End UNIT Test

	// Log success and return successfully
	logMessage( LOG_INFO_LEVEL, "UNIT TEST Successful." );
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_vread_unit_test
// Description  : This is the implementation of the vread UNIT test
//
// Inputs       : none
// Outputs      : 0 if successful, -1 otherwise

int smsa_vread_unit_test( void ) {

	// Setup variables and do a linear random walk of the address space
	uint32_t addr = 0, len;
	while ( addr <= MAX_SMSA_VIRTUAL_ADDRESS ) {

		// Get a random value, read bytes, and increment
		len = getRandomValue( 1, SMSA_MAXIMUM_RDWR_SIZE );
		logMessage( LOG_INFO_LEVEL, "*****" );
		logMessage( LOG_INFO_LEVEL, "VREAD Unit Test : reading address %lu, len %lu", addr, len );
		doVread( addr, len );
		addr += len;
	}

	// Return successfully
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : test_disk_block
// Description  : create a block for a specific drum and block ID
//
// Inputs       : did - drum ID
//                bid - block ID
//                blk - the block to setup
// Outputs      : a pointer to the block

unsigned char * test_disk_block( SMSA_DRUM_ID did, SMSA_BLOCK_ID bid, unsigned char *blk ) {

	// Set the block contents and return the block
	memset( blk, did^bid, SMSA_BLOCK_SIZE );
	return( blk );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : doVread
// Description  : do a virtual read of a block (test read/write algorithms)
//
// Inputs       : addr - the address to read from
//                len - the number of bytes to read
// Outputs      : a pointer to the block

int doVread( uint32_t addr, uint32_t len ) {

#if 0
	// Local variable
	uint32_t current, rlen, toread;
	SMSA_DRUM_ID drm;
	SMSA_BLOCK_ID blk;
	uint32_t offset;

	// Keep reading blocks until you get to the end
	current = addr;
	while ( current < addr + len ) {

		// Translate the address, figure out the length to read (for this block)
		translateVAddress( current, &drm, &blk, &offset );
		// logMessage( LOG_INFO_LEVEL, "Translate %u to (%u,%u,%u)", current, drm, blk, offset );


		// Compute the bytes left to read and then the bytes from block
		toread = ((addr+len)-current);
		// logMessage( LOG_INFO_LEVEL, "To read %lu = (%lu+%lu)-%lu", toread, addr, len, current );
		if ( offset+toread < SMSA_BLOCK_SIZE ) {

			// The current block does have enough to exhaust the read
			// SO, read the number of bytes left
			rlen = toread;

		} else {

			// The current block does not have enough to exhaust the read
			// SO, read until the end of the block
			rlen = SMSA_BLOCK_SIZE-offset;
		}
		// logMessage( LOG_INFO_LEVEL, "Toread %u Offset %u -> rlen %u", toread, offset, rlen );

		// Read the block (virtually)
		logMessage( LOG_INFO_LEVEL, "Reading [%u, %u bytes, left=%u] (%u/%u), "
				"copying len=%u bytes from buf[%u] into dest[%u]", addr, len, toread,
				drm, blk, rlen, offset, current-addr );

		// Add some assertions just to careful
		assert( (rlen>0) && (rlen <= SMSA_BLOCK_SIZE) );
		assert( (offset >= 0) && (offset<SMSA_BLOCK_SIZE) );
		assert( (current-addr >= 0) && (current-addr<len) );

		logMessage( LOG_INFO_LEVEL, "VREAD [%u/%u,len=%u]", drm, blk, rlen );
		logMessage( LOG_INFO_LEVEL, "MEMCPY(&dest[%u], &src[%u], %u)", current-addr, offset, rlen );

		// Increment bytes read counter
		current += rlen;
	}
#endif

	// Return successfully
	return( 0 );
}
