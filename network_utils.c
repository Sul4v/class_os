/* Tiny framing helpers shared by the client and server. */
#include "network_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Write exactly 'length' bytes unless the socket errors out. */
int send_all(int sockfd, const void *buffer, size_t length) {
    const char *ptr = (const char *)buffer;
    size_t sent = 0;
    
    while (sent < length) {
        ssize_t bytes = send(sockfd, ptr + sent, length - sent, 0);
        if (bytes <= 0) {
            if (bytes < 0 && errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)bytes;
    }
    
    return 0;
}

/* Read exactly 'length' bytes, looping on EINTR like send_all. */
int recv_all(int sockfd, void *buffer, size_t length) {
    char *ptr = (char *)buffer;
    size_t received = 0;
    
    while (received < length) {
        ssize_t bytes = recv(sockfd, ptr + received, length - received, 0);
        if (bytes <= 0) {
            if (bytes < 0 && errno == EINTR) {
                continue;
            }
            return -1;
        }
        received += (size_t)bytes;
    }
    
    return 0;
}

/* Prepend a 32-bit length prefix before sending the payload. */
int send_message(int sockfd, const char *data, size_t length) {
    uint32_t net_length = htonl((uint32_t)length);
    
    if (send_all(sockfd, &net_length, sizeof(net_length)) == -1) {
        return -1;
    }
    
    if (length == 0) {
        return 0;
    }
    
    return send_all(sockfd, data, length);
}

/* Read a framed message, allocate a NUL-terminated buffer, and report its size. */
int recv_message(int sockfd, char **data, size_t *length) {
    uint32_t net_length;
    
    if (recv_all(sockfd, &net_length, sizeof(net_length)) == -1) {
        return -1;
    }
    
    uint32_t payload_length = ntohl(net_length);
    char *buffer = malloc(payload_length + 1);
    if (!buffer) {
        return -1;
    }
    
    if (payload_length > 0) {
        if (recv_all(sockfd, buffer, payload_length) == -1) {
            free(buffer);
            return -1;
        }
    }
    
    buffer[payload_length] = '\0';
    *data = buffer;
    if (length) {
        *length = payload_length;
    }
    
    return 0;
}
