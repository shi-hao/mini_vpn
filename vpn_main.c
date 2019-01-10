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
#include <error.h>

#define  SERVER_PORT 9000

#define info_size  40
struct ifreq ifr;
//char client_info[info_size];


#define   MAX_PARAM   2
#define   STAT_START  0
#define   STAT_PRE    1
#define   Token        ' '
typedef struct vpn_setting{
	char * server_ip;
	char * port;
	char * device;
	char * vpn_mode;
}vpn_setting,*vpn_setting_p;

static void parse_line(char *p[], char* str){
	int state; 
	int  line_len;
	char *in;
	char *start;
	int  pos;
	line_len = strlen(str) - 1;
	start =	NULL;
	in = str;
	state = STAT_PRE;
	pos = 0;
	while(line_len--){
		// '#' is the comment token
		if(*in == '#'){
			break;
		}

		// ' ' is the parameter separator
		if(state == STAT_PRE){
			if(*in != Token){
				start = in;
				state = STAT_START;
			}
			in++;
		}else if(state == STAT_START){
			if((*in == Token) || (line_len == 0)){
				int str_len;
				if(*in == Token) str_len = in-start;
				else str_len = in-start+1;
				if(pos >= MAX_PARAM){
					printf("param num is out of range\n");
					break;
				}
				p[pos] =(unsigned char*)malloc(str_len + 1);
				memcpy(p[pos], start, str_len);
				*(p[pos] + str_len) = '\0';
				pos++;              
				state = STAT_PRE;
			}
			in++;
		}
	}
}

static int  parse_cmd(vpn_setting_p setting, char*p[]){

	if(p[0] == NULL)
		return -1;

	if(strcmp(p[0], "ip")==0 && p[1]){
		setting->server_ip = p[1];
	}else if(strcmp(p[0], "port")==0 && p[1]){
		setting->port = p[1];
	}else if(strcmp(p[0], "device")==0 && p[1]){
		setting->device = p[1];
	}else if(strcmp(p[0], "mode")==0 && p[1]){
		setting->vpn_mode = p[1];
	}
}

#define  BUFF_LEN    2048
typedef struct _buff_ {
	int capacity;
	char *buffer;
	char *data;
	int offset;
	int len;
}st_buff, *st_buff_p;

int st_buff_init(st_buff_p buff){
	if((buff->buffer = (unsigned char*)malloc(BUFF_LEN)) != NULL){
		buff->data = buff->buffer;
		buff->capacity = BUFF_LEN;
		buff->offset = 0;
		buff->len = 0;
		return 0;
	}else{
		return -1;
	}
}

int st_buff_clear(st_buff_p buff){
	buff->data = buff->buffer;
	buff->offset = 0;
	buff->len = 0;
}

int st_buff_free(st_buff_p buff){
	free(buff->buffer);
}

#define  P_CONTROL_HARD_RESET_CLIENT_V2   7     /* initial key from client, forget previous state */
#define  P_CONTROL_HARD_RESET_SERVER_V2   8
#define  P_DATA_V2                        9
#define  OP_SHIFT(opcode)                 (opcode >> 3)

struct ip_hdr {
#define IPH_GET_VER(v) (((v) >> 4) & 0x0F)
#define IPH_GET_LEN(v) (((v) & 0x0F) << 2)
	uint8_t  version_len;

	uint8_t  tos;
	uint16_t tot_len;
	uint16_t id;

#define IP_OFFMASK 0x1fff
	uint16_t frag_off;

	uint8_t  ttl;

	uint8_t  protocol;

	uint16_t check;
	uint32_t saddr;
	uint32_t daddr;
	/*The options start here. */
};


static unsigned char parse_packet(st_buff_p buff){
	unsigned char opcode = *(buff->data) >> 3;
	buff->data++;
	buff->offset++;
	buff->len--;

	if(opcode == P_CONTROL_HARD_RESET_CLIENT_V2){
		if(strncmp(buff->data, "hello,server", 12) == 0){
			return P_CONTROL_HARD_RESET_CLIENT_V2;
		}
	}else if(opcode == P_CONTROL_HARD_RESET_SERVER_V2){
		if(strncmp(buff->data, "hello,client", 12) == 0){
			return P_CONTROL_HARD_RESET_SERVER_V2;
		}
	}else if(opcode == P_DATA_V2){
		return P_DATA_V2;
	}
	return -1;
}

