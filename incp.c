#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <io.h>
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

typedef ptrdiff_t ssize_t;

typedef SOCKET OS_SOCKET;
#define OS_INVALID_SOCKET INVALID_SOCKET

#define OS_STAT _stat64

#else /* Unix */

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int OS_SOCKET;
#define OS_INVALID_SOCKET (-1)

#define OS_STAT stat

#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define DEFAULT_PORT "4627"
#define BACKLOG 10

#define BUFFER_SIZE 8192

#define CRLF "\r\n"
#define INCP_MSG_HELLO "HELLO"
#define INCP_MSG_OK "OK"

#define FILEINFO_IRUSR (1 << 0) /* Read by owner. */
#define FILEINFO_IWUSR (1 << 1) /* Write by owner. */
#define FILEINFO_IXUSR (1 << 2) /* Execute by owner. */
#define FILEINFO_IRGRP (1 << 3) /* Read by group. */
#define FILEINFO_IWGRP (1 << 4) /* Write by group. */
#define FILEINFO_IXGRP (1 << 5) /* Execute by group. */
#define FILEINFO_IROTH (1 << 6) /* Read by others. */
#define FILEINFO_IWOTH (1 << 7) /* Write by others. */
#define FILEINFO_IXOTH (1 << 8) /* Execute by others. */
#define FILEINFO_ISDIR (1 << 9) /* Directory. */
#define FILEINFO_ISREG (1 << 10) /* Regular file. */
#define FILEINFO_ISLNK (1 << 11) /* Symbolic link. */

static void print_usage(void)
{
    puts("USAGE:");
    puts("\tincp -l [port]");
    puts("\tincp source [source...] address[:port]:target");
}

typedef struct FileInfo {
    int32_t mode;
    size_t size;
    char name[1024];
} FileInfo;

/**
 * Given a string that has different file properties separated by a space,
 * return the info as a struct. The file info string is similar in output to 'ls
 * -l'. The columns are file type/permissions, size (in bytes), and then name.
 *
 * Example file info string:
 *   'drwxr-xr-x 4627 FileName.txt'
 *
 * Returns 0 on success or -1 if not given a valid file info string.
 */
static int fileinfo_parse(FileInfo* finfo, char* fileinfo)
{
    char* str = fileinfo;
    const char delim = ' ';
    char* prop_end = strchr(str, delim);
    if (prop_end == NULL) {
        return -1;
    }
    prop_end[0] = '\0';
    /* Parse mode. */
    finfo->mode = 0;
    size_t prop_len = strlen(str);
    if (prop_len < 10) {
        return -1;
    }
    if (str[0] != 'd' && str[0] != '-') {
        return -1;
    }
    if (str[0] == 'd') {
        finfo->mode = FILEINFO_ISDIR;
    } else {
        finfo->mode = FILEINFO_ISREG;
    }
    if (str[1] == 'r')
        finfo->mode |= FILEINFO_IRUSR;
    if (str[2] == 'w')
        finfo->mode |= FILEINFO_IWUSR;
    if (str[3] == 'x')
        finfo->mode |= FILEINFO_IXUSR;
    if (str[4] == 'r')
        finfo->mode |= FILEINFO_IRGRP;
    if (str[5] == 'w')
        finfo->mode |= FILEINFO_IWGRP;
    if (str[6] == 'x')
        finfo->mode |= FILEINFO_IXGRP;
    if (str[7] == 'r')
        finfo->mode |= FILEINFO_IROTH;
    if (str[8] == 'w')
        finfo->mode |= FILEINFO_IWOTH;
    if (str[9] == 'x')
        finfo->mode |= FILEINFO_IXOTH;
    str = prop_end + 1;
    prop_end = strchr(str, delim);
    if (prop_end == NULL) {
        return -1;
    }
    prop_end[0] = '\0';
    /* Parse size. */
    errno = 0;
    finfo->size = strtoull(str, NULL, 10);
    if (finfo->size == ULLONG_MAX && errno == ERANGE) {
        return -1;
    }
    /* Parse name. */
    str = prop_end + 1;
    size_t len = strlen(str);
    if (len >= (sizeof(finfo->name) - 1)) {
        return -1;
    }
    strcpy(finfo->name, str);

    return 0;
}

