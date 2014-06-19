#ifndef SMSA_NETWORK_INCLUDED
#define SMSA_NETWORK_INCLUDED

////////////////////////////////////////////////////////////////////////////////
//
//  File          : smsa_network.h
//  Description   : This is the network definitions for  the SMSA simulator.
//
//  Author        : Patrick McDaniel
//  Last Modified : Fri Nov  1 11:51:40 PDT 2013
//

// Include Files

// Project Include Files

// Defines
#define SMSA_MAX_BACKLOG 5
#define SMSA_NET_HEADER_SIZE (sizeof(uint16_t)+sizeof(uint32_t)+sizeof(uint16_t))
#define SMSA_DEFAULT_IP "127.0.0.1"
#define SMSA_DEFAULT_PORT 16784

//
// Type Definitions

//
// Funtional Prototypes

int smsa_client_operation( uint32_t op, unsigned char *block );
    // This is the implementation of the client operation

int smsa_server( void );
    // This is the implementation of the server application

#endif
