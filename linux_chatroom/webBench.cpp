#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <iostream>
using namespace std;

long counter_read=0;
long counter_write=0;

//发送的消息内容
static const char* request = "GET http://localhost/index.html HTTP/1.1\r\nConnection: keep-alive\r\n\r\nxxxxxxxxxxxx";

//设置为非阻塞
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

//把文件描述符加入epoll内核监听事件表
void addfd( int epoll_fd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLERR | EPOLLOUT;
    epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

//向服务器sokcfd写数据，非阻塞地写，写完了会返回-1或者0的，所以可以用while
bool write_nbytes( int sockfd, const char* buffer, int len )
{
	usleep(1000);
    int bytes_write = 0;
    // printf( "write out %d bytes to socket %d\n", len, sockfd );
    while( 1 ) 
    {   
        bytes_write = send( sockfd, buffer, len, 0 );
        if ( bytes_write == -1 )
        {   
            return false;
        }   
        else if ( bytes_write == 0 ) 
        {   
            return false;
        }   

        len -= bytes_write;
        buffer = buffer + bytes_write;
        if ( len <= 0 ) 
        {   
            return true;
        }   
    }   
}

//从服务器读数据，非阻塞地读，读完了会返回-1或者0的，所以可以用while
bool read_once( int sockfd, char* buffer, int len )
{
    int bytes_read = 0;
    memset( buffer, '\0', len );
    bytes_read = recv( sockfd, buffer, len, 0 );
    if ( bytes_read == -1 )
    {
        return false;
    }
    else if ( bytes_read == 0 )
    {
        return false;
    }
	// printf( "read in %d bytes from socket %d with content: %s\n", bytes_read, sockfd, buffer );

    return true;
}

//向服务器建立num条连接，每一条连接都会由不同的文件描述符，把他们都丢到epoll监听表上，后面直接从epoll返回的内容操作
void start_conn( int epoll_fd, int num, const char* ip, int port )
{
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );
    int counter=0;
    for ( int i = 0; i < num; ++i )
    {
        usleep( 10000 );
        int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
        printf( "create 1 sock\n" );
        if( sockfd < 0 )
        {
            continue;
        }
        counter++;
        if (  connect( sockfd, ( struct sockaddr* )&address, sizeof( address ) ) == 0  )
        {
            printf( "build connection %d\n", i );
            addfd( epoll_fd, sockfd );
        }
    }
    cout << "成功连接--"<<counter<<"个\n";
}

//先从epoll监听表上删除sockfd，再关闭sockfd
void close_conn( int epoll_fd, int sockfd )
{
    epoll_ctl( epoll_fd, EPOLL_CTL_DEL, sockfd, 0 );
    close( sockfd );
}

int main( int argc, char* argv[] )
{
    assert( argc == 4 );
    //需要 四个参数 ./XXXX ip port num（建立多少个客户连接到服务器）
    int epoll_fd = epoll_create( 100 );
    start_conn( epoll_fd, atoi( argv[ 3 ] ), argv[1], atoi( argv[2] ) );
    epoll_event events[ 10000 ];
    char buffer[ 2048 ];
    int num_send=10*atoi(argv[3]);	//建设每个客户发10条消息，其实控制不了一定每个客户10条，只能确保总数确实有这么多
    //注意：这里有一个点，epoll要同时注册EPOLLIN和EPOLLOUT，才可以同时又收又发，平时我们一般只注册收或者发
    while ( 1 )
    {
        int fds = epoll_wait( epoll_fd, events, 10000, 2000 );
        for ( int i = 0; i < fds; i++ )
        {   
            int sockfd = events[i].data.fd;
            if ( events[i].events & EPOLLIN )
            {   

                if ( ! read_once( sockfd, buffer, 2048 ) )
                {
                    close_conn( epoll_fd, sockfd );
                    continue;
                }

                counter_read++;
                //后面这里多余了，可以删除，之前写代码的时候遗留的
                struct epoll_event event;
                event.events = EPOLLIN | EPOLLET | EPOLLERR | EPOLLOUT;
                event.data.fd = sockfd;
                epoll_ctl( epoll_fd, EPOLL_CTL_MOD, sockfd, &event );

            }
            if( events[i].events & EPOLLOUT ) 
            {
            	if(counter_write == num_send) continue;
                if ( ! write_nbytes( sockfd, request, strlen( request ) ) )
                {
                    close_conn( epoll_fd, sockfd );
                    continue;
                }
                counter_write++;
                //后面这里多余了，可以删除，之前写代码的时候遗留的
                struct epoll_event event;
                event.events = EPOLLIN | EPOLLET | EPOLLERR | EPOLLOUT;
                event.data.fd = sockfd;
                epoll_ctl( epoll_fd, EPOLL_CTL_MOD, sockfd, &event );
                
            }
            else if( events[i].events & EPOLLERR )
            {
                close_conn( epoll_fd, sockfd );
            }
        }
        //counter_read表示目前收到多少个，ideal_read表示如果不丢包应该收到多少个（注意是广播，所以是消息条数*客户人数，
        //比如100客户，每个客户10条，那就是一共收到100*10*100=10W条），counter_write表示目前写了多少
        cout << "counter_read = "<<counter_read<<"\n"<<"counter_write = "<<counter_write<<"ideal counter_read = "
    	<< num_send*atoi(argv[3]) <<endl;
    }

}

