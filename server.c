#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "wrap.h"
#include "mylink.h"
#include "qq_ipc.h"

#define MAXLINE 4096
#define SERV_PORT 9999

mylink head = NULL;                         /*定义用户描述客户端信息的结构体*/

struct s_info {                     //定义一个结构体, 将地址结构跟cfd捆绑
	struct sockaddr_in cliaddr;
	int connfd;
};


/*有新用户登录,将该用户插入链表*/
int login_qq(struct QQ_DATA_INFO *buf, int fd, mylink *head)
{

	mylink node = make_node(buf->srcname, fd);  /*利用用户名和文件描述符创建一个节点*/
	if(node == NULL)
		printf("make node error");
	mylink_insert(head, node);                  /*将新创建的节点插入链表*/
	if(head == NULL)
		printf("insert node error");
	return 0;
}

/*客户端发送聊天,服务器负责转发聊天内容*/
void transfer_qq(struct QQ_DATA_INFO *buf, mylink *head)
{
	mylink p = mylink_search(head, buf->destname);      /*遍历链表查询目标用户是否在线*/
	if (p == NULL) {
		struct QQ_DATA_INFO lineout = {3};              /*目标用户不在, 封装3号数据包*/
		strcpy(lineout.destname, buf->destname);        /*将目标用户名写入3号包*/
		mylink q = mylink_search(head, buf->srcname);   /*获取源用户节点,得到对应私有管道文件描述符*/

		Write(q->fifo_fd, &lineout, sizeof(lineout));   /*通过私有管道写给数据来源客户端*/
	} else
		Write(p->fifo_fd, buf, sizeof(*buf));           /*目标用户在线,将数据包写给目标用户*/
	printf("transfer char:%s from %s to %s !\n",buf->data,buf->srcname,buf->destname);
	return;
}


/*客户端发送数据,服务器负责转发或者保存到本地*/
void transfer_data(struct QQ_DATA_INFO *buf, mylink *head)
{
	mylink p = mylink_search(head, buf->srcname);      /*遍历链表查询源用户*/
	printf("receive data from %s to server, total size = %s !\n",buf->srcname,buf->data);
	if(strcmp("S",buf->destname) == 0){

		char file_buf[MAXLINE];
		int fd = open("./test.txt",O_RDWR|O_CREAT|O_TRUNC,0666);
		int len = atoi(buf->data);//文件大小
		ftruncate(fd,len);
		int write_len = 0;//记录已写字节数
		int ret = 0;

		while(write_len < len){
			bzero(file_buf,sizeof(file_buf));
			ret = Read(p->fifo_fd,file_buf,sizeof(file_buf));
			if(ret <= 0)
			{
				printf("\nreceive file done!!!\n");
				break;
			}
			Write(fd, file_buf, ret);
			write_len += ret;//记录写入的字节数 
			//动态的输出接收进度
			//printf("uploading %.2f%% \n", (float)write_len/len * 100);
		}
		Close(fd);
		printf("receive data from %s to server, received size = %d !\n",buf->srcname,write_len);
	}
	return;
}

/*客户端退出*/
void logout_qq(struct QQ_DATA_INFO *buf, mylink *head)
{
	mylink p = mylink_search(head, buf->srcname);       /*从链表找到该客户节点*/

	Close(p->fifo_fd);                                  /*关闭其对应的私有管道文件描述符*/
	mylink_delete(head, p);                             /*将对应节点从链表摘下*/
	free_node(p);                                       /*释放节点*/
}

void err_qq(struct QQ_DATA_INFO *buf)
{
	fprintf(stderr, "bad client %s connect \n", buf->srcname);
}

void *do_work(void *arg)
{
	int n;
	struct s_info *ts = (struct s_info*)arg;
	char str[INET_ADDRSTRLEN];      //#define INET_ADDRSTRLEN 16  可用"[+d"查看
	struct QQ_DATA_INFO buf;                           /*定义数据包结构体对象*/



	while (1) {
		n = Read(ts->connfd, &buf, sizeof(buf));                     //读客户端,分析数据包，处理数据 
		if (n == 0){
			printf("the client %d closed...\n", ts->connfd);
			break;                                              //跳出循环,关闭cfd
		}
		printf("received from %s at PORT %d\n",
				inet_ntop(AF_INET, &(*ts).cliaddr.sin_addr, str, sizeof(str)),
				ntohs((*ts).cliaddr.sin_port));                 //打印客户端信息(IP/PORT)


		switch (buf.protocal) {
			case 1:
				login_qq(&buf, ts->connfd, &head); 
				printf("%s login in, fd = %d !\n",buf.srcname,ts->connfd);
				break;      
			case 2:
				transfer_qq(&buf, &head); 
				break;   
			case 5:
				transfer_data(&buf, &head); 
				break;   
			case 4:
				logout_qq(&buf, &head);
				printf("%s login out!\n",buf.srcname);
				break;
			default:
				err_qq(&buf);
		}

	}
	Close(ts->connfd);

	return (void *)0;
}

int main(void)
{
	struct sockaddr_in servaddr, cliaddr;
	socklen_t cliaddr_len;
	int listenfd, connfd;
	pthread_t tid;
	struct s_info ts[256];      //根据最大线程数创建结构体数组.
	int i = 0;

	listenfd = Socket(AF_INET, SOCK_STREAM, 0);                     //创建一个socket, 得到lfd

	bzero(&servaddr, sizeof(servaddr));                             //地址结构清零
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);                   //指定本地任意IP
	servaddr.sin_port = htons(SERV_PORT);                           //指定端口号 8000

	Bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)); //绑定

	Listen(listenfd, 128);      //设置同一时刻链接服务器上限数

	printf("Accepting client connect ...\n");

	mylink_init(&head);                                 /*初始化链表*/

	while (1) {
		cliaddr_len = sizeof(cliaddr);
		connfd = Accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len);   //阻塞监听客户端链接请求
		ts[i].cliaddr = cliaddr;
		ts[i].connfd = connfd;

		/* 达到线程最大数时，pthread_create出错处理, 增加服务器稳定性 */
		pthread_create(&tid, NULL, do_work, (void*)&ts[i]);
		pthread_detach(tid);                                                    //子线程分离,防止僵线程产生.
		i++;
	}

	return 0;
}

