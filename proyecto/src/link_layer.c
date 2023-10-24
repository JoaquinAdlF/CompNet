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
#include <stdbool.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define FALSE 0
#define TRUE 1
#define FRAME_MAX_SIZE 1050

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
} frameStates;

frameStates frameState = stateStart;

int fd, resendSize, alarmMax, alarmTime, alarmCounter = 0;
char resendStr[255] = {0}, SET[5], UA[5], DISC[5];

// Picks up alarm
void alarmPickup() {
    int res;
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

void sendFrame(int fd, unsigned char C) {
    int resFrame;
    char frame[5];
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
    if(switchRR == 0)
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

        if (res == -1) {
            perror("Error reading from descriptor");
            return; // exit the function in case of an error
        }

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
                } else if (buf[0] == (A_RCV^messageType) || buf[0] == (ALT_A_RCV^messageType)) {
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
    switchreadRR = !switchreadRR;
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
int llopen(LinkLayer connectionParameters) {
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);   // Open serial port
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
int llwrite(const unsigned char *buf, int bufSize) {
    if (buf == NULL || bufSize <= 0 || bufSize > 2000)
        return -1; // Invalid buffer or size
    
    int resData, auxSize = 0;

    char xor = buf[0], auxBuf[2000];
    for(int i = 1; i < bufSize; i++)
        xor = xor^buf[i];

    // Byte stuffing
    for(int i = 0; i < bufSize; i++){
        auxBuf[i] = buf[i];
    }
    
    auxSize = bufSize;

    for(int i = 0; i < auxSize; i++){
        if(auxBuf[i] == 0x5d){ // If (0x5d) occurs, it is replaced by the sequence 0x5d 0x7d
            for(int j = auxSize+1; j > i+1; j--)
                auxBuf[j] = auxBuf[j-1];
            auxBuf[i+1] = 0x7d;
            auxSize++;
        }
    }

    for(int i = 1; i < auxSize; i++){
        if(auxBuf[i] == 0x5c){ // If (0x5c) occurs, it is replaced by the sequence 0x5d 0x7c
            auxBuf[i] = 0x5d;
            for(int j = auxSize+1; j > i+1; j--)            
                auxBuf[j] = auxBuf[j-1];
            auxBuf[i+1] = 0x7c;
            auxSize++;
        }
    }

    char str[auxSize+6];

    str[0] = FLAG_RCV;          // F
    str[1] = A_RCV;             // A
    if (switchwrite_C_RCV == 0) // C
        str[2] = C_NS_0;       
    else
        str[2] = C_NS_1;
    switchwrite_C_RCV = !switchwrite_C_RCV;
    str[3] = str[2]^A_RCV;      // BCCOK

    for(int i = 0; i < auxSize; i++)
        str[i+4] = auxBuf[i];
    
    if(xor == 0x5c){
        auxSize++;
        str[auxSize+4] = 0x5d;
        str[auxSize+5] = 0x7c;
        str[auxSize+6] = FLAG_RCV;

    }
    else{
        str[auxSize+4] = xor;
        str[auxSize+5] = FLAG_RCV;
    }

    for(int i = 0; i < auxSize+6; i++){
        resendStr[i] = str[i];
    }

    resendSize = auxSize+6;

    resData = write(fd, str, auxSize+6);
    printf("%d bytes written\n", resData);

    alarm(alarmTime); 
    readRR(fd);
    printf("--- RR READ OK ! ---\n");
    alarm(0);
    alarmCounter = 0;
    printf("--- RR Checked ---\n");
    return resData;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
// Receives data in packet
int llread(unsigned char *packet) {
    int x = 0, res;
    char buf[1], str[FRAME_MAX_SIZE];
    frameState = stateStart;
    char C_RCV = switchread_C_RCV ? C_NS_1 : C_NS_0;
    
    bool destuffFlag = FALSE, SMFlag = FALSE, skip = FALSE;

    while (STOP == FALSE) {
        res = read(fd, buf, 1);
        if (res == -1) {
            perror("Error reading from descriptor");
            return -1;
        }

        // Frame synchronization logic
        switch(frameState) {
            case stateStart:
                if (buf[0] == FLAG_RCV) {
                    frameState = stateFlagRCV;
                }
                break;

            case stateFlagRCV:
                if (buf[0] == FLAG_RCV) {
                    frameState = stateFlagRCV;
                    SMFlag = false;
                } else if (buf[0] == A_RCV || buf[0] == ALT_A_RCV) {
                    frameState = stateARCV;
                } else {
                    frameState = stateStart;
                    SMFlag = false;
                }
                break;

            case stateARCV:
                if (buf[0] == FLAG_RCV) {
                    frameState = stateFlagRCV;
                } else if (buf[0] == C_RCV) {
                    frameState = stateCRCV;
                } else {
                    frameState = stateStart;
                    SMFlag = false;
                }
                break;

            case stateCRCV:
                if (buf[0] == FLAG_RCV) {
                    frameState = stateFlagRCV;
                    SMFlag = false;
                } else if (buf[0] == (A_RCV^C_RCV) || buf[0] == (ALT_A_RCV^C_RCV)) {
                    frameState = stateBCCOK;
                } else {
                    frameState = stateStart;
                    SMFlag = false;
                }
                break;

            case stateBCCOK:
                frameState = stateStart;
                break;
        }

        // Byte destuffing logic
        if (buf[0] == 0x5d) {
            destuffFlag = TRUE;
        } else if (destuffFlag) {
            if (buf[0] == 0x7c) {
                str[x-1] = 0x5c;
                skip = TRUE;
            } else if (buf[0] == 0x7d) {
                skip = TRUE;
            }
            destuffFlag = FALSE;
        }

        // Filling the buffer after destuffing
        if (!skip) {
            str[x++] = buf[0];
        } else {
            skip = FALSE;
        }

        // Buffer overflow check
        if (x >= FRAME_MAX_SIZE - 1) {
            fprintf(stderr, "Buffer overflow detected in llread.\n");
            return -1;
        }

        // Checking XOR integrity after destuffing
        if (buf[0] == FLAG_RCV && SMFlag && x > 0) {
            char xorValue = str[4];
            for (int i = 5; i < x-2; i++) {
                xorValue ^= str[i];
            }

            if (str[x-2] == xorValue) {
                STOP = TRUE;
                printf("---- Frame Read OK ----");
            } else {
                printf("XOR mismatch. Expected: 0x%02x, Received: 0x%02x\n", xorValue, str[x-2]);
                return -1;
            }
        }

        // Set the SMFlag if current flag matches
        if (buf[0] == FLAG_RCV) {
            SMFlag = TRUE;
        }
    }

    // Toggle the read control value for next frame
    switchread_C_RCV = !switchread_C_RCV;

    // Copy the destuffed data to the packet
    memcpy(packet, &str[4], x-6);

    printf("\n\n --- DESTUFFED DATA ---\n\n");

    sendRR(fd);

    return x-6;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    return 1;
}
