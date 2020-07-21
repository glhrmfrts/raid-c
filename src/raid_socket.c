#include <stdio.h>
#include "raid.h"
#include "raid_internal.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#endif

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")

static raid_error_t socket_impl_connect(raid_socket_t* s)
{
    int ret = 0;
    int conn_fd;

    static WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Translate the human-readable address to a network binary address.
    struct addrinfo* addr_info = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ret = getaddrinfo(s->host, s->port, &hints, &addr_info);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo failed with error: %d", ret);
        return RAID_INVALID_ADDRESS;
    }

    // Create the socket.
    s->handle = socket(AF_INET, SOCK_STREAM, 0);
    if (s->handle == -1) {
        fprintf(stderr, "error creating socket");
        return RAID_SOCKET_ERROR;
    }

    // Connect to the first address we found.
    struct sockaddr* server_addr = addr_info->ai_addr;
    ret = connect(s->handle, server_addr, sizeof(struct sockaddr));
    if (ret == -1) {
        fprintf(stderr, "error connecting to: %s", s->host);
        freeaddrinfo(addr_info);
        return RAID_CONNECT_ERROR;
    }

    // Set the socket recv timeout
    const int kSocketTimeoutSeconds = 10*1000;
    struct timeval tv;
    tv.tv_sec = kSocketTimeoutSeconds;
    setsockopt(s->handle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    freeaddrinfo(addr_info);
    return RAID_SUCCESS;
}

static raid_error_t socket_impl_send(raid_socket_t* s, const char* data, size_t data_len)
{
    int nwrite = send(s->handle, data, data_len, 0);
    return RAID_SUCCESS;
}

static raid_error_t socket_impl_recv(raid_socket_t* s, char* buf, size_t buf_len, int* out_len)
{
    *out_len = recv(s->handle, buf, buf_len, 0);
    int err = WSAGetLastError();
    if (errno == EWOULDBLOCK || errno == EAGAIN || err == WSAETIMEDOUT) {
        return RAID_RECV_TIMEOUT;
    }
    if (err == WSAECONNRESET || err == WSAEINTR) {
        return RAID_NOT_CONNECTED;
    }
    if (*out_len < 0) {
        wprintf(L"recv failed with error: %d\n", err);
        return RAID_UNKNOWN;
    }
    return RAID_SUCCESS;
}

static raid_error_t socket_impl_disconnect(raid_socket_t* s)
{
    int ret;

    ret = shutdown(s->handle, SD_BOTH);
    if (ret == -1) {
        return RAID_SHUTDOWN_ERROR;
    }

    ret = closesocket(s->handle);
    if (ret == -1) {
        return RAID_CLOSE_ERROR;
    }

    s->handle = -1;
    return RAID_SUCCESS;
}

#else

static raid_error_t socket_impl_connect(raid_socket_t* s)
{
    int ret = 0;

    // Translate the human-readable address to a network binary address.
    struct addrinfo* addr_info = NULL;
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ret = getaddrinfo(s->host, s->port, &hints, &addr_info);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo failed with error: %d\n", ret);
        return RAID_INVALID_ADDRESS;
    }

    // Create the socket.
    s->handle = socket(AF_INET, SOCK_STREAM, 0);
    if ((int)s->handle == -1) {
        fprintf(stderr, "error creating socket\n");
        return RAID_SOCKET_ERROR;
    }

    // Connect to the first address we found.
    struct sockaddr* server_addr = addr_info->ai_addr;
    ret = connect((int)s->handle, server_addr, sizeof(struct sockaddr));
    if (ret == -1) {
        fprintf(stderr, "error connecting to: %s\n", s->host);
        freeaddrinfo(addr_info);
        return RAID_CONNECT_ERROR;
    }

    // Set the socket recv timeout
    const int kSocketTimeoutSeconds = 10;
    struct timeval tv = { 0 };
    tv.tv_sec = kSocketTimeoutSeconds;
    tv.tv_usec = 0;
    setsockopt((int)s->handle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    freeaddrinfo(addr_info);
    return RAID_SUCCESS;
}

static raid_error_t socket_impl_send(raid_socket_t* s, const char* data, size_t data_len)
{
    //printf("%d %d %d %d\n", data[0], data[1], data[2], data[3]);
    //printf("%s\n", data);
    //printf("%d\n", data_len);
    int nwrite = send((int)s->handle, data, data_len, 0);
    return (nwrite == data_len) ? RAID_SUCCESS : RAID_UNKNOWN;
}

static raid_error_t socket_impl_recv(raid_socket_t* s, char* buf, size_t buf_len, int* out_len)
{
    *out_len = recv((int)s->handle, buf, buf_len, 0);
    //printf("%d\n", *out_len);
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return RAID_RECV_TIMEOUT;
    }
    if (*out_len < 0) {
        fprintf(stderr, "recv failed with error: %d\n", errno);
        return RAID_UNKNOWN;
    }
    return RAID_SUCCESS;
}

static raid_error_t socket_impl_disconnect(raid_socket_t* s)
{
    int ret;

    ret = shutdown((int)s->handle, SHUT_RDWR);
    if (ret == -1) {
        return RAID_SHUTDOWN_ERROR;
    }

    ret = close(s->handle);
    if (ret == -1) {
        return RAID_CLOSE_ERROR;
    }

    s->handle = -1;
    return RAID_SUCCESS;
}

#endif

raid_error_t raid_socket_connect(raid_socket_t* s, const char* host, const char* port)
{
    s->host = strdup(host);
    s->port = strdup(port);
    return socket_impl_connect(s);
}

bool raid_socket_connected(raid_socket_t* s)
{
    return s->handle != -1;
}

raid_error_t raid_socket_send(raid_socket_t* s, const char* data, size_t data_len)
{
    return socket_impl_send(s, data, data_len);
}

raid_error_t raid_socket_recv(raid_socket_t* s, char* buf, size_t buf_len, int* out_len)
{
    return socket_impl_recv(s, buf, buf_len, out_len);
}

raid_error_t raid_socket_close(raid_socket_t* s)
{
    raid_error_t err = socket_impl_disconnect(s);
    free(s->host);
    free(s->port);
    return err;
}
