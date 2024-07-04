#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
    const char* delim = " ";
    char* token = strtok(str, delim);
    if (token == NULL) {
        return -1;
    }
    /* Parse mode. */
    finfo->mode = 0;
    size_t token_len = strlen(token);
    if (token_len < 10) {
        return -1;
    }
    if (token[0] != 'd' && token[0] != '-') {
        return -1;
    }
    if (token[0] == 'd') {
        finfo->mode = FILEINFO_ISDIR;
    } else {
        finfo->mode = FILEINFO_ISREG;
    }
    if (token[1] == 'r')
        finfo->mode |= FILEINFO_IRUSR;
    if (token[2] == 'w')
        finfo->mode |= FILEINFO_IWUSR;
    if (token[3] == 'x')
        finfo->mode |= FILEINFO_IXUSR;
    if (token[4] == 'r')
        finfo->mode |= FILEINFO_IRGRP;
    if (token[5] == 'w')
        finfo->mode |= FILEINFO_IWGRP;
    if (token[6] == 'x')
        finfo->mode |= FILEINFO_IXGRP;
    if (token[7] == 'r')
        finfo->mode |= FILEINFO_IROTH;
    if (token[8] == 'w')
        finfo->mode |= FILEINFO_IWOTH;
    if (token[9] == 'x')
        finfo->mode |= FILEINFO_IXOTH;
    token = strtok(NULL, delim);
    if (token == NULL) {
        return -1;
    }
    /* Parse size. */
    errno = 0;
    finfo->size = strtoull(token, NULL, 10);
    if (finfo->size == ULLONG_MAX && errno == ERANGE) {
        return -1;
    }
    token = strtok(NULL, delim);
    if (token == NULL) {
        return -1;
    }
    /* Parse name. */
    size_t len = strlen(token);
    if (len >= (sizeof(finfo->name) - 1)) {
        return -1;
    }
    strcpy(finfo->name, token);

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
    return snprintf(str, n, "%s %ld %s", modestr, finfo->size, finfo->name);
}

/**
 * Copy file permissions from file info to the file at path.
 *
 * On success, zero is returned. On error, -1 is returned and errno is set
 * appropriately.
 */
static int fileinfo_cpyperm(const FileInfo* finfo, char* path)
{
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
}

/**
 * Copy permissions from a stat structure to file info structure.
 */
static void fileinfo_setperm(FileInfo* finfo, const struct stat* s)
{
    finfo->mode = 0;
    if ((s->st_mode & S_IFMT) == S_IFDIR)
        finfo->mode |= FILEINFO_ISDIR;
    if ((s->st_mode & S_IFMT) == S_IFREG)
        finfo->mode |= FILEINFO_ISREG;
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
}

/**
 * Sends all bytes in a buffer.
 *
 * Returns the number of bytes sent or -1 if an error occurred.
 */
static ssize_t send_all(int sockfd, const void* buffer, size_t n, int flags)
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
static ssize_t recv_str(int sockfd, void* buffer, size_t n, int flags)
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

static int send_file(int sockfd, void* buffer, size_t n, int flags, FILE* srcfile)
{
    size_t nread = 0;
    while ((nread = fread(buffer, 1, n, srcfile)) > 0) {
        if (send_all(sockfd, buffer, nread, flags) != (ssize_t)nread) {
            return -1;
        }
    }
    return 0;
}

static int recv_file(int sockfd, void* buffer, size_t n, int flags, FILE* outfile, size_t fsize)
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
static int connect_retry(int sockfd, const struct sockaddr* addr, socklen_t socklen)
{
    int maxsleep = 64; /* About 1 minute. */
    for (int numsec = 1; numsec < maxsleep; numsec <<= 1) {
        if (connect(sockfd, addr, socklen) == 0) {
            return 0;
        }
        if (numsec <= maxsleep / 2) {
            sleep(numsec);
        }
    }
    return -1;
}

static int incp_connect(int argc, char* argv[])
{
    struct addrinfo* ailist;
    struct addrinfo* aip;
    struct addrinfo hints;
    int sockfd = -1;
    int err = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((err = getaddrinfo("127.0.0.1", DEFAULT_PORT, &hints, &ailist)) != 0) {
        fprintf(stderr, "Error: getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    for (aip = ailist; aip != NULL; aip = aip->ai_next) {
        if ((sockfd = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol)) <= 0) {
            continue;
        }
        if (connect_retry(sockfd, aip->ai_addr, aip->ai_addrlen) != 0) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        break;
    }
    if (sockfd <= 0) {
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
    size_t len = strlen(argv[argc - 1]);
    if (len >= sizeof(finfo.name) - 1) {
        errno = ENAMETOOLONG;
        perror("Error: destination path");
        err = -1;
        goto cleanup;
    }
    strcpy(finfo.name, argv[argc - 1]);
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
        struct stat statinfo;
        if ((err = stat(argv[i], &statinfo)) != 0) {
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
        if (send_file(sockfd, buffer, sizeof(buffer), MSG_NOSIGNAL, srcfile) != 0) {
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
    close(sockfd);
    freeaddrinfo(ailist);
    return err;
}

static int incp_listen(void)
{
    struct addrinfo* ailist;
    struct addrinfo* aip;
    struct addrinfo hints;
    int sockfd = -1;
    int err = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((err = getaddrinfo(NULL, DEFAULT_PORT, &hints, &ailist)) != 0) {
        fprintf(stderr, "Error: getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    for (aip = ailist; aip != NULL; aip = aip->ai_next) {
        if ((sockfd = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol)) <= 0) {
            continue;
        }
        int on = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        if (bind(sockfd, aip->ai_addr, aip->ai_addrlen) != 0) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        if (listen(sockfd, BACKLOG) != 0) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        break;
    }

    freeaddrinfo(ailist);

    if (sockfd <= 0) {
        perror("Error: failed to start server");
        return -1;
    }

    struct sockaddr_storage client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    int clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_size);
    if (clientfd < 0) {
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
    struct stat s;
    if (stat(destfinfo.name, &s) == 0) {
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

        /* Send OK */
        if ((err = send_all(clientfd, INCP_MSG_OK CRLF, strlen(INCP_MSG_OK CRLF), 0)) == -1) {
            perror("Error: send");
            goto cleanup;
        }

        /* Copy file to destination. */
        char path[1024];
        if (destfinfo.mode & FILEINFO_ISDIR) {
            if (snprintf(path, sizeof(path), "%s/%s", destfinfo.name, srcfinfo.name) >= (int)sizeof(path)) {
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
        if (stat(path, &s) == 0) {
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
        if ((err = recv_file(clientfd, buffer, sizeof(buffer), MSG_NOSIGNAL, outfile, srcfinfo.size)) != 0) {
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
    close(clientfd);
    close(sockfd);
    return err;
}

void print_usage(void)
{
    puts("USAGE:");
    puts("\tincp -l");
    puts("\tincp source ... <address>[:port]:target");
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    int is_listen = strcmp(argv[1], "-l") == 0;
    if (!is_listen && argc < 3) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    if (is_listen) {
        if (incp_listen() != 0) {
            exit(EXIT_FAILURE);
        }
    } else {
        if (incp_connect(argc - 1, &argv[1]) != 0) {
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}
