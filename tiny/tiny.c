/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// HTTP 트랜잭션을 처리하는 함수
void doit(int fd)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, fd);           // rio 초기화
    Rio_readlineb(&rio, buf, MAXLINE); // 한줄 단위로 읽는다.
    printf("Request headers: \n");
    printf("%s", buf);                             // 요청 헤더 출력
    sscanf(buf, "%s %s %s", method, uri, version); // method, uri , version 입력 받기
    // GET method가 아닐 시 error
    if (strcasecmp(method, "GET"))
    {
        clienterror(fd, method, "501", "Not implemented",
                    "Tiny does not implement this method");
        return;
    }
    // 다른 요청의 헤더들은 무시한다.
    read_requesthdrs(&rio);

    /* Parase URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);
    // 파일이 디스크 상에 존재하는지 확인
    if (stat(filename, &sbuf) < 0) // stat() => 파일의 상태나 파일의 정보를 얻는 함수
    {
        clienterror(fd, filename, "404", "Not Found",
                    "Tiny couldn't find this file");
        return;
    }
    // 정적 컨텐츠일 경우
    if (is_static)
    {
        // 일반 파일 여부와 read permission 확인
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't run the CGI program");
            return;
        }
        serve_static(fd, filename, sbuf.st_size);
    }
    // 동적 컨텐츠일 경우
    else
    {
        // 일반 파일 여부와 execute permission 확인
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs);
    }
}

// 적절한 상태 코드와 상태 메시지를 HTTP 응답과 함께 클라이언트에 보내는 함수
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)

{
    char buf[MAXLINE], body[MAXLINE];

    /* Build the HTTP response body */
    // sprintf() 함수는 배열 버퍼에 일련의 문자와 값의 형식을 지정하고 저장한다.(배열에 작성된 바이트 수를 리턴)
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor="
                  "ffffff"
                  ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

// 요청 헤더를 읽고 무시하는 함수 (tiny 서버는 요청 헤더 내의 정보를 사용하지 않는다.)
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    // strcmp() => 두 문자열이 일치하면 return 0, 다르면 return 0이 아닌 값
    while (strcmp(buf, "\r\n"))
    {
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

// uri을 분석하여 정적 컨텐츠와 동척 컨텐츠를 구분하여 처리하는 함수
int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;
    // 정적 컨텐츠일 경우
    if (!strstr(uri, "cgi-bin"))
    {
        strcpy(cgiargs, ""); // CGI인자를 지운다.
        strcpy(filename, ".");
        strcat(filename, uri);
        if (uri[strlen(uri) - 1] == '/')
            strcat(filename, "home.html");
        return 1;
    }
    // 동적 컨텐트일 경우
    else
    {
        ptr = index(uri, '?');
        // CGI인자 추출
        if (ptr)
        {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        // CGI인자를 지운다.
        else
            strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }
}

// 정적 컨텐츠의 내용을 포함하는 HTTP 응답을 보내는 함수
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    // client에게 HTTP 응답과 응답 header 보내기
    get_filetype(filename, filetype);    // filetype에 type이 담긴다.
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); // HTTP 응답 저장.
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); // 빈 줄 하나로 헤더 종료를 나타낸다.
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n");
    printf("%s", buf);

    // client에게 HTTP 응답 body 보내기
    srcfd = Open(filename, O_RDONLY, 0);                        // filename을 오픈하여 식별자를 얻는다.
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // fd가 가리키는 객체를 파일에서 offset 바이트 지점을 기준으로 filesize큼 메모리에 맵핑하도록 커널에 요청한다.
    Close(srcfd);                                               // filename을 닫는다.
    Rio_writen(fd, srcp, filesize);                             //
    Munmap(srcp, filesize);                                     // srcp가 가리키는 메모리 맵핑을 제거한다.
}

// filename으로부터 filetype을 얻는 함수
void get_filetype(char *filename, char *filetype)
{
    // html 타입
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    // gif 타입
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    // png 타입
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    // jpg 타입
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    // text 타입
    else
        strcpy(filetype, "text/plain");
}

// 동적 컨텐츠의 내용을 포함하는 HTTP 응답을 보내는 함수
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = {NULL};

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    // 자식 프로세스를 포크한다.
    if (Fork() == 0)
    {
        setenv("QUERY_STRING", cgiargs, 1);   // QUERY_STRING 환경변수를 URI의 CGI인자들로 설정한다.
        Dup2(fd, STDOUT_FILENO);              // 자식 프로세스는 자식의 표준 출력을 연결 파일 식별자로 재지정한다.
        Execve(filename, emptylist, environ); // CGI 프로그램 실행
    }
    Wait(NULL); // 자식 프로세스가 종료되길 기다린다.
}

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr,
                        &clientlen); // line:netp:tiny:accept
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);  // line:netp:tiny:doit
        Close(connfd); // line:netp:tiny:close
    }
}
