/*
 * Copyright 2014-2016 CyberVision, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#include <esp_libc.h>

#include "../../platform/sock.h"
#include "../../platform/ext_tcp_utils.h"
#include "../../kaa_common.h"

#include <lwip/lwip/netdb.h>

kaa_error_t ext_tcp_utils_open_tcp_socket(kaa_fd_t *fd,
                                            const kaa_sockaddr_t *destination,
                                            kaa_socklen_t destination_size)
{
    KAA_RETURN_IF_NIL3(fd, destination, destination_size, KAA_ERR_BADPARAM);
    kaa_fd_t sock = socket(destination->sa_family, SOCK_STREAM, 0);

    int flags = lwip_fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        ext_tcp_utils_tcp_socket_close(sock);
        printf("Error getting sicket access flags\n");
        return KAA_ERR_SOCKET_ERROR;
    }

    if (lwip_fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        ext_tcp_utils_tcp_socket_close(sock);
        printf("Error setting socket flags. fcntl(F_GETFL): %d\n", lwip_fcntl(sock, F_GETFL, 0));
        return KAA_ERR_SOCKET_ERROR;
    }

    if (connect(sock, destination, destination_size) && errno != EINPROGRESS) {
        ext_tcp_utils_tcp_socket_close(sock);
        printf("Error connecting to destination\n");
        return KAA_ERR_SOCKET_CONNECT_ERROR;
    }

    *fd = sock;

    return KAA_ERR_NONE;
}

ext_tcp_utils_function_return_state_t ext_tcp_utils_getaddrbyhost(kaa_dns_resolve_listener_t *resolve_listener
                                                                , const kaa_dns_resolve_info_t *resolve_props
                                                                , kaa_sockaddr_t *result
                                                                , kaa_socklen_t *result_size)
{
    (void)resolve_listener;
    KAA_RETURN_IF_NIL4(resolve_props, resolve_props->hostname, result, result_size, RET_STATE_VALUE_ERROR);
    if (*result_size < sizeof(struct sockaddr_in))
        return RET_STATE_BUFFER_NOT_ENOUGH;

    struct addrinfo hints;
    memset(&hints, 0 , sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    /* LWIP does not support ipv6, so sockaddr_in6 is not even defined in lwip/sockets.h */
    /* if (*result_size < sizeof(struct sockaddr_in6)) */
    hints.ai_family = AF_INET;

    char hostname_str[resolve_props->hostname_length + 1];
    memcpy(hostname_str, resolve_props->hostname, resolve_props->hostname_length);
    hostname_str[resolve_props->hostname_length] = '\0';

    struct addrinfo *resolve_result = NULL;
    int resolve_error = 0;

    if (resolve_props->port) {
        char port_str[6];
        snprintf(port_str, 6, "%u", resolve_props->port);
        resolve_error = getaddrinfo(hostname_str, port_str, &hints, &resolve_result);
    } else {
        resolve_error = getaddrinfo(hostname_str, NULL, &hints, &resolve_result);
    }

    if (resolve_error || !resolve_result)
        return RET_STATE_VALUE_ERROR;

    if (resolve_result->ai_addrlen > *result_size) {
        freeaddrinfo(resolve_result);
        return RET_STATE_BUFFER_NOT_ENOUGH;
    }

    memcpy(result, resolve_result->ai_addr, resolve_result->ai_addrlen);
    *result_size = resolve_result->ai_addrlen;
    freeaddrinfo(resolve_result);
    return RET_STATE_VALUE_READY;
}

ext_tcp_socket_state_t ext_tcp_utils_tcp_socket_check(kaa_fd_t fd
                                                    , const kaa_sockaddr_t *destination
                                                    , kaa_socklen_t destination_size)
{
    if (connect(fd, destination, destination_size) < 0 ) {
        switch (errno) {
            case EINPROGRESS:
            case EALREADY:
                return KAA_TCP_SOCK_CONNECTING;
            case EISCONN:
                return KAA_TCP_SOCK_CONNECTED;
            default:
                return KAA_TCP_SOCK_ERROR;
        }
    }
    return KAA_TCP_SOCK_CONNECTED;
}

ext_tcp_socket_io_errors_t ext_tcp_utils_tcp_socket_write(kaa_fd_t fd
                                                        , const char *buffer
                                                        , size_t buffer_size
                                                        , size_t *bytes_written)
{
    KAA_RETURN_IF_NIL2(buffer, buffer_size, KAA_TCP_SOCK_IO_ERROR);
    ssize_t write_result = write(fd, buffer, buffer_size);
    if (write_result < 0 && errno != EAGAIN)
        return KAA_TCP_SOCK_IO_ERROR;
    if (bytes_written)
        *bytes_written = (write_result > 0) ? write_result : 0;
    return KAA_TCP_SOCK_IO_OK;
}



ext_tcp_socket_io_errors_t ext_tcp_utils_tcp_socket_read(kaa_fd_t fd
                                                       , char *buffer
                                                       , size_t buffer_size
                                                       , size_t *bytes_read)
{
    KAA_RETURN_IF_NIL2(buffer, buffer_size, KAA_TCP_SOCK_IO_ERROR);
    ssize_t read_result = read(fd, buffer, buffer_size);
    if (!read_result)
        return KAA_TCP_SOCK_IO_EOF;
    if (read_result < 0 && errno != EAGAIN)
        return KAA_TCP_SOCK_IO_ERROR;
    if (bytes_read)
        *bytes_read = (read_result > 0) ? read_result : 0;
    return KAA_TCP_SOCK_IO_OK;
}

kaa_error_t ext_tcp_utils_tcp_socket_close(kaa_fd_t fd)
{
    return (close(fd) < 0) ? KAA_ERR_SOCKET_ERROR : KAA_ERR_NONE;
}
