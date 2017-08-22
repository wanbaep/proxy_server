#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <pwd.h>
#include <dirent.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#define BUFFSIZE	2048	//buf size
#define REQSIZE	2048	//packet size
#define PORTNO		40077	//port number
#define SPORT		80		//HTTP port number

//Global Variables
int browser_fd, web_fd, cache_fd;	//Global Variables to use in Signal Handler
char* file;		//hashed URL file pathname
time_t now;			//time_t type variable to get system current time

char* sha1_hash(char* input_url);	//Making hashed_URL function
char* getHomeDir();		//Getting home directory function
char* getIPAddr(char* addr);	//Get IP from Hostname function
void myhandler(int signo);	//Signal Handler Function
unsigned short stringtoshort(char* port);	//user define function which change string to unsigned short value
void p(int semid);	//semaphore p operation lock
void v(int semid);	//semaphore v operation unlock
void logfile(int semid,char* log);	//log url or hashed_url that critical section

int main()
{
	struct sockaddr_in proxy_addr, browser_addr, web_addr;	//declare sockaddr_in structure type for server&client address
	int proxy_fd;	//proxy server socket file descripter
	int len, len_out;				//length of client address, length of requested message
	char buf[BUFFSIZE];			//buffer
	char request[REQSIZE];		//request message
	char addr[] = "128.134.52.60";	//Address of Proxy Server
	int opt=1;			//set socket option value
	char* host;			//point string tokend value of host name
	char* port;
	char* hostname;	//save host name array
	char* portnum;
	char* ipaddr;		//point IP address
	pid_t pid;			//pid struct to get pid
	char proc[10];		//array to save pid infomation
	char* url;	
	char* hashed;			//hashed URL
	char* hashed_log;	//hashed URL to log in logfile
	char* home;			//home directory / proxy_cache
	char* dir1;			//hashed URL first directory pathname
  	char* dir2;			//hashed URL second directory pathname
	char* token;		//to use in strtok function
	int search=0;	//to find original directory or file is exist
	int i=0;			//index number i
	int Hit=0;		//variable of Hit or Miss judging
	struct dirent* pFile;	//directory information pointer
	DIR *pDir;			//point directory
	FILE* fout;			//FILE Stream pointer
	unsigned short real_port=0;	//real port number
	int semid;		//semaphore ID value

	//sem union variable
	union semun{
		int val;	//to use SETVAL
		struct semid_ds* buf;	//to use IPC_STAT or IPC_SET buffer
		unsigned short int* array;	//to use GETALL, SETALL command array
	}arg;	//name of variable

	if((semid = semget((key_t)40077,1,IPC_CREAT|0666))==-1)	//Create Semaphore with IPC_CREAT flags and ID is My Port 40077
	{
		printf("semget failed\n");	//fail case
		exit(1);
	}

	arg.val = 1;	//arg member variable val is set one
	if((semctl(semid,0,SETVAL,arg))==-1)	//control semaphore function with SETVAL
	{
		printf("semctl failed\n");
		exit(1);
	}

	memset(proc,0,strlen(proc));	//set zero of pid array
	memset(buf,0,strlen(buf));	//set zero of buffer
	memset(request,0,strlen(request));	//set zero of request message
	umask(0);

	//signal install
	if(signal(SIGALRM, myhandler)==SIG_ERR)	//install SIGALRM with handler is 'myhandler' function
		printf("Can't Catch SIGALRM\n");		//Error case
	if(signal(SIGCHLD, myhandler)==SIG_ERR)	//install SIGCHLD with handler is 'myhandler' function
		printf("Can't Catch SIGCHLD\n");		//Error case
	
	////make 'proxy_cache' directory if not Exist
	home=getHomeDir();	//get home directory path name
	pDir=opendir(home);	//pDir point home pathname directory
	//make pathname which ~/proxy_cache
	strcat(home,"/");
	strcat(home,"proxy_cache");
		
	if(pDir==NULL)	//Directory pointer is Null
	{
		printf("Dir read error\n");
		return 0;
	}
	else	//if something directory or file is exist
	{
		for(pFile=readdir(pDir);pFile;pFile=readdir(pDir))	//execute until pFile which get Directory information is Null
		{
			if(strcmp(pFile->d_name,"proxy_cache")==0)	//find same name directory
			{
				search=1;	//search control variable is set
				break;
			}
		}
	
		if(search==0)	//if there is no such a directory
			mkdir(home, S_IRWXU | S_IRWXG | S_IRWXO);	//make directory
	}
	fout=fopen("logfile","a+");	//FILE Strem open with 'logfile'name and a+ condition, it means concantanous write with open	or create
	chmod("logfile",S_IRWXU|S_IRWXG|S_IRWXO);	//change mode permission would be 777
	fclose(fout);	//close file
	
	search=0;	//be zero to use next control
	closedir(pDir);	//close directory
		
	if((proxy_fd = socket(PF_INET,SOCK_STREAM,0))<0)	//Create Socket with 'PF_INET' and 'SOCK_STREAM' it means TCP connection
	{
		printf("Proxy Server: Can't open stream socket.");	//if not create socket print error message
		return 0;	//exit program
	}
	setsockopt(proxy_fd,SOL_SOCKET, SO_REUSEADDR,&opt,sizeof(opt));	//To prevent bind error

	bzero((char*)&proxy_addr, sizeof(proxy_addr));	//initialize struct with zero
	proxy_addr.sin_family = AF_INET;						//server_addr struct data's sin_family member variable get AF_INET Protocol internet domain
	proxy_addr.sin_addr.s_addr = inet_addr(addr);	//server IP address would be host address to network type(Big endian) bytes any address
	proxy_addr.sin_port = htons(PORTNO);					//Port Number would be network type bytes(Big endian) with 'PORTNO' -> Port variable is unsigned short type
	
	
	if(bind(proxy_fd, (struct sockaddr *)&proxy_addr,sizeof(proxy_addr))<0)	//Connect Socket with Address server_addr would be socket address
	{
		printf("Proxy Server: Can't bind local address.\n");	//if not bind print error message
		return 0;
	}

	listen(proxy_fd,5);	//server announces that it is willing to accept connect request
	
	len = sizeof(browser_addr);	//Get client address length

	while(1)	//Server
	{
		//Parent Process Area
		browser_fd = accept(proxy_fd, (struct sockaddr*)&browser_addr,&len);	//server accept client and return client socket descripter		
		if(browser_fd < 0)	//fail to access
		{
			printf("Proxy Server: accept failed.\n");
			return 0;
		}
		time(&now);		//Get current time using time function
		
		if((pid=fork())<0)	//fork -> Create Child Process
			printf("Fork error\n");
		else if(pid==0)		//Child Process Area
		{
			//initalize about array & pointer using in child
			memset(buf,0,BUFFSIZE);
			host = NULL;
			memset(request,0,REQSIZE);
			token = NULL;
			port = NULL;
			ipaddr=NULL;
			portnum=NULL;
			hashed=NULL;
			url=NULL;
			hashed_log=NULL;
			sprintf(proc,"%d",getpid());	//getpid change to character
				
			if((len_out = read(browser_fd, buf, BUFFSIZE))>0)	//Read Request information from Browser
			{

				write(STDOUT_FILENO, "\n* PID : ",9);		//print PID of child
				write(STDOUT_FILENO, proc,strlen(proc));	
				write(STDOUT_FILENO, "\n* request :\n", 13);	//print received from client information with echo in standard out which Terminal
				strcpy(request,buf);	//copy of request message by using string copy
			
				////Fetching Url from HTTP Request
				token = strtok(buf," ");	//string token by space delimeter
				token = strtok(NULL," ");	//again then token indicate URL
				url = (char*)malloc(sizeof(char)*(strlen(token)+1));	//memory allocation about url size with token
				memset(url,0,sizeof(char)*(strlen(token)+1));	//memory set zero about url
				strcpy(url,token);	//copy token to url
				
				////Fetching Hostname & if port number is exist get port number
				token = strtok(NULL,"\r\n");	//next header field
				token=strtok(NULL," ");			//token by space that means field
				while(strcmp(token,"Host:")!=0)	//Do until HTTP field is Host:
				{
					token=strtok(NULL,"\n");	//token by \n next field
					token=strtok(NULL," ");	//token by space to get field
				}
				token=strtok(NULL,"\r\n");	//token by '\r'or'\n' again this would point host name
				
				host=strtok(token,":");		//if port number is exist or not get host by ':' delimeter
				port=strtok(NULL,"NULL");	//token by NULL
				if(port!=NULL)	//if port is exist
				{
					portnum=(char*)malloc(sizeof(char)*(strlen(port)+1));	//memory allocation about port number
					memset(portnum,0,sizeof(char)*(strlen(port)+1));		//memory set with zero
					strcpy(portnum,port);					//copy port number
				}

				ipaddr = getIPAddr(host);	//get IP Address by host name
				hostname=(char*)malloc(sizeof(char)*strlen(host));	//memory allocation to save host name value
				memset(hostname,0,sizeof(char)*strlen(host));
				strcpy(hostname,host);	//string copy host to hostname value
				
				memset(buf,0,BUFFSIZE);	//buffer set to zero
			}

			//print Hostname & IP Address to Terminal
			write(STDOUT_FILENO,request, strlen(request));
			write(STDOUT_FILENO,"\n* Hostname : ",15);
			write(STDOUT_FILENO,hostname,strlen(hostname));
			write(STDOUT_FILENO,"\n* IP address : ",16);
			write(STDOUT_FILENO,ipaddr,strlen(ipaddr));
			write(STDOUT_FILENO,"\n",1);
		
			//Determine Port Number
			if(portnum==NULL)	//if port number is not exist
				real_port=SPORT;		//Real Port number is 80
			else
				real_port=stringtoshort(portnum);	//Real Port number is value would be string to short function return value

			free(portnum);		//free dynamic allocation
			
			////Hashing URL Stage
			hashed=sha1_hash(url);	//call hashing function with argument 'url'
			hashed_log=(char*)malloc(sizeof(char)*(strlen(hashed)+1));	//hashed_log dynamic allocation
			strcpy(hashed_log,hashed);		//copy hashed to hashed_log to use when Hit case log file

			////////Determine Hit or Miss////////

			//////////////////////////////////////////////////////////////
			//*******dir1 format => /home/user/proxy_cache/'dir1'*******//
			//////////////////////////////////////////////////////////////
			token = NULL;	//token point NULL
			pDir=opendir(home);	//open directory 
			dir1=(char*)malloc(strlen(home)+3);	//dynamic allocation dir1 to set pathname
			strcpy(dir1,home);	//copy home's path name
			strcat(dir1,"/");		//make 'dir1''s pathname
			token=strtok(hashed,"/");
			strcat(dir1,token);	//complete dir1 pathname

			if(pDir==NULL)	//exception case
			{
				printf("Dir read error\n");
				return 0;
			}
			else	//other case
			{
				for(pFile=readdir(pDir);pFile;pFile=readdir(pDir)) //execute until pFile which get Directory information is NULL
				{
					if(strcmp(pFile->d_name,token)==0)	//find same name directory
					{
						search=1;	//search is set
						break;
					}
				}
			
				if(search==0)	//there is no such a directory => MISS
				{
					mkdir(dir1, S_IRWXU | S_IRWXG | S_IRWXO);	//make directory with dir1 pathname
					Hit=0;	//value would be zero cause MISS
				}
				else	//	=> HIT
				{
					Hit++;	//Hit value would be +1
					search=0;	//be zero search
				}
			}
			closedir(pDir);	//close directory
		
		
			/////////////////////////////////////////////////////////////////////
			//*******dir2 format => /home/user/proxy_cache/'dir1'/'dir2********//
			/////////////////////////////////////////////////////////////////////

			pDir=opendir(dir1);	//open directory
			dir2=(char*)malloc(strlen(dir1)+3);	//dynamic allocation dir2
			strcpy(dir2,dir1);	//copy dir1 pathname
			strcat(dir2,"/");	//process of make dir2 pathname
			token=strtok(NULL,"/");
			strcat(dir2,token);

			if(pDir==NULL)	//execption
			{
				printf("Dir read error\n");
				return 0;
			}
			else	//other case
			{
				for(pFile=readdir(pDir);pFile;pFile=readdir(pDir)) //execute until pFile which get Directory information is NULL
				{
					if(strcmp(pFile->d_name,token)==0)//find same name directory in dir1
					{
						search=1;//set search
						break;
					}
				}

				if(search==0)//if no such a directory => Miss
				{
					Hit=0;	//value would be zero cause MISS
					mkdir(dir2, S_IRWXU | S_IRWXG | S_IRWXO);//make directory
				}
				else	// => Hit
				{
					Hit++;	//Hit value would be +1
					search=0;//be zero
				}
			}
			closedir(pDir);	//close directory

			/////////////////////////////////////////////////////////////////////
			//*****file format => /home/user/proxy_cache/'dir1'/'dir2/file*****//
			/////////////////////////////////////////////////////////////////////
		
			pDir = opendir(dir2);	//open directory dir2
			file=(char*)malloc(strlen(dir2)+40);//dynamic allocation file
			strcpy(file,dir2);//copy dir2 pathname
			strcat(file,"/");//make file's pathname
			token=strtok(NULL,"\0");
			strcat(file,token);
	
			if(pDir==NULL)	//exception
			{
				printf("Dir read error\n");
				return 0;
			}
			else	//other
			{
				for(pFile=readdir(pDir);pFile;pFile=readdir(pDir)) //execute until pFile which get file information is NULL
				{
					if(strcmp(pFile->d_name,token)==0)//find same name file in dir2
					{
						search=1;//set search
						break;
					}
				}

				if(search==0)//if no exist => Miss
				{
					Hit=0;	//value would be zero cause MISS
					cache_fd=open(file,O_CREAT|O_RDWR|O_TRUNC,0777);//make file with file pathname
				}
				else	//Find HTTP Response file in Cache Directory
				{
					search=0;
					cache_fd=open(file,O_RDWR);	//Open find Cache file Read Write Only
					Hit++;	//Hit value would be +1
					
					//logfile location of Cache Hit
					logfile(semid,hashed_log);
					
				}
			}
			closedir(pDir);	//close directory
			free(hashed);		//free hashed allocation
			free(hashed_log);
			free(dir1);
			free(dir2);

			////If hit, Write Hash file to Browser if Hit value is 3, it means hit!
			if(Hit==3)
			{
				Hit=0;	//Hit value would be zero
				
				//Write cache_fd file to Browser_fd
				while((len_out=read(cache_fd,buf,BUFFSIZE))>0)	//read from cache_fd amount of len_out
				{
					if(len_out!=write(browser_fd,buf,len_out))	//write to browser from cache
					{
						printf("Hit_case, Write Error.\n");
						return -1;
					}
					memset(buf,0,BUFFSIZE);		//memory set zero about buffer
				}
				free(url);	//free url allocation
			}
			else  ///////////else if miss -> execute below code but, file Response_fd would be Hashed file!
			{
				Hit=0;	//Hit value would be zero

				if((web_fd = socket(PF_INET,SOCK_STREAM,0))<0)	//making web server socket
				{
					printf("can't create socket.\n");
					return -1;
				}

				//make Web Server address
				bzero((char*)&web_addr,sizeof(web_addr));
				web_addr.sin_family = AF_INET;
				web_addr.sin_addr.s_addr = inet_addr(ipaddr);
				web_addr.sin_port = htons(real_port);
	
				if(connect(web_fd, (struct sockaddr*)&web_addr,sizeof(web_addr))<0)	//Proxy server connection request to web server
				{
					printf("can't connect.\n");	//fail case
					return -1;
				}
			
			
				if(write(web_fd, request, strlen(request))!=strlen(request))	//write request message to web server
				{
					printf("Write to Web Sever fail.\n");	//write fail case
					return -1;

				}

				//Before Get HTTP Response log file
				logfile(semid,url);	//logfile location in Miss call logfile function with argument semaphore id, Hit, hashed_log & url

				free(url);	//free memory allocation

				alarm(10);	//Set alarm with alarming after 10 seconds

				while((len_out=read(web_fd,buf,BUFFSIZE))>0)	//Do this statement until read value is large than zero
				{
					if(len_out!=write(browser_fd,buf,len_out))	//write to browser simultaneously if the length is not same it means fail
					{
						printf("Write error.\n");
						return -1;
					}
					if(len_out!=write(cache_fd,buf,len_out))	//write to response file and if the length is not same it means fail
					{
						printf("Write error.\n");	//fail case
						unlink(file);
						return -1;
					}
					memset(buf,0,BUFFSIZE);	//buff set zero
				}

				alarm(0);	//delete alarm
			
			}

			free(file);	//free file allocation
			close(browser_fd);	//close browser socket descirptor
			close(web_fd);		//close web server socket descriptor
			close(cache_fd);		//close cache file descriptor
			exit(0);		//Child Process is END occur SIGCHLD
		}
		//child exit

	}
	if((semctl(semid,0,IPC_RMID,arg))==-1)	//semaphore control function to delete semaphore flag is IPC_RMID
	{
		printf("semctl IPC_RMID Miss failed");
		exit(1);
	}
	close(proxy_fd);	//close proxy socket descripter
	return 0;
}

