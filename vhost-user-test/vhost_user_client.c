/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <vhost_user.h>


int receive_reply(int sockfd, char *buf, int buflen)
{
    struct iovec iov;
    struct msghdr msgh;

    memset(&msgh, 0, sizeof(msgh));
    iov.iov_base = buf;
    iov.iov_len  = buflen;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;

    return recvmsg(sockfd, &msgh, 0);
}


static int set_owner(int c)
{

    VhostUserMsg vmsg = {
        .request = VHOST_USER_SET_OWNER,
        .flags = VHOST_USER_VERSION,
        .size = 0
    };

    struct iovec iov = {
        .iov_base = (char *)&vmsg,
        .iov_len = 12,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0
    };

    return sendmsg(c, &msg, 0);
}

static int get_features(int fd)
{
    VhostUserMsg rmsg;
    uint64_t payload;

    VhostUserMsg vmsg = {
        .request = VHOST_USER_GET_FEATURES,
        .flags = VHOST_USER_VERSION,
        .size = 8
    };

    struct iovec iov = {
        .iov_base = (char *)&vmsg,
        .iov_len = VHOST_USER_HDR_SIZE + sizeof(payload),
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0
    };

    if (sendmsg(fd, &msg, 0) <0) {
        printf("send message failed\n");
        return -1;
    }
    if (receive_reply(fd, (char *)&rmsg, VHOST_USER_HDR_SIZE) != VHOST_USER_HDR_SIZE) {
        printf("failed to receive\n");
        return -1;
    }
    if (rmsg.request.backend != VHOST_USER_GET_FEATURES) {
        printf("the message format is wrong\n");
        return -1;
    }
    if (read(fd, &payload, sizeof(payload)) != sizeof(payload)) {
        printf("failed to read payload\n");
        return -1;
    }
    return 0;
}

static int set_features(int fd)
{
    VhostUserMsg vmsg = {
        .request = VHOST_USER_SET_FEATURES,
        .flags = VHOST_USER_VERSION,
        .payload.u64 = 0x100000000UL,
        .size = 8
    };

    struct iovec iov = {
        .iov_base = (char *)&vmsg,
        .iov_len = VHOST_USER_HDR_SIZE + sizeof(vmsg.payload.u64),
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0
    };

    if (sendmsg(fd, &msg, 0) <0) {
        printf("send message failed\n");
        return -1;
    }
    return 0;
}

static int set_vring_num(int fd)
{
    VhostUserMsg vmsg = {
        .request = VHOST_USER_SET_VRING_NUM,
        .flags = VHOST_USER_VERSION,
        .payload.state.index = 0,
        .payload.state.num = 256,
        .size = sizeof(vmsg.payload.state)
    };

    struct iovec iov = {
        .iov_base = (char *)&vmsg,
        .iov_len = VHOST_USER_HDR_SIZE + sizeof(vmsg.payload.state),
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0
    };

    if (sendmsg(fd, &msg, 0) <0) {
        printf("send message failed\n");
        return -1;
    }
    return 0;
}

static int set_vring_base(int fd)
{
    VhostUserMsg vmsg = {
        .request = VHOST_USER_SET_VRING_BASE,
        .flags = VHOST_USER_VERSION,
        .payload.state.index = 0,
        .payload.state.num = 0,
        .size = sizeof(vmsg.payload.state)
    };

    struct iovec iov = {
        .iov_base = (char *)&vmsg,
        .iov_len = VHOST_USER_HDR_SIZE + sizeof(vmsg.payload.state),
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0
    };

    if (sendmsg(fd, &msg, 0) <0) {
        printf("send message failed\n");
        return -1;
    }
    return 0;
}

