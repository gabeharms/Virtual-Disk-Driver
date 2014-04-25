#
# Sample Makefile

# Variables
ARCHIVE=ar
CC=gcc 
LINK=gcc
CFLAGS=-c -Wall -I. -fpic -g -std=gnu99
LINKFLAGS=-L. -g
LIBFLAGS=-shared -Wall
LINKLIBS=-lgcrypt

# Files to build

SMSA_CLIENT_OBJS=	smsa_sim.o \
			smsa_client.o \
			smsa_driver.o \
			smsa_cache.o \
			smsa.o \
			cmpsc311_log.o \
			cmpsc311_util.o

SMSA_SERVER_OBJS=	smsa_srvr.o \
			smsa_server.o \
			smsa.o \
			cmpsc311_log.o \
			cmpsc311_util.o

TARGETS=		smsasvr \
			smsaclt \
			verify

					
# Suffix rules
.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS)  -o $@ $<

# Productions

dummy : $(TARGETS) 

smsasvr : $(SMSA_SERVER_OBJS)
	$(LINK) $(LINKFLAGS) -o $@ $(SMSA_SERVER_OBJS) $(LINKLIBS) 
	
smsaclt : $(SMSA_CLIENT_OBJS)
	$(LINK) $(LINKFLAGS) -o $@ $(SMSA_CLIENT_OBJS) $(LINKLIBS) 

verify : verify.o
	$(LINK) $(LINKFLAGS) -o $@ verify.o

# Cleanup 
clean:
	rm -f $(TARGETS) $(LIBS) $(SMSA_CLIENT_OBJS) $(SMSA_SERVER_OBJS)
  
# Dependancies
