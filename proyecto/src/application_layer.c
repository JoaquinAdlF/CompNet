// Application layer protocol implementation

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "application_layer.h"
#include "link_layer.h"

void transmitter(const char *serialPort, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    int size_aux = 0; 
    int result;

    // Try to open file to read
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        printf("Error opening \"%s\".\n", filename);
        return;
    }

    // Get the file size and allocate a control packet with appropriate size.
    struct stat st;
    if (stat(filename, &st) == -1) {
        printf("Error allocating control packet");
        return;
    }

    // Cast to unsigned long for size to avoid potential overflow with large files.
    unsigned long size = (unsigned long) st.st_size;

    // Calculate L1 based on the size of the file.
    int L1 = (int)ceil(log2(size) / 8 + 1);

    // Calculate L2 based on the length of the filename.
    int L2 = strlen(filename);

    // Calculate the total size needed for auxiliary control packet.
    size_aux = 5 + L1 + L2;

    // Allocate the control packet with the calculated size.
    unsigned char* control_packet = (unsigned char*)malloc(size_aux);
    if (control_packet == NULL) {
        printf("Error allocating control packet");
        return;
    }

    // Initialize control packet
    int iter = 0;
    control_packet[iter++] = 2;
    control_packet[iter++] = 0;
    control_packet[iter++] = L1;

    // Insert the size into the control packet in a big-endian format.
    int size_aux_aux= size_aux;
    for (unsigned char i = 0 ; i < L1 ; i++) {
        control_packet[2+L1-i] = size_aux_aux & 0xFF;
        size_aux_aux >>= 8;
    }

    // Adjust 'i' to skip over the size bytes just written.
    iter += L1;
    control_packet[iter++] = 1;  // Type of file name field
    control_packet[iter++] = L2; // Length of file name field

    // Copy the filename into the control packet.
    memcpy(control_packet + iter, filename, L2);

    // Set connection parameters
    LinkLayer connectionParameters;
    LinkLayerRole role = LlTx;
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.role = role;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // Open connection and handle error
    if (llopen(connectionParameters) == -1) {
        printf("Error setting connection.\n");
        return;
    }
    printf("Size of control packet: %d \n", size_aux);

    // Write the control packet
    int err = llwrite(control_packet, size_aux);

    if (err == -1) {
        printf("Error transmitting information.1\n");
        return;
    }
    
    // Read response
    unsigned char* content = (unsigned char*)malloc(sizeof(unsigned char) * size);  
    read(fd, content, size);

    long int bytesLeft = size;

    // Write content
    while (bytesLeft >= 0) {
        printf("Bytes left to send: %li \n", bytesLeft);
        // Determine the size of the data to send in this iteration. 
        int dataSize = bytesLeft > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytesLeft;
        
        // Allocate the memory for the data
        unsigned char* data = (unsigned char*) malloc(dataSize);

        // Copy datasyze bytes from the content to the buffer
        memcpy(data, content, dataSize);

        // Calculate packet size
        int packetSize;
        packetSize = 1 + 1 + 1 + dataSize;

        // Allocate memory for the packet
        unsigned char* packet = (unsigned char*)malloc(packetSize);

        // Set header and data of the packet
        packet[0] = 1;
        packet[2] = dataSize & 0xFF;
        packet[1] = (dataSize >> 8) & 0xFF;
        memcpy(packet + 3, data, dataSize);
        

        // Write the data and handle error
        if (llwrite(packet, packetSize) == -1) {
            printf("Failed transmitting data packet.\n");
            return;
        }
    
        // Decrease bytes set and move the content pointer forward
        bytesLeft -= (long int) MAX_PAYLOAD_SIZE; 
        content += dataSize; 
    }

    printf("All bytes sent\n");

    // Set the first byt of the end packet
    control_packet[0] = 3;

    // Send the packet handle result
    result = llwrite(control_packet, size_aux); 
    if (result == -1) {
        printf("Error transmitting information.3\n");
        return;
    } else {
        result = llclose(TRUE);
    }

    // Free the allocated memory
    free(control_packet);

    // Check the result of the llclose() function
    if (result == -1) {
        printf("Error closing connection.\n");
        return;
    }

    printf("Connection closed\n");

    close(fd);
}

void receiver(const char *serialPort, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    int result;
    int fd;
        
    // See whatt to do with the received file
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

    // Set connection parameters
    LinkLayer connectionParameters;
    LinkLayerRole role= LlRx;
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.role = role;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // Open connection and hanlde error
    if (llopen(connectionParameters) == -1) {
        printf("Not opening the serial port.\n");
        return;
    } 
    unsigned char* buffer = (unsigned char*) malloc (MAX_PAYLOAD_SIZE);
    int packetSize = -1;
    while ((packetSize = llread(buffer)) < 0);

    // Initialize a variable to hold the size of the data and loop over the next 8 bytes
    unsigned long size = 0;
    for (int i = 0; i < 8; i++) {
        size <<= 8;
        size += buffer[i + 3];
    }

    int bytesRead;

    // Read the buffer
    while(1) {
        // Read until packet has no data
        bytesRead = llread(buffer);
        if(bytesRead == -1) {
            printf("Keep reading...\n");
            continue;
        }
        if(buffer[0] != 1) break;

        // Assemble de size of the current packet
        unsigned int current_size = buffer[1];

        // Shift by 8 bits to make room for the low byte
        current_size <<= 8;
        current_size += buffer[2];
        
        // Write data to the file descriptor
        write(fd, buffer + 3, current_size);

        printf("Bytes read: %i\n", current_size);
    }

    // Check if the first byte indicates the end of data
    if(buffer[0] != 3) {
        printf("Error receiving information.\n");
        return;
    }

    // Start storing the data of the end packet
    unsigned long new_size = 0;

    // Loop and shift to make room for the next byte
    for(int i = 0; i < 8; i++){
        new_size <<= 8;
        new_size += buffer[i + 3];
    }
    
    // Compare the received data vs the expected data
    if(new_size != size) {
        printf("Size of packets differs.\n");
        return;
    }
    
    // Free de memory form the buffer
    free(buffer);
    
    result = llclose(TRUE);
    if(result == -1) {
        printf("Error closing connection.\n");
        return;
    }
    close(fd);
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    if (strcmp(role, "tx") == 0) {
        transmitter(serialPort, baudRate, nTries, timeout, filename);
        return;
    } else if (strcmp(role, "rx") == 0) {
        receiver(serialPort, baudRate, nTries, timeout, filename);
        return;
    }
    
}