static int get_vring_base(int fd)
{
    VhostUserMsg rmsg;
    struct vhost_vring_state vstate;

    VhostUserMsg vmsg = {
        .request = VHOST_USER_GET_VRING_BASE,
        .flags = VHOST_USER_VERSION,
        .payload.state.index = 0,
        .size = sizeof(vmsg.payload.state)
    };

    struct iovec iov = {
        .iov_base = (char *)&vmsg,
        .iov_len = VHOST_USER_HDR_SIZE + sizeof(vmsg.payload.state),
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0
    };

    if (sendmsg(fd, &msg, 0) <0) {
        printf("send message failed\n");
        return -1;
    }
    if (receive_reply(fd, (char *)&rmsg, VHOST_USER_HDR_SIZE) != VHOST_USER_HDR_SIZE) {
        printf("failed to receive\n");
        return -1;
    }
    if (rmsg.request.backend != VHOST_USER_GET_VRING_BASE) {
        printf("the message format is wrong\n");
        return -1;
    }
    if (read(fd, &vstate, sizeof(vstate)) != sizeof(vstate)) {
        printf("failed to read payload\n");
        return -1;
    }
    return 0;
}

int s[2];
static int set_kick(int c)
{
    int fds[8];
    size_t fd_num = 0;
    VhostUserMsg vmsg = {
        .request = VHOST_USER_SET_VRING_KICK,
        .flags = VHOST_USER_VERSION,
        .payload.u64 = 0x0 & VHOST_USER_VRING_IDX_MASK,
        .size = sizeof(vmsg.payload.u64),
    };
    if (socketpair(AF_UNIX,SOCK_STREAM,0,s) == -1 ){
        printf("failed to create socketpair\n");
        return -1;
    }
    fds[fd_num++] = s[1];

    struct iovec iov = {
        .iov_base = (char *)&vmsg,
        .iov_len = VHOST_USER_HDR_SIZE + vmsg.size,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };

    char control[CMSG_SPACE(sizeof(int) * 8)] = { 0 };
    size_t fdsize = sizeof(int) * fd_num;
    struct cmsghdr *cmsg;
    if (fd_num > 0) {
        msg.msg_control = control;
        msg.msg_controllen = CMSG_SPACE(fdsize);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(fdsize);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsg), fds, fdsize);
    }

    if (sendmsg(c, &msg, 0) < 0) {
        printf("failed to send message\n");
        return -1;
    }
    return 0;
}

int run_vhost_user_client(char *socket_path) {
    int sock;
    struct sockaddr_un  sock_addr;

    if (!socket_path) {
        printf("please provide socket path\n");
        return -1;
    }

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sun_family = AF_UNIX;
    strlcpy(sock_addr.sun_path, (char *)socket_path, sizeof(sock_addr.sun_path));
    printf("sock path = %s\n", sock_addr.sun_path);
    sock_addr.sun_path[sizeof(sock_addr.sun_path) - 1] = '\0';

    if (connect(sock, (struct sockaddr *)&sock_addr, sizeof(struct sockaddr_un))<0) {
        printf("connect failed\n");
        return -1;
    }
    printf("connect ok\n");

    usleep(2000000);
    for (int i = 0; i < 1; i++) {
        if (get_features(sock) <0)
            printf("get features FAILED\n");
        else
            printf("<get features PASS>\n");

        if (set_features(sock) <0)
            printf("set features FAILED\n");
        else
            printf("<set features PASS>\n");

        if (set_owner(sock) < 0)
            printf("set owner FAILED\n");
        else
            printf("<set owner PASS>\n");

        if (set_vring_num(sock) < 0)
            printf("set vring num FAILED\n");
        else
            printf("<set vring num PASS>\n");

        if (set_vring_base(sock) < 0)
            printf("set vring base FAILED\n");
        else
            printf("<set vring base PASS>\n");

        /*
        if (set_kick(sock) < 0)
            printf("set vring kick FAILED\n");
        else
            printf("<set vring kick PASS>\n");
        */
        if (get_vring_base(sock) < 0)
            printf("get vring base FAILED\n");
        else
            printf("<get vring base PASS>\n");

        usleep(1000000);
        close(s[0]);
        close(s[1]);
    }
    close(sock);
    return 0;
}
