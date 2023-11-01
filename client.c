/*
 * James Langille
 * A01251664
 */

// Error Handling
#include <errno.h>

// Data Types and Limits
#include <inttypes.h>
#include <stdint.h>

// Network Programming
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

// Standard Library
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Macros
#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BASE_TEN 10
#define LINE_LENGTH 1024

// ----- Function Headers -----

// Argument Parsing
static void      parse_arguments(int argc, char *argv[], char **ip_address, char **port, char **command);
static void      handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, const char *command, in_port_t *port);
static in_port_t parse_in_port_t(const char *binary_name, const char *port_str);

// Error Handling
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);

// Network Handling
static void convert_address(const char *address, struct sockaddr_storage *addr);
static int  socket_create(int domain, int type, int protocol);
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void socket_close(int sockfd);
static void write_to_socket(int sockfd, const char *command);
static void read_from_socket(int sockfd);

int main(int argc, char *argv[])
{
    char                   *command;
    char                   *ip_address;
    char                   *port_str;
    in_port_t               port;
    int                     sockfd;
    struct sockaddr_storage addr;

    ip_address = NULL;
    command    = NULL;
    port_str   = NULL;

    // Set up client
    parse_arguments(argc, argv, &ip_address, &port_str, &command);
    handle_arguments(argv[0], ip_address, port_str, command, &port);
    convert_address(ip_address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    socket_connect(sockfd, &addr, port);

    write_to_socket(sockfd, command);
    read_from_socket(sockfd);
    return 0;
}

// ----- Function Definitions -----

// Argument Parsing Functions
static void parse_arguments(const int argc, char *argv[], char **ip_address, char **port, char **command)
{
    int opt;
    opterr = 0;

    // Option parsing
    while((opt = getopt(argc, argv, "h:")) != -1)
    {
        switch(opt)
        {
            case 'h':
            {
                usage(argv[0], EXIT_SUCCESS, NULL);
            }
            case '?':
            {
                char message[UNKNOWN_OPTION_MESSAGE_LEN];

                snprintf(message, sizeof(message), "Unknown option '-%c'.", optopt);
                usage(argv[0], EXIT_FAILURE, message);
            }
            default:
            {
                usage(argv[0], EXIT_FAILURE, NULL);
            }
        }
    }

    // Check for sufficient args
    if(argc < 4)
    {
        usage(argv[0], EXIT_FAILURE, "Error: Too few arguments.");
    }

    // Check for extra args
    if(argc > 4)
    {
        usage(argv[0], EXIT_FAILURE, "Error: Too many arguments.");
    }

    *ip_address = argv[optind];
    *port       = argv[optind + 1];
    *command    = argv[optind + 2];
}

static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, const char *command, in_port_t *port)
{
    if(ip_address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The ip address is required.");
    }

    if(port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    if(command == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The command is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

static in_port_t parse_in_port_t(const char *binary_name, const char *port_str)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(port_str, &endptr, BASE_TEN);

    // Check for errno was signalled
    if(errno != 0)
    {
        perror("Error parsing in_port_t.");
        exit(EXIT_FAILURE);
    }

    // Check for any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is within valid range of in_port_t
    if(parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

// Error Handling Functions

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] <ip address> <port> <command>\n", program_name);
    fputs("Options:\n", stderr);
    fputs(" -h Display this help message\n", stderr);
    exit(exit_code);
}

// Network Handling Functions

/**
 * Converts the address from a human-readable string into a binary representation.
 * @param address string IP address in human-readable format (e.g., "192.168.0.1")
 * @param addr    pointer to the struct sockaddr_storage where the binary representation will be stored
 */
static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    // Converts the str address to binary address and checks for IPv4 or IPv6
    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        // IPv4 address
        addr->ss_family = AF_INET;
        printf("IPv4 found\n");
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        // IPv6 address
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

/**
 * Creates a socket with the specified domain, type, and protocol.
 * @param domain   the communication domain, e.g., AF_INET for IPv4 or AF_INET6 for IPv6
 * @param type     the socket type, e.g., SOCK_STREAM for a TCP socket or SOCK_DGRAM for a UDP socket
 * @param protocol the specific protocol to be used, often set to 0 for the default protocol
 * @return         the file descriptor for the created socket, or -1 on error
 */
static int socket_create(int domain, int type, int protocol)
{
    int sockfd;

    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        fprintf(stderr, "Socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

/**
 * Establishes a network connection to a remote address and port using a given socket.
 * @param sockfd the file descriptor of the socket for the connection
 * @param addr   a pointer to a struct sockaddr_storage containing the remote address
 * @param port   the port to which the connection should be established
 */
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];    // Array to store human-readable IP address for either IPv4 or IPv6
    in_port_t net_port;                      // Stores network byte order representation of port number

    // Converts binary IP address (IPv4 or IPv6) to a human-readable string, stores it in addr_str, and handles errors
    if(inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to %s:%u\n", addr_str, port);
    net_port = htons(port);    // Convert port number to network byte order (big endian)

    // Handle IPv4
    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
    }
    // Handle IPv6
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
    }
    else
    {
        fprintf(stderr, "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    printf("Sock fd: %d\nAddr: %hu\nSize: %zu", sockfd, addr->ss_family, sizeof(*addr));

    // Connect to server
    if(connect(sockfd, (struct sockaddr *)addr, sizeof(*addr)) == -1)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
}

/**
 * Closes a socket with the specified file descriptor.
 * @param sockfd the file descriptor of the socket to be closed
 */
static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("error closing socket");
        exit(EXIT_FAILURE);
    }
}

/**
 * Writes a command string to a socket.
 * @param sockfd   the file descriptor of the socket to write to
 * @param command  the command string to write to the socket
 */
static void write_to_socket(int sockfd, const char *command)
{
    size_t  command_len;
    uint8_t size;

    command_len = strlen(command);
    size        = (uint8_t)command_len;

    send(sockfd, &size, sizeof(uint8_t), 0);    // Send the size of the command
    write(sockfd, command, command_len);        // Write the command string
}

static void read_from_socket(int sockfd)
{
    ssize_t bytes_read;
    char    buffer[LINE_LENGTH];

    bytes_read = recv(sockfd, buffer, sizeof(buffer), 0);

    printf("Read bytes: %zd\n", bytes_read);
    write(STDOUT_FILENO, buffer, (size_t)bytes_read);
    socket_close(sockfd);
}
