#include "stdio.h"

#include "log.h"
#include "sys/socket.h"
#include "sys/types.h"
#include "string.h"
#include "stdint.h"


void start_telemetry_client(const char* telemetry_ip, const int telemetry_port);

static void start_http_server(const char* ip, const int port);


int main(int argc, char* argv[])
{

    if (argc < 2)
    {
        printf("Must supply IP of telemetry node\n");
        return 0;
    }

    char* telemetry_ip = argv[1];
    int telemetry_port = 80;

    start_telemetry_client(telemetry_ip, telemetry_port);
    //start_http_server("127.0.0.1", 9090);

    return 0;
}

#include "arpa/inet.h"
#include "unistd.h"
#include "errno.h"
#include "stdbool.h"

static void start_http_server(const char* ip, const int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        printf("Failed to open TCP socket\n");
        return;
    }

    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    bool is_running = true;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);//inet_addr(ip);
    addr.sin_port = htons(port);

    int res = bind(sockfd, (struct sockaddr*) &addr, sizeof(addr));
    if (res == -1)
    {
        printf("Failed to bind server socket on: %s:%d, error: %d\n",
               ip, port, errno);
        return;
    }

    res = listen(sockfd, 0);
    if (res == -1)
    {
        printf("Failed to listen server socket on: %s:%d, error: %d\n",
               ip, port, errno);
        return;
    }

    printf("Starting HTTP server on %s:%d\n", ip, port);
    while (is_running)
    {
        struct sockaddr_in client;
        int len = sizeof(client);
        int client_fd = accept(sockfd, (struct sockaddr*) &client, &len);

        // Handle client
        char http_buf[2048];
        int bytes_read = 0;
        int read_res;
        char prev1 = 0;
        char prev2 = 0;

        printf("New client received!\n");

        while (1)
        {
            char c;
            read_res = read(client_fd, &c, 1);

            printf("RES: %d\n", read_res);
            fflush(stdout);

            if (read_res == 0)
            {
                printf("%s", http_buf);
                break;
            }
            else if (read_res == -1)
            {
                printf("Error reading: %d\n", errno);
                break;
            }
            else
            {
                printf("c: %c, prev1: %c, prev2: %c\n", c, prev1, prev2);
                if (c == '\n' && (prev1 == '\n' && prev2 == '\n'))
                {
                    printf("%s", http_buf);
                    // Done!
                    char http_response[2048];
                    char* header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                    char* body = "<html><h1>OK</h1></html>\r\n\r\n";

                    int header_len = strlen(header);
                    int body_len = strlen(body);

                    memcpy(http_response, header, header_len);
                    memcpy(&http_response[header_len], body, body_len);

                    int response_len = header_len + body_len;
                    printf("Writing: %d bytes:\n", response_len);
                    for (int i = 0; i < response_len; i++)
                    {
                        putchar(http_response[i]);
                    }

                    int bytes_written = send(client_fd, http_response, response_len, 0);
                    printf("Bytes written: %d\n", bytes_written);
                    close(client_fd);


                    bytes_read = 0;
                    prev1 = 0;
                    prev2 = 0;

                    break;
                }
                else
                {
                    http_buf[bytes_read++] = c;
                    prev2 = prev1;
                    prev1 = c;
                }
            }

        }
    }

}


static int connect_to_client(const char* telemetry_ip, const int telemetry_port);

void start_telemetry_client(const char* telemetry_ip, const int telemetry_port)
{
    //printf("Telemetry client started\n");

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