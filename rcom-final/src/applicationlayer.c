// Application layer protocol implementation

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "applicationlayer.h"
#include "DataLink.h"

double t_prop;

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    int size_aux=0; 
    int result;
    int showStatistics=TRUE;
    clock_t start_, end_;

    

    if (strcmp(role, "tx") == 0) {
        int fd = open(filename, O_RDONLY);
        if (fd == -1) {
            printf("Error opening \"%s\".\n", filename);
            return;
        }

        struct stat st;
        stat(filename, &st);
        unsigned long size = (unsigned long) st.st_size;
        int L1 = ceil( floor(log2(size )+1) / 8);
	    int L2 = strlen(filename);
        size_aux = 1 + 1 + 1 + L1 + 2 + L2;
        int i = 0;
        unsigned char* control_packet = (unsigned char*) malloc(size_aux);
        control_packet[i++] = C_START;
        control_packet[i++] = 0;
        control_packet[i++] = L1;
        int size_aux_aux= size_aux;
       for (unsigned char j = 0 ; j < L1 ; j++) {
        control_packet[2+L1-j] = size_aux_aux & 0xFF;
        size_aux_aux >>= 8;
        }	
    i += L1;
    control_packet[i++]=1;
    control_packet[i++]=L2;

    memcpy(control_packet + i,filename,L2);





        LinkLayer connectionParameters;
        Role role= Transmissor;
        strcpy(connectionParameters.serialPort, serialPort);
        connectionParameters.role = role;
        connectionParameters.baudRate = baudRate;
        connectionParameters.numTransmissions = nTries;
        connectionParameters.timeout = timeout;

        if (llopen(connectionParameters) == -1) {
            printf("Error setting connection.\n");
            return;
        }
        printf("Size of control packet: %d \n", size_aux);
        //sleep(5);
        start_=clock(); // Tempo de processamento
        printf("Start: %ld\n", start_);
        int err= llwrite(control_packet, size_aux);
        printf("%d",err);
        if (err ==-1) {
            printf("Error transmitting information.1\n");
            return;
        }
        end_=clock(); 
        printf("End: %ld\n", end_);
        printf("Start: %ld\n", start_);
        t_prop = ((double) (start_ - end_)) / (double) CLOCKS_PER_SEC; 
       

        unsigned char* content = (unsigned char*)malloc(sizeof(unsigned char) * size);  
        read(fd, content, size);

        long int bytesLeft = size;

        

        while (bytesLeft >= 0) {
            int dataSize = bytesLeft > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytesLeft;
            unsigned char* data = (unsigned char*) malloc(dataSize);
            memcpy(data, content, dataSize);
            int packetSize;
            packetSize = 1 + 1 + 1 + dataSize;
            unsigned char* packet = (unsigned char*)malloc(packetSize);
            packet[0] = 1;
            packet[2] = dataSize & 0xFF;
            packet[1] = (dataSize >> 8) & 0xFF;
            memcpy(packet + 3, data, dataSize);
           

            
            if (llwrite(packet, packetSize) == -1) {
                printf("Failed transmitting data packet.2\n");
                return;
            }
        

            

            bytesLeft -= (long int) MAX_PAYLOAD_SIZE; 
            content += dataSize; 
        }


        control_packet[0] = C_END;
        result=llwrite(control_packet, size_aux); 
        if (result== -1) {
            printf("Error transmitting information.3\n");
            return;
        }else{
            
            //sleep(5);
            result=llclose(showStatistics);}
        free(control_packet);

        if (result == -1) {
            printf("Error closing connection.\n");
            return;
        }

        close(fd);

    } else if (strcmp(role, "rx") == 0) {
        int fd;
        
        int fd_temp = open(filename, O_WRONLY | O_TRUNC);
        if(fd_temp == -1)
            fd = open(filename, O_APPEND | O_CREAT | O_WRONLY);
        else {
            write(fd_temp, "", 0);
            close(fd_temp);
            fd = open(filename, O_APPEND | O_WRONLY);
        }
        if (fd == -1) {
            printf("Error opening \"%s\".\n", filename);
            return;
        }
        LinkLayer connectionParameters;

        Role role = Receptor;
        strcpy(connectionParameters.serialPort, serialPort);
        connectionParameters.role = role;
        connectionParameters.baudRate = baudRate;
        connectionParameters.numTransmissions = nTries;
        connectionParameters.timeout = timeout;
        if (llopen(connectionParameters) == -1) {
            printf("Not opening the serial port.\n");
            return;
        } 
        unsigned char* buffer = (unsigned char*) malloc (MAX_PAYLOAD_SIZE);
        int packetSize = -1;
        while ((packetSize = llread(buffer)) < 0);
        

         
        printf("---ACCEPTED FIRST PACKET CONTROL-----\n");

        //--------------------
        unsigned long size = 0;
        for (int i = 0; i < 8; i++) {
            size <<= 8;
            size += buffer[i + 3];
        }
        //-------------------

        while(1) {
            if(llread(buffer) == -1) {
                printf("------------------------------------------------------------Continuou\n");
                continue;
            }
            if(buffer[0] != C_DATA) break;
            unsigned int current_size = buffer[1];
            current_size <<= 8;
            current_size += buffer[2];
            
            write(fd, buffer + 3, current_size);
        }

        if(buffer[0] != C_END) {
            printf("Error receiving information(C_END).\n");
            return;
        }

        unsigned long new_size = 0;
        for(int i = 0; i < 8; i++){
            new_size <<= 8;
            new_size += buffer[i + 3];
        }
        if(new_size != size) {
            printf("Size of packets not coincident.\n");
            return;
        }
        free(buffer);
        
        result=llclose(SHOW_STATISTICS);
        if(result == -1) {
            printf("Error closing connection.\n");
            return;
        }
        close(fd);
        return;
    }
}