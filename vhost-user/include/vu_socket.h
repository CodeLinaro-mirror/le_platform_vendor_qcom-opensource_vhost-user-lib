/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef SOCKET_H
#define SOCKET_H
#include <sys/socket.h>
#include <sys/un.h>

struct vhost_user_socket {
    int socket_fd;
    struct sockaddr_un un;
};
int send_fd_message(int sockfd, char *buf, int buflen, int *fds, int fd_num);
int read_fd_message(int sockfd, char *buf, int buflen, int *fds, int max_fds, int *fd_num);
int create_unix_socket(struct vhost_user_socket *vsocket, char *socket_path);
int vhost_user_start_server(struct vhost_user_socket *vsocket);

#endif
