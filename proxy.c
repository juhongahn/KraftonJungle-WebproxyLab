#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
sem_t mutex, w;
typedef struct
{
	char *buf;
	char *uri;
	int use;
	int end;
} cache_struct;
cache_struct cache[10];

void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *varg);

int main(int argc, char **argv)
{
	int listenfd, *connfd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	pthread_t tid;

	if (argc != 2)
	{
		fprintf(stderr, "usage :%s <port> \n", argv[0]);
		exit(1);
	}
	Sem_init(&w, 0, 1);
	Sem_init(&mutex, 0, 1);
	Signal(SIGPIPE, SIG_IGN); // SIGPIPE 시그널 무시
	listenfd = Open_listenfd(argv[1]);
	while (1)
	{
		clientlen = sizeof(clientaddr);
		connfd = Malloc(sizeof(int));
		*connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s %s).\n", hostname, port);
		Pthread_create(&tid, NULL, thread, connfd);
		// doit(connfd);
		// Close(connfd);
	}
	return 0;
}
// thread 루틴 함수
void *thread(void *varg)
{
	int connfd = *((int *)varg);
	Pthread_detach(Pthread_self());
	Free(varg);
	doit(connfd);
	Close(connfd);
	return NULL;
}

// 클라이언트 HTTP 트랜잭션을 처리하는 함수
void doit(int connfd)
{
	int end_serverfd;
	int port;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char http_header[MAXLINE];
	char hostname[MAXLINE], path[MAXLINE];
	char portStr[10];

	rio_t rio, server_rio;

	Rio_readinitb(&rio, connfd);
	Rio_readlineb(&rio, buf, MAXLINE);
	printf("Request headers: \n");
	printf("%s", buf);							   // 요청 헤더 출력
	sscanf(buf, "%s %s %s", method, uri, version); // method, uri , version 입력 받기
	// GET method가 아닐 시 error
	if (strcasecmp(method, "GET"))
	{
		clienterror(connfd, method, "501", "Not implemented",
					"Tiny does not implement this method");
		return;
	}
	/* Parase URI from GET request */
	sscanf(uri, "%*[^:]://%[^:]:%d%s", hostname, &port, path);
	printf("parsed hostname: %s\n", hostname);
	printf("parsed port: %d\n", port);
	printf("parsed path: %s\n", path);
	// end sever에 보낼 http_header를 만든다.
	build_http_header(http_header, hostname, path, port, &rio);

	sprintf(portStr, "%d", port); // port 타입을 string으로
	if (portStr != NULL)
		end_serverfd = Open_clientfd(hostname, portStr); // end server와 연결
	else
		end_serverfd = Open_clientfd(hostname, "80");
	// end server와 연결 실패시
	if (end_serverfd < 0)
	{
		printf("connection failed\n");
		return;
	}
	Rio_writen(end_serverfd, http_header, strlen(http_header)); // end server로 http_header 전송

	// end server로부터 받은 후 다시 client에 전송한다.
	size_t n;
	Rio_readinitb(&server_rio, end_serverfd);
	while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
	{
		printf("proxy received %d bytes,then send\n", n);
		Rio_writen(connfd, buf, n);
	}
	Close(end_serverfd);
}

// endsever에 보낼 http_header를 만드는 함수
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio)
{
	char buf[MAXLINE], request_hdr[MAXLINE], host_hdr[MAXLINE];

	// request header 읽는다.
	while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
	{
		// EOF
		if (strcmp(buf, "\r\n") == 0)
			break;
		// Host
		if (!strncasecmp(buf, "Host", 4))
		{
			strcpy(host_hdr, buf);
			continue;
		}
	}
	// host_hdr가 설정되지 않았을 경우
	if (strlen(host_hdr) == 0)
		sprintf(host_hdr, "Host: %s\r\n", hostname);
	// requset_hdr 설정
	sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);
	// http 헤더 설정
	sprintf(http_header, "%s%s%s%s%s%s",
			request_hdr,
			host_hdr,
			conn_hdr,
			prox_hdr,
			user_agent_hdr,
			"\r\n");
	printf("http_hdr:\n%s\n", http_header);
	return;
}

// 적절한 상태 코드와 상태 메시지를 HTTP 응답과 함께 클라이언트에 보내는 함수
void clienterror(int fd, char *cause, char *errnum,
				 char *shortmsg, char *longmsg)

{
	char buf[MAXLINE], body[MAXLINE];

	/* Build the HTTP response body */
	// sprintf() 함수는 배열 버퍼에 일련의 문자와 값의 형식을 지정하고 저장한다.(배열에 작성된 바이트 수를 리턴)
	sprintf(body, "<html><title>Proxy Error</title>");
	sprintf(body, "%s<body bgcolor="
				  "ffffff"
				  ">\r\n",
			body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}

// parse the uri to get hostname, file path, port
// void parse_uri(char *uri, char *hostname, char *path, int *port)
// {
// 	*port = 80;
// 	char *pos = strstr(uri, "//");

// 	pos = pos != NULL ? pos + 2 : uri;

// 	char *pos2 = strstr(pos, ":");
// 	if (pos2 != NULL)
// 	{
// 		*pos2 = '\0';
// 		sscanf(pos, "%s", hostname);
// 		sscanf(pos2 + 1, "%d%s", port, path);
// 	}
// 	else
// 	{
// 		pos2 = strstr(pos, "/");
// 		if (pos2 != NULL)
// 		{
// 			*pos2 = '\0';
// 			sscanf(pos, "%s", hostname);
// 			*pos2 = '/';
// 			sscanf(pos2, "%s", path);
// 		}
// 		else
// 		{
// 			sscanf(pos, "%s", hostname);
// 		}
// 	}
// 	return;
// }