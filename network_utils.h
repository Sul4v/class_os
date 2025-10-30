#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <stddef.h>

int send_all(int sockfd, const void *buffer, size_t length);
int recv_all(int sockfd, void *buffer, size_t length);
int send_message(int sockfd, const char *data, size_t length);
int recv_message(int sockfd, char **data, size_t *length);

#endif /* NETWORK_UTILS_H */
