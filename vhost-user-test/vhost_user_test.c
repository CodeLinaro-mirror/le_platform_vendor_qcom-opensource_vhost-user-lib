/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <getopt.h>
#include <stdio.h>

extern int run_vhost_user_server(char *socket_path);
extern int run_vhost_user_client(char *socket_path);
void
usage(void)
{
    printf("Usage: vhost-user -p <socket_path> < -s >/ < -c > \n"
            "-s: vhost user server\n"
            "-c: vhost user client\n"
            "-p: socket file path\n");
}

int main(int argc, char **argv)
{
    // Create a new device
    char *socket_path = NULL;
    int is_server = 0;
    int opt;

    while ((opt = getopt(argc, argv, "p:hsc")) != -1) {
        switch (opt) {
            case 'p':
                socket_path = optarg;
                printf("socket_path = %s\n", socket_path);
                break;
            case 's':
                is_server = 1;
                break;
            case 'c':
                is_server = 0;
                break;
            case 'h':
            default:
                usage();
                return(-1);
        }
    }
    if (!socket_path) {
        usage();
    }
    if (is_server)
        return run_vhost_user_server(socket_path);
    else
        return run_vhost_user_client(socket_path);
}

