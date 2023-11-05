#include "DataLink.h"

LinkLayerState state = START;

volatile int alarmEnabled = FALSE;
volatile int alarmCount = 0;
int fd=0;
unsigned attempts= 0;
unsigned char info_frame_number_transmitter=0;
unsigned char info_frame_number_receiver=1;
unsigned timeout_=0;
unsigned baudrate=0;
struct termios oldtio;
struct termios newtio;
Role role;
clock_t start,end;
double total_time=0;
int bytes_sent=0;
extern double t_prop;
bool waitingforUA=false;
bool lastbyte=false;
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    
    printf("Alarm #%d\n", alarmCount);
    fflush(stdout);
    
}

int establish_connection(const char* port, LinkLayer sp_config){
    fd = open(port, O_RDWR | O_NOCTTY);

    if (fd< 0)
    {
        return -1;
    }
    
    struct termios oldtio;
    struct termios newtio;



    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        return -1;
    }
    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = sp_config.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    if(sp_config.role==Transmissor){
    newtio.c_cc[VMIN] = 0;// Not blocking the read at all
    }
    if(sp_config.role==Receptor){
    newtio.c_cc[VMIN] = 1;
    }   
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }
    printf("New termios structure set\n");

    return 0;
}

int sendSupervisionFrame(unsigned char A, unsigned char C){
    unsigned char FRAME[5] = {FLAG, A, C, A ^ C, FLAG};
    return write(fd, FRAME, 5);
}

