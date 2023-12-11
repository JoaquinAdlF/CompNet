#include "download.h"

int parseArguments(char *input, struct DATA *data) {
    regex_t regex;
    regcomp(&regex, "@", 0);
    if (regexec(&regex, input, 0, NULL, 0) != 0) {
        strcpy(data->user, "anonymous");
        strcpy(data->password, "password");~
        strcpy(data->host, "%*[^/]//%[^/]");
    }


    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <url>\n", argv[0]);
        exit(1);
    }

    struct DATA data;
    memset(&data, 0, sizeof(data));
    int parseArguments = parseArguments(argv[1], &data);
    if (parseArguments > 0) {
        fprintf(error, "Error parsing arguments\n");
        exit(1);
    }

    return 0;
}