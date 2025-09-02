#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    char currentNick[64];
    currentNick[0] = '\0';
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
        error("ERROR connecting");
    // initial prompt
    printf("You: ");
    fflush(stdout);
    fd_set readfds;
    int fdmax = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sockfd, &readfds);
        if (select(fdmax + 1, &readfds, NULL, NULL, NULL) < 0) {
            error("ERROR on select");
        }

        // Socket ready: read and print immediately
        if (FD_ISSET(sockfd, &readfds)) {
            bzero(buffer,256);
            n = read(sockfd, buffer, 255);
            if (n < 0) error("ERROR reading from socket");
            if (n == 0) break; // server closed

            // Clear current input line
            printf("\33[2K\r");

            // Highlight our nick if present
            if (currentNick[0] != '\0') {
                char out[512];
                out[0] = '\0';
                const char *p = buffer;
                const char *name = currentNick;
                size_t nameLen = strlen(name);
                while (*p && strlen(out) + 1 < sizeof(out)) {
                    const char *hit = strstr(p, name);
                    if (!hit) {
                        strncat(out, p, sizeof(out) - strlen(out) - 1);
                        break;
                    }
                    strncat(out, p, (size_t)(hit - p) < (sizeof(out) - strlen(out) - 1) ? (size_t)(hit - p) : (sizeof(out) - strlen(out) - 1));
                    const char *pre = "\033[1;33m";
                    const char *post = "\033[0m";
                    if (strlen(out) + strlen(pre) + nameLen + strlen(post) + 1 >= sizeof(out)) {
                        strncat(out, hit, sizeof(out) - strlen(out) - 1);
                        break;
                    }
                    strcat(out, pre);
                    strncat(out, name, nameLen);
                    strcat(out, post);
                    p = hit + nameLen;
                }
                if (out[0] != '\0') printf("%s\n", out); else printf("%s\n", buffer);
            } else {
                printf("%s\n", buffer);
            }

            // Redraw prompt
            printf("You: ");
            fflush(stdout);
        }

        // Stdin ready: read a line and send
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            bzero(buffer,256);
            if (fgets(buffer,255,stdin) == NULL) break; // EOF

            // Track local nickname when user issues /nick command
            if (strncmp(buffer, "/nick ", 6) == 0) {
                char *nick = buffer + 6;
                int L = (int)strlen(nick);
                while (L > 0 && (nick[L-1] == '\n' || nick[L-1] == '\r')) { nick[--L] = '\0'; }
                if (nick[0] != '\0') {
                    strncpy(currentNick, nick, sizeof(currentNick)-1);
                    currentNick[sizeof(currentNick)-1] = '\0';
                }
            }

            // Clear prompt line before sending
            printf("\33[2K\r");
            n = write(sockfd, buffer, strlen(buffer));
            if (n < 0) error("ERROR writing to socket");

            // Redraw prompt
            printf("You: ");
            fflush(stdout);
        }
    }
    close(sockfd);
    return 0;
}