char* sha1_hash(char* input_url)//Making hash function
{
	char* hashed_url; //character pointer will get hashed url which contain divider = '/'
	unsigned char hashed_160bits[20]; //160bits hashed value
	char hashed_hex[41]; //hexadecimal hashed value
	int i,k; //index variable

	SHA1(input_url, strlen(input_url), hashed_160bits);	//hashing, length hashing, hashed. get hashed number with 160bits

	for(i=0;i<sizeof(hashed_160bits);i++)	//change hashed_160bits value to two hexadecimal in hashed_hex
		sprintf(hashed_hex + i*2, "%02x", hashed_160bits[i]);
	
	//Make Hashed URL which contain divider '/'
	hashed_url=(char*)malloc(sizeof(char)*(strlen(hashed_hex)+3));	//Dynamic allocation
	memset(hashed_url,0,strlen(hashed_url));	//memory set with zero
	hashed_url[0]=hashed_hex[0];	//first directory
	strcat(hashed_url,"/");			//divide by '/'
	hashed_url[2]=hashed_hex[1];	//second directory
	strcat(hashed_url,"/");			//divide by '/'
	strcat(hashed_url,&hashed_hex[2]);	//string cat from 2 index of hashed_hex which would be file name

	return hashed_url; //return character pointer
}

