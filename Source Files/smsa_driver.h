#ifndef SMSA_DRIVER_INCLUDED
#define SMSA_DRIVER_INCLUDED

////////////////////////////////////////////////////////////////////////////////
//
//  File           : smsa_driver.h
//  Description    : This is the driver for the SMSA simulator.
//
//   Author        : Patrick McDaniel
//   Last Modified : Tue Sep 17 07:15:09 EDT 2013
//

// Include Files
#include <stdint.h>
#include <string.h>

// Project Include Files
#include <smsa.h>

//
// Type Definitions
typedef uint32_t SMSA_VIRTUAL_ADDRESS; // SMSA Driver Virtual Addresses
typedef uint32_t SMSA_OPCODE;
typedef uint32_t SMSA_RESERVED;

typedef struct {
    SMSA_DRUM_ID     drum;  // drum head position
    SMSA_BLOCK_ID    block; // block head position
} HEAD;





//error return value defintions used in checkForErrors
typedef enum {
SMSA_OPERATION			= 1,
GET_DISK_BLOCK_PARAMETERS 	= 2,
WRITE_LOW_LEVEL			= 3,
READ_LOW_LEVEL			= 4,
SEEK_IF_NEED_TO			= 5,
SET_DRUM_HEAD			= 6,
SET_BLOCK_HEAD			= 7,
GENERATE_OP_COMMAND		= 8,
SAVE_DISK_TO_FILE		= 9,
RESTORE_DISK_FROM_FILE		= 10,
SMSA_PUT_CACHE_LINE		= 11,
} ERROR_SOURCE;


typedef struct { 
	int drum;
	int block;
} diskHead; 



// Interfaces
int smsa_vmount( int cache_size );
	// Mount the SMSA disk array virtual address space

int smsa_vunmount( void );
	// Unmount the SMSA disk array virtual address space

int smsa_vread( SMSA_VIRTUAL_ADDRESS addr, uint32_t len, unsigned char *buf );
	// Read from the SMSA virtual address space

int smsa_vwrite( SMSA_VIRTUAL_ADDRESS addr, uint32_t len, unsigned char *buf );
	// Write to the SMSA virtual address space


//////////////////////////////////////////////////////////////////////////////
//private functions
//
//these are to be used only by the driver, and not to be called by the program
//////////////////////////////////////////////////////////////////////////

int getDiskBlockParameters( SMSA_VIRTUAL_ADDRESS addr, uint32_t len,
	       			SMSA_DRUM_ID *diskStart, SMSA_BLOCK_ID *blockStart,
				SMSA_DRUM_ID *diskEnd, SMSA_BLOCK_ID *blockEnd, 
				uint32_t *byteStart, uint32_t *byteEnd );
	//generates usable indexes to target disks and blocks

int writeLowLevel ( unsigned char* buffer );
	//writes to an already set drum head and block head the contents of buffer	

int readLowLevel ( unsigned char* buffer );
	//reads to an already set drum head and block head and puts it in buffer

int seekIfNeedTo ( uint32_t currentDrum, uint32_t currentBlock );
	//sets the drum and block head appropriately

int setDrumHead ( uint32_t drumID );
	//sets the drum head to the specified drumID

int setBlockHead ( uint32_t blockID );
	//sets the block head to the specified blockID

int generateOPCommand( uint32_t *command, SMSA_OPCODE opcode, SMSA_DRUM_ID drumID, 
	       			SMSA_RESERVED reserved, SMSA_BLOCK_ID blockID );
	//generates op parameter for smsa_util command

int saveDiskToFile ( );
	//saves all of the contents of the current memory to a text file

int restoreDiskFromFile ( );
	//loads the contents of a file into the disk memory


int findMemCpyBounds ( uint32_t startDrum, uint32_t startBlock, uint32_t byteStart, uint32_t currentDrum, uint32_t currentBlock, uint32_t endDrum, uint32_t endBlock, uint32_t endByte, uint32_t* lowerBound, uint32_t* upperbound );
	//finds the upper and lower bounds of the memcpy function based on start and end parameters


int checkForErrors ( ERROR_SOURCE err, char* currentFunction, SMSA_VIRTUAL_ADDRESS addr,  uint32_t len,  uint32_t diskStart,  
					uint32_t blockStart,  uint32_t currentDisk,  uint32_t currentBlock, 
					uint32_t diskEnd, uint32_t blockEnd);
	//function that checks for error, and prints out info based on those errors

	
#endif
