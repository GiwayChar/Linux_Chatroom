#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/epoll.h>
#include <poll.h>
#include <sys/sendfile.h>
#include <string.h>


#define MAX_SIZE  2048

void show_error(const char * erro_info){
	printf("%s\n", erro_info);
	exit(1);
}

int main(int argc, char const *argv[])
{
	if(argc < 3){
		printf("The correct format is %s server_ip port\n",basename(argv[0]));
		exit(1);
	}

	//得到服务器的IP和端口号
	const char * server_ip = argv[1];
	int server_port = atoi(argv[2]);

	//获取一个套接字
	int server_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) show_error("socket error!");

    //准备一个IP+端口的TCP地址
    struct sockaddr_in server_addr;
    bzero(&server_addr,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    //尝试连接
    int ret;
    ret = connect(server_fd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    if(ret < 0 ) show_error("connect failure!");

    //用poll监听2个fd，一个是标准输入流，一个是服务器套接字。如果输入流有消息，那么就应该将他流入服务器套接字，
    pollfd pfds[2];
    //0号是监听输入流
    pfds[0].fd = 0;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
	//1号监听套接字
	pfds[1].fd = server_fd;
    pfds[1].events = POLLIN | POLLRDHUP;
    pfds[1].revents = 0;
    int pipefd[2];
    ret = pipe( pipefd );
    char buf[MAX_SIZE];

    while(true){
    	//开始处理事务
    	ret = poll(pfds,2,-1);
    	if(ret < 0){
    		show_error("poll failure");
    	}

    	//先处理服务器的输入
    	if( pfds[1].revents & POLLRDHUP )
        {
            printf( "server close the connection\n" );
            break;
        }else if(pfds[1].revents & POLLIN){
            bzero(buf,MAX_SIZE);
            recv(pfds[1].fd,buf,MAX_SIZE-1,0);
            if(ret<0) show_error("network error!");
            printf("%s\n", buf);
    	}
    	//再处理客户输入
    	if(pfds[0].revents & POLLIN){
            ret = splice( pfds[0].fd, NULL, pipefd[1], NULL, MAX_SIZE, SPLICE_F_MORE | SPLICE_F_MOVE );
            if(ret<0) show_error("process input error!");
            ret = splice( pipefd[0], NULL, pfds[1].fd, NULL, MAX_SIZE, SPLICE_F_MORE | SPLICE_F_MOVE );
            if(ret<0) show_error("process input error!");
    	}
    }
    close(server_fd);
	return 0;
}