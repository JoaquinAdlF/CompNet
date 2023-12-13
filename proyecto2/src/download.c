#include "../include/download.h"

int parseArguments(char *input, struct DATA *data) {
    regex_t regex;
    regcomp(&regex, "/", 0);
    if (regexec(&regex, input, 0, NULL, 0)) return -1;

    regcomp(&regex, "@", 0);
    if (regexec(&regex, input, 0, NULL, 0) != 0) {
        sscanf(input, "%*[^/]//%[^/]", data->domain);
        strcpy(data->user, "anonymous");
        strcpy(data->password, "password");
    } else {
        sscanf(input, "%*[^/]//%*[^@]@%[^/]", data->domain);
        sscanf(input, "%*[^/]//%[^:/]", data->user);
        sscanf(input, "%*[^/]//%*[^:]:%[^@\n$]", data->password);
    }

    sscanf(input, "%*[^/]//%*[^/]/%s", data->filePath);
    strcpy(data->fileName, strrchr(input, '/')+1);
    
    struct hostent *h;
    if (strlen(data->domain) == 0) return -1;
    if ((h = gethostbyname(data->domain)) == NULL) {
        perror("Invalid hostname.\n");
        exit(-1);
    }

    strcpy(data->ip, inet_ntoa(*((struct in_addr *) h->h_addr)));

    regfree(&regex);

    if (!(strlen(data->domain) && strlen(data->filePath))) {
        perror("Data incomplete.\n");
        return -1;
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
        perror("socket()");
        exit(-1);
    }
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }
    
    return sockfd;
}

/*
int readResponse(const int socket, char *response) {
    FILE *socketfd = fdopen(socket, "r");

    if (socketfd == NULL) {
        perror("Error reading socket.\n");
        exit(-1);
    }

    char *buf;
    size_t bytesRead = 0;
    int code = 0;
    memset(response, 0, MAX_LENGTH);

    printf("Reading response...\n");  // Debug print

    while (getline(&buf, &bytesRead, socketfd) > 0) {

        if (buf[3] == ' ')
        {
            strncat(response, buf, bytesRead - 1);
            sscanf(buf, "%d", &code);
            strncpy(response, buf, bytesRead - 1);
            response[bytesRead - 1] = '\0';
            break;
        }
    }

    if (buf != NULL) {
        free(buf);
    }

    printf("Final response: %s\n", response);
    return code;
}
*/


int readResponse(const int socket, char *response) {
    char byte;
    int index = 0, responseCode;
    State state = START;
    memset(response, 0, MAX_LENGTH);

    while (state != END) {
        
        read(socket, &byte, 1);
        switch (state) {
            case START:
                if (byte == ' ') state = SINGLE;
                else if (byte == '-') state = MULTI;
                else if (byte == '\n') state = END;
                else response[index++] = byte;
                break;
            case SINGLE:
                if (byte == '\n') state = END;
                else response[index++] = byte;
                break;
            case MULTI:
                if (byte == '\n') {
                    memset(response, 0, MAX_LENGTH);
                    state = START;
                    index = 0;
                }
                else response[index++] = byte;
                break;
            case END:
                break;
            default:
                break;
        }
    }

    sscanf(response, "%d", &responseCode);
    return responseCode;

}



int authenticateUser(const int socket, const char *user, const char *password) {
    char response[MAX_LENGTH];
    char buffer[MAX_LENGTH];

    // Send USER command
    snprintf(buffer, sizeof(buffer), "user %s\n", user);
    write(socket, buffer, strlen(buffer));
    printf("user %s\n", buffer);
    if (readResponse(socket, response) != USERFOUND) {
        fprintf(stderr, "Error when searching for user: %s\n", response);
        return -1; // Return error code
    }

    // Send PASS command
    snprintf(buffer, sizeof(buffer), "pass %s\n", password);
    write(socket, buffer, strlen(buffer));
    printf("password %s\n", buffer);
    if (readResponse(socket, response) != LOGINSUCCESS) {
        fprintf(stderr, "Error with password: %s\n", response);
        return -1; // Return error code
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

    sscanf(response, "%*[^(](%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2);
    *port = p1 * 256 + p2;
    sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);

    printf("Port: %d, IP: %s\n", *port, ip);

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
    return 0;
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
    fclose(fd);

    char response[MAX_LENGTH];
    
    if (readResponse(socketfd, response) != TRANSFERSUCCESS) {
        perror("Error during transfer.\n");
        exit(-1);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n or ftp://<host>/<url-path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct DATA data;
    memset(&data, 0, sizeof(data));
    if (parseArguments(argv[1], &data) < 0) {
        fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n or ftp://<host>/<url-path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("Host: %s\nResource: %s\nFile: %s\nUser: %s\nPassword: %s\nIP Address: %s\n", data.domain, data.filePath, data.fileName, data.user, data.password, data.ip);

    char response[MAX_LENGTH];
    int socketfd = createSocket(data.ip, FTP_PORT);
    if (socketfd < 0 || readResponse(socketfd, response) != SOCKETSUCCESS) {
        printf("Socket to '%s' and port %d failed\n", data.ip, FTP_PORT);
        exit(-1);
    }

    printf("Passed socket\n");

    authenticateUser(socketfd, data.user, data.password);

    printf("Passed auth\n");

    int port;
    char ip[MAX_LENGTH];

    passiveMode(socketfd, ip, &port);

    printf("Passed passive\n");

    int dataSocket = createSocket(ip, port);

    printf("Passed socket 2\n");

    requestFile(socketfd, data.filePath);

    printf("Passed request\n");

    getFile(socketfd, dataSocket, data.fileName);

    printf("Passed download\n");

    write(socketfd, "QUIT\n", 5);
    if(readResponse(socketfd, response) != SOCKETEND) {
        perror("Error when closing socket.\n");
        exit(-1);
    }
    close(socketfd);
    close(dataSocket);

    return 0;
}