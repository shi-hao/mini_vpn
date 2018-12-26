/*
 * mini vpn 
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include<time.h>
#include<unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <linux/if_tun.h>
#include<stdlib.h>
#include<stdio.h>
#include<poll.h>

#define  SERVER_PORT 9000
#define  BUFF_LEN    1024

int tun_alloc(int flags)
{
	struct ifreq ifr;
	int fd, err;
	char *clonedev;

	clonedev = "/dev/net/tun";
	if ((fd = open(clonedev, O_RDWR)) < 0) {
		printf("open dev failed\n");
		return fd;
	}
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = flags;
	if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
		close(fd);
		return err;
	}
	printf("open tun/tap device: %s for reading...\n", ifr.ifr_name);
	return fd;
}

/*
 * mini vpn main
 */
int main(int argc, char* argv[])
{
	if(argc < 5){
		printf("\n usage: ip  port  tun/tap  server/client\n ");
		exit(0);
	}

	printf("\n minivpn start running \n \
			server ip  : %s\n \
			server port: %s\n \
			device     : %s\n \
			vpn mode   : %s\n", argv[1],argv[2],argv[3],argv[4]);

	///////////////////////////////////////////////////////////////////
	int tun_fd, tun_cnt;
	char tun_buf[BUFF_LEN];
	/* Flags: IFF_TUN
	   - TUN device (no Ethernet headers)
	 *
	 IFF_TAP
	 - TAP device
	 *
	 IFF_NO_PI - Do not provide packet information
	 */
	if(strcmp("tun", argv[3]) == 0)
	{
		tun_fd = tun_alloc(IFF_TUN | IFF_NO_PI);
		printf("create tun device\n");
	}else if(strcmp("tap", argv[3]) == 0){
		tun_fd = tun_alloc(IFF_TAP | IFF_NO_PI);
		printf("create tap device\n");
	}else{
		printf("set mode error: tun/tap\n");
		exit(1);
	}
	if (tun_fd < 0) {
		perror("Allocating interface");
		exit(1);
	}

	///////////////////////////////////////////////////////////////////
	int socket_fd, server_fd, client_fd;
	struct sockaddr_in server_addr, client_addr, dst_addr; 
	int sock_cnt;
	socklen_t len;
	char sock_buf[BUFF_LEN];  //接收缓冲区，1024字节

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	//server_addr.sin_addr.s_addr = htonl(INADDR_ANY); //IP地址，需要进行网络序转换，INADDR_ANY：本地地址
	//server_addr.sin_port = htons(SERVER_PORT);  //端口号，需要网络序转换
	server_addr.sin_addr.s_addr = inet_addr(argv[1]); //ip
	server_addr.sin_port = htons(atoi(argv[2]));      //端口号，需要网络序转换

	if(strcmp("server", argv[4]) == 0)
	{
		server_fd = socket(AF_INET, SOCK_DGRAM, 0); //AF_INET:IPV4;SOCK_DGRAM:UDP
		if(server_fd < 0)
		{
			printf("create socket fail!\n");
			return -1;
		}

		if( bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
		{
			printf("socket bind fail!\n");
			return -1;
		}

#if 1
		//block until the client connects
		memset(sock_buf, 0, BUFF_LEN);
		len = sizeof(client_addr);
		sock_cnt = recvfrom(server_fd, sock_buf, BUFF_LEN, 0, (struct sockaddr*)&client_addr, &len); 
		if(sock_cnt == -1)
		{
			printf("recvfrom client failed\n");
		}
		printf("read %d byte data:%s\n",sock_cnt, sock_buf);  //打印client发过来的信息
		printf("client_addr.sin_addr:%s\n",inet_ntoa(client_addr.sin_addr));  //打印client ip
		printf("client_addr.sin_port:%d\n",client_addr.sin_port);  //打印client port
#endif

		socket_fd = server_fd;
		dst_addr = client_addr;
	}else if(strcmp("client", argv[4]) == 0){
		char say_hello[]="hello, i am client!";
		client_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if(client_fd < 0)
		{
			printf("create socket fail!\n");
			return -1;
		}

		//send hello data to server
		sendto(client_fd, say_hello,sizeof(say_hello), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

		socket_fd = client_fd;
		dst_addr = server_addr;
	}
	////////////////////////////////////////////////////////
	int fd, poll_ret;
	struct pollfd fds[2]; // 监视文件描述符结构体，2 个元素

	fds[0].fd = tun_fd;     // tun设备
	fds[1].fd = socket_fd;  //socket

	fds[0].events = POLLIN; // 普通或优先级带数据可读
	fds[1].events = POLLIN; // 普通或优先级带数据可读

	while(1){
		// 监视并等待多个文件（标准输入，有名管道）描述符的属性变化（是否可读）
		// 没有属性变化，这个函数会阻塞，直到有变化才往下执行，这里没有设置超时
		poll_ret = poll(fds, 2, -1);
		//poll_ret = poll(&fd, 2, 1000);

		if(poll_ret == -1){ //出错
			perror("poll()");
		}else if(poll_ret > 0){ //文件描述符就绪
			if( ( fds[0].revents & POLLIN ) ==  POLLIN ){ // tun设备
				//read data from tun/tap device
				tun_cnt = read(fds[0].fd, tun_buf, sizeof(tun_buf));
				if (tun_cnt < 0) {
					perror("Reading from interface");
					close(tun_fd);
					exit(1);
				}
				printf("read %d bytes from tun/tap device\n", tun_cnt);

				//send data to socket
				sock_cnt = sendto(fds[1].fd, tun_buf,tun_cnt,0,(struct sockaddr*)&dst_addr, sizeof(dst_addr)); 
				printf("write %d bytes to socket\n", sock_cnt);
			}else if( ( fds[1].revents & POLLIN ) ==  POLLIN ){ //socket
				//read data from socket
				len = sizeof(client_addr);
				sock_cnt = recvfrom(fds[1].fd, sock_buf, BUFF_LEN, 0, (struct sockaddr*)&client_addr, &len); 
#if 1
				if((dst_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr) || 
						(dst_addr.sin_port != client_addr.sin_port)){
					dst_addr = client_addr;
					printf("read %d byte data:%s\n",sock_cnt, sock_buf);  //打印client发过来的信息
					printf("client_addr.sin_addr:%s\n",inet_ntoa(client_addr.sin_addr));  //打印client ip
					printf("client_addr.sin_port:%d\n",client_addr.sin_port);  //打印client port
				}
#endif
				printf("read %d bytes from socket\n", sock_cnt);
				if(sock_cnt == -1)
				{
					printf("recieve data fail!\n");
				}

				//send data to tun/tap device
				tun_cnt = write(fds[0].fd, sock_buf, sock_cnt);
				printf("write %d bytes to tun/tap\n", tun_cnt);
			}
		}else if(poll_ret == 0){ // 超时
			printf("poll time out\n");
		}
	}

	close(fds[0].fd);
	close(fds[1].fd);
	return 0;
}