static int fileinfo_snprint(const FileInfo* finfo, char* str, size_t n)
{
    char modestr[11];
    modestr[0] = finfo->mode & FILEINFO_ISDIR ? 'd' : '-';
    modestr[1] = finfo->mode & FILEINFO_IRUSR ? 'r' : '-';
    modestr[2] = finfo->mode & FILEINFO_IWUSR ? 'w' : '-';
    modestr[3] = finfo->mode & FILEINFO_IXUSR ? 'x' : '-';
    modestr[4] = finfo->mode & FILEINFO_IRGRP ? 'r' : '-';
    modestr[5] = finfo->mode & FILEINFO_IWGRP ? 'w' : '-';
    modestr[6] = finfo->mode & FILEINFO_IXGRP ? 'x' : '-';
    modestr[7] = finfo->mode & FILEINFO_IROTH ? 'r' : '-';
    modestr[8] = finfo->mode & FILEINFO_IWOTH ? 'w' : '-';
    modestr[9] = finfo->mode & FILEINFO_IXOTH ? 'x' : '-';
    modestr[10] = '\0';
    return snprintf(str, n, "%s %zu %s", modestr, finfo->size, finfo->name);
}

/**
 * Copy file permissions from file info to the file at path.
 *
 * On success, zero is returned. On error, -1 is returned and errno is set
 * appropriately.
 */
static int fileinfo_cpyperm(const FileInfo* finfo, char* path)
{
#if defined(_WIN32)
    unsigned short perms = 0;
    if (finfo->mode & FILEINFO_IRUSR)
        perms |= S_IREAD;
    if (finfo->mode & FILEINFO_IWUSR)
        perms |= S_IWRITE;
    if (finfo->mode & FILEINFO_IXUSR)
        perms |= S_IEXEC;
    return _chmod(path, perms);
#else
    mode_t perms = 0;
    if (finfo->mode & FILEINFO_IRUSR)
        perms |= S_IRUSR;
    if (finfo->mode & FILEINFO_IWUSR)
        perms |= S_IWUSR;
    if (finfo->mode & FILEINFO_IXUSR)
        perms |= S_IXUSR;
    if (finfo->mode & FILEINFO_IRGRP)
        perms |= S_IRGRP;
    if (finfo->mode & FILEINFO_IWGRP)
        perms |= S_IWGRP;
    if (finfo->mode & FILEINFO_IXGRP)
        perms |= S_IXGRP;
    if (finfo->mode & FILEINFO_IROTH)
        perms |= S_IROTH;
    if (finfo->mode & FILEINFO_IWOTH)
        perms |= S_IWOTH;
    if (finfo->mode & FILEINFO_IXOTH)
        perms |= S_IXOTH;
    return chmod(path, perms);
#endif
}

/**
 * Copy permissions from a stat structure to file info structure.
 */
static void fileinfo_setperm(FileInfo* finfo, const struct OS_STAT* s)
{
    finfo->mode = 0;
    if ((s->st_mode & S_IFMT) == S_IFDIR)
        finfo->mode |= FILEINFO_ISDIR;
    if ((s->st_mode & S_IFMT) == S_IFREG)
        finfo->mode |= FILEINFO_ISREG;
#if defined(_WIN32)
    unsigned short perms = s->st_mode & ~S_IFMT;
    if (perms & S_IREAD)
        finfo->mode |= FILEINFO_IRUSR;
    if (perms & S_IWRITE)
        finfo->mode |= FILEINFO_IWUSR;
    if (perms & S_IEXEC)
        finfo->mode |= FILEINFO_IXUSR;
#else
    mode_t perms = s->st_mode & ~S_IFMT;
    if (perms & S_IRUSR)
        finfo->mode |= FILEINFO_IRUSR;
    if (perms & S_IWUSR)
        finfo->mode |= FILEINFO_IWUSR;
    if (perms & S_IXUSR)
        finfo->mode |= FILEINFO_IXUSR;
    if (perms & S_IRGRP)
        finfo->mode |= FILEINFO_IRGRP;
    if (perms & S_IWGRP)
        finfo->mode |= FILEINFO_IWGRP;
    if (perms & S_IXGRP)
        finfo->mode |= FILEINFO_IXGRP;
    if (perms & S_IROTH)
        finfo->mode |= FILEINFO_IROTH;
    if (perms & S_IWOTH)
        finfo->mode |= FILEINFO_IWOTH;
    if (perms & S_IXOTH)
        finfo->mode |= FILEINFO_IXOTH;
#endif
}

