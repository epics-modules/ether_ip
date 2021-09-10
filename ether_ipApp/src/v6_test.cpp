/** IPv6 Demo
 *
 *  Kay Kasemir
 */
 
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, const char **argv)
{
    printf("IPv6 Demo\n");
    if (argc != 2)
    {
        printf("USAGE: v6_test IP_address\n");
        printf("\n");
        printf("As 'server', run either\n");
        printf("    nc -4 -l 127.0.0.1 44818\n");
        printf("or\n");
        printf("    nc -6 -l ::1 44818\n");
        printf("\n");
        printf("Then try this with IP_address of either 127.0.0.1 or ::1\n");
        return 0;
    }

    const char *address_spec = argv[1];
    unsigned short port = 44818;

    struct addrinfo hints = {};
    hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICSERV;
    hints.ai_family = strchr(address_spec, ':') == NULL
                    ? AF_INET
                    : AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char szPort[6];
    sprintf(szPort, "%hu", port);

    // See https://stackoverflow.com/questions/57784632/how-to-create-sockaddr-from-sockaddr-in-sockaddr-in6-structure
    struct addrinfo *addr;
    if (getaddrinfo(address_spec, szPort, &hints, &addr) != 0)
    {
        printf("Cannot resolve address\n");
        return 0;
    }
    if (addr->ai_family == AF_INET)
    {
        struct sockaddr_in *in = (struct sockaddr_in *) addr->ai_addr;
        char buf[100];
        printf("Address: IPv4 %s\n", inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf)));
    }
    else if (addr->ai_family == AF_INET6)
    {
        struct sockaddr_in6 *in = (struct sockaddr_in6 *) addr->ai_addr;
        char buf[100];
        printf("Address: IPv6 %s\n", inet_ntop(AF_INET6, &in->sin6_addr, buf, sizeof(buf)));
    }
    else
    {
        freeaddrinfo(addr);
        printf("(?)\n");
        return 0;
    }

    int s = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (s == -1)
    {
        freeaddrinfo(addr);
        printf("No socket\n");
        return 0;
    }

    if (connect(s, addr->ai_addr, addr->ai_addrlen) != 0)
    {
        freeaddrinfo(addr);
        perror("Cannot connect");
        return 0;
    }
    freeaddrinfo(addr);


    send(s, "Hello!\n", 7, 0);

    close(s);


    return 0;
}
