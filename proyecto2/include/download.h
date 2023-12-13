#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <termios.h>

#define MAX_LENGTH  500
#define FTP_PORT    21

/* Server responses */
#define SOCKETSUCCESS           220
#define USERFOUND               331
#define LOGINSUCCESS            230
#define PASSIVEMODE             227
#define FILEFOUND               150
#define TRANSFERSUCCESS         226
#define SOCKETEND               221

struct DATA {
    char user[MAX_LENGTH];
    char password[MAX_LENGTH];
    char domain[MAX_LENGTH];
    char filePath[MAX_LENGTH];
    char fileName[MAX_LENGTH];
    char ip[MAX_LENGTH];
};

int parseArguments(char *input, struct DATA *data);

int createSocket(char *ip, int port);

int authenticateUser(const int socket, const char *user, const char *password);

int readResponse(const int socket, char *response);

int passiveMode(const int socket, char *ip, int *port);

int requestFile(const int socket, const char *filePath);

int getFile(const int socketA, const int socketB, const char *fileName);

