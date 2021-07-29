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
#include <list>
#include <sys/types.h>
#include <signal.h>
using namespace std;

#define MAX_CLIENT 2048
#define MAX_LENGTH 2048
//信号-管道-epoll
static  int sig_pipe[2];

const char * postfix = "\n\n";
char ret_info[MAX_LENGTH];

void show_error(const char * erro_info){
	printf("%s\n", erro_info);
	exit(1);
}

int setnonblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

//将fd添加到epoll的内核表中
void addfd(int epollfd,int fd){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP|EPOLLHUP|EPOLLERR;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnonblocking(fd);
}

void broadcast(const char * str,list<int>&client_fds,int excep=-1){
	list<int>::iterator it;
    // strncat(str,postfix,strlen(postfix));
    sprintf(ret_info,"%s%s",str,postfix);
	int ret;
	for (it = client_fds.begin(); it != client_fds.end() ; ++it)
	{
		ret = send(*it,ret_info,MAX_LENGTH,0);
	}
}

void del_fd(int epollfd,int fd,list<int>&client_fds){
	list<int>::iterator it;
	for (it = client_fds.begin(); it != client_fds.end() ; ++it)
	{
		if(*it == fd){
			client_fds.erase(it);
			break;
		}
	}
	epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);

}

void show_offline(char *info,int fd,list<int>&client_fds){
	bzero(info,MAX_LENGTH);
    sprintf(info,"客户端%d下线了...\n>>>>>>当前在线人数%d<<<<<<<",fd,(int)client_fds.size());
    // printf("%s\n", info);
    broadcast(info,client_fds,fd);
}

void show_content(char info[],char prefix[],int fd,list<int>&client_fds){
    bzero(prefix,MAX_LENGTH);
	sprintf(prefix,">>>>>>当前在线人数%d<<<<<<<\n客户端%d说:",(int)client_fds.size(),fd);
	strncat(prefix,info,MAX_LENGTH-strlen(prefix));
	// printf("%s\n", prefix);
	broadcast(prefix,client_fds,fd);
}

void sig_handler(int sig){
	int save_errno = errno;
	int msg = sig;
	send(sig_pipe[1],(char *)&msg,1,0);
	errno = save_errno;
}

void add_sig(int sig){
	struct sigaction sa;
	bzero(&sa,sizeof(sa));
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig,&sa,NULL)!=-1);
}

//服务器还是用epoll吧
int main(int argc, char const *argv[])
{
	if(argc < 3){
		printf("The correct format is %s server_ip port\n",basename(argv[0]));
		exit(1);
	}

	//存放客户的句柄
	list<int>client_fds;

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

    //复用端口
    int opt = 1;
    socklen_t len = sizeof(opt);
    setsockopt(server_fd,SOL_SOCKET, SO_REUSEADDR, &opt, len);

    //绑定端口
    int ret;
    ret = bind(server_fd,(struct sockaddr *)&server_addr,sizeof(server_addr));
    if(ret < 0) show_error("bind error!");

    //监听端口
    ret = listen(server_fd,5);
    if(ret < 0) show_error("listen error!");

    //创建epoll句柄
    epoll_event events[MAX_CLIENT];
    int epollfd = epoll_create(5);
    addfd(epollfd,server_fd);

    //通知使用
    char info[MAX_LENGTH];
    char prefix[MAX_LENGTH];

    //统一事件源
    ret = socketpair(PF_UNIX,SOCK_STREAM,0,sig_pipe);
    setnonblocking(sig_pipe[1]);
    addfd(epollfd,sig_pipe[0]);
    //添加一些事件信号
    add_sig(SIGTERM);
    add_sig(SIGINT);
    bool server_stop=false;
    


    while(!server_stop){
    	//等待epoll事件，阻塞等待
    	int number = epoll_wait(epollfd,events,MAX_CLIENT,-1);
    	if(number<0 && (errno!=EINTR)) show_error("epoll_wait failure!");

    	for (int i = 0; i < number; ++i)
    	{
    		int sockfd = events[i].data.fd;
    		if(sockfd == server_fd){
    			//有新的客户连接，拿一个句柄出来连接
    			struct sockaddr_in client_addr;
                socklen_t client_addrlen = sizeof(client_addr);
                int connfd = accept(server_fd,(struct sockaddr *)&client_addr,&client_addrlen);
                addfd(epollfd,connfd);

                //放入订阅者
                client_fds.push_back(connfd);

                //显示
                // bzero(info,MAX_LENGTH);
                // sprintf(info,"客户端%d加入聊天室\n>>>>>>>当前在线人数%d<<<<<<",connfd,(int)client_fds.size());
                // broadcast(info,client_fds);
                // printf("%s\n", info);
    		}
    		else if(sockfd == sig_pipe[0] && (events[i].events & EPOLLIN)){
                printf("服务器即将退出...\n");
    			// broadcast("服务器即将退出...",client_fds);
    			server_stop = true;
                //退出后可以关闭epollfd和server_fd，正常关闭资源
    		}
    		else if(events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
    			//如果是客户端下线了
    			//从内核事件表中删除这个句柄，从订阅者中删除
    			del_fd(epollfd, events[i].data.fd,client_fds);
    			//显示客户下线
    			// show_offline(info,events[i].data.fd,client_fds);
    		}else if(events[i].events & EPOLLIN){
    			//有客户要写
    			//读取客户内容，然后broadcast
    			bzero(info,MAX_LENGTH);
				recv(events[i].data.fd,info,MAX_LENGTH,0);
				// printf("00==%d\n",strlen(info) );
    			if(strcmp(info,"quit")==0){
    				//客户主动要求下线
    				del_fd(epollfd, events[i].data.fd,client_fds);
    				// show_offline(info,events[i].data.fd,client_fds);
    			}else{
    				show_content(info,prefix,events[i].data.fd,client_fds);
    			}
    		}
    	}
    }

    close(epollfd);
    close(server_fd);
	return 0;
}