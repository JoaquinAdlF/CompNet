#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define BAUDRATE B38400
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

#define FLAG_RCV 0x5c
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

void alarmPickup();
void sendSET(int fd);
void readUA(int fd);
void sendUA(int fd);
void readUA(int fd);
void sendRR(int fd);
void readRR(int fd);
void sendDISC(int fd);
void readDISC(int fd);

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

typedef struct linkLayer{
    char serialPort[50];
    int role; /* defines the role of the program: 0 = Transmitter, 1 = Receiver */
    int baudRate;
    int numTries;
    int timeOut;
}linkLayer;

void alarmPickup(){ /* picks up alarm */
    int res, i;
    printf("Retrying connection in %d seconds...\n", alarmTime);
   
    res = write(fd,resendStr,resendSize);
    printf("%d bytes written\n", res);
    alarmCounter++;
    if (alarmCounter == alarmMax){
        printf("## WARNING: Reached max (%d) retries. ## \n Exiting...\n", alarmMax);
        exit(1);
    }
    
    alarm(alarmTime);
}

void sendSET(int fd){
    int resSET;
    printf("--- Sending SET ---\n");
    SET[0] = FLAG_RCV;    /*   F   */
    SET[1] = A_RCV;       /*   A   */
    SET[2] = C_SET;       /*   C   */
    SET[3] = C_SET^A_RCV; /* BCCOK */
    SET[4] = FLAG_RCV;    /*   F   */
    resSET = write(fd,SET, 5);
    printf("%d bytes written\n", resSET);
}

void sendUA(int fd){    
    int resUA;
    printf("--- Sending UA ---\n");
    UA[0] = FLAG_RCV;    /*   F   */
    UA[1] = A_RCV;       /*   A   */
    UA[2] = C_UA;        /*   C   */
    UA[3] = C_UA^A_RCV;  /* BCCOK */
    UA[4] = FLAG_RCV;    /*   F   */
    resUA = write(fd,UA,5);
    printf("%d bytes written\n", resUA);
}