int llopen(LinkLayer sp_config) {
    srand(time(NULL));
    clock_t start_bits, end_bits;
    double cpu_time_used;
    start = clock();
    start_bits = clock();
    srand(NULL);
    (void) signal(SIGALRM, alarmHandler);
    int check_connection = establish_connection(sp_config.serialPort, sp_config);
    if (check_connection < 0) {
        return -1;
    }
    attempts = sp_config.numTransmissions;
    timeout_ = sp_config.timeout;
    role= sp_config.role;
    unsigned char byte;
    if (sp_config.role == Transmissor) {
        while ((alarmCount < attempts) && state != STOP) {
            if (alarmEnabled == FALSE) {
                if(sendSupervisionFrame(A_FSENDER, C_SET)==-1){return -1;};
                alarm(timeout_);
                alarmEnabled = TRUE;
            }
            while (alarmEnabled == TRUE && state != STOP) {
                if (read(fd, &byte, 1) > 0) {
                    bytes_sent++;
                    
                    
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
        if(alarmCount==attempts && state!=STOP){return -1;}
    } else if (sp_config.role == Receptor) {
       
        while (state != STOP) {
            if (read(fd, &byte, 1) > 0) {
                bytes_sent++;
               
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
                            printf("Validated it all");
                        } else {
                            printf("BCC1");
                            state = START;
                        }
                       
                        break;
                    default:
                        break;
                }
            }
        }
        if(sendSupervisionFrame(A_FRECEIVER, C_UA)==-1){return -1;};
    }

    end_bits = clock();
    
    total_time += (((double) (end_bits - start_bits)) / (double) CLOCKS_PER_SEC);

    return 0;
}


int llwrite(unsigned char *buf, int bufSize){
    alarmCount=0;
    alarmEnabled=FALSE;
    int frameSize = 6 + bufSize;
    unsigned char *frame = (unsigned char *)malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_FSENDER;
    frame[2] = C_INF(info_frame_number_transmitter); 
    frame[3] = frame[1] ^ frame[2];
    alarmEnabled=FALSE;
    memcpy(frame + 4, buf, bufSize);
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++)
    {
        BCC2 ^= buf[i];
        

    }

    unsigned char* bufwithbcc=(unsigned char*)malloc(bufSize+1);
    memcpy(bufwithbcc, buf, bufSize);
    bufwithbcc[bufSize]= BCC2;
    int j = 4;
    for (int i = 0; i < bufSize+1; i++)
    {
        if (bufwithbcc[i] == FLAG || bufwithbcc[i] == ESC)
        {
            frameSize= frameSize+1;
            frame = realloc(frame,frameSize);
            if(frame==NULL){return -1;}
            frame[j++] = ESC;
            if(bufwithbcc[i] == FLAG){
                
                frame[j++]=0x5E;
            }
            if(bufwithbcc[i] == ESC){
                
                frame[j++]=0x5D;
                
            }
        }
        else{
       frame[j++] = bufwithbcc[i];
        }
    }

    frame[j++] = FLAG;
    int reject = 0;
    int accept = 0;
    while (alarmCount< attempts)
    {
        if(alarmEnabled==FALSE){
        
        alarm(timeout_);
        reject=0;
        accept=0; 
        alarmEnabled=TRUE;

            if(write(fd, frame, frameSize) == -1){
                printf("Error writing.\n");
            }
            
        }
      
      
        while (reject==0 && accept==0 && alarmEnabled==TRUE)
        {
            /*
            if(write(fd, frame, frameSize) == -1){
                printf("Error writing.\n");
            }
            */

            unsigned char result = readControlByte();
            //printf("Result: %x\n", result);
            if(result==0) continue;
            else if(result==C_REJ(0) || result==C_REJ(1)){
                reject=1;
            }
            else if (result==C_RR(0) || result==C_RR(1)){
                accept=1;
                info_frame_number_transmitter= (info_frame_number_transmitter+1)%2;
                // não reseta o número de transmissões
            }
        }
        
        if(accept==1){
            break;
        }
    }
    free(frame);
    if(accept==1){
        
        return bufSize;
    }
    else{
        return -1;
    }
}
int llread(unsigned char *buf){
    printf("Reached llread! \n");
    unsigned char byte;
    char control_field;
    int data_byte_counter=0;
    clock_t start_bits, end_bits;
    start_bits = clock();
    LinkLayerState state = START;
    while (state!= STOP){
        if(read(fd, &byte,1) >0){
            bytes_sent++;

        if(BIT_FLIPPING){
        if(!(state==BCC1 || state== READING_DATA || state==DATA_RECEIVED_ESC)){
        byte = simulateBitError(byte, BER_HEADERFIELD);
        }else if (state==READING_DATA && byte==FLAG)
        {
            byte = simulateBitError(byte, BER_HEADERFIELD);
        }
        else{
            byte = simulateBitError(byte, BER_DATAFIELD);
        }
        }
        
        switch (state){
            
            case START:
                if(byte== FLAG){
                    state= FLAG_RCV;
                }
                break; 
            case FLAG_RCV:
                if(byte==A_FSENDER){
                    state = A_RCV;
                }else if(byte== FLAG){
                    state= FLAG_RCV;
                }
                else  { state= START;}
                break;

            case A_RCV:
                if(byte== FLAG){
                    state= FLAG_RCV;
                }
                //trama de informação
                else if(byte== 0x00 || byte == 0x40) {
                    state= C_RCV;
                    control_field= byte;
                    
                }
                //trama de supervisão
                
                else if (byte== 0x0B){
                    control_field=byte;
                    state=C_RCV;
                   
                }
                
                else { state=START; }
                
                break;
            case C_RCV:                 
                if (byte== (A_FSENDER ^ control_field) ){
                    state= BCC1;
                    break;
                }
                if(byte== FLAG){
                    state= FLAG_RCV;
                    
                }
                else{state=START;}
                break;
            case BCC1:
                if(control_field==0x0B){
                    return 0;}
                if(byte== ESC){state=DATA_RECEIVED_ESC;}
                else{state= READING_DATA; buf[0]=byte; data_byte_counter++;}

                break;
            case READING_DATA:
                if (byte== ESC){ state= DATA_RECEIVED_ESC; break;}
                if (byte== FLAG){
                    
                    unsigned char bcc2 = buf[data_byte_counter-1];
                    unsigned char accumulator=0;
                    data_byte_counter--;
                    buf[data_byte_counter]='\0';
                    for(int i =0; i <=data_byte_counter; i++){

                        accumulator=(accumulator ^ buf[i]);
                    }
                    if(bcc2==accumulator){
                        printf("Sending RR\n");
                        state=STOP;
                        sendSupervisionFrame(A_FSENDER, C_RR(info_frame_number_receiver));
                        info_frame_number_receiver=(info_frame_number_receiver+1)%2;
                        alarm(0);
                        
                        end_bits = clock();
                        total_time += ((double) (end_bits - start_bits)) / (double) CLOCKS_PER_SEC;
                        return data_byte_counter;
                    }
                    
                    else{
                        printf("Sending REJ\n");
                        //printf("data_byte_counter: %d\n", data_byte_counter);
                        sendSupervisionFrame(A_FSENDER, C_REJ(info_frame_number_receiver));
                        return -1;
                    }

                }
                else{
                   
                    buf[data_byte_counter++]= byte;
                }
                break;
            case DATA_RECEIVED_ESC:
                state= READING_DATA;
                if(byte == 0X5E){
                   
                    buf[data_byte_counter++]= FLAG;
                }
                if(byte == 0x5D){
                    
                    buf[data_byte_counter++]= ESC;
                    
                }
                /*
                else{
                    printf("Cleaning counter");
                    data_byte_counter=0;
                    if(byte==FLAG){
                        state=FLAG_RCV;
                    }
                    else{
                        state=START;
                    }
                }
                */
            break;
            case STOP:
                break;               
        }
    }
   }

return -1;
}

