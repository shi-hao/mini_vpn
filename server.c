/*
 * mini vpn 
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <linux/if_tun.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>

#define  SERVER_PORT 9000
#define  BUFF_LEN    1024

#define info_size  40
struct ifreq ifr;
//char client_info[info_size];


int tun_alloc(int flags)
{
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
#define mode_server  0
#define mode_client  1
	if(argc < 5){
		printf("\n usage: ip  port  tun/tap  server/client\n ");
		exit(0);
	}

	printf("\n minivpn start running \n \
			server ip   : %s\n \
			server port : %s\n \
			device mode : %s\n \
			vpn mode    : %s\n", argv[1],argv[2],argv[3],argv[4]);

	/*create the tun/tap device*/
	int  tun_fd, tun_cnt;
	char tun_buf[BUFF_LEN];
	/* Flags: 
	   IFF_TUN
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
		//printf("create tun device\n");
	}else if(strcmp("tap", argv[3]) == 0){
		tun_fd = tun_alloc(IFF_TAP | IFF_NO_PI);
		//printf("create tap device\n");
	}else{
		printf("parameter mode error: tun/tap\n");
		exit(1);
	}
	if (tun_fd < 0) {
		perror("Allocating interface");
		exit(1);
	}
	///////////////////////////////////////////////////////
	/*create the socket*/
	int socket_fd, server_fd, client_fd, ret;
	struct sockaddr_in server_addr, src_addr, dst_addr; 
	int sock_cnt;
	int vpn_mode;
	socklen_t len;
	char sock_buf[BUFF_LEN];  //接收缓冲区，1024字节
	char hi_client[]="hello,client";
	char hi_server[]="hello,server";

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	//server_addr.sin_addr.s_addr = htonl(INADDR_ANY); //IP地址，需要进行网络序转换，INADDR_ANY：本地地址
	//server_addr.sin_port = htons(SERVER_PORT);  //端口号，需要网络序转换
	server_addr.sin_addr.s_addr = inet_addr(argv[1]); //ip
	server_addr.sin_port = htons(atoi(argv[2]));      //端口号，需要网络序转换

	if(strcmp("server", argv[4]) == 0)
	{
		vpn_mode = mode_server;//server
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

		while(1){
			//block until the client connects
			memset(sock_buf, 0, BUFF_LEN);
			len = sizeof(src_addr);
			sock_cnt = recvfrom(server_fd, sock_buf, BUFF_LEN, 0, (struct sockaddr*)&src_addr, &len); 
			if(sock_cnt == -1)
			{
				printf("recvfrom client failed\n");
				exit(0);
			}

			if(strncmp(hi_server, sock_buf, 12) == 0){
				printf("|---receive the client[%s:%d]---|\n",inet_ntoa(src_addr.sin_addr),src_addr.sin_port); 
				sendto(server_fd, hi_client,sizeof(hi_client),0,(struct sockaddr*)&src_addr, sizeof(src_addr));
				break;
			}
		}

		socket_fd = server_fd;
		dst_addr = src_addr;
	}else if(strcmp("client", argv[4]) == 0){
		vpn_mode = mode_client;//client
		client_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if(client_fd < 0)
		{
			printf("create socket fail!\n");
			exit(-1);
		}

		//send hello data to server
		sendto(client_fd, hi_server,sizeof(hi_server), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

		//wait the server response
		len = sizeof(src_addr);
		sock_cnt = recvfrom(client_fd, sock_buf, BUFF_LEN, 0, (struct sockaddr*)&src_addr, &len); 

		if(strncmp(hi_client, sock_buf, 12) != 0){
			printf("connect server failed\n");
			exit(-1);
		}
		printf(" mini vpn connect to server done\n");

		socket_fd = client_fd;
		dst_addr = server_addr;
	}
	////////////////////////////////////////////////////////
	/*linux io multiplexing--poll() tun/tap socket*/
#define nfds            2
#define poll_time      -1
	int fd, poll_ret;
	struct pollfd fds[nfds]; // tun/tap and socket fds

	fds[0].fd = tun_fd;     //tun设备
	fds[1].fd = socket_fd;  //socket

	fds[0].events = POLLIN; // data is ready to read
	fds[1].events = POLLIN; // data is ready to read

	while(1){
		//poll block until the fds is ready, no timeout
		poll_ret = poll(fds, nfds, poll_time);
		if(poll_ret == -1){ //error
			perror("poll()");
		}else if(poll_ret > 0){ //fds is ready
			if( ( fds[0].revents & POLLIN ) ==  POLLIN ){ // tun设备
				//read data from tun/tap device
				tun_cnt = read(fds[0].fd, tun_buf, sizeof(tun_buf));
				if (tun_cnt < 0) {
					perror("Reading from interface");
					close(tun_fd);
					exit(1);
				}
				printf("read %d bytes from %s\n", tun_cnt, ifr.ifr_name);

				//send data to socket
				sock_cnt = sendto(fds[1].fd, tun_buf,tun_cnt,0,(struct sockaddr*)&dst_addr, sizeof(dst_addr)); 
				printf("write %d bytes to [%s:%d]\n",sock_cnt,inet_ntoa(dst_addr.sin_addr),dst_addr.sin_port);
			}else if( ( fds[1].revents & POLLIN ) ==  POLLIN ){ //socket
				//read data from socket
				//memset(sock_buf, 0, BUFF_LEN);
				len = sizeof(src_addr);
				sock_cnt = recvfrom(fds[1].fd, sock_buf, BUFF_LEN, 0, (struct sockaddr*)&src_addr, &len); 

				printf("read %d bytes from [%s:%d]\n",sock_cnt,inet_ntoa(src_addr.sin_addr),src_addr.sin_port);
				if(sock_cnt <= 0)
				{
					printf("recieve data fail!\n");
				}else{ 
					if((vpn_mode == mode_server) && ((dst_addr.sin_addr.s_addr != src_addr.sin_addr.s_addr) || 
								(dst_addr.sin_port != src_addr.sin_port))){
						if(strncmp(hi_server, sock_buf, 12) == 0){
							printf("|----receive the client [%s:%d]----|\n",
									inet_ntoa(src_addr.sin_addr),src_addr.sin_port); 
							sendto(fds[1].fd, hi_client,sizeof(hi_client),0,
									(struct sockaddr*)&src_addr, sizeof(src_addr));
							dst_addr = src_addr;
							continue;
						}
					}
				}

				//send data to tun/tap device
				tun_cnt = write(fds[0].fd, sock_buf, sock_cnt);
				printf("write %d bytes to %s\n",sock_cnt, ifr.ifr_name);
			}
		}else if(poll_ret == 0){ //time out
			printf("poll() time out\n");
		}
	}

	close(fds[0].fd);
	close(fds[1].fd);
	return 0;
}
