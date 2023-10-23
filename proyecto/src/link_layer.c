// Link layer protocol implementation

#include "link_layer.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define FALSE 0
#define TRUE 1

#define FLAG_RCV 0x5E
#define A_RCV 0x03
#define ALT_A_RCV 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_NS_0 0x00
#define C_NS_1 0x02
#define C_RR_0 0x01
#define C_RR_1 0x21
#define C_REJ_0 0x05
#define C_REJ_1 0x25
#define C_DISC 0x0B

volatile int STOP = FALSE;
volatile int switchwrite_C_RCV = 0;
volatile int switchread_C_RCV = 0;
volatile int switchRR = 0;
volatile int switchreadRR = 0;

struct termios oldtio, newtio;

typedef enum{
    stateStart,
    stateFlagRCV,
    stateARCV,
    stateCRCV,
    stateBCCOK,
    stateOther,
    stateStop
} frameStates;

frameStates frameState = stateStart;

int fd, resendSize, alarmMax, alarmTime, alarmCounter = 0;
char resendStr[255] = {0}, SET[5], UA[5], DISC[5];

// Picks up alarm
void alarmPickup() {
    int reas, i;
    printf("Retrying connection in %d seconds...\n", alarmTime);

    res = write(fd, resendStr, resendSize);
    printf("%d bytes written\n", res);
    alarmCounter++;
    if (alarmCounter == alarmMax){
        printf("## WARNING: Reached max (%d) retries. ## \n Exiting...\n", alarmMax);
        exit(1);
    }
    
    alarm(alarmTime);
}

sendFrame(int fd, unsigned char C) {
    int resFrame;
    frame[0] = FLAG_RCV;    // F
    frame[1] = A_RCV;       // A
    frame[2] = C;           // C
    frame[3] = C^A_RCV;     // BCCOK
    frame[4] = FLAG_RCV;    // F
    resFrame = write(fd, frame, 5);

    if (resFrame == -1) {
        perror("write");
        return;
    }

    printf("%d bytes written\n", resFrame);
}

void sendSET(int fd) {
    printf("--- Sending SET ---\n");
    sendFrame(fd, C_SET);
}

void sendUA(int fd) {    
    printf("--- Sending UA ---\n");
    sendFrame(fd, C_UA);
}

void sendRR(int fd) {
    printf("---- Sending RR ----\n");
    char C_RCV;
    if(switchreadRR == 0)
        C_RCV = C_RR_1;
    else
        C_RCV = C_RR_0;

    sendFrame(fd, C_RCV);

    switchRR = !switchRR;

}

void sendDISC(int fd) {
    printf("--- Sending DISC ---\n");
    sendFrame(fd, C_DISC);
}

void processMessage(int fd, char messageType) {
    int res, SMFlag = 0;
    char buf[255];

    while (STOP == FALSE) {
        res = read(fd, buf, 1);

        switch (frameState) {
            case stateStart:
                if (buf[0] == FLAG_RCV)
                    frameState = stateFlagRCV;
                break;

            case stateFlagRCV:
                if (buf[0] == FLAG_RCV) {
                    frameState = stateFlagRCV;
                    SMFlag = 0;
                } else if (buf[0] == A_RCV || buf[0] == ALT_A_RCV) {
                    frameState = stateARCV;
                } else {
                    frameState = stateStart;
                    SMFlag = 0;
                }
                break;

            case stateARCV:
                if (buf[0] == FLAG_RCV)
                    frameState = stateFlagRCV;
                else if (buf[0] == messageType)
                    frameState = stateCRCV;
                else {
                    frameState = stateStart;
                    SMFlag = 0;
                }
                break;

            case stateCRCV:
                if (buf[0] == FLAG_RCV) {
                    frameState = stateFlagRCV;
                    SMFlag = 0;
                } else if (buf[0] == A_RCV^messageType || buf[0] == ALT_A_RCV^messageType) {
                    frameState = stateBCCOK;
                    if (messageType != C_SET) STOP = TRUE;
                } else {
                    frameState = stateStart;
                    SMFlag = 0;
                }
                break;

            case stateBCCOK:
                frameState = stateStart;
                break;
        }

        if (buf[0] == FLAG_RCV && SMFlag == 1)
            STOP = TRUE;

        if (buf[0] == FLAG_RCV)
            SMFlag = 1;
    }
    STOP = FALSE;
}


void readSET(int fd) {
    printf("---- Reading SET ----\n");
    processMessage(fd, C_SET);
}

void readUA(int fd) {
    printf("--- Reading UA ---\n");
    processMessage(fd, C_UA);
}

void readRR(int fd) {
    printf("---- Reading RR ----\n");
    char C_RCV;
    if(switchreadRR == 0)
        C_RCV = C_RR_1;
    else
        C_RCV = C_RR_0;
    
    processMessage(fd, C_RCV);
}

void readDISC(int fd) {
    printf("--- Reading DISC ---\n");
    processMessage(fd, C_DISC);
}

void sendTux() {
    printf("\nWork by: Joaquin Aguirre de la Fuente, FEUP, RCOM 2023/24\n\n");
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
// Opens a connection using the port parameters, returns -1 on error and 0 on success.
int llopen(LinkLayer connectionParameters)
{
    fd = open(connectionParameters.serialPort, O_RDWR | ONOCTTY);   // Open serial port
    if (fd < 0) {
        perror(connectionParameters.serialPort);
        exit(1);
    }
    if (tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
        perror("tcgetattr");
        exit(-1);
    }

    // Configuration of port settings
    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0.1; // Inter-character timer unused
    newtio.c_cc[VMIN] = 1;

    tcflush(fd, TCIOFLUSH);     // Flushes data received but not read

    sleep(1);
    if (tcsetattr(fd,TCSANOW,&newtio) == -1) {     // Set the new port settings
        perror("tcsetattr");
        exit(-1);
    }

    printf("---- New termios structure set ----\n");

    if (connectionParameters.role == LlTx) {
        (void) signal(SIGALRM, alarmPickup);
        sendSET(fd);
        for (int i = 0; i < 5; i++) {
            resendStr[i] = SET[i];;
        }
        resendSize = 5;

        alarmMax = connectionParameters.nRetransmissions;
        alarmTime = connectionParameters.timeout;

        alarm(alarmTime);
        printf("---- UA State Machine has started ----\n");
        readUA(fd);
        alarm(0);
        printf("---- UA Read OK ----\n");
        alarmCounter = 0;

        sleep(1);

        return 0;
    }

    else if (connectionParameters.role == LlRx) {
        printf("---- SET State Machine has started ----\n");

        readSET(fd);
        printf("---- SET Read OK ----\n");
        sendUA(fd);

        sleep(1);

        return 0;
    }

    return -1;

}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
// Sends data in buf with size bufSize
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    return 1;
}
