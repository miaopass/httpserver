#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>

using std::vector;
using std::string;
typedef struct sockaddr SA;

#define LISTENQ 1024
#define MAXLINE 1024

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void not_found(int client);
int open_listenfd(int port);
int get_line(int, char *, int);
void* doit(void* arg);
void serve_get(int, string);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void serve_post(int fd, string uri);
void serve_dynamic(int fd, const string &filename, const string &cgiargs);
void serve_static(int fd, const string &filename, int filesize);

void not_found(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

int open_listenfd(int port) {
    int listenfd, optval = 1;
    struct sockaddr_in serveraddr;
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
                   sizeof(int)) < 0)
        return -1;
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)port);
    if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
}

int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) {
            if (c == '\r') {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        } else
            c = '\n';
    }
    buf[i] = '\0';

    return (i);
}

void* doit(void* arg) {
    char buf[MAXLINE];
    int fd = *((int*)arg);
    pthread_detach(pthread_self());
    free(arg);
    get_line(fd, buf, MAXLINE) ;
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    sscanf(buf, "%s %s %s", method, uri, version);
    get_line(fd, buf, MAXLINE);
    int n = 0;
    do {
        n = get_line(fd, buf, MAXLINE);
        // printf("%s", buf);
    } while (n > 0 && strcmp(buf, "\n"));

    if (!strcmp(method, "GET")) {
        serve_get(fd, uri);
    } else if (!strcmp(method, "POST")) {
        serve_post(fd, uri);
    } else
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
    close(fd);
    //return NULL;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
    write(fd, shortmsg, strlen(shortmsg));
    return;
}

void serve_get(int fd, string uri) {
    bool is_cgi = false;
    string filename, cgiargs;
    if (uri.find("cgi-bin") != string::npos) // dynamic
    {
        auto ptr = uri.find("?");
        if (ptr != string::npos) {
            cgiargs = string(uri.begin() + ptr + 1, uri.end());
            uri = string(uri.begin(), uri.begin() + ptr);
        } else
            cgiargs = "";
        filename = "." + uri;
        serve_dynamic(fd, filename, cgiargs);
    } else {	// static

        filename = "." + uri;
        if (*(uri.end() - 1) == '/')
            filename += "default.html";
        printf("%s\n", filename.c_str());
        serve_static(fd, filename, 0);
    }
}

void serve_post(int fd, string uri) {

}

void serve_dynamic(int fd, const string &filename, const string &cgiargs) {
    char buf[MAXLINE], *emptylist[] = {NULL};
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    write(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    write(fd, buf, strlen(buf));

    if (fork() == 0) { // child

        setenv("QUERY_STRING", cgiargs.c_str(), 1);
        dup2(fd, STDOUT_FILENO);
        execve(filename.c_str(), emptylist, environ);

    }
    wait(NULL); // parents
}

void serve_static(int fd, const string &filename, int filesize) {

    FILE *resource = fopen(filename.c_str(), "r");

    if (resource == NULL) {    //not found
        not_found(fd);
        close(fd);
        return;
    }

    char buf[1024];

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(fd, buf, strlen(buf), 0);
    strcpy(buf, "Server: jdbhttpd/0.1.0\r\n");
    send(fd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(fd, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(fd, buf, strlen(buf), 0);

    //从文件文件描述符中读取指定内容
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource)) {
        send(fd, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

int main(int argc, char **argv) {
    int listenfd;
    if (argc != 2) {
        fprintf(stderr, "usage: $s <port>\n", argv[0]);
        exit(0);
    }
    if ((listenfd = open_listenfd(atoi(argv[1]))) < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        exit(0);
    }

    pthread_t tid;

    while (true) {
        sockaddr_in cliaddr;
        socklen_t len;
        int fd = accept(listenfd, (sockaddr*)&cliaddr, &len);
        if(fd > 0) {
            int* pfd = (int*)malloc(sizeof(int));
            *pfd = fd;

            int n;
            pthread_create(&tid, NULL, doit, pfd);
        }
        else {
            fprintf(stderr, "%s\n", strerror(errno));
            exit(0);
        }
    }
    close(listenfd);
    exit(0);
}
