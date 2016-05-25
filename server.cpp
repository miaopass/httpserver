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
int doit(int);
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

int doit(int fd) {
	char buf[MAXLINE];
	if (get_line(fd, buf, MAXLINE) == 0)
		return -1;
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

	return 0;
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
	int maxfd = listenfd, maxi = -1;
	int client[FD_SETSIZE];
	fd_set allset;
	sockaddr_in cliaddr;
	for (int i = 0; i < FD_SETSIZE; i++)
		client[i] = -1;
	FD_ZERO(&allset);
	FD_SET(listenfd, &allset);
	while (true) {
		fd_set rset = allset;
		int connfd, i;
		int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
		if (nready < 0) {
			fprintf(stderr, "%s\n", strerror(errno));
			exit(0);
		}
		if (FD_ISSET(listenfd, &rset)) {
			socklen_t clilen = sizeof(cliaddr);
			if ((connfd = accept(listenfd, (sockaddr *)&cliaddr, &clilen)) < 0) {
				fprintf(stderr, "%s\n", strerror(errno));
				exit(0);
			}

			for (i = 0; i < FD_SETSIZE; i++) {
				if (client[i] < 0) {
					client[i] = connfd;
					break;
				}
			}

			if (i == FD_SETSIZE) {
				fprintf(stderr, "too many clients\n");
				exit(0);
			}

			FD_SET(connfd, &allset);
			if (connfd > maxfd)
				maxfd = connfd;
			if (i > maxi)
				maxi = i;

			if (--nready <= 0)
				continue;
		}

		for (i = 0; i <= maxi; i++) {
			int sockfd;
			if ((sockfd = client[i]) < 0)
				continue;
			if (FD_ISSET(sockfd, &rset)) {
				doit(sockfd);
				close(sockfd);
				FD_CLR(sockfd, &allset);
				client[i] = -1;

				if (--nready <= 0)
					break;
			}
		}
	}
	close(listenfd);
	exit(0);
}
