#include "stdio.h"

#include "log.h"
#include "sys/socket.h"
#include "sys/types.h"
#include "string.h"
#include "stdint.h"


void start_telemetry_client(const char* telemetry_ip, const int telemetry_port);


int main(int argc, char* argv[])
{
    printf("Telemetry client started\n");

    if (argc < 2)
    {
        printf("Must supply IP of telemetry node\n");
        return 0;
    }

    char* telemetry_ip = argv[1];
    int telemetry_port = 80;

    start_telemetry_client(telemetry_ip, telemetry_port);

    return 0;
}

#include "arpa/inet.h"
#include "unistd.h"
#include "errno.h"
#include "stdbool.h"


static int connect_to_client(const char* telemetry_ip, const int telemetry_port);

void start_telemetry_client(const char* telemetry_ip, const int telemetry_port)
{
    int sockfd = connect_to_client(telemetry_ip, telemetry_port);
    if (sockfd == -1)
    {
        return;
    }

    uint8_t buf;
    int res;
    while (1)
    {
        res = read(sockfd, &buf, 1);
        if (res == -1)
        {    // Probably disconnected?
            printf("Read error: %d\n", errno);
            close(sockfd);
            sockfd = connect_to_client(telemetry_ip, telemetry_port);
        }
        else if (res == 0)
        {
            printf("Read EOF\n");
            close(sockfd);
            sockfd = connect_to_client(telemetry_ip, telemetry_port);
        }
        else
        {
            putchar(buf);
            fflush(stdout);
        }
    }

}

static int connect_to_client(const char* telemetry_ip, const int telemetry_port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        printf("Failed to open TCP socket\n");
        return -1;
    }

    printf("Created TCP socket, trying to connect to client: %s:%d\n",
            telemetry_ip, telemetry_port);

    int retry_delay_s = 1;
    bool connected = false;
    int attempts = 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(telemetry_ip);
    addr.sin_port = htons(telemetry_port);

    while (!connected)
    {
        if (connect(sockfd, (struct sockaddr*) &addr, sizeof(addr)) == 0)
        {
            connected = true;
        }

        //printf("Connected to client on %s:%d\n", telemetry_ip, telemetry_port);
        //printf("\r");
        printf("\rAttempt: %d ", ++attempts);
        fflush(stdout);
        sleep(retry_delay_s);
    }

    printf("\nConnected!\n");

    return sockfd;
}