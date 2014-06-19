///////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa_driver.c 
//  Description   : This is the driver for the SMSA simulator.
//
//  Author        : Gabe Harms 
//  Last Modified : 10/0/9/13 
//

// Include Files

#include <stdint.h>
#include <stdlib.h>


// Project Include Files
#include <smsa_network.h>
#include <smsa_driver.h>
#include <cmpsc311_log.h>
#include <smsa_cache.h>


// Defines
#define OPCODE_BIT_WIDTH 6		//Bit widths of command to send to smsa device
#define DRUM_ID_BIT_WIDTH 4
#define RESERVED_BIT_WIDTH 14
#define BLOCK_ID_BIT_WIDTH 8
#define TOTAL_COMMAND_BIT_WIDTH 32

#define DONT_CARE 0
#define BLOCK_SIZE 256

/* DEBUG */
#define DEBUG 0				//If set to 1, more info will be displayed when errors occur

// Functional Prototypes

//
// Global data
int cache_hits;				//Two variables to check the performance of the cache
int disk_reads;

HEAD head;			//This struct defined in the head will contain the disk and block head postions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_vmount
// Description  : Mount the SMSA disk array virtual address space
//
// Inputs       : none
// Outputs      : -1 if failure or 0 if successful
int smsa_vmount( int cache_size ) {
	
	uint32_t command;   	 //holds the generated op command
	ERROR_SOURCE err = 0;	 //holds return values of function calls to checkForErrors	
	

	//generate the op command, so that we can use this 
	//command to call the smsa_operation function to mount
	//the disk. DONT_CARE is defined as 0. After function call,
	//command will have the value needed to call the smsa_operation
	//function
	err = generateOPCommand( &command, SMSA_MOUNT, DONT_CARE, DONT_CARE, DONT_CARE );	

	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Calling smsa_client_operation to mount the Disk" );
	
	
	//send the mound command with NULL set as
	//the parameter for the char* since, we will
	//not have to use it.
	if ( smsa_client_operation( command, NULL ) ) {
		logMessage ( LOG_INFO_LEVEL, "_smsa_vmount:smsa_client_operation failed when given the Mount command" );
		return 1;
	}

	
	logMessage ( LOG_INFO_LEVEL, "Disk successfully Mounted. Connection established with server" );

	
	//initialize cache
	if ( smsa_init_cache ( cache_size ) ) {
		logMessage ( LOG_INFO_LEVEL, "_smsa_vmount:Failed to succesfully initialize the cache" );
		return 1;
	}
	

	//Initialize the drum and block head positions to zero
	head.drum = 0;
	head.block = 0;	
	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Block and Drum Head Structure Initalized Drum Head To %d and Block Head To %d", head.drum, head.block );

	//restore the memory that was saved in the txt file,
	//when vunmount finished. This will load the all disk
	//and blocks with the contents of the file. If this function
	//fails, we will not return 1 here and cause the program
	//to fail, because being unable to load the memory from 
	//the file is not a catastrophic error
//	err = restoreDiskFromFile();

	
	//initialize cache performance variables
	cache_hits = 0;
	disk_reads = 0;


	//if checkForErrors finds that err is non-zero, it will return 1. 
	//see smsa_driver.h for error enum definition
	return ( checkForErrors ( err, "_vmount", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, 
						DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_vunmount
// Description  :  Unmount the SMSA disk array virtual address space
//
// Inputs       : none
// Outputs      : -1 if failure or 0 if successful

int smsa_vunmount( void )  {
	
	uint32_t command;     		 //holds the generated "unmount" command.	
	ERROR_SOURCE err = 0;		 //holds return values of function calls to checkForErrors	


	if ( DEBUG )
		printCache( cache_hits, disk_reads);		

	
	//save the contents of the memory to a file, so that
	//it can be restored when mount is called again, rather
	//than setting it to all zeros. If an error occurs in this 
	//function we will not return a 1 since it is not a catastrophic
	//error, and the virtual memory will still function properly 
	//once mount is called
//	err = saveDiskToFile();

	//generate the op command so that we can use it to call
	//the smsa_operation function to unmount the disk. Once 
	//again, DONT_CARE is defined as 0. After function call
	//command will contain the value neccessary to call
	//smsa_operation in order to unmount the disk
	err = generateOPCommand ( &command, SMSA_UNMOUNT, DONT_CARE, DONT_CARE, DONT_CARE );

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Sending UNMOUNT Command Across the Network");

	//call function with the "mount" op command,
	//and NULL set as the parameter for the char*,
	//since we will not use this char* in the mount
	//function
	if ( smsa_client_operation ( command, NULL ) ) {
		logMessage ( LOG_INFO_LEVEL, "_smsa_vunmount:Failed to send UNMOUNT command on the network");
		return 1;
	

	logMessage ( LOG_INFO_LEVEL, "Successfully Unmounted the Disk" );


	//free cache
	if ( smsa_close_cache() )
		logMessage ( LOG_INFO_LEVEL, "_smsa_vunmount:Failed to properly close cache in smsa_close_cache()" );
		return 1;
	}

	
	//if checkForErrors finds that err is non-zero, it will return 1. 
	//see smsa_driver.h for error enum definition
	return ( checkForErrors ( err, "_vumount", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, 
						DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_vread
// Description  : Read from the SMSA virtual address space
//
// Inputs       : addr - the address to read from
//                len - the number of bytes to read
//                buf - the place to put the read bytes
// Outputs      : -1 if failure or 0 if successful

int smsa_vread( SMSA_VIRTUAL_ADDRESS addr, uint32_t len, unsigned char *buf ) {
	

	//all of the following variables are used so that we know
	//where the read should take place. These variables will
	//form a type of "bounds" for our read, and help us progress
	//through various loops so that the write can be completed
	SMSA_DRUM_ID drumStart, drumEnd, currentDrum;
	SMSA_BLOCK_ID blockStart, blockEnd, currentBlock;
	uint32_t byteStart, byteEnd, upperBound, lowerBound;


	unsigned char *temp;	
	unsigned char *cacheLine;		//variable to hold the value at the current block
	
	int bufferIndex = 0;			//holds the current index of the buffer that we are reading from
	
	ERROR_SOURCE err = 0;		 	//holds return values of function calls to checkForErrors	
	

	//get the start and stop positions of the drum, block and byte
	err = getDiskBlockParameters ( addr, len, &drumStart, &blockStart, &drumEnd, &blockEnd, &byteStart, &byteEnd );

	//current drum and block allow us to move through
	//the while loop. Set them to the starting positions
	currentDrum = drumStart;
	currentBlock = blockStart;

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL,"Initialized currentDrum and currentBlock Tnitialized To drumStart %d, and blockStart %d", drumStart, blockStart );


	//since during the while loop below, the drumHead won't 
	//necessarily be set on the first run through, we must set
	//it here to ensure it is set to the correct position
	err = seekIfNeedTo ( currentDrum, currentBlock );


	//this loop continues while we have not reached the end of (addr + len)
	//which is the place where the read will extend to. The conditions will
	//stop the loop, after the last block has been read
	while( !( ( currentDrum == drumEnd && currentBlock == blockEnd+1 )
	    		|| ( currentDrum == drumEnd + 1 && currentBlock == 0 ) ) ) {


		//In order for the memory to be presevered through multiple calls of v
		//v_read/v_write, we must allocate the memory so that it remains on the stack
		//rather than vanishing when the function returns.		
		temp = (unsigned char*) malloc ( SMSA_BLOCK_SIZE * sizeof ( char ) );		

		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "Successfully Malloced [%d] Bytes of Data To temp", SMSA_BLOCK_SIZE*sizeof( char ) );
		
		//check cache first
		cacheLine = smsa_get_cache_line ( currentDrum, currentBlock );

		//Since we don't know if the entire block should be 
		//stored in buf, we need to put the entire contents
		//of the block in a temp, then we can decide which 
		//bytes to copy to bu
		if ( cacheLine == NULL ) {
		
			//Cache miss, now read disk
			err = readLowLevel ( temp ) ;

			//Now that this is the most recently used block of memory, we 
			//need to make sure that it is in the cache.
			err = smsa_put_cache_line ( currentDrum, currentBlock, temp );	
			
			//performance stats
			disk_reads++;
		}
		else {
		
			//cacheLine contains the block line we are looking for,
			//so assign this to temp for further use in teh function
			temp = cacheLine;
			
			//performance stats
			cache_hits++;
		}	

	
		//now is where we decide the specific bytes ( letters ) 
		//from the current block values that should be overwritten.
		//After this function call, lowerBound, and upperBound will be 
		//set to the proper values in order for the memcpy to work
		err = findMemCpyBounds ( drumStart, blockStart, byteStart, drumEnd, blockEnd, byteEnd, currentDrum, currentBlock, &lowerBound, &upperBound );

		//memcopy will copy all of the desired temp to buf.
		//This is all based on the lower and upper bounds which was
		//determined by the findMemCpyBounds above. The bufferIndex 
		//will increase throughout each iteration of the while loop 
		//in order to get a buffer of "len" size. How much of the 
		//temp goes into the buffer is determined by the findMemCpyBounds
		//function above 	
		memcpy ( &buf[bufferIndex], &temp[lowerBound], upperBound - lowerBound );


		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "Memcpy ( buf[%d], temp[%d], %d )", bufferIndex, lowerBound, upperBound-lowerBound );


		//now that we have copied (upperBound-lowerBound) amount of
		//bytes into buf, we must make sure that we do not overwrite
		//this data when we attempt to copy bytes from the next block.
		//That means we need to increase the position that were copying to,
		//to just after where we copied.	
		bufferIndex += upperBound - lowerBound;

		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "BufferIndex Set From [%d] to [%d]", bufferIndex-(upperBound-lowerBound), bufferIndex);

		//check for errors during the loop, and at the end. This will allow us
		//to find where the errors occur, since were checking throughout the 
		//entire process. However, we don't want to stop the program unless
		//a error was found. So, we will return 1, only if checkForErrors results
		//in one
		if ( checkForErrors ( err, "_smsa_vread", addr, len, drumStart, blockStart, currentDrum, currentBlock, drumEnd, blockEnd ) )
			return 1; 
	
		
		//increment the block
		currentBlock++;

		//if the current block is out of the bounds for the 
		//disk size (currentBlock > 256), then set the current 
		//block to zero, and start reading the next disk
		if ( currentBlock == SMSA_BLOCK_SIZE ) {
			currentDrum++;
			currentBlock = 0;
		}

		//adjust the block and drum head if neccessary	
		err = seekIfNeedTo ( currentDrum, currentBlock );	


		
	}
	
		
	
	//if checkForErrors finds that err is non-zero, it will return 1. 
	//see smsa_driver.h for error enum definition
	return ( checkForErrors ( err, "_vread", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, 
						DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) );

}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_vwrite
// Description  : Write to the SMSA virtual address space
//
// Inputs       : addr - the address to write to
//                len - the number of bytes to write
//                buf - the place to read the read from to write
// Outputs      : -1 if failure or 0 if successful

int smsa_vwrite( SMSA_VIRTUAL_ADDRESS addr, uint32_t len, unsigned char *buf )  {


	//all of the following variables are used so that we know
	//where the write should take place. These variables will
	//form a type of "bounds" for our write, and help us progress
	//through various loops so that the write can be completed
	SMSA_DRUM_ID drumStart, drumEnd, currentDrum;
	SMSA_BLOCK_ID blockStart, blockEnd, currentBlock;
	uint32_t byteStart, byteEnd, upperBound, lowerBound;
	

	unsigned char *temp;		//variable to hold the value at the current block
	unsigned char *cacheLine;
	int bufferIndex = 0;			//holds the current index of the buffer that we are reading from


	ERROR_SOURCE err = 0;		 	//holds return values of function calls to checkForErrors
       	
		
	//get the start and stop positions of the drum, block, and byte
	err = getDiskBlockParameters ( addr, len, &drumStart, &blockStart, &drumEnd, &blockEnd, &byteStart, &byteEnd );
	

	//current drum and block allow us to move through
	//the while loop. Set them to the starting positions
	currentDrum = drumStart;
	currentBlock = blockStart;
	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL,"Initialized currentDrum and currentBlock Initialized To drumStart %d, and blockStart %d", drumStart, blockStart );


	//since during the while loop below, the drumHead won't 
	//necessarily be set on the first run through, we must set
	//it here to ensure it is set to the correct position
	err = seekIfNeedTo ( currentDrum, currentBlock );	

	
	//this loop continues while we have not reached the end of (addr + len)
	//which is the place where the write will extend to. The conditions will
	//stop the loop, after the last block has been written too
	while( !( ( currentDrum == drumEnd && currentBlock == blockEnd+1 )
	    		|| ( currentDrum == drumEnd + 1 && currentBlock == 0 ) ) ) {
	
			
		//In order for the memory to be presevered through multiple calls of v
		//v_read/v_write, we must allocate the memory so that it remains on the stack
		//rather than vanishing when the function returns.		
		temp = (unsigned char*) malloc ( SMSA_BLOCK_SIZE * sizeof ( char ) );		
	
		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "Successfully Malloced [%d] Bytes of Data To temp", SMSA_BLOCK_SIZE*sizeof( char ) );
						
		//This is where we decide the specific bytes ( letters ) 
		//from the current block values that should be overwritten
		//this function sets the lowerBound and upperBound to the appropriate
		//values. 
		err = findMemCpyBounds ( drumStart, blockStart, byteStart, drumEnd, blockEnd, byteEnd, currentDrum, currentBlock, &lowerBound, &upperBound );
		

		//in order to handle a write, sometimes, we must
		//write only some of a given block. Therefore, we 
		//have to first read whats currently at the block
		//and then overwrite the part of that block that we 
		//need too using memcpy below. Don't want to waste
		//time reading if we are writing the entire block. If
		//we are writing the whole block, read will be skipped 
		if ( !( lowerBound == 0 && upperBound == SMSA_BLOCK_SIZE) ) {
			
			//check cache first	
			cacheLine = smsa_get_cache_line ( currentDrum, currentBlock );
					
			if ( cacheLine == NULL )  { //if not in cache perform read	
				
				//cache miss. read disk.
				err = readLowLevel ( temp );
				
				//performance stats
				disk_reads++;
			}
			else {			//otherwise copy the cache line into our temp 

				//Now cacheLine contains the block that we were looking for
				//so set temp equal to it
				temp = cacheLine;
		
				//performance stats	
				cache_hits++;
			}
		}
		
		//If we are writing only part of a block, then first we 
		//need to read whats at that block currently. Then we 
		//have to overwrite the intended bytes of what is currently
		//in the block. Which bytes must be overwritten is dependent
		//on findMemCpyBounds above. finally at the end, we write this 
		//new, partially modified, version of the block to the same address. 
		//If the whole block should be overwriten, the bounds will have 
		//been set to completely overwrite temp with buf.
		memcpy ( &temp[lowerBound], &buf[bufferIndex], upperBound - lowerBound );


		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "Memcpy ( temp[%d], buf[%d], %d )", bufferIndex, lowerBound, upperBound-lowerBound );
					
		//now that we have copied (upperBound-lowerBound) amount of
		//bytes into temp, we must make sure that we choose the correct
		//data to write to the block after this one. That means we need 
		//to increase the position that getting our data from to just 
		//after where we just took from.
		bufferIndex += upperBound - lowerBound;
			
		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "BufferIndex Set From [%d] to [%d]", bufferIndex-(upperBound-lowerBound), bufferIndex);

		//since the block was incremented in the previous 
		//smsa_operation (read) call, we need to set it back
		//to the desired write postion
		err = setBlockHead ( currentBlock );

		//in all scenarios we write temp back to the current block
		//location. If only part of the current block should be
		//overwritten, then temp will contain the partially
		//modified version. If all of the current block should
		//be overwritten, then temp will contain 255 characters
		//from the buffer
		err = writeLowLevel ( temp );
		
		//update the cache so that it contains our new block
		err = smsa_put_cache_line ( currentDrum, currentBlock, temp );

		//check for errors during the loop, and at the end. This will allow us
		//to find where the errors occur, since were checking throughout the 
		//entire process. However, we don't want to stop the program unless
		//a error was found. So, we will return 1, only if checkForErrors results in 
		//one
		if ( checkForErrors ( err, "_vwrite", addr, len, drumStart, blockStart, currentDrum, currentBlock, drumEnd, blockEnd ) )
			return 1; 
		
		
		//increment the block
		currentBlock++;

		//if the current block is out of the bounds for the 
		//disk size, then set the current block to zero, 
		//and start reading the next disk
		if ( currentBlock == 256 ) {
			currentDrum++;
			currentBlock = 0;
		}

		//make sure the drum and block heads are properly set
		err = seekIfNeedTo( currentDrum, currentBlock );

			
	}

	
	//if checkForErrors finds that err is non-zero, it will return 1. 
	//see smsa_driver.h for error enum definition
	return ( checkForErrors ( err, "_vmount", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, 
						DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) );

}
///////////////////////////////////////////////
//
//PRIVATES
//
//////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////
//
// Function     : getDiskBlockParameters
// Description  : Generates indexes for disks and blocks to be used by write and read
//
// Inputs       : addr - the address to write to
//                len - the number of bytes to write
//                diskStart - to hold index of disk to start at
//                blockStart - to hold index of block to start at
//                diskEnd - to hold index of disk to end at
//                blockEnd - to hold index of block to end at
// Outputs      : -1 if failure or 0 if successful
//
int getDiskBlockParameters( SMSA_VIRTUAL_ADDRESS addr, uint32_t len,
		                                SMSA_DRUM_ID *diskStart, SMSA_BLOCK_ID *blockStart,
		                                SMSA_DRUM_ID *diskEnd, SMSA_BLOCK_ID *blockEnd, 
						uint32_t *byteStart, uint32_t *byteEnd ) {
	
		
	//find diskStart
	//Only need the last 4 of the 20 total bits. So shift away
	//the other 16 that we don't need
	*diskStart = addr >> 16;

	//find blockStart
	//We only want the bits from 7-15. So first we 
	//AND away the upper 4 bits ( disk number), then
	//we shift away the other beginning 8 bits ( byte number )
	//This leaves us with the 8 bits representing the 
	//block number
	*blockStart = addr & 0xffff;
	*blockStart = *blockStart >> 8;

	//find diskEnd
	//Add together addr and len to give the ending 
	//address. Then proceed with the same procedure
	//used for diskStart
	*diskEnd = ( addr + len - 1) >> 16;

	//find blockEnd 
	//Add together addr and len to give the ending address,
	//and then proceed with procedure from blockStart
	*blockEnd = ( addr + len -1 ) & 0xffff;
	*blockEnd = ( *blockEnd ) >> 8;

	//find byteStart
	//This will be used to determine bounds of 
	//where to read/write from a given block. In 
	//order to find it, we AND away the upper 12 bits,
	//leaving us with the bottom 8 which represents the
	//byte
	*byteStart = addr & 0xff;

	//find byteEnd
	//see comments for byteStart
	*byteEnd = ( addr + len -1) & 0xff;
	


	if ( DEBUG ) {
		logMessage ( LOG_INFO_LEVEL, "drumStart [%d], blockStart [%d], byteStart [%d]", *diskStart, *blockStart, *byteStart );
		logMessage ( LOG_INFO_LEVEL, "drumEnd [%d], blockEnd [%d], byteEnd[%d]", *diskEnd, *blockEnd, *byteEnd );
	}	

	//bounds check all data, we know it cant be below 
	//zero (unsigned int ) so just check upper bound
	if ( *diskStart > 15 || *diskEnd > 15 || *blockStart > SMSA_BLOCK_SIZE
			|| *blockEnd > SMSA_BLOCK_SIZE || *byteStart > SMSA_BLOCK_SIZE
			|| *byteEnd > SMSA_BLOCK_SIZE ) {	
		logMessage ( SMSA_MAX_ERRNO, "getDiskBlockParameters Failed. disk parameter outputs out of bounds. diskStart = [%d]. blockStart = [%d]. diskEnd = [%d]. blockEnd = [%d]. byteStart = [%d]. byteEnd = [%d]", *diskStart, *blockStart, *diskEnd, *blockEnd, *byteStart, *byteEnd );
		return 2; 
	}

	return 0;  //everything went fine

}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : writeLowLevel
// Description  : This function writes a string where the drumID and blockID 
//		    are already pointing. 
//
// Inputs       : buffer - string value to be written to memory
//             
// Outputs      : -1 if failure or 0 if successful
//
//
int writeLowLevel ( unsigned char * buffer ) {

	uint32_t command;	
	ERROR_SOURCE err = 0;		 //holds return values of function calls to check for errors	


	//use this function to generate the command that will be passed to 
	//the low level smsa_operation function.
	err = generateOPCommand( &command, SMSA_DISK_WRITE, DONT_CARE, DONT_CARE, DONT_CARE );


	//now that command contains the value needed to choose a write 
	//command, call the SMSA operation function with the command and buffer as parameters.
	//After function is donebuffer will be written to the current disk and block location
	err = smsa_client_operation( command, buffer );
	
	
	logMessage ( LOG_INFO_LEVEL, "Successfully Completed Write Of [%p]", buffer );

	//Increment the position of the block head
	head.block++;
	
	//check for errors. if checkForError returns 1, then
	//there are errors, and the function will return its
	//error number defined in the header
	if ( checkForErrors ( err, "_writeLowLevel", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) )
		return 3; 

	return 0; //everything went fine

}

