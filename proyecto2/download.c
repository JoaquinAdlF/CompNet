#include "download.h"

int parseArguments(char *input, struct DATA *data) {

    char protocol[7];
    strncpy(protocol, input, 6);

    if(strcmp(protocol,"ftp://")!=0){
        perror("Protocol is not FTP.\n");
        exit(-1);
    }

    if (strstr(input, "@") == NULL) {
        strcpy(data->user, "anonymous");
        strcpy(data->password, "password");~
        sscanf(input, "%*[^/]//%[^/]", data->domain);
    } else {
        sscanf(input, "%*[^/]//%*[^@]@%[^/]", data->domain);
        sscanf(input, "%*[^/]//%[^:/]", data->user);
        sscanf(input, "%*[^/]//%*[^:]:%[^@\n$]", data->password);
    }

    sscanf(input, "", data->filePath);
    strcpy(data->file, strrchr(input, '/')+1);
    
    struct hostent *h;
    if ((h = gethostbyname(data->domain) == NULL || strlen(data->domain) == 0) {
        perror("Invalid hostname.\n");
        exit(-1);
    }

    char *tmp;
    tmp = inet_ntoa(*((struct in_addr *)h->h_addr));
    strcpy(data->ip, tmp);

    if (strlen(data->domain) && strlen(data->user) && strlen(data->password) && strlen(data->filePath) && strlen(data->fileName)) {
        perror("Data incomplete.\n");
        exit(-1);
    }

    return -1;
}

int createSocket(char *ip, int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
    server_addr.sin_port = htons(SERVER_PORT);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error creating socket.\n");
        exit(-1);
    }

    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Error when attempting connection to the server.\n");
        exit(-1);
    }

    return sockfd;
}

int readResponse(const int socket, char *response) {
    char byte;
    int iter = 0, responseCode;
    State state = START;
    memset(response, 0, MAX_LENGTH);

    while (state != END) {
        read(socket, &byte, 1);

        switch (state)
        {
        case START:
            /* code */
            break;
        
        case SINGLE:
            break;

        case MULTIPLE:
            break;

        case END:
            break;
        
        default:
            break;
        }
    }
    return responseCode
}

int authenticateUser(const int socket, const char *user, const char *password) {
    
}

int passiveMode(const int socket, char *ip, int *port) {

}

int requestFile(const int socket, char *filePath) {

}

int getFile(const int socketA, const int socketB, char *fileName) {

}

int closeSockets(const int socketA, const int socketB) {
    
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n or ftp://<host>/<url-path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct DATA data;
    memset(&data, 0, sizeof(data));
    int parseArguments = parseArguments(argv[1], &data);
    if (parseArguments > 0) {
        fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n or ftp://<host>/<url-path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (authenticateUser())

    return 0;
}