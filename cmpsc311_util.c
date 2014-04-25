////////////////////////////////////////////////////////////////////////////////
//
//  File          : cmpsc311_util.c
//  Description   : This is a set of general-purpose utility functions we use
// 					for the 311 homework assignments.
//
//  Author   : Patrick McDaniel
//  Created  : Sat Sep 21 06:47:40 EDT 2013
//
//
//  Change Log:
//
//  10/11/13    Added the timer comparison function definition (PDM)

// System include files
#include <stdint.h>
#include <gcrypt.h>

// Project Include Files
#include <cmpsc311_util.h>
#include <cmpsc311_log.h>

//
// Global data

int gcrypt_initialized = 0;  // Flag indicating the library needs to be initialized
gcry_md_hd_t *hfunc = NULL;  // A pointer to the gcrypt hash structure

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : generate_md5_signature
// Description  : Generate MD5 signature from buffer
//
// Inputs       : buf - the buffer to generate the signature
//                size - the size of the buffer (in bytes)
//                sig - the signature buffer
//                sigsz - ptr to the size of the signature buffer
//                        (set to sig length when done)
// Outputs      : -1 if failure or 0 if successful

int generate_md5_signature( unsigned char *buf, uint32_t size,
		                    unsigned char *sig, uint32_t *sigsz ) {

	// Local variables
	gcry_error_t err;
	unsigned char *hvalue;

	// If the GCRYPT interface not initialized
	if ( ! gcrypt_initialized ) {

		// Initialize the library
		gcry_check_version( GCRYPT_VERSION );

		// Create the hash structure
		hfunc = malloc( sizeof(gcry_md_hd_t) );
		memset( hfunc, 0x0, sizeof(gcry_md_hd_t) );
		err = gcry_md_open( hfunc, CMPSC311_HASH_TYPE, 0 );
		if ( err != GPG_ERR_NO_ERROR  ) {
			logMessage( LOG_ERROR_LEVEL, "Unable to init hash algorithm  [%s]", gcry_strerror(err) );
			return( -1 );
		}

		// Set the initialized flag
		gcrypt_initialized = 1;
	}

	// Check the signature length
	if ( *sigsz < CMPSC311_HASH_LENGTH ) {
		logMessage( LOG_ERROR_LEVEL, "Signature buffer too short  [%d<%d]",	*sigsz, CMPSC311_HASH_LENGTH );
		return( -1 );
	}
	*sigsz = CMPSC311_HASH_LENGTH;

	// Perform the signature operation
	gcry_md_reset( *hfunc );
	gcry_md_write( *hfunc, buf, size );
	gcry_md_final( *hfunc );
	hvalue = gcry_md_read( *hfunc, 0 );

	// Check the result
	if ( hvalue == NULL ) {
		logMessage( LOG_ERROR_LEVEL, "Signature generation failed [NULL]" );
		return( -1 );
	}

	// Copy the signature bytes, return successfully
	memcpy( sig, hvalue, *sigsz );
	return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : bufToString
// Description  : Convert the buffer into a readable hex string
//
// Inputs       : buf - the buffer to make a string out of
//                blen - the length of the buffer
//                str - the place to put the string
//                slen - the length of the output string
// Outputs      : 0 if successful, -1 if failure

int bufToString( unsigned char *buf, uint32_t blen, unsigned char *str, uint32_t slen ) {

	// Variables and startup
	int i;
	char sbuf[25];
    str[0] = 0x0; // Null terminate the string

    // Now walk the bytes (up to a max 128 bytes)
    for (i=0; ((i<(int)blen)&&(i<128)); i++) {
        sprintf( sbuf, "0x%02x ", buf[i] );
        strncat( (char *)str, (char *)sbuf, (slen+1)-strlen((char *)str) );
    }

	// Return successfully
	return( 0 );
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : getRandomValue
// Description  : Using strong randomness, generate random number
//
// Inputs       : min - the minimum number
//                max - the maximum number
// Outputs      : the random value

uint32_t getRandomValue( uint32_t min, uint32_t max ) {

	// Define and random value
	uint32_t val;
	gcry_randomize( &val, sizeof(val), GCRY_STRONG_RANDOM );

	// Adjust to range
	val = (uint32_t)(val/(UINT32_MAX/(max-min+1)))+min;
	if ( val == max+1 ) val = max; // Deal with routing error
	return( val );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : compareTimes 
// Description  : Compare two timer values 
//
// Inputs       : tm1 - the first time value 
//                tm2 - the second time value
// Outputs      : <0, 0, <0 if tmr1 < tmr, tmr1==tmr2, tmr1 > tmr2 , respectively

long compareTimes( struct timeval * tm1, struct timeval * tm2 ) {

    // compute the difference in usec
    long retval = 0;
    if ( tm2->tv_usec < tm1->tv_usec ) {
	retval = (tm2->tv_sec-tm1->tv_sec-1)*1000000L;
	retval += ((tm2->tv_usec+1000000L)-tm1->tv_usec);
    } else {
	retval = (tm2->tv_sec-tm1->tv_sec)*1000000L;
	retval += (tm2->tv_usec-tm1->tv_usec);
    }
    return( retval );
}
