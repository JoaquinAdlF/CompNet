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
#define FALSE 0
#define TRUE 1

#define FLAG_RCV 0x5E
#define A_RCV 0x03
#define ALT_A_RCV 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_NS_0 0x00
#define C_NS_1 0x40
#define C_RR_0 0x05
#define C_RR_1 0x85
#define C_REJ_0 0x01
#define C_REJ_1 0x81
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
        C_RCV = C_RR_0;
    else
        C_RCV = C_RR_1;

    sendFrame(fd, C_RCV);

    switchRR = !switchRR;

}

void sendREJ(int fd) {
    printf("---- Sending RR ----\n");
    char C_RCV;
    if(switchRR == 0)
        C_RCV = C_REJ_0;
    else
        C_RCV = C_REJ_1;

    sendFrame(fd, C_RCV);

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

int readRR(int fd) {
    printf("---- Reading RR ----\n");
    char C_RCV, C_REJ;
    if(switchreadRR == 0) {
        C_RCV = C_RR_0;
        C_REJ = C_REJ_0;
}
    else {
        C_RCV = C_RR_1;
        C_REJ = C_REJ_1;
    }

    int res, SMFlag = 0;
    char buf[255];

    while (STOP == FALSE) {
        res = read(fd, buf, 1);

        if (res == -1) {
            perror("Error reading from descriptor");
            return -1; // exit the function in case of an error
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
                else if (buf[0] == C_REJ) {
                    return 0;
                } else if (buf[0] == C_RCV)
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
                } else if (buf[0] == (A_RCV^C_REJ) || buf[0] == (ALT_A_RCV^C_REJ)) {
                    return 0;
                } else if (buf[0] == (A_RCV^C_RCV) || buf[0] == (ALT_A_RCV^C_RCV)) {
                    frameState = stateBCCOK;
                    if (C_RCV != C_SET) STOP = TRUE;
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
    switchreadRR = !switchreadRR;
    return 1;
}

void readDISC(int fd) {
    printf("--- Reading DISC ---\n");
    processMessage(fd, C_DISC);
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
    if (tcgetattr(fd,&oldtio) == -1) { // Save current port settings
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

    // VIME and VIM must be modified so that the alarm works when reading characters

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
        if(auxBuf[i] == FLAG_RCV){ // If (FLAG_RCV) occurs, it is replaced by the sequence 0x5d 0x7c
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
    
    if(xor == FLAG_RCV){
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

    int acceptRR, tries = 3;
    // Handle REJ
    do {
        resData = write(fd, str, auxSize+6);
        printf("%d bytes written\n", resData);

        alarm(alarmTime); 
        acceptRR = readRR(fd);

        if (acceptRR == 0 && tries > 0) {
            tries--;
            printf("---- REJ Read. Retrying transfer ----\n");
            alarm(0);
        } else if (acceptRR == 0 && tries == 0) {
            printf("---- Couldn't send frame correctly ----\n");
            return -1;
        }
        

    } while (acceptRR == 0);
    printf("--- RR READ OK ! ---\n");
    alarm(0);
    alarmCounter = 0;
    printf("--- RR Checked ---\n");
    return resendSize;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
// Receives data in packet
int llread(unsigned char *packet) {
    int x = 0, res, xor, bytes_read, SMFlag = 0, destuffFlag = 0, skip = 0;
    char aux, C_RCV, buf[1], str[1050];
    frameState = stateStart;

    if (switchread_C_RCV == 0)
        C_RCV = C_NS_0;
    else
        C_RCV = C_NS_1;
    
    while (STOP == FALSE) {         // Loop for input
        res = read (fd, buf, 1);    // Returns after 5 chars have been input

        if (res == -1) {
            perror("Error reading from descriptor");
            return -1; // exit the function in case of an error
        }

        switch(frameState) {
            case stateStart:
                if (buf[0] == FLAG_RCV)
                    frameState = stateFlagRCV;
                break;
            
            case stateFlagRCV:
                if (buf[0] == FLAG_RCV){
                    frameState = stateFlagRCV;
                    SMFlag = 0;
                }
                else if (buf[0] == A_RCV || buf[0] == ALT_A_RCV)
                    frameState = stateARCV;
                
                else{
                    frameState = stateStart; 
                    SMFlag = 0;
                }
                break;

            case stateARCV:
                if (buf[0] == FLAG_RCV)
                    frameState = stateFlagRCV;
                
                else if (buf[0] == C_RCV)
                    frameState = stateCRCV;
                
                else{
                    frameState = stateStart; 
                    SMFlag = 0;
                }
                break;  

            case stateCRCV:
                if (buf[0] == FLAG_RCV){
                    frameState = stateFlagRCV;
                    SMFlag = 0;
                }
                else if (buf[0] == (A_RCV^C_RCV) || buf[0] == (ALT_A_RCV^C_RCV))
                    frameState = stateBCCOK;
                                
                else{
                    frameState = stateStart;
                    SMFlag = 0;
                }
                break;
            
            case stateBCCOK:
                frameState = stateStart;
                break;
        }

        // Byte destuffing
        if (buf[0] == 0x5d)
            destuffFlag = 1;
        
        if (destuffFlag && buf[0] == 0x7c) {
            str[x-1] = FLAG_RCV;
            skip = 1;
        }
        else if (destuffFlag && buf[0] == 0x7d) {
            skip = 1;
        }

        // If the program encounters an escape byte it will alter the first char and skip

        if (!skip) {
            str[x] = buf[0];
            if (x > 0)
                aux = str[x-1];
            x++;
        }
        else {
            skip = 0;
            destuffFlag = 0;
        }

        if (x >= sizeof(str) - 1) {
            fprintf(stderr, "Buffer overflow detected in llread.\n");
            return -1;
        }

        // After destuffing the data it calculates bcc2

        // Checks if the current flag is the final one and if bcc2 is ok
        if (buf[0] == FLAG_RCV && SMFlag && x > 0) {
            xor = str[4];

            for (int i = 5; i < x-2; i++)
                xor = xor^str[i];
            if (aux == xor) {
                STOP = TRUE;
                printf("---- Frame Read OK ----");
            }
            else {      // Error in XOR value, sends REJ to retry
                printf("XOR value is: 0x%02x\n Should be: 0x%02x\n", (unsigned int)(xor & 0xff), (unsigned int)(aux & 0xff));
                printf("\n---- BCC2 failed! Sending REJ to retry ----\n");
                sendREJ(fd);
                return -2;
            }
        }

        if (buf[0] == FLAG_RCV)
            SMFlag = 1;

    }

    STOP = FALSE;
    switchread_C_RCV = !switchread_C_RCV;

    for (int i = 4; i < x-2; i++)
        packet[i-4] = str[i];

    printf("\n\n --- DESTUFFED DATA ---\n\n");
    bytes_read = x-6;

    sendRR(fd);

    return bytes_read;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    printf("\n\n---- Closing connection ----\n\n");

    if (showStatistics == 1 ){
        sendDISC(fd);

        for(int i = 0; i < 5; i++) {
            resendStr[i] = DISC[i];
        }

        alarm(alarmTime);
        readDISC(fd);
        printf("---- DISC read ok ----\n");
        alarm(0);
        alarmCounter = 0;

        sendUA(fd);
        sleep(1);

        printf("---- Ready to close connection ----\n");

        if (tcsetattr(fd,TCSANOW,&oldtio) == -1){
            perror("tcsetattr");
            exit(-1);
        }

        close(fd);
    }

    else {
        sleep(1);
        alarm(alarmTime);
        readDISC(fd);
        printf("---- DISC read ok ----\n");
        alarm(0);
        alarmCounter = 0;

        sendDISC(fd);
        for(int i = 0; i < 5; i++)
            resendStr[i] = DISC[i];
        
        alarm(alarmTime);
        readUA(fd);
        printf("---- UA read ok ----\n");
        alarm(0);
        alarmCounter = 0;

        printf("---- Ready to close connection ----\n");
        sleep(1);
        if (tcsetattr(fd,TCSANOW,&oldtio) == -1){
            perror("tcsetattr");
            exit(-1);
        }

        close(fd);
    }

    printf("\nWork by: Joaquin Aguirre de la Fuente, FEUP, RCOM 2023/24\n\n");
    return 0;
}