////////////////////////////////////////////////////////////////////////////////
////
//// Function     : readLowLevel
//// Description  : This function reads a string where the drumID and blockID 
////                are already pointing. 
////
//// Inputs       : buffer - string value set to the read contents of the memory
////             
//// Outputs      : -1 if failure or 0 if successful
////
////
int readLowLevel ( unsigned char * buffer ) {

	uint32_t command;
	ERROR_SOURCE err = 0;		 //holds return values of function calls to checkForErrors	


	//use this function to generate the command that will be passed to 
	//the low level smsa_operation function. 
        err = generateOPCommand( &command, SMSA_DISK_READ, DONT_CARE, DONT_CARE, DONT_CARE );


        //Now that command has the appropriate value to perform a read,
	//call the SMSA operation function with the command and buffer 
	//as parameters.After function is done buffer will contain the 
	//value at the location of the current disk and block
	err = smsa_client_operation( command, buffer );


	logMessage ( LOG_INFO_LEVEL, "Successfully Completed Read, buf Is Now [%p]", buffer );

	
	//Increment the position of the block head
	head.block++;
		
	//check for errors. if checkForErrors returns a 1,
	//then this function will return its assigned error number
	//defined in the header
	if ( checkForErrors ( err, "_readLowLevel", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) )
		return 4; 

	return 0; //everything went fine
}	