int llclose(int showStatistics){
    clock_t start_bits_tx, end_bits_tx;
    clock_t start_bits_rx, end_bits_rx;
    alarmCount=0;
    alarmEnabled=FALSE;
    LinkLayerState state= START;
    unsigned char byte;
    (void) signal(SIGALRM,alarmHandler);
    printf("Reached llclose! \n");
    if (role==Transmissor){
        start_bits_tx = clock();
    while(state != STOP &&  (alarmCount < attempts) ){
        if(alarmEnabled==FALSE){
            
            sendSupervisionFrame(A_FSENDER, C_DISC);
            alarm(timeout_);
            alarmEnabled=TRUE;
        }
        if(alarmEnabled==TRUE){
               if(read(fd ,&byte, 1)>0){
                bytes_sent++;
                switch (state)
                {
                case START:
                    if(byte==FLAG){
                        state=FLAG_RCV;
                    }
                    break;
                case FLAG_RCV:
                    //printf("Byte Address :%x ",byte);
                    if(byte==A_FRECEIVER){
                        state=A_RCV;
                        break;
                    } 
                    if(byte==FLAG){
                        state=FLAG_RCV; 
                    }
                    else{
                        state=START;
                    }
                    break;
                case A_RCV:
                    //printf("Byte CONTROL:%x ",byte);
                    if(byte==FLAG){
                        state=FLAG_RCV;
                    }
                    if (byte==C_DISC){ state= C_RCV; alarm(0);break;}
                    else{
                        state=START;
                    }
                break;
                case C_RCV:
                    //printf("Byte BCC:%x ",byte);
                    if (byte==(C_DISC^A_FRECEIVER)){
                        state= BCC1;
                        break;
                    }
                    if(byte==FLAG){
                        state=FLAG_RCV;
                    }
                    else{
                        state=START;
                    }
                    break;
                case BCC1:
                    //printf("Byte FLAG:%x ",byte);
                    if(byte==FLAG){
                        //printf("Falling");
                        state=STOP;
                        
                    }
                    else{
                        state=START;
                        break;
                    }
                case STOP:
                    printf("Last UA");
                      //alarm(0);
                    sendSupervisionFrame(A_FSENDER,C_UA);
                       
                     if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
                    {
                            perror("tcsetattr");
                            return -1;
                    }
                    end_bits_tx=clock();
                    total_time += ((double) (end_bits_tx - start_bits_tx)) / (double) CLOCKS_PER_SEC;


                     return 0;
                default:
                    break;
                }
                

            }
            }
            
        }
    }
    else if (role==Receptor){
        start_bits_rx=clock();
        while(1){
            if(waitingforUA==false){
            byte=readresponseByte(waitingforUA);
            if(byte==C_DISC){waitingforUA=true;}
            }
        
        if(waitingforUA==true){
            byte=readresponseByte(waitingforUA);
            if(byte==-1){return -1;}
            if(byte==C_UA){
                printf("Finishing Program!!!!");
                end=clock();
                printf("End: %ld\n", end);
                end_bits_rx = clock();
                total_time += ((double) (end_bits_rx - start_bits_rx)) / (double) CLOCKS_PER_SEC;
                 if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
                    {
                            perror("tcsetattr");
                            return -1;
                    }
                close(fd);
                if(SHOW_STATISTICS){
                    ShowStatistics();
                }
                return 0;
            }
            
        }
        }
        // sendSupervisionFrame(A_FRECEIVER,C_DISC);
    }
    return -1;
}

