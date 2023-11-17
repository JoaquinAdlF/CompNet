// Link layer protocol implementation

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
#include "link_layer.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1
#define MAX_PAYLOAD_SIZE 1000

#define FLAG 0x7E
#define ESC 0x7d
#define A_FSENDER 0x03
#define A_FRECEIVER 0x01
#define C_SET 0x03
#define C_DISC 0x0B
#define C_UA 0x07
#define C_RR(n) ((n << 7) | 0x05) // Receiver Ready to receive 
#define C_REJ(n) ((n << 7) | 0x01) // Receiver Rejects to receive
#define C_INF(n) (n << 6) // Iframes to be sent

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

// Alarm variables
volatile int alarmEnabled = FALSE;
volatile int alarmCount = 0;
int attempts = 0;
int timeout = 0;

int fd = 0; // Declare file descriptor globally

// Number of iFrame
unsigned char iFrameNumTx = 0;
unsigned char iFrameNumRx = 1;


// Define termios structures
struct termios oldtio;
struct termios newtio;

LinkLayerRole role; // Declare role globally

// Process time variables
clock_t start, end;
float cpuTotalTime = 0;

int bytesSent = 0; // Bytes sent counter
bool waitingforUA =  FALSE; // Declare if the program is waiting for UA globally

////////////////////////////////////////////////
// HELPER FUNCTIONS
////////////////////////////////////////////////

// Alarm handler function
void alarmHandler(int signal) {
    alarmEnabled = FALSE;
    alarmCount++;
    printf("Alarm attempt #%d\n", alarmCount);
    fflush(stdout);
}

// Function to sptablish conection
int establishConnection(LinkLayer connectionParameters) {
    // Open port and handle error
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return -1;

    // Handle connection error
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        return -1;
    }

    // Configure connection
    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    
    newtio.c_cc[VTIME] = 0;
    if(connectionParameters.role == LlTx)
        newtio.c_cc[VMIN] = 0;

    if(connectionParameters.role == LlRx)
        newtio.c_cc[VMIN] = 1;
  
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        return -1;
    }

    printf("New termios structure set\n");

    return 0;
}

// Function to send the supervision frame
int sendSupervisionFrame(unsigned char A, unsigned char C) {
    unsigned char FRAME[5] = {FLAG, A, C, A ^ C, FLAG};
    return write(fd, FRAME, 5);
}

// Funciton to read the ontrol byte
unsigned char readControlByte() {
    clock_t startProcess, endProcess;
    startProcess = clock();

    unsigned char byte = 0;
    unsigned char controlByte = 0;
    LinkLayerState state = START;

    while (state != STOP && (alarmEnabled == TRUE|| role == LlRx)) { 
       
        if (read(fd, &byte, 1) > 0) {
            bytesSent++;

            switch (state) {
            case START:
                if (byte == FLAG) {
                    state = FLAG_RCV;
                }
                break;
            
            case FLAG_RCV:
                if (byte == A_FSENDER) {
                    state = A_RCV;
                } else if(byte != FLAG) {
                    state = START;
                }
                break;

            case A_RCV:
                if (byte == C_RR(0) || byte == C_RR(1) || byte == C_REJ(0) || byte == C_REJ(1)) {
                    state = C_RCV;
                    controlByte = byte;
                } else if (byte != FLAG) {
                    state = START;
                } else {
                    state = FLAG_RCV;
                }
                break;

            case C_RCV:
                if (byte == (A_FSENDER ^ controlByte)) {
                    state = BCC1;
                } else if (byte != FLAG) {
                    state = START;
                } else {
                    state = FLAG_RCV;
                }
                break;

            case BCC1:
                if (byte == FLAG) {
                    state = STOP;
                } else {
                    state = START;
                }
                break;

            default:
                break;
            }


        }
    }

    endProcess = clock();
    cpuTotalTime += ((double) (endProcess - startProcess)) / (double) CLOCKS_PER_SEC;

    return controlByte;

}

