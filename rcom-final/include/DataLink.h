#ifndef _DATA_LINK_H_
#define _DATA_LINK_H_

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

#define BUF_SIZE 256
#define FALSE 0
#define TRUE 1
#define MAX_PAYLOAD_SIZE 1000
#define BER_DATAFIELD 0.00001
#define BER_HEADERFIELD 0.000001
#define BIT_FLIPPING 0
#define SHOW_STATISTICS 1


#define FLAG 0x7E
#define ESC 0x7D
#define A_FSENDER 0x03
#define A_FRECEIVER 0x01
#define C_SET 0x03
#define C_DISC 0x0B
#define C_UA 0x07
#define C_RR(n) ((n << 7) | 0x05) // Receiver Ready to receive 
#define C_REJ(n) ((n << 7) | 0x01) // Receiver Rejects to receive

#define C_INF(n) (n << 6)

typedef enum{
	
	Transmissor,
	Receptor,
	
	
}Role;

typedef enum {
	START,
	FLAG_RCV,
	A_RCV,
	C_RCV,
	BCC1,
	READING_DATA,
	DATA_RECEIVED_ESC,
	STOP
} LinkLayerState;

typedef struct{
	
	char serialPort[20];
	int baudRate; /*Velocidade de transmissão*/
    Role role;
    unsigned int timeout; /*Valor do temporizador: 1 s*/
    unsigned int numTransmissions;   
	
}LinkLayer;

// Open a connection using the "port" parameters defined in struct linkLayer.
// Return "1" on success or "-1" on error.
int llopen(LinkLayer sp_config);
// Send data in buf with size bufSize.
// Return number of chars written, or "-1" on error.
int llwrite(unsigned char *buf, int bufSize);

// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread( unsigned char *buf);

// Close previously opened connection.
// if showStatistics == TRUE, link layer should print statistics in the console on close.
// Return "1" on success or "-1" on error.
int llclose(int showStatistics);

int sendSupervisionFrame(unsigned char A, unsigned char C);

int establish_connection(const char* port, LinkLayer sp_config);

void alarmHandler(int signal);

unsigned char readControlByte();

unsigned char readresponseByte(bool waitingforUA);

void ShowStatistics();

unsigned char simulateBitError(unsigned char byte, double errorRate);

#endif