char* getHomeDir() //Getting home directory function
{
	struct passwd* usr_info = getpwuid(getuid());	//passwd pointer type variable would point get user ID
	return usr_info->pw_dir;	//return home directory
}

char* getIPAddr(char* addr)
{
	struct hostent* hent;	//hostent structure value
	char* haddr;		//host IP address value
	int len = strlen(addr);	//length of host name

	if((hent=(struct hostent*)gethostbyname(addr))!=NULL)	//get host by name with argument addr
	{
		haddr=inet_ntoa(*((struct in_addr*)hent->h_addr_list[0]));	//get dotted decimal IP Address
	}

	return haddr;	//return IP Address
}

void myhandler(int signo)	//Signal Handler Function
{
	pid_t pid;	//pid struct to get pid
	switch(signo)	//case by signal
	{
		case SIGCHLD :		//SIGCHLD case which Child Process End occur
			pid = wait(NULL);	//wait Child Process
			printf("PID :%d Child End!\n",pid);	//print Child End
			break;
		case SIGALRM :		//SIGALRM case which Alarm is ringing
			printf("PID :%d No Response\n",getpid());	//Print No Response about PID
			close(browser_fd);		//Close file&socket descriptor browser_fd, web_fd, cache_fd
			close(web_fd);
			close(cache_fd);
			
			unlink(file);		//delete file by using unlink with pathname is file
			free(file);			//free file allocation
			exit(0);		//Exit Child occur SIGCHLD
			break;
		default	:	//default case
			break;
	}
	return ;
}

