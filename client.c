/* client.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "wrap.h"
#include "qq_ipc.h"
#include "mylink.h"

#define MAXLINE 1024
#define SERV_PORT 9999 


int main(int argc, char *argv[])
{
	char cmdbuf[MAXLINE];
	int sockfd;
	int flag,len;
	struct sockaddr_in servaddr;
	struct QQ_DATA_INFO cbuf,tmpbuf,talkbuf;

	if (argc < 2) {
		printf("./client name\n");
		exit(1);
	}

	sockfd = Socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr.s_addr);
	servaddr.sin_port = htons(SERV_PORT);

	Connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

	cbuf.protocal = 1;                                      /*1号包表登录包*/
	strcpy(cbuf.srcname,argv[1]);                           /*按既定设计结构,将登录者(自己的名字)写入包结构中*/

	flag = fcntl(sockfd, F_GETFL);                    /*设置socket的读写为非阻塞*/
	flag |= O_NONBLOCK;
	fcntl(sockfd, F_SETFL, flag);

	flag = fcntl(STDIN_FILENO, F_GETFL);                    /*设置标准输入缓冲区的读写为非阻塞*/
	flag |= O_NONBLOCK;
	fcntl(STDIN_FILENO, F_SETFL, flag);

	Write(sockfd, &cbuf, sizeof(cbuf));                  /*向公共管道中写入"登录包"数据,表示客户端登录*/



	while (1) {
		len = Read(sockfd, &tmpbuf, sizeof(tmpbuf));     /*读私有管道*/
		if (len > 0) {
			if (tmpbuf.protocal == 3) {                     /*对方不在线*/
				printf("%s is not online\n", tmpbuf.destname);
			} else if (tmpbuf.protocal == 2) {              /*显示对方对自己说的话*/
				printf("%s : %s\n", tmpbuf.srcname, tmpbuf.data);
			}
		}

		len = Read(STDIN_FILENO, cmdbuf, sizeof(cmdbuf));   /*读取客户端用户输入*/
		if (len > 0) {
			char *dname, *dtype, *databuf;
			memset(&talkbuf, 0, sizeof(talkbuf));           /*将存储聊天内容的缓存区清空*/
			cmdbuf[len] = '\0';                             /*填充字符串结束标记*/
			//destname#data type#data 
			//B#C#你好
			//B#F#文件，图片 

			dname = strtok(cmdbuf, "#\n");                  /*按既定格式拆分字符串*/
			dtype = strtok(NULL, "#\n");                  /*按既定格式拆分字符串*/
			printf("dname = %s,dtype = %s\n",dname,dtype);
			if (strcmp("exit", dname) == 0) {               /*退出登录:指定包号,退出者名字*/
				talkbuf.protocal = 4;
				strcpy(talkbuf.srcname, argv[1]);
				Write(sockfd, &talkbuf, sizeof(talkbuf));/*将退出登录包通过公共管道写给服务器*/
				break;
			} else if(strcmp("C",dtype) == 0) {
				talkbuf.protocal = 2;                       /*聊天*/
				strcpy(talkbuf.destname, dname);            /*填充聊天目标客户名*/
				strcpy(talkbuf.srcname, argv[1]);           /*填充发送聊天内容的用户名*/

				databuf = strtok(NULL, "\0");               
				strcpy(talkbuf.data, databuf);
				Write(sockfd, &talkbuf, sizeof(talkbuf));    /*将聊天包写入公共管道*/
			}
			else if(strcmp("F",dtype) == 0){
				talkbuf.protocal = 5;                       /*传输文件*/
				strcpy(talkbuf.destname, dname);            /*填充聊天目标客户名*/
				strcpy(talkbuf.srcname, argv[1]);           /*填充发送聊天内容的用户名*/

				char* file_name = strtok(NULL, "\n");
				char file_info[MAXLINE];
				int fd = open(file_name,O_RDONLY);           /*打开需要传输的文件*/

				int len = lseek(fd, 0, SEEK_END);            /*计算文件大小*/
				printf("send file[%s] begain, total size = %d!!!!\n", file_name,len);

				lseek(fd, 0, SEEK_SET);                      /*文件光标偏移到文件开始位置*/
				sprintf(file_info, "%d", len);               /*记录文件大小并发送到服务器*/
				strcpy(talkbuf.data,file_info);
				Write(sockfd, &talkbuf, sizeof(talkbuf));

				int send_len = 0;                             /*记录发送了多少字节*/
				int ret = 0;
				char file_buf[MAXLINE];
				while (send_len < len)	{	
					bzero(file_buf, sizeof(file_buf));
					ret = Read(fd, file_buf, sizeof(file_buf)); /*按固定大小读取文件*/
					Write(sockfd, &file_buf, ret);
					send_len += ret;//统计发送了多少字节
					usleep(1);
				}
				printf("send file [%s] succeed,send size = %d !!!\n", file_name,send_len);
			}
		}
	}


	Close(sockfd);

	return 0;
}
