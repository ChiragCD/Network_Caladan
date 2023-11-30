CC = gcc
LD = gcc 
RUNTIME_LIBS = $(ROOT_PATH)/libruntime.a $(ROOT_PATH)/libnet.a \
	       $(ROOT_PATH)/libbase.a -lpthread

RUNTIME_DEPS = $(ROOT_PATH)/libruntime.a $(ROOT_PATH)/libnet.a 
LDFLAGS = -T $(ROOT_PATH)/base/base.ld


ROOT_PATH=../..

client: pi_client.c
	gcc pi_client.c -o client
server: pi_server.c
	gcc pi_server.c -o server

INC += -I$(ROOT_PATH)/inc

combined: combined.o $(RUNTIME_DEPS)
	$(LD) $(LDFLAGS) $(INC) -o $@ $@.o $(RUNTIME_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INC) -c $< -o $@