/**
 * Converts all Windows path separators \ to /.
 */
static void normalize_sep(char* path)
{
    for (; *path; path++) {
        if (*path == '\\') {
            *path = '/';
        }
    }
}

static int os_closesocket(OS_SOCKET s)
{
#if defined(_WIN32)
    return closesocket(s);
#else
    return close(s);
#endif
}

/**
 * Sends all bytes in a buffer.
 *
 * Returns the number of bytes sent or -1 if an error occurred.
 */
static ssize_t send_all(OS_SOCKET sockfd, const void* buffer, size_t n, int flags)
{
    ssize_t nsent = 0;
    ssize_t sent_total = 0;
    while ((nsent = send(sockfd, buffer, n, flags)) > 0) {
        sent_total += nsent;
        if (sent_total == (ssize_t)n) {
            break;
        }
    }
    if (nsent < 0) {
        return nsent;
    }
    return sent_total;
}

/**
 * Reads from socket until a CRLF is found or until the buffer is full. If a
 * CRLF is found it is replaced by a terminating null. On success buffer will
 * always be filled with a valid string.
 *
 * Return the length of the string or -1 if an error occurred or if buffer is
 * filled before finding CRLF.
 */
static ssize_t recv_str(OS_SOCKET sockfd, void* buffer, size_t n, int flags)
{
    ssize_t nread = 0;
    size_t read_total = 0;
    while (1) {
        nread = recv(sockfd, ((char*)buffer) + read_total, n - read_total, flags);
        if (nread <= 0) {
            if (nread < 0 && errno == EINTR) {
                continue;
            } else {
                return nread;
            }
        }
        read_total += nread;
        if (read_total >= n) {
            printf("read_total");
            return -1;
        }
        /* We are assuming a CR is always followed by a LF */
        char* pos = memchr(buffer, '\r', read_total);
        if (pos != NULL) {
            *pos = '\0';
            read_total = pos - (char*)buffer;
            break;
        }
    }
    return read_total;
}

static int send_file(OS_SOCKET sockfd, void* buffer, size_t n, int flags, FILE* srcfile)
{
    size_t nread = 0;
    while ((nread = fread(buffer, 1, n, srcfile)) > 0) {
        if (send_all(sockfd, buffer, nread, flags) != (ssize_t)nread) {
            return -1;
        }
    }
    return 0;
}

