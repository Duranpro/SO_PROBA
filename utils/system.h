#ifndef SYSTEM_H
#define SYSTEM_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define CITADEL_MKDIR(path) _mkdir(path)
#define CITADEL_SOCKET_CLOSE closesocket
typedef SOCKET citadel_socket_t;
#define CITADEL_INVALID_SOCKET INVALID_SOCKET
#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define CITADEL_MKDIR(path) mkdir(path, 0755)
#define CITADEL_SOCKET_CLOSE close
typedef int citadel_socket_t;
#define CITADEL_INVALID_SOCKET (-1)
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CITADEL_MAX_INPUT 1024
#define CITADEL_MAX_TOKENS 16
#define CITADEL_STDOUT STDOUT_FILENO
#define CITADEL_STDERR STDERR_FILENO

#endif