// Function to read the response byte
unsigned char readresponseByte(bool waitingforUA) {
    unsigned char byte = 0;
    unsigned char controlByte = 0;
    LinkLayerState state = START;

    while (state != STOP){ 
       
        if(read(fd, &byte, 1) > 0){
            bytesSent++;

            switch (state)
            {
            case START:
                if (byte == FLAG) {
                    state = FLAG_RCV;
                }
                break;
            
            case FLAG_RCV:
                if (byte == A_FSENDER) {
                    state = A_RCV;
                } else if (byte != FLAG) {
                    state = START;
                }
                break;

            case A_RCV:
                if (byte == C_UA || byte == C_DISC) {
                    state = C_RCV;
                    controlByte = byte;
                } else if (byte != FLAG) {
                    state = START;
                } else{
                    state = FLAG_RCV;
                }
                break;

            case C_RCV:
                if (byte == (A_FSENDER ^ controlByte)) {
                    state = BCC1;
                } else if (byte != FLAG){
                    state = START;
                } else {
                    state = FLAG_RCV;
                }
                break;

            case BCC1:
                if (byte == FLAG) {
                    state = STOP;
                } else {
                    state = START;
                }
                break;

            default:
                break;
            }


        }
    }
    if(waitingforUA == FALSE){ 
        if(sendSupervisionFrame(A_FRECEIVER,C_DISC) == -1) {
            return -1;
        }  
    }
    return controlByte;

}