void readSET(int fd){
    int res, SMFlag = 0;
    char buf[255];
    printf("--- Reading SET ---\n");

    while (STOP == FALSE) {       /* loop for input */
        res = read(fd,buf,1);     /* returns after 1 char has been input */    

        switch(frameState){
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
                
                else if (buf[0] == C_SET)
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
                else if (buf[0] == A_RCV^C_SET || buf[0] == ALT_A_RCV^C_SET)
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
        if (buf[0] == FLAG_RCV && SMFlag == 1)
            STOP = TRUE;
        if(buf[0] == FLAG_RCV)
            SMFlag = 1;
    }
    STOP = FALSE;
}

void readUA(int fd){
    int res, SMFlag = 0;
    char buf[255];
    printf("--- Reading UA ---\n");
    
    while (STOP == FALSE) {         // loop for input 
        res = read(fd,buf,1);       // returns after 1 char has been input       

        switch(frameState){

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
                
                else if (buf[0] == C_UA)
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
                else if (buf[0] == A_RCV^C_UA || buf[0] == ALT_A_RCV^C_UA){
                    frameState = stateBCCOK;
                    STOP = TRUE;
                }
                else{
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
        
        if(buf[0] == FLAG_RCV)
            SMFlag = 1;
    } 
    STOP = FALSE;
}

void sendRR(int fd){
    char RR[5];
    int resRR, i;
    printf("--- Sending RR ---\n");
    RR[0] = FLAG_RCV;    //   F   
    RR[1] = A_RCV;       //   A   
    if (switchRR == 0)   //   C   
        RR[2] = C_RR_1;       
    else
        RR[2] = C_RR_0;
    RR[3] = RR[2]^A_RCV; // BCCOK 
    RR[4] = FLAG_RCV;    //   F   
    resRR = write(fd,RR,5);

    switchRR = !switchRR;
    printf("\n%d bytes written\n", resRR);
}

void readRR(int fd){
    int res, SMFlag = 0;
    char C_RCV, buf[255];
    printf("--- Reading RR ---\n");
    
    if(switchreadRR == 0)
        C_RCV = C_RR_1;
    else
        C_RCV = C_RR_0;

    while (STOP == FALSE) {       // loop for input 
        res = read(fd,buf,1);     // returns after 1 char has been input       
        switch(frameState){

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
                else if (buf[0] == A_RCV^C_RCV || buf[0] == ALT_A_RCV^C_RCV){
                    frameState = stateBCCOK;
                    STOP = TRUE;
                }
                else{
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
        
        if(buf[0] == FLAG_RCV)
            SMFlag = 1;
        //printf("%s:%d\n", buf, res);  
    } 
    STOP = FALSE;
    switchreadRR = !switchreadRR;
}

void sendDISC(int fd){
    int resDISC;
    printf("--- Sending DISC ---\n");
    DISC[0] = FLAG_RCV;     /*   F   */
    DISC[1] = A_RCV;        /*   A   */
    DISC[2] = C_DISC;       /*   C   */
    DISC[3] = C_DISC^A_RCV; /* BCCOK */
    DISC[4] = FLAG_RCV;     /*   F   */
    resDISC = write(fd,DISC,5);
    printf("%d bytes written\n", resDISC);
}

void readDISC(int fd){
    int res, SMFlag = 0;
    char buf[255];
    printf("--- Reading DISC ---\n");

    while (STOP == FALSE) {       // loop for input 
        res = read(fd,buf,1);   // returns after 5 (1) chars have been input       
        //buf[res]=0;               // so we can printf... 
        //printf(":%s:%d", buf, res);
        switch(frameState){

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
                
                else if (buf[0] == C_DISC)
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
                else if (buf[0] == A_RCV^C_DISC || buf[0] == ALT_A_RCV^C_DISC){
                    frameState = stateBCCOK;
                    STOP = TRUE;
                }
                else{
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
        
        if(buf[0] == FLAG_RCV)
            SMFlag = 1;
    } 
    STOP = FALSE;
}

void sendTux(){
        printf("\nWork by: Carina Silva and Goncalo Cardoso, 3LEEC08, FEUP, RCOM 2022/23\n\n");
        printf("                                .:xxxxxxxx:.\n");
        printf("                             .xxxxxxxxxxxxxxxx.\n");
        printf("                            :xxxxxxxxxxxxxxxxxxx:.\n");
        printf("                           .xxxxxxxxxxxxxxxxxxxxxxx:\n");
        printf("                          :xxxxxxxxxxxxxxxxxxxxxxxxx:\n");
        printf("                          xxxxxxxxxxxxxxxxxxxxxxxxxX:\n");
        printf("                          xxx:::xxxxxxxx::::xxxxxxxxx:\n");
        printf("                         .xx:   ::xxxxx:     :xxxxxxxx\n");
        printf("                         :xx  x.  xxxx:  xx.  xxxxxxxx\n");
        printf("                         :xx xxx  xxxx: xxxx  :xxxxxxx\n");
        printf("                         'xx 'xx  xxxx:. xx'  xxxxxxxx\n");
        printf("                          xx ::::::xx:::::.   xxxxxxxx\n");
        printf("                          xx:::::.::::.:::::::xxxxxxxx\n");
        printf("                          :x'::::'::::':::::':xxxxxxxxx.\n");
        printf("                          :xx.::::::::::::'   xxxxxxxxxx\n");
        printf("                          :xx: '::::::::'     :xxxxxxxxxx.\n");
        printf("                         .xx     '::::'        'xxxxxxxxxx.\n");
        printf("                       .xxxx                     'xxxxxxxxx.\n");
        printf("                     .xxxxx:                          xxxxxxxxxx.\n");
        printf("                  .xxxxx:'                          xxxxxxxxxxx.\n");
        printf("                 .xxxxxx:::.           .       ..:::_xxxxxxxxxxx:.\n");
        printf("                .xxxxxxx''      ':::''            ''::xxxxxxxxxxxx.\n");
        printf("                xxxxxx            :                  '::xxxxxxxxxxxx\n");
        printf("               :xxxx:'            :                    'xxxxxxxxxxxx:\n");
        printf("              .xxxxx              :                     ::xxxxxxxxxxxx\n");
        printf("              xxxx:'                                    ::xxxxxxxxxxxx\n");
        printf("              xxxx               .                      ::xxxxxxxxxxxx.\n");
        printf("          .:xxxxxx               :                      ::xxxxxxxxxxxx::\n");
        printf("          xxxxxxxx               :                      ::xxxxxxxxxxxxx:\n");
        printf("          xxxxxxxx               :                      ::xxxxxxxxxxxxx:\n");
        printf("          ':xxxxxx               '                      ::xxxxxxxxxxxx:'\n");
        printf("            .:. xx:.                                   .:xxxxxxxxxxxxx'\n");
        printf("          ::::::.'xx:.            :                  .:: xxxxxxxxxxx':\n");
        printf("  .:::::::::::::::.'xxxx.                            ::::'xxxxxxxx':::.\n");
        printf("  ::::::::::::::::::.'xxxxx                          :::::.'.xx.'::::::.\n");
        printf("  ::::::::::::::::::::.'xxxx:.                       :::::::.'':::::::::\n");
        printf("  ':::::::::::::::::::::.'xx:'                     .'::::::::::::::::::::..\n");
        printf("    :::::::::::::::::::::.'xx                    .:: :::::::::::::::::::::::\n");
        printf("  .:::::::::::::::::::::::. xx               .::xxxx :::::::::::::::::::::::\n");
        printf("  :::::::::::::::::::::::::.'xxx..        .::xxxxxxx ::::::::::::::::::::'\n");
        printf("  '::::::::::::::::::::::::: xxxxxxxxxxxxxxxxxxxxxxx :::::::::::::::::'\n");
        printf("    '::::::::::::::::::::::: xxxxxxxxxxxxxxxxxxxxxxx :::::::::::::::'\n");
        printf("        ':::::::::::::::::::_xxxxxx::'''::xxxxxxxxxx '::::::::::::'\n");
        printf("             '':.::::::::::'                        `._'::::::''\n");
}

/* Opens a connection using the "port" parameters defined in struct linkLayer, returns "-1" on error and "1" on sucess */
int llopen(linkLayer connectionParameters){
    int i = 0;

    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY );     /* Open the serial port*/
    if (fd < 0) { perror(connectionParameters.serialPort); exit(-1); }
    if (tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
        perror("tcgetattr");
        exit(-1);
    }

    /* Configure new port settings*/
    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME]    = 0.1;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 1 char received */


    /*
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
    leitura do(s) pro'ximo(s) caracter(es)
    */


    tcflush(fd, TCIOFLUSH);     /* Flushes data received but not read */

    sleep(1);
    if (tcsetattr(fd,TCSANOW,&newtio) == -1) {     /* Set the new port settings*/
        perror("tcsetattr");
        exit(-1);
    }

    printf("--- New termios structure set ---\n");

    if (connectionParameters.role == 0){
        (void) signal(SIGALRM, alarmPickup); /* Installs routine that handles interrupt*/
        sendSET(fd);     // Send the SET command
        for(i = 0; i < 5; i++){
            resendStr[i] = SET[i];
        }
        resendSize = 5;

        alarmMax = connectionParameters.numTries;
        alarmTime = connectionParameters.timeOut;

        alarm(alarmTime);     /* Set an alarm to handle retries*/
        printf("--- UA State Machine has started ---\n"); 
        readUA(fd);   /* Read UA response*/
        alarm(0);     /* Disable the alarm*/
        printf("--- UA READ OK ! ---\n");
        alarmCounter = 0;
    
        sleep(1);
        /*if (tcsetattr(fd,TCSANOW,&oldtio) == -1) {     // Restore old port settings 
            perror("tcsetattr");
            exit(-1);
        }

        close(fd);     // Close the serial port 
        */
        return 0;
    }

    else if(connectionParameters.role == 1){
        printf("--- SET State Machine has started ---\n"); 

        readSET(fd);
        printf("--- SET READ OK ! ---\n");
        sendUA(fd);

        sleep(1);

        return 0;
    }
    return -1;
}

/* Sends data in buf with size bufSize*/
int llwrite(char* buf, int bufSize){

    int i, j, resData, auxSize = 0;
    for(i = 0; i < bufSize; i++){
        printf("var = 0x%02x\n",(unsigned int)(buf[0] & 0xff));
    }
    char xor = buf[0], auxBuf[2000];
    for(i = 1; i < bufSize; i++)
        xor = xor^buf[i];

    /* byte stuffing */
    for(i = 0; i < bufSize; i++){
        auxBuf[i] = buf[i];
    }
    
    auxSize = bufSize;

    for(i = 0; i < auxSize; i++){
        if(auxBuf[i] == 0x5d){ /*if (0x5d) occurs, it is replaced by the sequence 0x5d 0x7d */
            for(j = auxSize+1; j > i+1; j--)
                auxBuf[j] = auxBuf[j-1];
            auxBuf[i+1] = 0x7d;
            auxSize++;
        }
    }

    for(i = 1; i < auxSize; i++){
        if(auxBuf[i] == 0x5c){ /*if (0x5c) occurs, it is replaced by the sequence 0x5d 0x7c */
            auxBuf[i] = 0x5d;
            for(j = auxSize+1; j > i+1; j--)            
                auxBuf[j] = auxBuf[j-1];
            auxBuf[i+1] = 0x7c;
            auxSize++;
        }
    }

    char str[auxSize+6];

    str[0] = FLAG_RCV;          /*   F   */
    str[1] = A_RCV;             /*   A   */
    if (switchwrite_C_RCV == 0) /*   C   */
        str[2] = C_NS_0;       
    else
        str[2] = C_NS_1;
    switchwrite_C_RCV = !switchwrite_C_RCV;
    str[3] = str[2]^A_RCV;      /* BCCOK */

    for(j = 0; j < auxSize; j++)
        str[j+4] = auxBuf[j];
    
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

    for(j = 0; j < auxSize+6; j++){
        resendStr[j] = str[j];
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
    return 1;
}

/* Receive data in packet*/
int llread(char* packet){
    int i = 0, j, res, xor, bytes_read, SMFlag = 0, destuffFlag = 0, skip = 0;
    char aux, aux2, C_RCV, buf[1], str[1050];
    frameState = stateStart;
    if(switchread_C_RCV == 0)
        C_RCV = C_NS_0;
    else
        C_RCV = C_NS_1;

    while (STOP == FALSE){       // loop for input 
        res = read(fd,buf,1);   // returns after 5 (1) chars have been input *
        
        switch(frameState){

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
                else if (buf[0] == A_RCV^C_RCV || buf[0] == ALT_A_RCV^C_RCV)
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

        // byte destuffing 
        if (buf[0] == 0x5d)
            destuffFlag = 1;

        if (destuffFlag && buf[0] == 0x7c){
            str[i-1] = 0x5c;
            skip = 1;
        } 
        else if (destuffFlag && buf[0] == 0x7d){
            skip = 1;
        } 
        // in case it encounters an escape byte, it will just alter the first char and skip the second 
        if (!skip){
            str[i] = buf[0];
            if (i > 0)
                aux = str[i-1];
            i++;
        }
        else{
            skip = 0;
            destuffFlag = 0;
        }

        // after it destuffs all data, calculates bcc2 

        // checks if the current flag is the ending one and if bcc2 is ok 
        if (buf[0] == FLAG_RCV && SMFlag && i > 0){
                xor = str[4];
                for(j = 5; j < i - 2; j++)
                    xor = xor^str[j];
                if(aux == xor){
                    STOP = TRUE;
                    printf("\n --- FRAME READ OK! ---\n");
                }
                else{
                    printf("XOR VALUE IS: 0x%02x\n Should be: 0x%02x\n", (unsigned int)(xor & 0xff), (unsigned int)(aux & 0xff));
                    printf("\n --- BCC2 FAILED! Better luck next time ;) ---\n");
                    return -1;
                }
            
        }

        if(buf[0] == FLAG_RCV)
            SMFlag = 1;
    }
    STOP = FALSE;
    switchread_C_RCV = !switchread_C_RCV;

    for(j = 4; j < i-2; j++)
        packet[j-4] = str[j];
    
    printf("\n\n --- DESTUFFED DATA ---\n\n");
    bytes_read = i-6;

    sendRR(fd);

    return bytes_read;
}

/* Closes previously opened connection; if showStatistics==TRUE, link layer should print statistics in the console on close */
int llclose(linkLayer connectionParameters, int showStatistics){ 
    int i;
    printf("\n\n --- CLOSING CONNECTION ---\n\n");

    if(connectionParameters.role == 0){ //tx
        sendDISC(fd);
        for(i = 0; i < 5; i++)
            resendStr[i] = DISC[i];
        alarm(alarmTime);
        readDISC(fd);
        printf("--- DISC READ OK ! ---\n");
        alarm(0);
        alarmCounter = 0;
        printf("\n\n --- READY FOR CLOSURE ---\n\n");
        sendUA(fd);
        sleep(1);
        if (tcsetattr(fd,TCSANOW,&oldtio) == -1){
            perror("tcsetattr");
            exit(-1);
        }
        
        close(fd);
        sendTux();
        return 0;
    }

    if(connectionParameters.role == 1){ //rx    
        sleep(1); 

        readDISC(fd);
        printf("--- DISC READ OK ! ---\n");

        sendDISC(fd);
        for(i = 0; i < 5; i++)
            resendStr[i] = DISC[i];
        alarm(alarmTime);
        readUA(fd);
        alarm(0);
        alarmCounter = 0;

        printf("--- UA READ OK ! ---\n");

        ("\n\n --- READY FOR CLOSURE ---\n\n");
        sleep(1);
        if (tcsetattr(fd,TCSANOW,&oldtio) == -1){
            perror("tcsetattr");
            exit(-1);
        }
        
        close(fd);
        sendTux();
        return 0;
    }

    return -1;
}
