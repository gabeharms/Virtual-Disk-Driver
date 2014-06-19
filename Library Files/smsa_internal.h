#ifndef SMSA_INTERNAL_INCLUDED
#define SMSA_INTERNAL_INCLUDED

////////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa_internal.h
//  Description   : This is the interface to the virtual disk array.
//
//   Author : Patrick McDaniel
//   Last Modified : Fri Sep 13 06:55:42 EDT 2013
//

//
// Type definitions

// SMSA disk operation structure 
typedef struct {
	SMSA_DISK_COMMAND	cmd;	// The type of operation being performed
	SMSA_DRUM_ID		did;	// This is the drum to be written to/read from
	SMSA_BLOCK_ID		bid;	// This is the address to read/write
	unsigned short		len;	// Number of bytes to be read/written
	unsigned char		*blk;	// The buffer to place read or write data
} SMSA_OPERATION; 

//
// Disk interface (internals)

// SMSA Command functions
int SMSAMountArray( void );
int SMSAUnmountArray( void );
int SMSASeekDrum( SMSA_DRUM_ID did );
int SMSASeekBlock( SMSA_BLOCK_ID blk );
int SMSAReadBlock( unsigned char *block );
int SMSAWriteBlock( unsigned char *block );
int SMSAFormatDrum( void );

// Utility functions
int SMSAStoreArray( void );
int SMSALoadArray( void );
int decode_SMSA_operation( SMSA_OPERATION *dop, uint32_t op, unsigned char *block );
uint32_t encode_SMSA_operation( SMSA_DISK_COMMAND cmd, SMSA_DRUM_ID did, SMSA_BLOCK_ID addr );
unsigned char * block_address( SMSA_DRUM_ID did, SMSA_BLOCK_ID bid );
int operation_cycle_cost( SMSA_DISK_COMMAND cmd, SMSA_DRUM_ID did, SMSA_BLOCK_ID bid );

#endif
