#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <time.h>
#include <stdarg.h>
#include <strings.h>

typedef enum { LOG_ERROR = 0, LOG_WARN = 1, LOG_INFO = 2, LOG_DEBUG = 3 } LogLevel;

static LogLevel g_log_level = LOG_ERROR;
static char g_nicks[FD_SETSIZE][64];

static const char* level_to_string(LogLevel level) {
    return (level == LOG_ERROR) ? "ERROR" : (level == LOG_WARN) ? "WARN" : (level == LOG_INFO) ? "INFO" : "DEBUG";
}

static void set_log_level_from_env() {
    const char *env = getenv("GLTXIRC_DEBUG");
    if (!env) { g_log_level = LOG_ERROR; return; }
    if (strcasecmp(env, "0") == 0 || strcasecmp(env, "ERROR") == 0) g_log_level = LOG_ERROR;
    else if (strcasecmp(env, "1") == 0 || strcasecmp(env, "WARN") == 0) g_log_level = LOG_WARN;
    else if (strcasecmp(env, "2") == 0 || strcasecmp(env, "INFO") == 0) g_log_level = LOG_INFO;
    else if (strcasecmp(env, "3") == 0 || strcasecmp(env, "DEBUG") == 0) g_log_level = LOG_DEBUG;
    else g_log_level = LOG_ERROR;
}

static void log_msg(LogLevel level, const char *fmt, ...) {
    if (level > g_log_level) return;
    const char *level_str = level_to_string(level);
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);
    fprintf(stderr, "%s [%s] ", ts, level_str);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

void error(const char *msg) {
    perror(msg);
    exit(1);
}

static void send_crlf(int fd, const char *text) {
    if (!text) return;
    char line[1024];
    size_t len = strlen(text);
    if (len >= sizeof(line) - 3) len = sizeof(line) - 3; // leave room for CRLF and NUL
    size_t i = 0;
    for (; i < len; i++) line[i] = text[i];
    while (i > 0 && (line[i-1] == '\n' || line[i-1] == '\r')) i--;
    line[i++] = '\r';
    line[i++] = '\n';
    line[i] = '\0';
    write(fd, line, i);
}

static LogLevel parse_level(const char *s) {
    if (!s) return LOG_ERROR;
    if (strcasecmp(s, "0") == 0 || strcasecmp(s, "ERROR") == 0) return LOG_ERROR;
    if (strcasecmp(s, "1") == 0 || strcasecmp(s, "WARN") == 0) return LOG_WARN;
    if (strcasecmp(s, "2") == 0 || strcasecmp(s, "INFO") == 0) return LOG_INFO;
    if (strcasecmp(s, "3") == 0 || strcasecmp(s, "DEBUG") == 0) return LOG_DEBUG;
    return LOG_ERROR;
}