unsigned short stringtoshort(char* port)	//user define function which change string to unsigned short value
{
	unsigned short result=0;	//result of calculate
	int p = 1;		//digit
	int i;			//for loop
	int len=0;		//length of port

	len=strlen(port);	//get length of port

	for(i=0;i<len-1;i++)	//digit would be 10 times by length
		p=p*10;

	for(i=0;i<len;i++)	//calculate decimal
	{
		result=result+p*(port[i]-48);	//port is character ASCII so to get decimal we have to -48
		p=p/10;		//digit divided to 10
	}

	return result;	//return result
}

void p(int semid)	//semaphore p operation lock
{
	struct sembuf pbuf;	//sembuf type
	pbuf.sem_num = 0;	//semaphore number would be zero
	pbuf.sem_op = -1;	//semaphore operation would be minus one in P case
	pbuf.sem_flg = SEM_UNDO;	//setting semaphore flag
	if((semop(semid, &pbuf, 1))==-1)	//starting semaphore operation with 1 operation
	{
		printf("p : semop failed");	//fail case
		exit(1);
	}
}

void v(int semid)	//semaphore v operation unlock
{
	struct sembuf vbuf;	//sembuf type
	vbuf.sem_num = 0;	//semaphore number would be zero
	vbuf.sem_op = 1;	//semaphore operation would be one in V case
	vbuf.sem_flg = SEM_UNDO;	//setting semaphore flag
	if((semop(semid, &vbuf, 1))==-1)	//starting semaphore operation with 1 operation
	{
		printf("v : semop failed");	//fail case
		exit(1);
	}
}

void logfile(int semid, char* log)	//log url or hashed url that critical section
{
	struct tm *ltp;	//tm struct type pointer
	FILE* fout;			//FILE Stream pointer

	p(semid);
	//////critical section
	fout=fopen("logfile","a+");	//FILE Strem open with 'logfile'name and a+ condition, it means concantanous write with open	or create
	
	//////This Part is just to showing the Semaphore is well working in Result screen
	//	fprintf(fout,"========================================\n");
	//	fprintf(fout,"%d process is using logfile\n",getpid());
	
	fprintf(fout,"%s",log);	//write to file hashed log

	ltp=localtime(&now); //Get Broken-down time with calendar time
	//record Time information, In year case would be + 1900year, In month case would be +1month
	fprintf(fout,"-[%d/%d/%d, %d:%d:%d]\n",ltp->tm_year+1900,ltp->tm_mon+1,ltp->tm_mday,ltp->tm_hour,ltp->tm_min,ltp->tm_sec);
	
	//	fprintf(fout,"%d process is returning logfile\n",getpid());
	
	fclose(fout);     //file close
	//////critical section
	v(semid);

	return ;
}