static int recv_file(OS_SOCKET sockfd, void* buffer, size_t n, int flags, FILE* outfile, size_t fsize)
{
    ssize_t nread = 0;
    size_t read_total = 0;
    while (read_total < fsize) {
        nread = recv(sockfd, buffer, MIN(n, fsize - read_total), flags);
        if (nread <= 0) {
            if (nread < 0 && errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        }
        read_total += nread;
        if (fwrite(buffer, nread, 1, outfile) != 1) {
            return -1;
        }
    }
    return 0;
}

/**
 * Exponential backoff on connection tries.
 */
static int connect_retry(OS_SOCKET sockfd, const struct sockaddr* addr, socklen_t socklen)
{
    int maxsleep = 64; /* About 1 minute. */
    for (int numsec = 1; numsec < maxsleep; numsec <<= 1) {
        if (connect(sockfd, addr, socklen) == 0) {
            return 0;
        }
        if (numsec <= maxsleep / 2) {
#if defined(_WIN32)
            Sleep(numsec * 1000);
#else
            sleep(numsec);
#endif
        }
    }
    return -1;
}

/**
 * Parses a string of the form <IPv4 address>[:port]:path/to/file/or/directory
 * and sets the appropriate address, port, and dest strings.
 *
 * The input string is modified and address, port, and dest will be pointers to
 * different areas of the input string.
 *
 * Returns -1 if it is not a valid input string, otherwise returns 0 and sets
 * the given strings.
 */
static int parse_destination(char* str, char** address, char** port, char** dest)
{
    const char delim = ':';
    *address = *port = *dest = NULL;
    char* ptr = strchr(str, delim);
    if (ptr == NULL) {
        return -1;
    }
    *address = str;
    *ptr = '\0';
    ptr++;
    str = ptr;
    while (isdigit(*ptr)) {
        ptr++;
    }
    if (*ptr != delim) {
        *dest = str;
        return 0;
    }
    *port = str;
    *ptr = '\0';
    *dest = ptr + 1;
    return 0;
}

static int incp_connect(int argc, char* argv[])
{
    struct addrinfo* ailist;
    struct addrinfo* aip;
    struct addrinfo hints;
    OS_SOCKET sockfd = OS_INVALID_SOCKET;
    int err = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /* Parse the address, port, and destination from the last argument. They
     * should be in the form 127.0.0.1:4627:dest/path and port is optional. */
    char *address, *port, *dest;
    if (parse_destination(argv[argc - 1], &address, &port, &dest) != 0) {
        print_usage();
        return -1;
    }

    if ((err = getaddrinfo(address, port == NULL ? DEFAULT_PORT : port, &hints, &ailist)) != 0) {
        fprintf(stderr, "Error: getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    for (aip = ailist; aip != NULL; aip = aip->ai_next) {
        if ((sockfd = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol)) == OS_INVALID_SOCKET) {
            continue;
        }
        if (connect_retry(sockfd, aip->ai_addr, aip->ai_addrlen) != 0) {
            os_closesocket(sockfd);
            sockfd = OS_INVALID_SOCKET;
            continue;
        }
        break;
    }
    if (sockfd == OS_INVALID_SOCKET) {
        perror("Error");
        freeaddrinfo(ailist);
        return -1;
    }

    FILE* srcfile = NULL;
    FileInfo finfo;
    memset(&finfo, 0, sizeof(finfo));
    int send_len = 0;
    char buffer[BUFFER_SIZE];
    /* Get greeting from server. */
    if (recv_str(sockfd, buffer, sizeof(buffer), 0) <= 0 || strcmp(buffer, INCP_MSG_HELLO) != 0) {
        fprintf(stderr, "Error: unexpected reply from server\n");
        err = -1;
        goto cleanup;
    }

    /* Send server destination info. */
    /* If there are more than 1 source files we expect the destination file to be a directory. */
    size_t len = strlen(dest);
    if (len >= sizeof(finfo.name) - 1) {
        errno = ENAMETOOLONG;
        perror("Error: destination path");
        err = -1;
        goto cleanup;
    }
    strcpy(finfo.name, dest);
    if ((send_len = fileinfo_snprint(&finfo, buffer, sizeof(buffer))) >= (int)sizeof(buffer)) {
        errno = ENAMETOOLONG;
        perror("Error: destination path");
        err = -1;
        goto cleanup;
    }
    if (send_all(sockfd, buffer, send_len, 0) != send_len) {
        fprintf(stderr, "Error: failed to send file info\n");
        err = -1;
        goto cleanup;
    }
    send_len = strlen(CRLF);
    if (send_all(sockfd, CRLF, send_len, 0) != send_len) {
        fprintf(stderr, "Error: failed to send CRLF\n");
        err = -1;
        goto cleanup;
    }

    /* Expect OK reply. */
    if (recv_str(sockfd, buffer, sizeof(buffer), 0) <= 0 || strcmp(buffer, INCP_MSG_OK) != 0) {
        fprintf(stderr, "Error: server did not reply OK\n");
        err = -1;
        goto cleanup;
    }

    for (size_t i = 0; (int)i < argc - 1; i++) {
        /* Send server source info. */
        memset(&finfo, 0, sizeof(finfo));
        struct OS_STAT statinfo;
        if ((err = OS_STAT(argv[i], &statinfo)) != 0) {
            perror("Error: stat");
            goto cleanup;
        }
        fileinfo_setperm(&finfo, &statinfo);
        finfo.size = statinfo.st_size;
        len = strlen(argv[i]);
        if (len >= sizeof(finfo.name) - 1) {
            errno = ENAMETOOLONG;
            perror("Error: source path");
            err = -1;
            goto cleanup;
        }
        strcpy(finfo.name, argv[i]);
        if ((send_len = fileinfo_snprint(&finfo, buffer, sizeof(buffer))) >= (int)sizeof(buffer)) {
            errno = ENAMETOOLONG;
            perror("Error: destination path");
            err = -1;
            goto cleanup;
        }
        if (send_all(sockfd, buffer, send_len, 0) != send_len) {
            fprintf(stderr, "Error: failed to send file info\n");
            err = -1;
            goto cleanup;
        }
        send_len = strlen(CRLF);
        if (send_all(sockfd, CRLF, send_len, 0) != send_len) {
            fprintf(stderr, "Error: failed to send CRLF\n");
            err = -1;
            goto cleanup;
        }

        /* Expect OK reply. */
        if (recv_str(sockfd, buffer, sizeof(buffer), 0) <= 0 || strcmp(buffer, INCP_MSG_OK) != 0) {
            fprintf(stderr, "Error: server did not reply OK\n");
            err = -1;
            goto cleanup;
        }

        /* Send source file to server as bytes. */
        srcfile = fopen(argv[i], "rb");
        if (srcfile == NULL) {
            err = -1;
            perror("Error: fopen");
            goto cleanup;
        }
        // if (send_file(sockfd, buffer, sizeof(buffer), MSG_NOSIGNAL, srcfile) != 0) {
        if (send_file(sockfd, buffer, sizeof(buffer), 0, srcfile) != 0) {
            perror("Error: failed to upload file");
            err = -1;
            goto cleanup;
        }
        fclose(srcfile);
        srcfile = NULL;

        /* Expect OK reply. */
        if (recv_str(sockfd, buffer, sizeof(buffer), 0) <= 0 || strcmp(buffer, INCP_MSG_OK) != 0) {
            fprintf(stderr, "Error: server did not reply OK\n");
            err = -1;
            goto cleanup;
        }
    }

cleanup:
    if (srcfile != NULL) {
        fclose(srcfile);
    }
    os_closesocket(sockfd);
    freeaddrinfo(ailist);
    return err;
}

static int incp_listen(const char* port)
{
    struct addrinfo* ailist;
    struct addrinfo* aip;
    struct addrinfo hints;
    OS_SOCKET sockfd = OS_INVALID_SOCKET;
    int err = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((err = getaddrinfo(NULL, port, &hints, &ailist)) != 0) {
        fprintf(stderr, "Error: getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    for (aip = ailist; aip != NULL; aip = aip->ai_next) {
        if ((sockfd = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol)) == OS_INVALID_SOCKET) {
            continue;
        }
        int on = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on)) != 0) {
            os_closesocket(sockfd);
            sockfd = OS_INVALID_SOCKET;
            continue;
        }
        if (bind(sockfd, aip->ai_addr, aip->ai_addrlen) != 0) {
            os_closesocket(sockfd);
            sockfd = OS_INVALID_SOCKET;
            continue;
        }
        if (listen(sockfd, BACKLOG) != 0) {
            os_closesocket(sockfd);
            sockfd = OS_INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(ailist);

    if (sockfd == OS_INVALID_SOCKET) {
        perror("Error: failed to start server");
        return -1;
    }

    struct sockaddr_storage client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    OS_SOCKET clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_size);
    if (clientfd == OS_INVALID_SOCKET) {
        perror("Error: accept");
        return -1;
    }

    FILE* outfile = NULL;
    FileInfo destfinfo;
    memset(&destfinfo, 0, sizeof(destfinfo));
    FileInfo srcfinfo;
    memset(&srcfinfo, 0, sizeof(srcfinfo));
    char buffer[BUFFER_SIZE];
    ssize_t read;

    /* Send hello. */
    if ((err = send_all(clientfd, INCP_MSG_HELLO CRLF, strlen(INCP_MSG_HELLO CRLF), 0)) == -1) {
        perror("Error: send");
        goto cleanup;
    }

    /* Get destination file info from client. */
    read = recv_str(clientfd, buffer, sizeof(buffer), 0);
    if (read <= 0) {
        fprintf(stderr, "Error: failed to get data from client\n");
        goto cleanup;
    }
    if ((err = fileinfo_parse(&destfinfo, buffer)) != 0) {
        fprintf(stderr, "Error: bad file info\n");
        goto cleanup;
    }
    normalize_sep(destfinfo.name);
    struct OS_STAT s;
    if (OS_STAT(destfinfo.name, &s) == 0) {
        /* File exists. */
        fileinfo_setperm(&destfinfo, &s);
    } else {
        /* File does not exist. */
        destfinfo.mode = FILEINFO_ISREG;
    }

    /* Send OK */
    if ((err = send_all(clientfd, INCP_MSG_OK CRLF, strlen(INCP_MSG_OK CRLF), 0)) == -1) {
        perror("Error: send");
        goto cleanup;
    }

    while (1) {
        /* Get source file info from client. */
        read = recv_str(clientfd, buffer, sizeof(buffer), 0);
        if (read == 0) {
            /* No more files to process. */
            err = 0;
            goto cleanup;
        } else if (read < 0) {
            fprintf(stderr, "Error: failed to get data from client\n");
            goto cleanup;
        }
        if ((err = fileinfo_parse(&srcfinfo, buffer)) != 0) {
            fprintf(stderr, "Error: bad file info\n");
            goto cleanup;
        }
        normalize_sep(srcfinfo.name);

        /* Send OK */
        if ((err = send_all(clientfd, INCP_MSG_OK CRLF, strlen(INCP_MSG_OK CRLF), 0)) == -1) {
            perror("Error: send");
            goto cleanup;
        }

        /* Copy file to destination. */
        char path[1024];
        if (destfinfo.mode & FILEINFO_ISDIR) {
            char* name = strrchr(srcfinfo.name, '/'); /* Only get the file name. */
            if (name != NULL) {
                name++; /* Do not start the name with '/'. */
            } else {
                name = srcfinfo.name;
            }
            size_t len = strlen(destfinfo.name);
            bool has_sep = len > 0 && destfinfo.name[len - 1] == '/';
            if (snprintf(path, sizeof(path), "%s%s%s", destfinfo.name, has_sep ? "" : "/", name) >= (int)sizeof(path)) {
                errno = ENAMETOOLONG;
                perror("Error");
                err = -1;
                goto cleanup;
            }
        } else {
            /* This should always be false. In case path and FileInfo.name have
             * different buffer sizes. */
            if (strlen(destfinfo.name) >= sizeof(path) - 1) {
                errno = ENAMETOOLONG;
                perror("Error");
                err = -1;
                goto cleanup;
            }
            strcpy(path, destfinfo.name);
        }
        FileInfo info_tocopy;
        memset(&info_tocopy, 0, sizeof(info_tocopy));
        if (OS_STAT(path, &s) == 0) {
            /* File exists. */
            fileinfo_setperm(&info_tocopy, &s);
        } else {
            /* File does not exist. */
            info_tocopy = srcfinfo;
        }
        outfile = fopen(path, "wb");
        if (outfile == NULL) {
            perror("Error: fopen");
            err = -1;
            goto cleanup;
        }
        // if ((err = recv_file(clientfd, buffer, sizeof(buffer), MSG_NOSIGNAL, outfile, srcfinfo.size)) != 0) {
        if ((err = recv_file(clientfd, buffer, sizeof(buffer), 0, outfile, srcfinfo.size)) != 0) {
            fprintf(stderr, "Error: an error occurred while trying to download file\n");
            goto cleanup;
        }
        fclose(outfile);
        outfile = NULL;
        if ((err = fileinfo_cpyperm(&info_tocopy, path)) != 0) {
            perror("Error");
            goto cleanup;
        }

        /* Send OK */
        if ((err = send_all(clientfd, INCP_MSG_OK CRLF, strlen(INCP_MSG_OK CRLF), 0)) == -1) {
            perror("Error: send");
            goto cleanup;
        }
    }

cleanup:
    if (outfile != NULL) {
        fclose(outfile);
    }
    os_closesocket(clientfd);
    os_closesocket(sockfd);
    return err;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage();
        exit(EXIT_FAILURE);
    }

#if defined(_WIN32)
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
        fprintf(stderr, "Error: WSAStartup failed\n");
        exit(EXIT_FAILURE);
    }
#endif

    int is_listen = strcmp(argv[1], "-l") == 0;
    if (!is_listen && argc < 3) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    if (is_listen) {
        char* port = NULL;
        if (argc >= 3) {
            port = argv[2];
        }
        if (incp_listen(port == NULL ? DEFAULT_PORT : port) != 0) {
            exit(EXIT_FAILURE);
        }
    } else {
        if (incp_connect(argc - 1, &argv[1]) != 0) {
            exit(EXIT_FAILURE);
        }
    }

#if defined(_WIN32)
    WSACleanup();
#endif

    return 0;
}
