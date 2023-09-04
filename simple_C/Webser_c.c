/*****************************************************************************************
 * File name: Webser_c.c
 * Description: 此程序实现了一个简单的静态文件服务器，
 *              能够接收HTTP请求并向客户端发送请求的文件内容或错误响应。
 *              这是一个非常基本的示例，用于说明如何设置一个最小功能的Web服务器。
 * Author: Zhengzhi
 * Date: 2023.09.04
 * Version: v1
 * **************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

int socket_init();
void do_work(int c);

int main()
{
    int sockfd = socket_init();
    if ( sockfd == -1 )
    {
        printf("socket init err\n");
        exit(1);
    }
    struct sockaddr_in caddr;
    int len = -1;
    while( 1 )
    {
        len = sizeof(caddr);
        int c = accept(sockfd,(struct sockaddr*)&caddr,&len);
        if ( c < 0 )
        {
            continue;
        }
        do_work(c);
        close(c);
    }
}

char* get_name(char buff[])
{
    if ( buff == NULL )
    {
        return NULL;
    }
    char * s = strtok(buff," ");
    if ( s == NULL )
    {
        return NULL;
    }
    printf("请求方法:%s\n",s);
    s = strtok(NULL," ");
    return s;
}

void do_work(int c)
{
    char buff[512] = {0};
    int n = recv(c,buff,511,0);
    if ( n <= 0 )
    {
        return;
    }
    char * filename = get_name(buff);
    printf("filename:%s\n",filename);
    char path[128] = {"/home/ubuntu/Webser/simple_C"};
    if ( strcmp(filename,"/") == 0 )
    {
        strcat(path,"/index.html");
    }
    else
    {
        strcat(path,filename);
    }
    int fd = open(path,O_RDONLY);
    if ( fd == -1 )
    {
        send(c,"404",3,0);
        return;
    }
    int filesize = lseek(fd,0,SEEK_END);
    lseek(fd,0,SEEK_SET);
    char http_head[512] = {"HTTP/1.0 200 OK\r\n"};
    strcat(http_head,"Server: myhttp\r\n");
    sprintf(http_head + strlen(http_head),"Content-Length:%d\r\n",filesize);
    strcat(http_head,"\r\n");
    
    send(c,http_head,strlen(http_head),0);
    printf("head:\n%s\n",http_head);
    char data[1024] = {0};
    int len = 0;
    while( (len = read(fd,data,1024)) > 0 )
    {
        send(c,data,len,0);
    }
    close(fd);
}

int socket_init()
{
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    if ( sockfd == -1 )
    {
        return -1;
    }
    struct sockaddr_in saddr;
    memset(&saddr,0,sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(9000);//80
    saddr.sin_addr.s_addr = inet_addr("0.0.0.0");//121.4.121.204:9000
    int res = bind(sockfd,(struct
    sockaddr*)&saddr,sizeof(saddr));
    if ( res == -1 )
    {
        return -1;
    }
    if ( listen(sockfd,5) == -1 )
    {
        return -1;
    }
    return sockfd;
}