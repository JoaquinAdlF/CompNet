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
#define SV_READY4AUTH           220
#define SV_READY4PASS           331
#define SV_LOGINSUCCESS         230
#define SV_PASSIVE              227
#define SV_READY4TRANSFER       150
#define SV_TRANSFER_COMPLETE    226
#define SV_GOODBYE              221

struct DATA {
    char user[MAX_LENGTH];
    char password[MAX_LENGTH];
    char domain[MAX_LENGTH];
    char filePath[MAX_LENGTH];
    char fileName[MAX_LENGTH];
    char ip[MAX_LENGTH];
};

typedef enum {
    START,
    SINGLE,
    MULTIPLE,
    END
} State;

int parseArguments(char *input, struct DATA *data);

int createSocket(char *ip, int port);

int authenticateUser(const int socket, const char *user, const char *password);

int readResponse(const int socket, char *response);

int passiveMode(const int socket, char *ip, int *port);

int requestFile(const int socket, const char *filePath);

int getFile(const int socketA, const int socketB, const char *filePath);

int closeSockets(const int socketA, const int socketB);

