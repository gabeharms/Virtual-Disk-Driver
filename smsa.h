#ifndef SMSA_INCLUDED
#define SMSA_INCLUDED

////////////////////////////////////////////////////////////////////////////////
//
//  File           : smsa.h
//  Description    : This is the interface to the virtual disk array.
//
//   Author        : Patrick McDaniel
//   Last Modified : Fri Sep 13 03:47:15 PDT 2013
//

//
// Include Files
#include <stdint.h>

// Defines
#define SMSA_DISK_ARRAY_SIZE	16
#define SMSA_DISK_SIZE			65536
#define SMSA_BLOCK_SIZE			256
#define SMSA_MAX_BLOCK_ID		(SMSA_DISK_SIZE/SMSA_BLOCK_SIZE)
#define SMSA_DISK_FILE 			"smsa_data.dat"

// Workload related defines
#define MAX_SMSA_VIRTUAL_ADDRESS (SMSA_DISK_ARRAY_SIZE*SMSA_DISK_SIZE)
#define SMSA_WORKLOAD_READ	"READ"
#define SMSA_WORKLOAD_WRITE	"WRITE"
#define SMSA_WORKLOAD_MOUNT	"MOUNT"
#define SMSA_WORKLOAD_UNMOUNT	"UNMOUNT"
#define SMSA_WORKLOAD_SIGNALL	"SIGNALL"
#define SMSA_MAXIMUM_RDWR_SIZE	1024

// Extracting op code definitions
#define SMSA_OPCODE(op) (op >> 26)
#define SMSA_DRUMID(op) ((op >> 22)&0xf)
#define SMSA_BLOCKID(op) ((op & 0xff)

// Type definitions

// The drum identifier (should be 0..15)
typedef unsigned char SMSA_DRUM_ID;

// The drum address 
typedef unsigned short SMSA_BLOCK_ID;

// The operations the disk can perform
typedef enum {
	SMSA_MOUNT		    = 0,  // Mount the disk array
	SMSA_UNMOUNT		= 1,  // Unmount the disk array
	SMSA_SEEK_DRUM		= 2,  // See to a new drum
	SMSA_SEEK_BLOCK		= 3,  // Seek to a disk address in the current drum
	SMSA_DISK_READ 		= 4,  // Read from the disk
	SMSA_DISK_WRITE		= 5,  // Write to the disk
	SMSA_GET_STATE		= 6,  // Get the current disk state (UNIMPLEMENTED)
	SMSA_FORMAT_DRUM	= 7,  // Format the current drum (zeros)
	SMSA_BLOCK_SIGN		= 8,  // Generate a signature for a block (and output to log)
	SMSA_MAX_COMMAND	= 9,  // The largest value of a command (+1)
} SMSA_DISK_COMMAND;

// These are the disk error levels
typedef enum {
	SMSA_NO_ERROR 			= 0,	// No error has occurred
	SMSA_UNMOUNTED_DISK		= 1,	// Operation attempted on unmounted disk
	SMSA_ILLEGAL_DRUM		= 2,	// Operating with bad/illegal drum
	SMSA_DISK_CACHELOAD_FAIL	= 3,	// Unable to load array cache file
	SMSA_DISK_CACHEWRITE_FAIL	= 4,	// Unable to write array cache file
	SMSA_BAD_OPCODE			= 5,	// Bad command data
	SMSA_BAD_DRUM_ID		= 6,	// Bad drum id
	SMSA_BAD_BLOCK_ID		= 7,	// Bad block ID
	SMSA_BAD_READ			= 8,	// Performed an illegal read
	SMSA_BAD_WRITE			= 9,	// Performed an illegal write
	SMSA_SIG_FAIL			= 10,	// Signature generation failed
	SMSA_NET_ERROR			= 11,   // Network failure error
	SMSA_MAX_ERRNO			= 12	// The highest error level (not an error)
} SMSA_ERROR_LEVEL;

//
// Global data
extern SMSA_ERROR_LEVEL smsa_error_number;
//
// Disk interface

int smsa_operation( uint32_t op, unsigned char *block );
	// This is the (*only*) interface to the disk array

int SMSABlockSign( SMSA_DRUM_ID drum, SMSA_BLOCK_ID block );
	// Generate a signature for a particular block

// 
// Utility Functions

unsigned long smsa_get_cycle_count( void );
	// Return the cycle count

const char * smsa_error_string( int eno );
	// This returns a constant string detailing the meaning of an SMSA error

#endif
