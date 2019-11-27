#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "drone_movement.h"

#define MAX 1000

// STRUCTS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct Product{
	char name[MAX];
	int stock;
}Product;
typedef struct Order{
	int ORDER_NO;
	char product[MAX];
	int amount;
	double x;
	double y;
}Order;
typedef struct Warehouse{
	pid_t warehousePID;
	int W_NO;
	Product* products;
	char name[MAX];
	double x;
	double y;
}Warehouse;
typedef struct Drone{
	int droneID;
	double x;
	double y;
	Order *order;
}Drone;
typedef struct Base{
	char name[MAX];
	double x;
	double y;
}Base;
typedef struct SharedData{
	int stat_nEncomendasEntregues;
	int stat_nProdudutosEntregues;
	int stat_nProdutosTotal;
	int stat_nEncomendasAtribuidas;
	char* product[MAX];
}SharedData;

// GLOBAL VARS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int pipeFD;

// LOGWRITE FUNCTIONS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
char* fetchTime(char*t)																													{
	time_t TIME = time(NULL);
	struct tm *tmp = gmtime(&TIME);
	sprintf(t,"%02d:%02d:%02d: ",tmp->tm_hour,tmp->tm_min,tmp->tm_sec);
}
void logWrite(char logString[])																									{

	char* timeString = malloc(10*sizeof(char));
	fetchTime(timeString);
	FILE* logfile;
	logfile = fopen("log.txt","a");
	if (logfile==NULL){
		printf("Logging Error - Logfile not found!");
		perror("\nERRNO: ");
		exit(-1);
	}
	char newline[2] = "\n";

	if (strcmp(logString,newline)!=0){
		fprintf(logfile, timeString, MAX);
		fprintf(logfile, logString, MAX);
		fprintf(logfile, "\n");
		printf("%s\n",logString);
	}
	else{
		fprintf(logfile, "\n");
	}
	fclose(logfile);
}

// SIGINT FUNCTIONS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void shutdownConsole(int signo)																										{
	printf("\n\n");
	logWrite("INPUT CONSOLE: INPUT PIPE: Input Pipe console closing.");
	close(pipeFD);
	pipeFD = open("inputPipe", O_WRONLY|O_NONBLOCK);
	char EXIT[MAX];
	sprintf(EXIT, "Console EXIT SIGNAL was recieved. Input Pipe no longer recieving commands");
	write(pipeFD, EXIT, MAX);
	close(pipeFD);
	printf("------------CONSOLE END------------\n");
	exit(0);
}

// MAIN //|O_NONBLOCK
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[]){
	char buffer[MAX];
	signal(SIGINT,shutdownConsole);
	if ((pipeFD=open("inputPipe",O_WRONLY|O_NONBLOCK))<0)											{
		logWrite("INPUT CONSOLE: INPUT PIPE: Input Pipe DEAD AS FUCK.");exit(-1);}

	printf("------------CONSOLE START------------\n");
	logWrite("\n");
	logWrite("INPUT CONSOLE: INPUT PIPE: Input Pipe ready for commands.");
	printf("\n\t\tAvaliable actions:\n");
	printf("\n   -> Order a product \n\t(must be: ORDER 'ORDER_NAME' prod: 'PRODUCT', 'AMOUNT' to: 'X', 'Y')");
	printf("\n   -> Set amount of drones \n\t(must be: SET DRONE 'NUMBER')\n");
	printf("\nPress Ctrl+C at any point to quit console\n------------------------------------------------------\n");

	while(1){
		pipeFD = open("inputPipe", O_WRONLY|O_NONBLOCK);
		fgets(buffer, sizeof(buffer), stdin);
		if (buffer[strlen(buffer)-1] == '\n'){
			buffer[strlen(buffer)-1] = '\0';
		}
		if(strcmp(buffer,"^C")==0){
			break;
		}
		write(pipeFD, buffer, MAX);
		close(pipeFD);
	}
	pause();
}
