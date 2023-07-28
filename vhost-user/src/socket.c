// provoide API to create a service and wait for connection
/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include "vu_socket.h"

static int log_info = 1;
static int log_debug;

#define pr_info(fmt, args...) do {if (log_info)  printf(fmt, ##args);} while(0)
#define pr_debug(fmt, args...) do {if (log_debug)  printf(fmt, ##args);} while(0)
#define pr_err(fmt, args...) do { printf(fmt, ##args);} while(0)

/*
 * return bytes# of read on success or negative val on failure. Update fdnum
 * with number of fds read.
 */
int
read_fd_message(int sockfd, char *buf, int buflen, int *fds, int max_fds, int *fd_num)
{
    struct iovec iov;
    struct msghdr msgh;
    char control[CMSG_SPACE(max_fds * sizeof(int))];
    struct cmsghdr *cmsg;
    int got_fds = 0;
    int ret;

    *fd_num = 0;

    memset(&msgh, 0, sizeof(msgh));
    iov.iov_base = buf;
    iov.iov_len  = buflen;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = control;
    msgh.msg_controllen = sizeof(control);

    pr_debug("wait for message..\n");
    ret = recvmsg(sockfd, &msgh, 0);
    if (ret < 0) {
        pr_err("recvmsg failed on fd %d (%s)\n",
                sockfd, strerror(errno));
        return ret;
    }

    if (ret == 0)
        return ret;

    if (msgh.msg_flags & MSG_TRUNC)
        pr_err("truncated msg (fd %d)\n", sockfd);

    /* MSG_CTRUNC may be caused by LSM misconfiguration */
    if (msgh.msg_flags & MSG_CTRUNC)
        pr_err("truncated control data (fd %d)\n", sockfd);

    for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL;
        cmsg = CMSG_NXTHDR(&msgh, cmsg)) {
        if ((cmsg->cmsg_level == SOL_SOCKET) &&
            (cmsg->cmsg_type == SCM_RIGHTS)) {
            got_fds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            *fd_num = got_fds;
            memcpy(fds, CMSG_DATA(cmsg), got_fds * sizeof(int));
            break;
        }
    }

    /* Clear out unused file descriptors */
    while (got_fds < max_fds)
        fds[got_fds++] = -1;

    return ret;
}

int
send_fd_message(int sockfd, char *buf, int buflen, int *fds, int fd_num)
{

    struct iovec iov;
    struct msghdr msgh;
    size_t fdsize = fd_num * sizeof(int);
    char control[CMSG_SPACE(fdsize)];
    struct cmsghdr *cmsg;
    int ret;

    memset(&msgh, 0, sizeof(msgh));
    iov.iov_base = buf;
    iov.iov_len = buflen;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;

    if (fds && fd_num > 0) {
        msgh.msg_control = control;
        msgh.msg_controllen = sizeof(control);
        cmsg = CMSG_FIRSTHDR(&msgh);
        if (cmsg == NULL) {
            pr_err("cmsg == NULL\n");
            errno = EINVAL;
            return -1;
        }
        cmsg->cmsg_len = CMSG_LEN(fdsize);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsg), fds, fdsize);
    } else {
        msgh.msg_control = NULL;
        msgh.msg_controllen = 0;
    }

    do {
        ret = sendmsg(sockfd, &msgh, MSG_NOSIGNAL);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        pr_err("sendmsg error on fd %d (%s)\n",
            sockfd, strerror(errno));
        return ret;
    }

    return ret;
}

int
create_unix_socket(struct vhost_user_socket *vsocket, char *socket_path)
{
    int fd;
    struct sockaddr_un *un = &vsocket->un;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    unlink(socket_path);
    memset(un, 0, sizeof(*un));
    un->sun_family = AF_UNIX;
    strlcpy(un->sun_path, socket_path, sizeof(un->sun_path));
    un->sun_path[sizeof(un->sun_path) - 1] = '\0';

    vsocket->socket_fd = fd;
    return 0;
}

int
vhost_user_start_server(struct vhost_user_socket *vsocket)
{
    int ret;
    int fd = vsocket->socket_fd;

    /*
     * bind () may fail if the socket file with the same name already
     * exists. But the library obviously should not delete the file
     * provided by the user, since we can not be sure that it is not
     * being used by other applications. Moreover, many applications form
     * socket names based on user input, which is prone to errors.
     *
     * The user must ensure that the socket does not exist before
     * registering the vhost driver in server mode.
     */
    ret = bind(fd, (struct sockaddr *)&vsocket->un, sizeof(vsocket->un));
    if (ret < 0) {
        pr_err("failed to bind: %s; remove it and try again\n",
            strerror(errno));
        goto err;
    }
    pr_debug("binding succeeded\n");

    ret = listen(fd, 1);
    if (ret < 0)
        goto err;

    fd = accept(fd, NULL, NULL);
    if (fd < 0)
        return -1;

    pr_debug("new connection setup\n");
    vsocket->socket_fd = fd;
    return 0;

err:
    close(fd);
    return -1;
}