unsigned char readControlByte(){

    clock_t start_bits, end_bits;
    start_bits = clock();

    unsigned char byte = 0;
    unsigned char control_byte = 0;
    LinkLayerState state_ = START;

    while (state_ != STOP && (alarmEnabled == TRUE|| role==Receptor)){ 
       
        if(read(fd, &byte, 1) > 0){
            bytes_sent++;
            //printf("byte: %x\n", byte);

            switch (state_)
            {
            case START:
                if(byte == FLAG){
                    state_ = FLAG_RCV;
                }
                break;
            
            case FLAG_RCV:
                if(byte == A_FSENDER){
                    state_ = A_RCV;
                }
                else if(byte != FLAG){
                    state_ = START;
                }
                break;

            case A_RCV:
                if(byte == C_RR(0) || byte == C_RR(1) || byte == C_REJ(0) || byte == C_REJ(1)){
                    state_ = C_RCV;
                    control_byte = byte;
                }
                else if (byte != FLAG) state_ = START;
                else{
                    state_ = FLAG_RCV;
                }
                break;
            case C_RCV:
                if(byte == (A_FSENDER ^ control_byte)){
                    state_ = BCC1;
                }
                else if(byte != FLAG){
                    state_ = START;
                }
                else{
                    state_ = FLAG_RCV;
                }
                break;
            case BCC1:
                if(byte == FLAG){
                    state_ = STOP;
                }
                else{
                    state_ = START;
                }
                break;
            default:
                break;
            }


        }
    }

    end_bits = clock();
    total_time += ((double) (end_bits - start_bits)) / (double) CLOCKS_PER_SEC;

    return control_byte;

}
unsigned char readresponseByte(bool waitingforUA){


    unsigned char byte = 0;
    unsigned char control_byte = 0;
    LinkLayerState state_ = START;

    while (state_ != STOP){ 
       
        if(read(fd, &byte, 1) > 0){
            bytes_sent++;
            //printf("byte: %x\n", byte);

            switch (state_)
            {
            case START:
                if(byte == FLAG){
                    state_ = FLAG_RCV;
                }
                break;
            
            case FLAG_RCV:
                if(byte == A_FSENDER){
                    state_ = A_RCV;
                }
                else if(byte != FLAG){
                    state_ = START;
                }
                break;

            case A_RCV:
                if(byte ==C_UA ||byte==C_DISC ){
                    state_ = C_RCV;
                    control_byte = byte;
                }
                else if (byte != FLAG) state_ = START;
                else{
                    state_ = FLAG_RCV;
                }
                break;
            case C_RCV:
                if(byte == (A_FSENDER ^ control_byte)){
                    state_ = BCC1;
                }
                else if(byte != FLAG){
                    state_ = START;
                }
                else{
                    state_ = FLAG_RCV;
                }
                break;
            case BCC1:
                if(byte == FLAG){
                    state_ = STOP;
                }
                else{
                    state_ = START;
                }
                break;
            default:
                break;
            }


        }
    }
    if(waitingforUA==false){
        
        if(sendSupervisionFrame(A_FRECEIVER,C_DISC)==-1){return -1;}
        
        }
    return control_byte;

}

void ShowStatistics(){
    printf("\n---STATISTICS---\n");
    double cpu_time_used = ((double) (end - start)) / (double) CLOCKS_PER_SEC;
    printf("Time elapsed: %f\n", cpu_time_used);
    printf("Maximum Data to be transmitted: %d\n", MAX_PAYLOAD_SIZE);
    printf("Number of bytes sent: %d\n", bytes_sent);
    printf("Time spent sending bits: %f\n", total_time);
    double debit= (double)(bytes_sent * 8)/ total_time;
    printf("Debit: %f\n", debit);
    printf("Propagating time: %f\n", t_prop);
    printf("\n---STATISTICS---\n");
}
unsigned char simulateBitError(unsigned char byte, double errorRate) {
    
     
    double randomError = (double)rand() / RAND_MAX;

    if (randomError < errorRate) {
        
        int bitToFlip = rand() % 8;  
        byte ^= (1 << bitToFlip);    

    }
    return byte;
}
