#ifndef SMSA_UNIT_TEST_INCLUDED
#define SMSA_UNIT_TEST_INCLUDED

////////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa_unittest.h
//  Description   : This is the UNIT TEST implementation of the SASA
//
//   Author : Patrick McDaniel
//   Last Modified : Sun Sep 22 16:34:32 EDT 2013
//

// Include Files

// Project Includes
#include <smsa.h>

// Defines

//
// Functional Prototypes

int smsa_unit_test( void );
	// This is the implementation of the UNIT test for the program.

int smsa_vread_unit_test( void );
	// This is the implementation of the vread UNIT test

unsigned char * test_disk_block( SMSA_DRUM_ID did, SMSA_BLOCK_ID bid, unsigned char *blk );
	// create a block for a specific drum and block ID

#endif
