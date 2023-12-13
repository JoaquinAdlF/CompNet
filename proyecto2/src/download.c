#include "../include/download.h"

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

    sscanf(input, "%*[^/]//%*[^/]/%s", data->filePath);
    strcpy(data->fileName, strrchr(input, '/')+1);
    
    struct hostent *h;
    if (h == gethostbyname(data->domain) == NULL) {
        perror("Invalid hostname.\n");
        exit(-1);
    }

    strcpy(data->ip, inet_ntoa(*((struct in_addr *) h->h_addr)));

    if (strlen(data->domain) && strlen(data->user) && strlen(data->password) && strlen(data->filePath) && strlen(data->fileName)) {
        perror("Data incomplete.\n");
        exit(-1);
    }

    return 0;
}

int createSocket(char *ip, int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error creating socket.\n");
        exit(-1);
    }

    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Error when attempting connection to the server.\n");
        exit(-1);
    }

    char response[MAX_LENGTH];

    if (readResponse(sockfd, response) != SOCKETSUCCESS || sockfd < 0) {
        perror("Error creating socket\n");
        exit(-1);
    }

    return sockfd;
}

int readResponse(const int socket, char *response) {
    FILE *socketfd = fdopen(socket, "r");

    char *buf;
    size_t bytesRead = 0;
    int code;
    
    while (getline(&buf, &bytesRead, socketfd) > 0)
    {
        strncat(response, buf, bytesRead - 1);

        if (buf[3] == ' ')
        {
            sscanf(buf, "%d", &code);
            break;
        }
    }
    return code;
}

int authenticateUser(const int socket, const char *user, const char *password) {
    char response[MAX_LENGTH], bufferA[5 + strlen(user) + 1];
    int n;

    snprintf(bufferA, "USER %s\n", user);
    write(socket, bufferA, strlen(bufferA));
    if (readResponse(socket, response) != USERFOUND) {
        perror("Error when searching for user.\n");
        exit(-1);
    }

    char bufferB[5 + strlen(user) + 1];
    sprintf(bufferB, "PASS %s\n", password);
    write(socket, bufferB, strlen(bufferB));
    if (readResponse(socket, response) != LOGINSUCCESS) {
        perror("Error with password.\n");
        exit(-1);
    }

    return 0;
}

int passiveMode(const int socket, char *ip, int *port) {
    char response[MAX_LENGTH];
    int h1, h2, h3, h4, p1, p2;

    write(socket, "PASV\n", 5);
    if (readResponse(socket, response) != PASSIVEMODE) {
        perror("Error in passive mode.\n");
        exit(-1);
    }

    sscanf(response,"%*[^(](%d,%d,%d,%d,%d,%d)%*[^\n$)]", &h1, &h2, &h3, &h4, &p1, &p2);
    sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);

    return 0;
}

int requestFile(const int socket, const char *filePath) {
    char buffer[5 + strlen(filePath) + 1], response[MAX_LENGTH];
    sprintf(buffer, "RETR %s\n", filePath);
    write(socket, buffer, sizeof(buffer));
    if (readResponse(socket, response) != FILEFOUND) {
        perror("File not found.\n");
        exit(-1);
    }
}

int getFile(const int socketfd, const int dataSocket, const char *fileName) {
    FILE *fd = fopen(fileName, "wb");
    if (fd == NULL) {
        perror("Error creating file.\n");
        exit(-1);
    }

    char buffer[MAX_LENGTH];
    int bytes;
    while ((bytes = read(dataSocket, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, bytes, 1, fd) < 0) {
            perror("Error downloading file.\n");
            exit(-1);
        }
    }
    close(fd);

    char response[MAX_LENGTH];
    
    if (readResponse(socketfd, response) != TRANSFERSUCCESS) {
        perror("Error during transfer.\n");
        exit(-1);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n or ftp://<host>/<url-path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct DATA data;
    memset(&data, 0, sizeof(data));
    if (parseArguments(argv[1], &data) < 0) {
        fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n or ftp://<host>/<url-path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int socketfd = createSocket(data.ip, FTP_PORT);

    authenticateUser(socketfd, data.user, data.password);

    int port;
    char ip[MAX_LENGTH];

    passiveMode(socketfd, ip, &port);

    int dataSocket = createSocket(ip, port);

    requestFile(socketfd, data.filePath);

    getFile(socketfd, dataSocket, data.fileName);

    char response[MAX_LENGTH];
    write(socketfd, "QUIT\n", 5);
    if(readResponse(socketfd, response) != SOCKETEND) {
        perror("Error when closing socket.\n");
        exit(-1);
    }
    close(socketfd);
    close(dataSocket);

    return 0;
}