static char pack_packet(st_buff_p buff, unsigned char* data, int len, unsigned char opcode){
	*(buff->buffer) = opcode;
	buff->len++;
	if((buff->capacity-1) > len){
		memcpy(buff->buffer+1, data, len);	
		buff->len+=len;
		return 0;
	}
	return -1;
}


static int tun_alloc(int flags)
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
vpn_setting msetting;

int main(int argc, char* argv[])
{
#define  mode_server  0
#define  mode_client  1

	char* p[MAX_PARAM]={NULL};
	FILE* file;
	char str[BUFF_LEN];

	if(argc < 2){
		printf("\n usage: [dir][file_name]\n ");
		exit(0);
	}
	/* open the vpn config file*/
	if((file = fopen(argv[1], "r")) <= 0 )
	{
		printf("open the config file %s fail\n",argv[1]);
		exit(-1);
	}

	//load the config file setting
	while(fgets(str, BUFF_LEN, file)){
		parse_line(p, str);
		parse_cmd(&msetting, p);
		free(p[0]);
		p[0] = NULL;
		p[1] = NULL;
	}

	//print the config info
	printf("\n minivpn start running \n \
			server ip   : %s\n \
			server port : %s\n \
			device mode : %s\n \
			vpn mode    : %s\n", msetting.server_ip,msetting.port,msetting.device,msetting.vpn_mode);

	///////////////////////////////////////////////////////////////////
	/*create the tun/tap device*/
	int  tun_fd, tun_cnt;
	st_buff tun_buf; 
	if(st_buff_init(&tun_buf) < 0){
		printf("tun_buf malloc failed exit\n");
		return 0;
	}
	/* 
	 *Flags: 
	 *  IFF_TUN   - TUN device (no Ethernet headers)
	 *  IFF_TAP   - TAP device
	 *  IFF_NO_PI - Do not provide packet information
	 */
	if(strcmp("tun", msetting.device) == 0)
	{
		tun_fd = tun_alloc(IFF_TUN | IFF_NO_PI);
	}else if(strcmp("tap", msetting.device) == 0){
		tun_fd = tun_alloc(IFF_TAP | IFF_NO_PI);
	}else{
		printf("config file device mode error: tun/tap\n");
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
	char hi_client[56]={0};
	char hi_server[56]={0};

	st_buff sock_buf;  
	if(st_buff_init(&sock_buf) < 0){
		printf("sock_buf malloc failed exit\n");
		return 0;
	}

	bzero(&server_addr,sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	//server_addr.sin_addr.s_addr = htonl(INADDR_ANY); //IP地址，需要进行网络序转换，INADDR_ANY：本地地址
	//server_addr.sin_port = htons(SERVER_PORT);  //端口号，需要网络序转换
	server_addr.sin_addr.s_addr = inet_addr(msetting.server_ip); //ip
	server_addr.sin_port = htons(atoi(msetting.port));      //端口号，需要网络序转换

	if(strcmp("server", msetting.vpn_mode) == 0)
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

		socket_fd = server_fd;
		//dst_addr = NULL;
	}else if(strcmp("client", msetting.vpn_mode) == 0){
		vpn_mode = mode_client;//client
		client_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if(client_fd < 0)
		{
			printf("create socket fail!\n");
			exit(-1);
		}

		while(1){
			struct timeval timeout;
			timeout.tv_sec = 5;
			timeout.tv_usec = 0;

			//send hello data to server
			hi_server[0] = P_CONTROL_HARD_RESET_CLIENT_V2 << 3;
			strcat(&hi_server[1],"hello,server");
			sendto(client_fd, hi_server,strlen(hi_server), 0, (struct sockaddr*)&server_addr, 
					sizeof(server_addr));
			printf("send hello to server\n");

			//wait the server response
			if (setsockopt (client_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
				perror("setsockopt failed\n");
			len = sizeof(src_addr);
			printf("waiting the server response\n");
			sock_buf.len = recvfrom(client_fd, sock_buf.buffer, sock_buf.capacity, 0, 
					(struct sockaddr*)&src_addr, &len); 

			if(parse_packet(&sock_buf) == P_CONTROL_HARD_RESET_SERVER_V2){
				timeout.tv_sec = 0;
				timeout.tv_usec = 0;
				if (setsockopt (client_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
					perror("setsockopt failed\n");
				printf(" mini vpn connect to server done\n");
				break;
			}
		}

		socket_fd = client_fd;
		dst_addr = server_addr;
	}
	////////////////////////////////////////////////////////
	/*linux io multiplexing poll(): tun/tap fd and udp socket fd*/
#define  nfds            2
#define  poll_time      -1
	int fd, poll_ret;
	struct pollfd fds[nfds]; // tun/tap and socket fds

	fds[0].fd = tun_fd;     //tun device
	fds[1].fd = socket_fd;  //socket

	fds[0].events = POLLIN; // data is ready to read
	fds[1].events = POLLIN; // data is ready to read

	while(1){
		//poll block until the fds is ready, no timeout
		poll_ret = poll(fds, nfds, poll_time);
		if(poll_ret == -1){ //error
			perror("poll()");
		}else if(poll_ret == 0){
			printf("poll() time out\n");//time out
		}else if(poll_ret > 0){ //fds is ready
			if((fds[0].revents & POLLIN) ==  POLLIN){ // tun/tap device
				//read data from tun/tap device
				st_buff_clear(&tun_buf);
				tun_buf.buffer[0] = P_DATA_V2 << 3;
				tun_buf.len++;
				tun_buf.len += read(fds[0].fd, tun_buf.buffer+1, tun_buf.capacity-1);
				if (tun_buf.len < 0) {
					perror("Reading from interface");
					close(tun_fd);
					exit(1);
				}
				printf("read %d bytes from %s\n", tun_buf.len, ifr.ifr_name);

				//send data to socket
				sock_cnt = sendto(fds[1].fd, tun_buf.data,tun_buf.len,0,
						(struct sockaddr*)&dst_addr, sizeof(dst_addr)); 
				printf("write %d bytes to [%s:%d]\n",sock_cnt,inet_ntoa(dst_addr.sin_addr),
						ntohs(dst_addr.sin_port));
			}else if((fds[1].revents & POLLIN) ==  POLLIN){ //socket
				//read data from socket
				//memset(sock_buf, 0, BUFF_LEN);
				st_buff_clear(&sock_buf);
				len = sizeof(src_addr);
				sock_buf.len = recvfrom(fds[1].fd, sock_buf.buffer, sock_buf.capacity, 0, 
						(struct sockaddr*)&src_addr, &len); 
				printf("read %d bytes from [%s:%d]\n",sock_buf.len,inet_ntoa(src_addr.sin_addr),
						ntohs(src_addr.sin_port));

				if(sock_buf.len <= 0)
				{
					printf("recvfrom data failed\n");
					continue;
				} 

				unsigned char opcode = parse_packet(&sock_buf);
				switch(opcode){
					case P_CONTROL_HARD_RESET_CLIENT_V2:
						printf("|----receive the client [%s:%d]----|\n",
								inet_ntoa(src_addr.sin_addr),ntohs(src_addr.sin_port)); 
						hi_client[0] = P_CONTROL_HARD_RESET_SERVER_V2 << 3;
						strcat(&hi_client[1],"hello,client");
						sendto(fds[1].fd, hi_client,strlen(hi_client),0,
								(struct sockaddr*)&src_addr, sizeof(src_addr));
						dst_addr = src_addr;
						continue;
						break;
					case P_CONTROL_HARD_RESET_SERVER_V2:
						break;
					case P_DATA_V2:
						break;
					default:
						break;
				}

				struct ip_hdr *ip_header =(struct ip_hdr*)sock_buf.data;
				printf("saddr = %#.8x\n", ip_header->saddr);
				printf("daddr = %#.8x\n", ip_header->daddr);

				//send data to tun/tap device
				tun_cnt = write(fds[0].fd, sock_buf.data, sock_buf.len);
				printf("write %d bytes to %s\n",tun_cnt, ifr.ifr_name);
			}
		}
	}

	close(fds[0].fd);
	close(fds[1].fd);
	return 0;
}
