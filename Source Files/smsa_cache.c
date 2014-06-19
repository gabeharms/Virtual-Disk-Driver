/////////////////////////////////////////////////////////////////////////////////
//
//  File           : smsa_cache.c
//  Description    : This is the cache for the SMSA simulator.
//
//   Author        : 
//   Last Modified : 
//

// Include Files
#include <stdint.h>
#include <stdlib.h>

// Project Include Files
#include <smsa_cache.h>
#include <cmpsc311_log.h>

/* DEBUG */
#define DEBUG 0

// Global Variables
SMSA_CACHE_LINE *cache;			//This array of SMSA_CACHE_LINEs is the cache
int currentIndex;			//Very important variable that tells how full the cache is
int maxIndex;				//Determined by the lines parameter in smsa_init_cache
int misses;
int hits;

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_init_cache
// Description  : Setup the block cache
//
// Inputs       : lines - the number of cache entries to create
// Outputs      : 0 if successful test, -1 if failure

int smsa_init_cache( uint32_t lines ) {
		
	// Dynamically allocate memory using malloc. We 
	// Multiply the size of the SMSA_CACHE_LINE struct
	// by the number of lines in the cache, and then 
	// castes it as a SMSA_CACHE_LINE pointer
	cache = ( SMSA_CACHE_LINE *) calloc ( lines, sizeof ( SMSA_CACHE_LINE ) );	
	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Successfully Calloc'ed [%d] Bytes of Data to the Cache", lines*sizeof( SMSA_CACHE_LINE ) );


	// reset the "fullness" of the cache to zero.
	// this variable will increase as items are added
	// to the cache, but will not be allowed to exceed
	// the value in lines
	currentIndex = 0;
	
	// save the value of lines in a global variable so that 
	// it can be used in other functions in the cache
	maxIndex = lines;

	// initialize data that will be used for efficiency checking
	misses = 0;
	hits = 0;

	logMessage ( LOG_INFO_LEVEL, "Cache Initialized To a Size Of [%d]", maxIndex );
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_close_cache
// Description  : Clear cache and free associated memory
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int smsa_close_cache( void ) {
	
	// Now that we no longer need the cache we will
	// free it back to the operating system
	free ( cache );
	
	// Just in case "cache" is referenced again after it 
	// has been freed, we want to set it to 0, so that the
	// progam immediatly crashes and we can identify the 
	// probelem
	cache = NULL;

	logMessage ( LOG_INFO_LEVEL, "Cache Successfully Realeased" );
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_get_cache_line
// Description  : Check to see if the cache entry is available
//
// Inputs       : drm - the drum ID to look for
//                blk - the block ID to lookm for
// Outputs      : pointer to cache entry if found, NULL otherwise

unsigned char *smsa_get_cache_line( SMSA_DRUM_ID drm, SMSA_BLOCK_ID blk ) {

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Checking for Drum [%d], Block [%d] in the Cache First...", drm, blk );
	
	// Walk through entire cache and check for the 
	// proper drm and blk match. If this item exists 
	// in the cache, then we return the line member
	// of the struct.
	for ( int i = 0; i <= currentIndex; i++ ) {
		if ( cache[i].block == blk && cache[i].drum == drm ) {
			
			// If the item exists in the highest index, then it does not 
			// need to be upddated to the newest spot in the cache because it is 
			// already there. Therefore only update the position in the cache, if
			// the item is not in the newest position already.
			if ( i != currentIndex )
				justUsedAdjust ( drm, blk, cache[i].line, i ); 		
			
			hits++; 	//monitor cache performance
		
			//Due to the structure of the cache array, the highest index of the
			//cache ( currentIndex ) gets incremented after each time a line of cache
			//gets added. This means that if the cache isn't full, the currentIndex will
			//contain memory that is unallocated yet, as it is waiting to put the next line
			//into it. Therefore if the cache isn't full we return the line at the currentIndex-1.
			//But if the cache is full, then we return the line at currentIndex, as the currentIndex
			//will no longer be incremented thorughout the program	
			if ( currentIndex == maxIndex -1 ) {
				logMessage ( LOG_INFO_LEVEL, "Drum [%d], Block [%d], Found in the Cache With a Value of [%p] at Line [%d] Out of [%d] Cache Lines", cache[currentIndex].drum, cache[currentIndex].block, *cache[currentIndex].line, currentIndex, maxIndex-1);
				return cache[currentIndex].line;
			}
			else { 
				logMessage ( LOG_INFO_LEVEL, "Drum [%d], Block [%d], Found in the Cache With a Value of [%p] at Line [%d] Out of [%d] Cache Lines", cache[i].drum, cache[i].block, *cache[i].line, i, maxIndex-1);
				return cache[currentIndex-1].line;
			}
		}
	}


	
	// not found, return null. Also since cache reads are designed to be 
	// fast, we will not be calling smsa_put_cache_line here. That can be
	// done by the vread function
	logMessage ( LOG_INFO_LEVEL, "Drum [%d], Block [%d], Not Found in the Cache", drm, blk );
	misses++; 		//monitor cache performance

	
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : smsa_put_cache_line
// Description  : Put a new line into the cache
//
// Inputs       : drm - the drum ID to place
//                blk - the block ID to lplace
//                buf - the buffer to put into the cache
// Outputs      : 0 if successful, -1 otherwise

int smsa_put_cache_line( SMSA_DRUM_ID drm, SMSA_BLOCK_ID blk, unsigned char *buf ) {

	uint32_t err = 0;		//value to hold function return values


	logMessage ( LOG_INFO_LEVEL, "Writing Drum [%d], Block [%d] To the Cache...", drm, blk );

	// Before we just put the block into the cache, we want 
	// To check to  make sure it doesn't already exist in the 
	// cache, or else we could have duplicates in the cache. If 
	// it is found, just change the line member of the struct, 
	// and place in the newest spot of the cache ( highest index )
	for ( int i = 0; i <= currentIndex; i++ ) {
		if ( cache[i].block == blk && cache[i].drum == drm ) {
			if ( DEBUG )
				logMessage ( LOG_INFO_LEVEL, "Drum [%d], Block [%d], Exists in the Cache. Overwriting Now...", drm, blk );


			cache[i].line = buf;		//update the line member of the cache block	
			
			// If the item exists in the highest index, then it does not 
			// need to be upddated to the newest spot in the cache because it is 
			// already there. Therefore only update the position in the cache, if
			// the item is not in the newest position already.
			if ( i == currentIndex ) {
				if ( currentIndex < maxIndex -1 )
					currentIndex++; 
				if ( DEBUG )
					logMessage ( LOG_INFO_LEVEL, "Drum [%e], Block [%e], Is the Newest Item In the Cache. Updated Cache Index ( if the cache was not full ) Is Mow [%d]", drm, blk );
				return 0;
			}

			//update the already existent line in the cache to the newest
			//postion in the cache, (currentIndex or currentIndex-1 )
			err = justUsedAdjust ( drm, blk, cache[i].line, i );
			return 0;
		}
	}


	logMessage ( LOG_INFO_LEVEL, "Drum [%d], Block [%d], Doesn't Exist In Cache, Must Eject and Vverwrite", drm, blk );

	// Not found in the cache, so bring it into the cache 
	err = writeToCache ( drm, blk, buf );


	// Make return value recognizable by smsa_driver error handler
	if ( err > 0 )
		err = 11;


	
	//printCache(0,0);	
	return err;		
	
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : justUsedAdjust
// Description  : The contents at the given drm and block were just read/written, need to update
//		  the order of the array.
//
// Inputs       : drm - the drum ID to reorder
//                blk - the block ID to reorder
// Outputs      : 0 if successful, -1 otherwise

int justUsedAdjust ( SMSA_DRUM_ID drm, SMSA_BLOCK_ID blk, unsigned char *buf, int index ) {
	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Current Position in Cache [%d], with Drum [%d], Block = [%d]", index, currentIndex, drm, blk);	
	
	//if it is the newest item in the cache already, then we do not
	//need to update it's postion
	if ( index == currentIndex-1 && currentIndex != maxIndex-1 ) 
		return 0;
		

	// This loop overwrites every index, with the values one index
	// ahead of it. This eliminates the object data in index 0 (LRU), and 
	// makes room at the very last index for the newest, soon to be
	// written item. 
	for ( int i = index; i < currentIndex-1; i++ ) {		
		
		//if there is a duplicate of a line in the cache, then 
		//the cache has failed, and return an error
		if ( cache[i].drum == cache[i+1].drum && cache[i].block == cache[i+1].block ) {
			logMessage( LOG_INFO_LEVEL, "_justUsedAdjust: Error: identical blocks in cache at index %d. drum = %d, block = %d", i, cache[i].drum, cache[i].block );
			printCache(0,0);
			assert(!(cache[i].drum == cache[i+1].drum && cache[i].block == cache[i+1].block));	
			return 1;
		}

		//make the current line equal to the one ahead of it
		cache[i].drum = cache[i+1].drum;
		cache[i].block = cache[i+1].block;
		cache[i].used = cache[i+1].used;	
		cache[i].line = cache[i+1].line;
	}

	//After the for loop, we have two conditions. Either the cache 
	//is full, and we need to copy the second to last block to the 
	//last block, and then put our just used block in the currentIndex position.
	//Or, the cache is not full, and we just need to put our just used block
	//in the currentIndex-1 position.
	if ( currentIndex == maxIndex-1 ) {
		cache[currentIndex-1].drum = cache[currentIndex].drum;
 		cache[currentIndex-1].block = cache[currentIndex].block;  	
		cache[currentIndex-1].line = cache[currentIndex].line;

		 
		cache[currentIndex]. drum = drm;
		cache[currentIndex].block = blk;	
		gettimeofday( &cache[currentIndex].used, NULL );
		cache[currentIndex].line = buf;
	
		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "New Postion in Cache Ss [%d] Out of [%d] Lines, With Drum [%d] and Block [%d]", currentIndex, maxIndex-1, cache[currentIndex].drum, cache[currentIndex].block );
	}
	else {
		//place the item to be written at the newest point in the cache
		cache[currentIndex-1]. drum = drm;
		cache[currentIndex-1].block = blk;	
		gettimeofday( &cache[currentIndex-1].used, NULL );
		cache[currentIndex-1].line = buf;

		if ( DEBUG )
			logMessage ( LOG_INFO_LEVEL, "New Postion In Cache is [%d] Out of [%d] Lines, With Drum [%d] and Block [%d]", currentIndex-1, maxIndex-1, cache[currentIndex-1].drum, cache[currentIndex-1].block );
	}

	if ( currentIndex == 0 )
		currentIndex++;


	return 0;

}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : writeToCache
// Description  : writes the drum, block, and buf to the cache. IMPORTANT: This 
//		  function assumes that the drum and block do not exist in the
//		  the cache currently. Therefore it will be written to the end
//		  of the array no matter what. 
//
// Inputs       : drm - the drum ID to reorder
//                blk - the block ID to reorder
//		  buf - pointer to the memory the cache should hold
// Outputs      : 0 if successful, -1 otherwise

int writeToCache ( SMSA_DRUM_ID drm, SMSA_BLOCK_ID blk, unsigned char *buf ) {

	uint32_t err = 0;	 // holds function return values

	
	// If the cache is full, an item will have to be ejected from the
	// oldest index of the cache ( index 0 ). This will make room 
	// at the end of the array for our new item.
	if ( currentIndex == maxIndex-1 && cache[currentIndex].line != NULL) 
		err = evictLRU( );
	
	//since the newest spot in the cache is now available, put the memory there
	
	cache[currentIndex].drum = drm;
	cache[currentIndex].block = blk;
	gettimeofday( &cache[currentIndex].used, NULL );
	cache[currentIndex].line = buf;


	// If the array is already full, we don't want to increment the
	// currentIndex past the capacity. It will then remain at this
	// value until the program reaches completion.
	if ( currentIndex < maxIndex -1)
		currentIndex++;

	if ( currentIndex >= maxIndex ) {
		logMessage( LOG_INFO_LEVEL, "_writeToCache:Error currentIndex >= maxIndex");
		err = 1;
	}

	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Successfully Wrote Drum [%d], Block [%d] to Cache Position of [%d] Out of [%d] Cache Lines", drm, blk, currentIndex, maxIndex-1 );
	

	return err;
	
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : evictLRU
// Description  : Takes the first item in the array ( the oldest ) and overwrites
//		  it with the item in front of it. This is done for every object
//		  in the array until the last. Leaving the last object in the array
//		  available to be written to
//
// Inputs       : 
// Outputs      : 0 if successful, -1 otherwise

int evictLRU ( ) {
	
	if ( DEBUG )
		logMessage ( LOG_INFO_LEVEL, "Evicting Drum [%d], Block [%d], from Cache at First Index", cache[0].drum, cache[0].block );

	// Need to evict the oldest item (index 0), so that 
	// there is room at the end of the cache for a new item
	// which will be written in the function that calls this one
	for ( int i = 0; i < currentIndex-1; i++ ) {
		if ( cache[i].drum == cache[i+1].drum && cache[i].block == cache[i+1].block ) {
			logMessage( LOG_INFO_LEVEL, "_evictLRU: Error: identical blocks in cache at index %d, and %d. drum = %d, block = %d", i, i+1, cache[i].drum, cache[i].block );
			printCache(0,0);
			assert( !(cache[i].drum == cache[i+1].drum && cache[i].block == cache[i+1].block) );	
			return 1;
		}

		//copy the current line in cache to the line above it
		cache[i].drum = cache[i+1].drum;
		cache[i].block = cache[i+1].block;
		gettimeofday( &cache[i].used, NULL );
		cache[i].line = cache[i+1].line;	

	} 	

	//if the cache is full the write the last cache line to the 
	//second to last cache line.
	if ( currentIndex == maxIndex-1 ) {
		cache[currentIndex-1].drum = cache[currentIndex].drum;
 		cache[currentIndex-1].block = cache[currentIndex].block;  	
		cache[currentIndex-1].line = cache[currentIndex].line;
	}


	return 0;

}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : printCache
// Description  : prints contents of the cache
//
// Inputs       : cacheHits - number of cache hits seen from smsa_driver.c
//		  diskReads - number of times smsa_driver.c had to read the disk (cache miss )
// Outputs      : 0 if successful, -1 otherwise

int printCache ( int cacheHits, int diskReads) {

	
	for ( int i = 0; i <= currentIndex; i++ ) 	
		logMessage ( LOG_INFO_LEVEL, "_printCache: index %d, drm = %d, blk = %d, line = %p, last used = .%6ld", i, cache[i].drum, cache[i].block, cache[i].line, cache[i].used.tv_usec );


	logMessage( LOG_INFO_LEVEL, "Cache Performance: From Cache: Cache lines: %d. Cache lines used: %d. Cache Hits: %d. Cache Misses: %d. Total Cache Requests: %d. Percent Hit: %f. Percent Miss: %f\n\t\t\tFrom SMSA: Cache Hits: %d. Cache Misses: %d. Total Cache Requests: %d. Percent Hit: %f. Percent Miss %f", maxIndex, currentIndex, hits, misses, hits+misses, (float) hits/(hits+misses)*100,(float) misses/(hits+misses)*100, cacheHits, diskReads, cacheHits+diskReads, (float) cacheHits/(cacheHits+diskReads)*100, (float) diskReads/(cacheHits+diskReads)*100 ); 


	return 0;
}