int main(int argc, char **argv) {
    int server_socket_fd, port_number;
    socklen_t client_address_length;
    char buffer[256];
    struct sockaddr_in server_address, client_address;
    int num_bytes;
    fd_set read_fds, ready_fds;
    int fd_max;
    int client_fds[FD_SETSIZE];

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }

    set_log_level_from_env();
    log_msg(LOG_INFO, "Starting server");
    srand((unsigned int)time(NULL));

    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0)
        error("ERROR opening socket");

    bzero((char *) &server_address, sizeof(server_address));
    port_number = atoi(argv[1]);
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port_number);

    int enable_reuse = 1;
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable_reuse, sizeof(int));

    if (bind(server_socket_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
        error("ERROR on binding");

    listen(server_socket_fd, 16);
    log_msg(LOG_INFO, "Listening on port %d", port_number);
    log_msg(LOG_INFO, "Log level set to %s", level_to_string(g_log_level));

    FD_ZERO(&read_fds);
    FD_SET(server_socket_fd, &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    fd_max = server_socket_fd > STDIN_FILENO ? server_socket_fd : STDIN_FILENO;

    for (int i = 0; i < FD_SETSIZE; i++) client_fds[i] = -1;

    while (1) {
        ready_fds = read_fds;
        if (select(fd_max + 1, &ready_fds, NULL, NULL, NULL) < 0) {
            error("ERROR on select");
        }

        // New incoming connection
        if (FD_ISSET(server_socket_fd, &ready_fds)) {
            client_address_length = sizeof(client_address);
            int new_fd = accept(server_socket_fd, (struct sockaddr *) &client_address, &client_address_length);
            if (new_fd >= 0) {
                // track client
                int placed = 0;
                for (int i = 0; i < FD_SETSIZE; i++) {
                    if (client_fds[i] == -1) { client_fds[i] = new_fd; placed = 1; break; }
                }
                if (!placed) {
                    close(new_fd);
                } else {
                    FD_SET(new_fd, &read_fds);
                    if (new_fd > fd_max) fd_max = new_fd;
                    log_msg(LOG_INFO, "Client %d connected", new_fd);

                    // Broadcast randomized join greeting
                    const char *greets[] = {
                        "Welcome!",
                        "Glad you made it!",
                        "Ahoy!",
                        "Howdy!",
                        "Nice to see you!",
                        "Hello there!"
                    };
                    size_t greets_count = sizeof(greets) / sizeof(greets[0]);
                    const char *g = greets[rand() % greets_count];
                    char join_msg[256];
                    snprintf(join_msg, sizeof(join_msg), "Client %d has joined the channel — %s", new_fd, g);
                    for (int k = 0; k < FD_SETSIZE; k++) {
                        int fd = client_fds[k];
                        if (fd != -1) send_crlf(fd, join_msg);
                    }
                }
            }
        }

        // Server-side stdin input -> broadcast to all clients
        if (FD_ISSET(STDIN_FILENO, &ready_fds)) {
            bzero(buffer, 256);
            if (fgets(buffer, 255, stdin) == NULL) {
                // EOF on stdin, ignore or break; here we ignore to keep server running
            } else {
                // echo to console and broadcast
                log_msg(LOG_DEBUG, "stdin: %s", buffer);
                for (int i = 0; i < FD_SETSIZE; i++) {
                    int fd = client_fds[i];
                    if (fd != -1) {
                        send_crlf(fd, buffer);
                    }
                }
            }
        }

        // Handle client messages
        for (int i = 0; i <= fd_max; i++) {
            if (i == server_socket_fd || i == STDIN_FILENO) continue;
            if (!FD_ISSET(i, &ready_fds)) continue;

            bzero(buffer, 256);
            num_bytes = read(i, buffer, 255);
            if (num_bytes <= 0) {
                // closed or error
                // Compose leave message with name
                int sender_index = -1;
                for (int k = 0; k < FD_SETSIZE; k++) if (client_fds[k] == i) { sender_index = k; break; }
                char namebuf[64];
                if (sender_index >= 0 && g_nicks[sender_index][0]) snprintf(namebuf, sizeof(namebuf), "%s", g_nicks[sender_index]);
                else snprintf(namebuf, sizeof(namebuf), "Client %d", i);
                char leave_msg[256];
                snprintf(leave_msg, sizeof(leave_msg), "*[System] %s has left the channel*", namebuf);
                for (int k = 0; k < FD_SETSIZE; k++) {
                    int fd = client_fds[k];
                    if (fd != -1 && fd != i) send_crlf(fd, leave_msg);
                }

                close(i);
                FD_CLR(i, &read_fds);
                for (int k = 0; k < FD_SETSIZE; k++) if (client_fds[k] == i) client_fds[k] = -1;
                continue;
            }

            // Support /nick and broadcast with names
            // Identify sender slot
            int sender_index = -1;
            for (int k = 0; k < FD_SETSIZE; k++) if (client_fds[k] == i) { sender_index = k; break; }

            // Use global nickname table

            if (strncmp(buffer, "/nick ", 6) == 0) {
                char *nick = buffer + 6;
                // trim CRLF
                int L = (int)strlen(nick);
                while (L > 0 && (nick[L-1] == '\n' || nick[L-1] == '\r')) { nick[--L] = '\0'; }
                if (sender_index >= 0) {
                    strncpy(g_nicks[sender_index], nick, sizeof(g_nicks[sender_index]) - 1);
                    g_nicks[sender_index][sizeof(g_nicks[sender_index]) - 1] = '\0';
                    char ack[160];
                    snprintf(ack, sizeof(ack), "*[System] Username set to %s*", g_nicks[sender_index][0] ? g_nicks[sender_index] : "(empty)");
                    send_crlf(i, ack);
                }
                continue;
            }

            if (strncmp(buffer, "/help", 5) == 0) {
                const char *help =
                    "Available commands:\n"
                    "/help                - Show this help\n"
                    "/nick <name>         - Set your nickname\n"
                    "/who                 - List connected users\n"
                    "/me <action>         - Emote, e.g. \/me waves\n"
                    "/ping                - Ping the server\n"
                    "/debug <level>       - Set log level (ERROR|WARN|INFO|DEBUG)\n"
                    "/quit                - Disconnect";
                send_crlf(i, help);
                continue;
            }

            if (strncmp(buffer, "/who", 4) == 0) {
                char out[1024];
                snprintf(out, sizeof(out), "Users online:");
                send_crlf(i, out);
                for (int k = 0; k < FD_SETSIZE; k++) {
                    if (client_fds[k] != -1) {
                        char line[256];
                        if (g_nicks[k][0]) snprintf(line, sizeof(line), " - %s", g_nicks[k]);
                        else snprintf(line, sizeof(line), " - Client %d", client_fds[k]);
                        send_crlf(i, line);
                    }
                }
                continue;
            }

            if (strncmp(buffer, "/me ", 4) == 0) {
                char action[256];
                strncpy(action, buffer + 4, sizeof(action) - 1);
                action[sizeof(action) - 1] = '\0';
                int Lm = (int)strlen(action);
                while (Lm > 0 && (action[Lm-1] == '\n' || action[Lm-1] == '\r')) { action[--Lm] = '\0'; }
                char namebuf[64];
                if (sender_index >= 0 && g_nicks[sender_index][0]) snprintf(namebuf, sizeof(namebuf), "%s", g_nicks[sender_index]);
                else snprintf(namebuf, sizeof(namebuf), "Client %d", i);
                char msg[512];
                snprintf(msg, sizeof(msg), "*%s %s*", namebuf, action);
                for (int k = 0; k < FD_SETSIZE; k++) {
                    int fd = client_fds[k];
                    if (fd != -1 && fd != i) send_crlf(fd, msg);
                }
                continue;
            }

            if (strncmp(buffer, "/ping", 5) == 0) {
                send_crlf(i, "PONG");
                continue;
            }

            if (strncmp(buffer, "/debug ", 7) == 0) {
                char *lvl = buffer + 7;
                int Ld = (int)strlen(lvl);
                while (Ld > 0 && (lvl[Ld-1] == '\n' || lvl[Ld-1] == '\r')) { lvl[--Ld] = '\0'; }
                g_log_level = parse_level(lvl);
                char ack[128];
                snprintf(ack, sizeof(ack), "*[System] Log level set to %s*", level_to_string(g_log_level));
                send_crlf(i, ack);
                continue;
            }

            if (strncmp(buffer, "/quit", 5) == 0) {
                close(i);
                FD_CLR(i, &read_fds);
                for (int k = 0; k < FD_SETSIZE; k++) if (client_fds[k] == i) client_fds[k] = -1;
                continue;
            }

            // Any other leading '/' commands are handled locally and not broadcast
            if (buffer[0] == '/') {
                char reply[128];
                // trim message to avoid echoing CRLF in the reply
                int L2 = (int)strlen(buffer);
                while (L2 > 0 && (buffer[L2-1] == '\n' || buffer[L2-1] == '\r')) { buffer[--L2] = '\0'; }
                snprintf(reply, sizeof(reply), "*[System] Unknown command: %s*", buffer);
                send_crlf(i, reply);
                continue;
            }

            // Trim message
            int L = (int)strlen(buffer);
            while (L > 0 && (buffer[L-1] == '\n' || buffer[L-1] == '\r')) { buffer[--L] = '\0'; }

            char namebuf[64];
            if (sender_index >= 0 && g_nicks[sender_index][0]) snprintf(namebuf, sizeof(namebuf), "%s", g_nicks[sender_index]);
            else snprintf(namebuf, sizeof(namebuf), "Client %d", i);

            log_msg(LOG_INFO, "%s: %s", namebuf, buffer);

            char out[512];
            snprintf(out, sizeof(out), "%s: %s", namebuf, buffer);
            for (int k = 0; k < FD_SETSIZE; k++) {
                int fd = client_fds[k];
                if (fd != -1 && fd != i) {
                    send_crlf(fd, out);
                }
            }
        }
    }

    close(server_socket_fd);
    return 0;
}