////////////////////////////////////////////////////////////////////////////////
//
// Function     : seekIfNeedTo
// Description  : Checks to see if the drum/block heads need to be set depending on 
//		  if this is the first seek, and on the currentDrum and currentBlock
//		  values
// 		  
//
// Inputs       : currentDrum - holds the disk number that we are currently read/writing at
//             	  currentBlock - holds the block value we are currently read/writing at
//		  initialSeek - if true, we need to set drum and block head, otherwise
//				just check to see if drumHead needs set	
// Outputs      : -1 if failure or 0 if successful
//
int seekIfNeedTo ( uint32_t currentDrum, uint32_t currentBlock ) {

	ERROR_SOURCE err = 0;		 //holds return values of function calls to checkForErrors

		
	//place both the drum and block heads in the correct position
	//we will only change the drumHead if the currentBlock has reset
	//to zero
	if ( currentDrum != head.drum) {
		setDrumHead ( currentDrum );
		head.block = 0;
	}
	if ( currentBlock != head.block ) {
		setBlockHead ( currentBlock );
	}
	
	//check for errors. if checkForError returns 1, then
	//there are errors, and the function will return its
	//error number defined in the header
	if ( checkForErrors ( err, "_seekIfNeedTo", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) )
		return 5; 
	
	return 0;	
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : setDrumHead
// Description  : Takes a drumID and sets the drum head to the specified position
// 		  using the base SMSA operation command 
//
// Inputs       : drumID - holds the disk number to set the disk head to
//             	  buffer - where read/writes are put, (NOT USED IN THIS FUNCTION YET )
// Outputs      : -1 if failure or 0 if successful
//
int setDrumHead ( uint32_t drumID ) {
	
	uint32_t command;
	unsigned char buffer[BLOCK_SIZE];
	ERROR_SOURCE err = 0;		 //holds return values of function calls to checkForErrors	


	//use the function to generate the command that will be passed to 
	//the low level smsa_operation function
	err = generateOPCommand ( &command, SMSA_SEEK_DRUM , drumID, DONT_CARE, DONT_CARE );

	//Now that command contains the proper value to perform
	// a seek to a drum,call the SMSA operation function with 
	//the command and buffer as parameters. After function is done,
	//the drum head will be moved to the value specified
	err = smsa_client_operation( command, buffer );

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Successfully Seeked to Drum [%d]", drumID );

	//Adjust the block head tracker
	head.drum = drumID;

	//check for errors. if checkForError returns 1, then
	//there are errors, and the function will return its
	//error number defined in the header
	if ( checkForErrors ( err, "_setDrumHead", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) )
		return 6; 

	return 0; //everything went fine
}

//////////////////////////////////////////////////////////////////////////////
//
// Function     : setBlockID
// Description  : Takes a blockID and sets the block head to the specified blockID 
//		  using the base SMSA operation function
//
// Inputs       : blockID - holds the block number to set the block head to 
//             
// Outputs      : -1 if failure or 0 if successful
//
//
int setBlockHead ( uint32_t blockID ) {

	uint32_t command;
 	unsigned char buffer[BLOCK_SIZE];
	ERROR_SOURCE err = 0;		 //holds return values of function calls to checkForErrors	


	//use the function to generate the command that will be passed to 
	//the low level smsa_operation function.
	err = generateOPCommand ( &command, SMSA_SEEK_BLOCK , DONT_CARE, DONT_CARE, blockID );

	//Now that command contains the proper value to seek
	//to a block,call the SMSA operation function with the 
	//command and buffer as parameters. After function is done,
	//the block head will be moved to the value specified
	err = smsa_client_operation (command, buffer );

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Successfully Seeked to Block [%d]", blockID );

	//Adjust the position of the block head 
	head.block = blockID;

	//check for errors. if checkForError returns 1, then
	//there are errors, and the function will return its
	//error number defined in the header
	if ( checkForErrors ( err, "_setBlockHead", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) )
		return 7;
 
	return 0; //everything went fine
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : generateOPCommand
// Description  : creates the deciaml number to be passed to our SMSA utility using binary
//
// Inputs       : command - will hold decimal representation of our OP
//                opCode - decimal of the opcode we wish to use
// Outputs      : -1 if failure or 0 if successful
//
int generateOPCommand( uint32_t *command, SMSA_OPCODE opcode, SMSA_DRUM_ID drumID, SMSA_RESERVED reserved, SMSA_BLOCK_ID blockID  ) {
	
	//concatenates all sections of the command, in
	//order to create on uint32_t command that can
	//be used in the smsa_operation function
	*command = ( ( opcode << (26) ) | ( drumID << (22) ) | ( reserved << (8) ) | ( blockID ) );        

	return 0;
}	


////////////////////////////////////////////////////////////////////////////////
//
// Function     : saveDiskToFile
// Description  : takes the contents of our virtual disk and writes them to a txt
//		  file during unmount, so that they can be restored during mount
//
// Inputs       : command - will hold decimal representation of our OP
//                opCode - decimal of the opcode we wish to use
// Outputs      : -1 if failure or 0 if successful
//
int saveDiskToFile ( ) {

	logMessage ( LOG_INFO_LEVEL, "Saving memory contents... " );

	int currentDisk = 0;
	int currentBlock = 0;
	unsigned char temp[SMSA_BLOCK_SIZE];

	ERROR_SOURCE err = 0;		 //holds return values of function calls to checkForErrors


	//open file
	FILE *ptr_file;
	ptr_file = fopen ( "saved_memory.txt", "w" );
	
	//make sure it was opened properly
	if ( !ptr_file ) {
		logMessage ( SMSA_MAX_ERRNO, "_vunmount:Could not load the file to save the memory to" );
		return 1;
	}


	//set drum and block head to initial states
	seekIfNeedTo ( currentDisk, currentBlock );

	while (  !( currentDisk == 16 && currentBlock == 0 ) ) {

		//adjust drum and block head appropriately
		err = seekIfNeedTo ( currentDisk, currentBlock );

		//perform read at current drum and head position
		err = readLowLevel ( temp );
	
		//copy the read block into ONE LINE of the
		//text file. this is the only way that vmount
		//will be able to recognize what is in the 
		//file.
		fprintf ( ptr_file, "%d", *temp );	
	

		//line break, since this block is done reading
		fprintf ( ptr_file, "\n" );

	
		//check for errors. if checkForError returns 1, then
		//there are errors, and the function will return its
		//error number defined in the header
		if ( checkForErrors ( err, "_saveDiskToFile", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) )
		return 9; 

		//increment block...
		currentBlock++;		

		// and check to make sure the block
		//does not exceed 255. If it does increment the disk
		//and set the block to 0		
		if ( currentBlock == 256 ) {
			currentDisk++;
			currentBlock = 0;
		}

		
		
	}
	fclose(ptr_file);
	
	logMessage ( LOG_INFO_LEVEL, "Successfully saved memory contents to file, [%d] [%d]", currentDisk, currentBlock );
	return 0;		
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : restoreDiskFromFile
// Description  : runs through a file, and restores the memory that was saved on
//		  this file to the memory.
//
// Inputs       : command - will hold decimal representation of our OP
//                opCode - decimal of the opcode we wish to use
// Outputs      : -1 if failure or 0 if successful
//
int restoreDiskFromFile () {


	logMessage ( LOG_INFO_LEVEL, "Restoring memory contents... " );

	int currentDisk = 0;
	int currentBlock = 0;
	unsigned char temp[SMSA_BLOCK_SIZE];

	ERROR_SOURCE err = 0;		 //holds return values of function calls to checkForErrors	


	//open file
	FILE *ptr_file;
	ptr_file = fopen ( "saved_memory.txt", "r" );
	
	//make sure it was opened properly
	if ( !ptr_file ) {
		logMessage ( SMSA_MAX_ERRNO, "_vmount:Could not load the file to restore memory from" );
		return 1;
	}

	//make sure drum and block head are initially set properly
	err = seekIfNeedTo ( currentDisk, currentBlock );


	while ( currentDisk != 16 ) {

		
		//adjust drum and block head appropriatly
		err = seekIfNeedTo ( currentDisk, currentBlock );


		int tempInt;
		//this conditional grabs the entire line of the text
		//file we have open, and stores it in temp. If the 
		//line is empty, fgets returns NULL, and therefore we
		//must set the temp array to all zeros. If the line 
		//is not empty, then temp contains the values we need
		//to write to the currentblock.
		fscanf ( ptr_file, "%d", &tempInt );
		*temp = tempInt;
		logMessage ( LOG_INFO_LEVEL,"temp = %d\n", *temp );	
	
		//now since the drum and block head are both set to the 
		//appropriate postion, and the temp string contains all 
		//of the contents that we want to write at this particular
		//block, we can call a write
		err = writeLowLevel ( temp );

		//check for errors. if checkForError returns 1, then
		//there are errors, and the function will return its
		//error number defined in the header
		if ( checkForErrors ( err, "_restoreDiskFromFile", DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE, DONT_CARE ) )
			return 10; 


		//increment block...
		currentBlock++;		

		// and check to make sure the block
		//does not exceed 255. If it does increment the disk
		//and set the block to 0		
		if ( currentBlock == SMSA_BLOCK_SIZE ) {
			currentDisk++;
			currentBlock = 0;
		}
	
	}	
	fclose(ptr_file);

	logMessage ( LOG_INFO_LEVEL, "Successfully restored memory contents from file" );
	return 0;
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : getMemCpyBounds
// Description  : This function will examine the start Drum/Block
//		  and end Drum/Block, and then determines what the lower and upper
//	          bounds of the memcpy function
//
// Inputs       : startDrum - drum to start read/writing
//		  startBlock - block to start read/writing
//		  endDrum - last drum to read/write from
//		  endBlock - last block to read/write from
//		  lowerBound - first byte of memory to start reading from
//		  upperBound - last byte of memory to start reading from
//        
// Outputs      : -1 if failure or 0 if successful
//
int findMemCpyBounds ( uint32_t drumStart, uint32_t blockStart, uint32_t byteStart, uint32_t drumEnd, uint32_t blockEnd, uint32_t byteEnd, uint32_t currentDrum, uint32_t currentBlock,  uint32_t *lowerBound, uint32_t *upperBound ) {
		
	if ( drumStart == drumEnd && blockStart == blockEnd ) {
		*lowerBound = byteStart;		//scenario where we are writing to one total block
		*upperBound = byteEnd+1;
	}
	else if ( currentDrum == drumStart && currentBlock == blockStart ) {
		*lowerBound = byteStart;		//scenario of writing to the first block of several at addr
		*upperBound = SMSA_BLOCK_SIZE;	
	}
	else if ( currentDrum == drumEnd && currentBlock == blockEnd ) {
		*lowerBound = 0;			//scenario of writing to the last block at addr + len
		*upperBound = byteEnd +1;
	}
	else {
		*lowerBound = 0;			//scenario of writing the whole block
		*upperBound = SMSA_BLOCK_SIZE;
	}

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Memcpy lower bound [%d], upper bound [%d]", *lowerBound, *upperBound );

	return 0;
}






////////////////////////////////////////////////////////////////////////////////
//
// Function     : checkForErrors
// Description  : based on the input, it will print out what errors have occured, and
//		  tell what function they came from
//
// Inputs       : err - number that will help the function decide which function caused the error
//        
// Outputs      : -1 if failure or 0 if successful
//
int checkForErrors ( ERROR_SOURCE err, char* currentFunction, SMSA_VIRTUAL_ADDRESS addr,  uint32_t len,  uint32_t diskStart,  
					uint32_t blockStart,  uint32_t currentDisk,  uint32_t currentBlock, 
						uint32_t diskEnd, uint32_t blockEnd) {


	//this switch handles any errors that were stored in err
	//and prints out information based on the error enum which
	//is defined in smsa_driver.h
	switch ( err ) {
		
		case 1: if ( DEBUG ) {		
				logMessage ( SMSA_MAX_ERRNO,  "smsa_operation failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );
			}
			else {
					logMessage ( SMSA_MAX_ERRNO,  "smsa_operation function failed during %s", currentFunction );	
			}
			return 1;

		case 2: if ( DEBUG ) {
				logMessage ( SMSA_MAX_ERRNO,  " getDiskBlockParameters failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );
			}
			else {
				logMessage ( SMSA_MAX_ERRNO,  "getDiskBlockParameters function failed during %s", currentFunction );
			}	
			return 1;
	
		case 3: if ( DEBUG ) {
				logMessage ( SMSA_MAX_ERRNO,  "writeLowLevel failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );
			}
			else {	
				logMessage ( SMSA_MAX_ERRNO,  "writeLowLevel function failed during %s", currentFunction );
			}	
			return 1;

		case 4: if ( DEBUG ) { 
				logMessage ( SMSA_MAX_ERRNO,  "readLowLevel failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );

			}
			else {	
				logMessage ( SMSA_MAX_ERRNO,  "readLowLevel function failed during %s", currentFunction );
			
			}	
	
		case 5: if ( DEBUG ) {	
				logMessage ( SMSA_MAX_ERRNO,  "seekIfNeedTo failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );
			}
			else {	
				logMessage ( SMSA_MAX_ERRNO,  "seekIfNeedTo function failed during %s", currentFunction );
			}	
			return 1;

		case 6: if ( DEBUG ) {	
				logMessage ( SMSA_MAX_ERRNO,  "setDrumHead failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );
			}
			else {
				logMessage ( SMSA_MAX_ERRNO,  "setDrumHead function failed during %s", currentFunction );
			}	
			return 1;

		case 7: if ( DEBUG ) {
				logMessage ( SMSA_MAX_ERRNO,  "setBlockHead failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );
			}
			else {	
				logMessage ( SMSA_MAX_ERRNO,  "setBlockHead function failed during %s", currentFunction );
			}
			return 1;

		case 8: if ( DEBUG ) {	
				logMessage ( SMSA_MAX_ERRNO,  "generateOPCommand failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );
			}
			else {	
				logMessage ( SMSA_MAX_ERRNO,  "generateOPCommand function failed during %s", currentFunction );
			
			}
			return 1;
	
		case 9: if ( DEBUG ) {	
				logMessage ( SMSA_MAX_ERRNO,  "saveDiskToFile function failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd );
			}
			else { 
				logMessage ( SMSA_MAX_ERRNO,  "saveDiskToFile function failed during %s", currentFunction );
			}	
			return 1;

		case 10: if ( DEBUG ) {	
				logMessage ( SMSA_MAX_ERRNO,  "restoreDiskFromFile function failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd ); 
			}
			else {
				logMessage ( SMSA_MAX_ERRNO,  "restoreDiskFromFile function failed during %s", currentFunction );
			}	
			return 1;

		case 11: if ( DEBUG ) {	
				logMessage ( SMSA_MAX_ERRNO,  "smsa_put_cache_line function failed during %s.\n				addr = [%d]\n				len = [%d].\n				diskStart = [%d].\n				blockStart = [%d].\n				currentDisk = [%d].\n				currentBlock = [%d].\n				diskEnd = [%d].\n				blockEnd = [%d]", currentFunction, addr, len, diskStart, blockStart, currentDisk, currentBlock, diskEnd, blockEnd ); 
			}
			else {
				logMessage ( SMSA_MAX_ERRNO,  "smsa_put_cache_line function failed during %s", currentFunction );
			}	
			return 1;

		default: return 0;

	}	

}