void ShowStatistics(){
    printf("\n--- Statistics ---\n");
    double total_time_seconds = ((double) (end - start)) / (double) CLOCKS_PER_SEC;
    printf("Time elapsed: %f\n", total_time_seconds);
    printf("Time spent sending bits: %f\n", cpuTotalTime);
    printf("Data transfer limit: %d\n", MAX_PAYLOAD_SIZE);
    printf("Number of bytes sent: %d\n", bytesSent);
    double speed = (double)(bytesSent * 8)/ total_time_seconds;
    printf("Speed: %f bps\n", speed);
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    // Start clock
    clock_t startProcess, endProcess;
    start = clock();
    startProcess = clock();

    // Start alarm handler
    (void) signal(SIGALRM, alarmHandler);

    // Stablishing connection
    if (establishConnection(connectionParameters) < 0) {
        return -1;
    }

    // Set parameters
    attempts = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    role = connectionParameters.role;

    // Initialize byte;
    unsigned char byte;

    LinkLayerState state = START;

    if (role == LlTx) {
        while ((alarmCount < attempts) && state != STOP) {
            // Enable alarm
            if (alarmEnabled == FALSE) {
                if(sendSupervisionFrame(A_FSENDER, C_SET) == -1)
                    return -1;
                alarm(timeout);
                alarmEnabled = TRUE;
            }
            // Loop to read UA
            while (alarmEnabled == TRUE && state != STOP) {
                if (read(fd, &byte, 1) > 0) {
                    bytesSent++;
                    
                    switch (state) {
                        case START:
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            break;
                        case FLAG_RCV:
                            if (byte == A_FRECEIVER) {
                                state = A_RCV;
                            } else if (byte == FLAG) {
                                state = FLAG_RCV;
                            } else {
                                state = START;
                            }
                            break;
                        case A_RCV:
                            if (byte == C_UA) {
                                state = C_RCV;
                            } else if (byte == FLAG) {
                                state = FLAG_RCV;
                            } else {
                                state = START;
                            }
                            break;
                        case C_RCV:
                            if (byte == (C_UA ^ A_FRECEIVER)) {
                                state = BCC1;
                            } else if (byte == FLAG) {
                                state = FLAG_RCV;
                            } else {
                                state = START;
                            }
                            break;
                        case BCC1:
                            if (byte == FLAG) {
                                state = STOP;
                            } else {
                                state = START;
                            }
                            break;
                        case STOP:
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        // End if attemps where exceded
        if(alarmCount == attempts && state != STOP) {
            return -1;
        }
    } else if (role == LlRx) {
        // Loop through control packet
        while (state != STOP) {
            if (read(fd, &byte, 1) > 0) {
                bytesSent++;
               
                switch (state) {
                    case START:
            
                        if (byte == FLAG) {
                            state = FLAG_RCV;
                            
                        }
                        break;
                    case FLAG_RCV:
                  
                        if (byte == A_FSENDER) {
                            state = A_RCV;
                        } else if (byte == FLAG) {
                            state = FLAG_RCV;
                        } else {
                            state = START;
                        }
                        break;
                    case A_RCV:
                    
                        if (byte == C_SET) {
                            state = C_RCV;
                        } else if (byte == FLAG) {
                            state = FLAG_RCV;
                        } else {
                            state = START;
                        }
                        break;
                    case C_RCV:
                   
                        if (byte == (C_SET ^ A_FSENDER)) {
                            state = BCC1;
                        } else if (byte == FLAG) {
                            state = FLAG_RCV;
                        } else {
                            state = START;
                        }
                        break;
                    case BCC1:
                 
                        if (byte == FLAG) {
                            state = STOP;
                        } else {
                            state = START;
                        }
                       
                        break;
                    default:
                        break;
                }
            }
        }
        // If couldn'n send UA frame
        if(sendSupervisionFrame(A_FRECEIVER, C_UA) == -1) {
            return -1;
        }
    }

    // Add up the time it took to process
    endProcess = clock();
    cpuTotalTime += (((double) (endProcess - startProcess)) / (double) CLOCKS_PER_SEC);

    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // Set up alarm
    alarmCount = 0;
    alarmEnabled = FALSE;

    // Initialize frame
    int frameSize = 6 + bufSize;
    unsigned char *frame = (unsigned char *)malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_FSENDER;
    frame[2] = C_INF(iFrameNumTx); 
    frame[3] = frame[1] ^ frame[2];
    alarmEnabled = FALSE;
    memcpy(frame + 4, buf, bufSize);
    unsigned char BCC2 = buf[0];

    // Fill frame
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }

    // Make new buffer and copy the contens of buf into it
    unsigned char* bufwithbcc = (unsigned char*)malloc(bufSize+1);
    memcpy(bufwithbcc, buf, bufSize);
    bufwithbcc[bufSize]= BCC2;

    int j = 4;

    // Set up frame
    for (int i = 0; i < bufSize + 1; i++) {
        if (bufwithbcc[i] == FLAG || bufwithbcc[i] == ESC)
        {
            frameSize= frameSize+1;
            frame = realloc(frame,frameSize);
            if(frame==NULL) {
                return -1;
            }
            frame[j++] = ESC;
            if(bufwithbcc[i] == FLAG){
                frame[j++] = 0x5e;
            }
            if(bufwithbcc[i] == ESC){
                frame[j++] = 0x5d;
            }
        }
        else {
            frame[j++] = bufwithbcc[i];
        }
    }

    frame[j++] = FLAG;

    // Initialize accept/reject protocol
    int reject = 0;
    int accept = 0;

    // Loop and retry in case of error
    while (alarmCount< attempts) {
        if(alarmEnabled==FALSE){
        
            alarm(timeout);
            reject=0;
            accept=0; 
            alarmEnabled=TRUE;

            if(write(fd, frame, frameSize) == -1){
                printf("Error writing.\n");
            }
            
        }
      
      
        while (reject==0 && accept==0 && alarmEnabled==TRUE)
        {
            unsigned char result = readControlByte();
            
            // Try again if there is no control byte
            if(result == 0) continue;
            
            // Retry if data was rejected
            else if(result == C_REJ(0) || result == C_REJ(1)){
                reject = 1;
            }

            // Set iframes if data was accepted
            else if (result == C_RR(0) || result == C_RR(1)){
                accept = 1;
                iFrameNumTx= (iFrameNumTx+1)%2;
            }
        }
        
        if(accept == 1){
            break;
        }
    }

    // Delocate frame memory
    free(frame);
    
    if(accept == 1){
        return bufSize;
    }
    else{
        return -1;
    }
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    unsigned char byte;
    char controlField;
    int data_byte_counter=0;
    clock_t startProcess, endProcess;
    startProcess = clock();
    LinkLayerState state = START;

    while (state!= STOP) {
        if(read(fd, &byte,1) > 0) {
            bytesSent++;

            switch (state) {
                case START:
                    if(byte == FLAG) {
                        state = FLAG_RCV;
                    }
                    break;

                case FLAG_RCV:
                    if(byte == A_FSENDER) {
                        state = A_RCV;
                    } else if (byte == FLAG){
                        state = FLAG_RCV;
                    } else { 
                        state = START;
                    }
                    break;

                case A_RCV:
                    if(byte == FLAG){
                        state = FLAG_RCV;
                    }
                    // Information byte
                    else if(byte == 0x00 || byte == 0x40) {
                        state = C_RCV;
                        controlField = byte;
                    }
                    // Supervision byte
                    else if (byte == 0x0B) {
                        controlField = byte;
                        state = C_RCV;
                    } else { 
                        state = START;
                    }
                    break;

                case C_RCV:                 
                    if (byte == (A_FSENDER ^ controlField)) {
                        state = BCC1;
                        break;
                    }
                    if( byte == FLAG) {
                        state = FLAG_RCV;
                    } else {
                        state = START;
                    }
                    break;

                case BCC1:
                    // End of transmission, disconnect
                    if (controlField == 0x0B) {
                        return 0;
                    }
                    // Data was received
                    if (byte == ESC) {
                        state = DATA_RECEIVED_ESC;
                    }
                    // Reading data
                    else {
                        state = READING_DATA; 
                        packet[0] = byte; 
                        data_byte_counter++;
                    }
                    break;

                case READING_DATA:
                    if (byte == ESC) {
                        state = DATA_RECEIVED_ESC;
                        break;
                    }
                    if (byte == FLAG) {
                        unsigned char bcc2 = packet[data_byte_counter-1];
                        unsigned char accumulator = 0;
                        data_byte_counter--;
                        packet[data_byte_counter] ='\0';
                        
                        // Destuff data
                        for (int i =0; i <=data_byte_counter; i++) {
                            accumulator = (accumulator ^ packet[i]);
                        }

                        // If bcc2 is correct we send RR(i)
                        if (bcc2 == accumulator) {
                            state = STOP;
                            sendSupervisionFrame(A_FSENDER, C_RR(iFrameNumRx));
                            iFrameNumRx = (iFrameNumRx + 1) % 2;
                            alarm(0);

                            endProcess = clock();
                            cpuTotalTime += ((double) (endProcess - startProcess)) / (double) CLOCKS_PER_SEC;
                            return data_byte_counter;
                        } 
                        
                        // If bcc2 was incorrect we send REJ(i)
                        else {
                            printf("Sending REJ\n");
                            sendSupervisionFrame(A_FSENDER, C_REJ(iFrameNumRx));
                            return -1;
                        }
                    } else {
                        packet[data_byte_counter++] = byte;
                    }
                    break;

                case DATA_RECEIVED_ESC:
                    state = READING_DATA;
                    if(byte == 0X5e) {
                        packet[data_byte_counter++] = FLAG;
                    }
                    if (byte == 0x5d) {
                        packet[data_byte_counter++] = ESC;
                    }
                    break;

                case STOP:
                    break;               
            }
        }
    }

    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    clock_t startProcess, endProcess;
    alarmCount = 0;
    alarmEnabled = FALSE;
    LinkLayerState state = START;
    unsigned char byte;
    (void) signal(SIGALRM,alarmHandler);

    if (role == LlTx) {

        while (state != STOP &&  (alarmCount < attempts)) {
            // Send disconnect signal
            if (alarmEnabled == FALSE) {
                sendSupervisionFrame(A_FSENDER, C_DISC);
                alarm(timeout);
                alarmEnabled = TRUE;
            }
            // Read disconnect response
            if (alarmEnabled == TRUE) {
                if (read(fd ,&byte, 1) > 0) {
                    bytesSent++;
                    switch (state) {
                        case START:
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            break;

                        case FLAG_RCV:
                            if (byte == A_FRECEIVER) {
                                state = A_RCV;
                                break;
                            } 
                            if (byte == FLAG) {
                                state = FLAG_RCV; 
                            } else {
                                state = START;
                            }
                            break;

                        case A_RCV:
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            if (byte == C_DISC) {
                                state = C_RCV;
                                alarm(0);
                                break;
                            } else {
                                state = START;
                            }
                            break;
                            
                        case C_RCV:
                            if (byte == (C_DISC^A_FRECEIVER)) {
                                state = BCC1;
                                break;
                            }
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                            } else {
                                state = START;
                            }
                            break;

                        case BCC1:
                            if (byte == FLAG) {
                                state=STOP;
                            } else {
                                state = START;
                                break;
                            }

                        case STOP:
                            sendSupervisionFrame(A_FSENDER,C_UA);

                            if (tcsetattr(fd, TCSANOW, &oldtio) == -1) {
                                perror("tcsetattr");
                                return -1;
                            }

                            close(fd);

                            return 0;

                        default:
                            break;
                    }
                }
            }
        }
    } else if (role == LlRx) {
        startProcess = clock();
        while (1) {
            // Wait for disconnection signal
            if (waitingforUA == FALSE) {
                byte = readresponseByte(waitingforUA);
                if(byte == C_DISC) {
                    waitingforUA = TRUE;
                }
            }
            
            // Read disconnection signal while alarm is active
            if (waitingforUA == TRUE) {
                byte = readresponseByte(waitingforUA);
                // If disconnect failed exit with -1
                if (byte == -1) {
                    return -1;
                }
                // If disconnect worked show statistics
                if (byte == C_UA) {
                    end = clock();
                    endProcess = clock();
                    cpuTotalTime += ((double) (endProcess - startProcess)) / (double) CLOCKS_PER_SEC;

                    if (tcsetattr(fd, TCSANOW, &oldtio) == -1) {
                        perror("tcsetattr");
                        return -1;
                    }

                    close(fd);
                    ShowStatistics();
                    return 0;
                }

            }
        }
    }
    return -1;
}
