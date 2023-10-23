// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer ConnectionParameters;

    // Copy the serialPort string into the struct member
    strcpy(ConnectionParameters.serialPort, serialPort);

    ConnectionParameters.baudRate = baudRate;
    ConnectionParameters.nRetransmissions = nTries;
    ConnectionParameters.timeout = timeout;

    if (strcmp(role, "tx") == 0) {
        ConnectionParameters.role = LlTx;
        printf("tx mode\n");

        // Open connection and handle error
        if(llopen(ConnectionParameters) == -1) {
            fprintf(stderr, "Could not initialize link layer connection\n");
            exit(1);
        }

        printf("connection opened\n");
        fflush(stdout);
        fflush(stderr);

        // Open file to read and handle error
        const char *file_path = filename;
        int file_desc = open(file_path, O_RDONLY);
        if (file_desc < 0) {
            fprintf(stderr, "Error opening file: %s\n", file_path);
            exit(1);
        }

        // Cycle through the file
        const int buf_size = MAX_PAYLOAD_SIZE-1;
        unsigned char buffer[buf_size+1];
        int write_result = 0;
        int bytes_read = 1;
        
        while (bytes_read > 0) {
            bytes_read = read(file_desc, buffer+1, buf_size);

            // Error from link layer
            if(bytes_read < 0) {
                    fprintf(stderr, "Error reading from file\n");
                    break;
            }

            // Continue with the transfer
            if (bytes_read > 0) {
                buffer[0] = 1;
                write_result = llwrite(buffer, bytes_read+1);
                // If the resutl is lower than 0 there was an error and the process is halted
                if (write_result < 0) {
                    fprintf(stderr, "Error sending data to link layer\n");
                    break;
                }
                printf("read from file -> write to link layer, %d\n", bytes_read);
            }
            // Transfer successful, stop receiver
            else if (bytes_read == 0) {
                buffer[0] = 0;
                llwrite(buffer, 1);
                printf("App layer done reading and sending file\n");
                break;
            }

            sleep(1);
        }
        // Close the connection
        llclose(1);
        close(file_desc);
    }
    else {
        // Handle the "rx" case or other cases if needed
        // For example: ConnectionParameters.role = LlRx;
        ConnectionParameters.role = LlRx;
        printf("rx mode\n");

        if (llopen(ConnectionParameters) == -1) {
            fprintf(stderr, "Could not initialize link layer connection\n");
            exit(1);
        }

        printf("connection opened\n");
        fflush(stdout);
        fflush(stderr);

        const char *file_path = filename;
        int file_desc = open(file_path, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if (file_desc < 0) {
            fprintf(stderr, "Error opening file: %s\n", file_path);
            exit(1);
        }

        int bytes_read = 0;
        int write_result = 0;
        const int buf_size = MAX_PAYLOAD_SIZE;
        unsigned char buffer[buf_size];
        int total_bytes = 0;

        while (bytes_read >= 0) {
            bytes_read = llread(buffer);
            fprint("Reading...\n");

            if (bytes_read < 0) {
                fprintf(stderr, "Error receiving from link layer\n");
                break;
            }

            else if (bytes_read > 0) {
                if (buffer[0] == 1) {
                    write_result = write(file_desc, buffer+1, bytes_read-1);

                    if (write_result < 0) {
                        fprintf(stderr, "Error writing to file\n");
                        break;
                    }

                    total_bytes = total_bytes + write_result;
                    printf("read from file -> write to link layer, %d %d %d\n", bytes_read, write_result, total_bytes);
                }
                else if (buffer[0] == 0) {
                    printf("App layer: done receiving file\n");
                    break;
                }
            }
        }

        llclose(2);
        close(file_desc);
    }

    // Other application layer logic...
}

