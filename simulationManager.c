//	Trabalho Projecto - Sistemas Operativos - 2018/2019
//	"Drone Delivery Simulator v0.16"
//	Por:
//	> José Miguel Dias Simões 	2º Ano-LEI	[2017252705]		jds.work.one@gmail.com
//	> Leandro Borges Pais				2º Ano-LEI	[2017251509]		leandropaisslb1@gmail.com

#include "drone_movement.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/msg.h>

#define DEBUG
#define MAX 1000

// STRUCTS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct Product{
	char name[MAX];
	int stock;
}Product;
typedef struct Order{
	int ORDER_NO;
	int PROD_NO;
	int ATTACHED_DRONEID;
	char product[MAX];
	char name[MAX];
	int amount;
	double x;
	double y;
}Order;
typedef struct Warehouse{
	pid_t warehousePID;
	int W_NO;
	Product products[3];
	char name[MAX];
	double x;
	double y;
}Warehouse;
typedef struct Drone{
	int droneID;
	int busy;
	double x;
	double y;
	Order *order;
}Drone;
typedef struct Base{

	double x;
	double y;
}Base;
typedef struct SharedData{
	int stat_nEncomendasEntregues;
	int stat_nProdudutosEntregues;
	int stat_nProdutosTotal;
	int stat_nEncomendasAtribuidas;
	Warehouse* warehouseList;
}SharedData;
typedef struct warehouseMessage{
	long msgtype;
	int W_NO;
}warehouseMessage;

// GLOBAL SIM VARS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct Warehouse* 	warehouseList;
struct SharedData* 	sharedData;
struct Drone* 			droneList;
struct Base*				base;
struct Order			globalOrderBuffer;
pthread_mutex_t			mutex;
pthread_cond_t 			condVar;
pthread_t*					threadsID;
pid_t SimManagerPID;
pid_t CentralPID;
fd_set read_set;
sem_t* centralsem;
sem_t* sem;
int fd[2];
int droneNum;
int warehouseNum;
int supplyRate;
int restockAmt;
int tUnit;
int numProds;
int droneID, wNumber, oNumber;
int shmID, semID, mqID;
double xMax, yMax;

// LOGWRITE FUNCTIONS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void logWrite(char logString[])																									{
	FILE* logfile;
	char* timeString = malloc(10*sizeof(char));
	time_t TIME = time(NULL);
	struct tm *tmp = gmtime(&TIME);
	sprintf(timeString,"%02d:%02d:%02d: ",tmp->tm_hour,tmp->tm_min,tmp->tm_sec);
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
		//printf("%s",timeString);
		printf("%s\n",logString);
	}
	else{
		fprintf(logfile, "\n");
	}
	fclose(logfile);
}


// BASE CREATING FUNCTION //
void baseSpawner()																															{
	base[0].x = xMax - xMax*0.75;
	base[0].y = xMax - xMax*0.75;
	base[1].x = xMax - xMax*0.25;
	base[1].y = xMax - xMax*0.75;
	base[2].x = xMax - xMax*0.25;
	base[2].y = xMax - xMax*0.25;
	base[3].x = xMax - xMax*0.75;
	base[3].y = yMax - yMax*0.25;
}


// PATH CREATING FUNCTION //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int pathFinder(Order order, int* dID, int* wID)																	{
	double warehouseDistance=0;
	double droneDistance=0;
	double minimumDistance=0;
	int BUSY=0;
	for(int d=0; d<droneNum; d++){
		if (droneList[d].busy!=0){
			BUSY++;
		}
	}
	if(BUSY==droneNum){
		*wID=-1;
		*dID=-1;
	}
	int EXISTS=0;
	for(int w=0; w<warehouseNum; w++){
		if(sharedData->warehouseList[w].products[order.PROD_NO].stock >= order.amount){
			warehouseDistance = distance(sharedData->warehouseList[w].x, sharedData->warehouseList[w].y, order.x, order.y);;
			for(int d=0; d<droneNum ; d++){
				if(droneList[d].busy==0){
					droneDistance = distance(sharedData->warehouseList[w].x, sharedData->warehouseList[w].y, droneList[d].x, droneList[d].y);
					if(EXISTS==0){
						*dID = d;
						*wID = w;
						minimumDistance = (warehouseDistance + droneDistance);
						EXISTS = 1;
					}
					else if((warehouseDistance + droneDistance) < minimumDistance){
						*dID = d;
						*wID = w;
						minimumDistance = (warehouseDistance + droneDistance);
					}
				}else{
					continue;
				}
			}
		}
	}
}


// SIGINT AND SHUTDOWN FUNCTS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void shutdownWarehouse(int signo)																								{
	char logString[MAX];
	usleep(500);
	shmdt(sharedData);
	sprintf(logString,"SHUTDOWN: Warehouse Process with PID [%d] exiting...",(int)getpid());
	logWrite(logString);
	exit(0);
}
void shutdownCentral(int signo)																									{
		sleep(1);
		shmdt(sharedData);
		logWrite("SHUTDOWN: Waiting for all Drone Threads and joining...");
		for(int j=0; j<droneNum; j++){
			pthread_join(threadsID[j], NULL);
		}
		exit(0);
}
void shutdownMain(int signo)																										{
	printf("\n");
	logWrite("\n");logWrite("--------------\tSIGINT RECIEVED\t-------------");	logWrite("\n");
	sleep(2);
	logWrite("SHUTDOWN: Shutting down and removing Shared Memory...");
	shmdt(sharedData);
	shmctl(shmID, IPC_RMID, NULL);
	logWrite("SHUTDOWN: Shutting down Message Queue...");
	msgctl(mqID, IPC_RMID, NULL);
	logWrite("SHUTDOWN: Shutting down and removing Named Pipe...");
	remove("inputPipe");
	logWrite("SHUTDOWN: Shutting down and removing Warehouse Communications Pipe...");
	remove("warehousePipe");
	logWrite("SHUTDOWN: Waiting for all Warehouse Processes and shutting down...");
	for(int i=0; i<warehouseNum; i++){
		wait(NULL);}
	logWrite("SHUTDOWN: Freeing structure memory...");
	free(warehouseList);
	free(droneList);
	printf("\n");
	remove("log.txt");
	logWrite("\n");logWrite("--------------\tSESSION FINISHED\t-------------");	logWrite("\n");
	exit(0);
}


// WAREHOUSE FUNCTS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printWarehouse(Warehouse wh)																								{
	int i;
	printf("> [%d] %s:[x:%.0lf, y:%.0lf]\n", wh.W_NO, wh.name, wh.x, wh.y);
	for(i=0;i<numProds;i++){
		printf("----------------");
	}
	printf("\n");
	for(i=0;i<numProds;i++){
		printf("|  %s:%4.1d  |", wh.products[i].name, wh.products[i].stock);
	}
	printf("\n");
	for(i=0;i<numProds;i++){
		printf("----------------");
	}
	printf("\n");
}
void printAllWarehouses(Warehouse* whList)																			{
	int i;
	for (i=0; i<warehouseNum; i++){
		printWarehouse(whList[i]);
	}printf("\n");
}


// DRONE FUNCTS //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* droneProcess(void *idp)																										{
	Drone* threadDrone = (Drone*)idp;

	while(1){
		pthread_mutex_lock(&mutex);
		while(globalOrderBuffer.ATTACHED_DRONEID != threadDrone->droneID){
			pthread_cond_wait(&condVar, &mutex);
		}
		pthread_mutex_unlock(&mutex);
		printf("\n[%d]",threadDrone->droneID);
		threadDrone->droneID=1000;


		//

		//

		//

		//
	}
	pthread_exit(NULL);
}
Drone* droneSpawner(Drone *drone, int i)																				{
	drone->busy=0;
	switch(i%4){
		case (0):
			drone->x = base[0].x;
			drone->y = base[0].y;
			break;
		case(1):
			drone->x = base[1].x;
			drone->y = base[1].y;
			break;
		case(2):
			drone->x = base[2].x;
			drone->y = base[2].y;
			break;
		case(3):
			drone->x = base[3].x;
			drone->y = base[3].y;
			break;
	}
}


// CONFIG READ FUNCT //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Warehouse* configRead()																													{
	FILE* config = fopen("config.txt", "r");
	char buffer[MAX];
	char logString[MAX*3];
	char wName[MAX];
	char*produtosDisponiveis[MAX];
	char*token;
	int i=0,j=0,k=0,l=0,p=0,c=0,stockbuffer;
	double x,y;

	struct Warehouse* warehouseLista;
	if(config!=NULL){
		logWrite("CONFIG FILE: Config file opened successfully.");
		fscanf(config,"%lf, %lf\n", &xMax, &yMax);
		fgets(buffer,MAX,config);
		fscanf(config,"%d\n%d, %d, %d\n%d\n", &droneNum, &supplyRate, &restockAmt, &tUnit, &warehouseNum);
		buffer[strlen(buffer) - 2] = '\0';
		token = strtok(buffer,",");
		while (token!=NULL){
			produtosDisponiveis[i] = (char*)malloc(sizeof(char*));
			strcpy(produtosDisponiveis[i], token);
			token = strtok(NULL, ",");
			i++;
		}
		numProds=i;
		warehouseLista = (struct Warehouse*)malloc((warehouseNum)*sizeof(struct Warehouse));
		for(j=0;j<(warehouseNum);j++){
			warehouseLista[j].W_NO=wNumber;
			wNumber++;
			c=0;
			fscanf(config,"%[^' ']", wName);
			fscanf(config," xy: %lf, %lf prod:", &x, &y);
			strcpy(warehouseLista[j].name, wName);
			warehouseLista[j].x=x;
			warehouseLista[j].y=y;
			//warehouseLista[j].products = (struct Product*)malloc(i*sizeof(struct Product));
			for(l=0;l<i;l++)
			{
				strcpy(warehouseLista[j].products[l].name, produtosDisponiveis[l]);
				warehouseLista[j].products[l].stock = 0;
			}
			fgets(buffer,MAX,config);
			buffer[strlen(buffer) - 2] = '\0';

			token = strtok(buffer,",");
			while (token!=NULL){
				for(l=0;l<i;l++){
					if ((strcmp(token,warehouseLista[j].products[l].name))==0){
						strcpy(warehouseLista[j].products[l].name,token);
						token = strtok(NULL, ",");
						stockbuffer=atoi(token);
						warehouseLista[j].products[l].stock+=stockbuffer;
						sprintf(logString,"CONFIG FILE: Populated '%s' with %d of %s",warehouseLista[j].name,stockbuffer,warehouseLista[j].products[l].name);
						logWrite(logString);
						c++;
					}
				}
				token = strtok(NULL, ",");
			}
		}
	}else{
			logWrite("ERROR:CONFIG FILE: - Config file loading failed.");
			exit(-1);
	}
	logWrite("CONFIG FILE: Config file loaded successfully.\n");
	return warehouseLista;
}
void configPrint(Warehouse wh)																									{

	printf("SIMULATION PARAMETERS:\nSize:%.0lfx%.0lf",xMax,yMax);
	printf("\nNumber of initially active Drones: %d",droneNum);
	printf("\nNumber of avaliable Products: %d", numProds);
	printf("\nWarehouse restock rate and ammount: %d %d",supplyRate, restockAmt);
	printf("\nLength of time Unit (read milliseconds) : %d",tUnit*1000);
	printf("\nExisting simulation products:\n");
	for (int i; i<numProds; i++){
		printf("| %s |",wh.products[i].name);
	}
	printf("\n\n");
}


// CENTRAL PROCESS FUNCT //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void centralProcess()																														{
	char logString[MAX*2];
	logWrite("\n");
	signal(SIGINT, shutdownCentral);

	// CENTRAL CHECK BLOCK ///////////////////////////////////////////////////////
	CentralPID = getpid();
	if (CentralPID<0)																															{
		logWrite("CENTRAL PROCESS: ERROR: Central Process creation failed.");	exit(-1);
	}
	else																																					{
		printf("\n");
		logWrite("CENTRAL PROCESS: Central Process initialized successfully.");
	}
	//////////////////////////////////////////////////////////////////////////////

	// MUTEX AND CONDVAR BLOCK ///////////////////////////////////////////////////
	pthread_cond_init(&condVar, NULL);
	pthread_mutex_init(&mutex, NULL);
	//////////////////////////////////////////////////////////////////////////////

	// THREAD CREATION BLOCK /////////////////////////////////////////////////////
	threadsID = malloc(droneNum*sizeof(pthread_t));
	char threadStr[MAX];
	droneList = malloc((droneNum)*sizeof(Drone));
	for (int i=0;i<droneNum;i++)																									{
		droneList[i].droneID = i;
		droneSpawner(&droneList[i],droneList[i].droneID);
		int threadcreatechk = pthread_create( &threadsID[i], NULL, droneProcess, &droneList[i]);
		if (threadcreatechk == 0){
			sprintf(threadStr,"CENTRAL PROCESS: THREAD: Thread [%d] initialization successful.",i+1);
			logWrite(threadStr);
		}
		else{
			printf("\nCENTRAL PROCESS: Thread [%d] initialization failed.",i+1);
			sprintf(threadStr,"ERROR: CENTRAL PROCESS: THREAD: Thread [%d] initialization failed.",i+1);
			logWrite(threadStr);
			exit(-1);
		}
	}
	for (int i=0;i<droneNum;i++)																									{
		printf("\t> Drone [%d] is at x:%lf y:%lf\n",droneList[i].droneID, droneList[i].x, droneList[i].y);
	}
	//////////////////////////////////////////////////////////////////////////////

	// INPUT PIPE CREATION BLOCK /////////////////////////////////////////////////
	if ((mkfifo("inputPipe", O_CREAT|O_EXCL|0600|O_NONBLOCK)<0)==0)								{
		logWrite("CENTRAL PROCESS: INPUT PIPE: Input Pipe initialized successfully.");
	}
	else																																					{
		logWrite("ERROR: CENTRAL PROCESS: INPUT PIPE: Input Pipe initialization failed.");
		if (errno == EEXIST){
			logWrite("ERROR: CENTRAL PROCESS: INPUT PIPE: Input Pipe 'path' already taken.");
		}
	exit(-1);
	}
	logWrite("CENTRAL PROCESS: Awaiting for Pipe data...");
	printf("\n");
	//////////////////////////////////////////////////////////////////////////////

	// OPENING PIPES /////////////////////////////////////////////////////////////
	if ((fd[0]=open("inputPipe",O_RDONLY|O_NONBLOCK))<0)													{
		logWrite("ERROR: CENTRAL PROCESS: INPUT PIPE: Input Pipe could not be read.");exit(-1);
	}
	if ((fd[1]=open("warehousePipe",O_RDONLY|O_NONBLOCK))<0)											{
		logWrite("ERROR: CENTRAL PROCESS: WAREHOUSE COMMS PIPE: Warehouse communications pipe could not be read.");exit(-1);
	}
	//////////////////////////////////////////////////////////////////////////////

	// PIPE READING BLOCK ////////////////////////////////////////////////////////
	sem_unlink("CENTRALSEM");
	centralsem = sem_open("CENTRALSEM", O_CREAT|O_EXCL, 0766, 1);
	Order order;
	Order orderQueue[100];
	char orderBuffer[MAX];
	char restockBuffer[MAX];
	char*token;
	int prodAux;
	int orderCounter;
	int fdaux;
	int EXIST=0;
	int ENOUGH=0;
	int QUEUE=0;
	int GOTQUEUED=0;
	globalOrderBuffer.ATTACHED_DRONEID=1000;
	while(1){
		close(fd[0]);
		close(fd[1]);
		fd[0]=open("inputPipe",O_RDONLY|O_NONBLOCK);
		fd[1]=open("warehousePipe",O_RDONLY|O_NONBLOCK);
		memset(orderBuffer, '\0', sizeof(orderBuffer));

		fd_set read_set;
		int i;
		FD_ZERO(&read_set);
		FD_SET(fd[0], &read_set);
		FD_SET(fd[1], &read_set);
		//Select Pipe to read from

		if (select(fd[1]+1, &read_set, NULL, NULL, NULL) > 0)												{
			if (FD_ISSET(fd[0], &read_set))																						{
				read(fd[0], orderBuffer, MAX);
				sem_wait(centralsem);
				printf("\n--------------------------\n");
				sprintf(logString,"[PIPE READ]: %s",orderBuffer);
				logWrite(logString);
				token = strtok(orderBuffer," ");
				if (token==NULL){
					logWrite("[PIPE READ]: ERROR: Did not accept order. (NULL read)");
					printf("--------------------------\n\n");
					sem_post(centralsem);
					continue;
				}
				if (strcmp(token,"Console")==0){
					printf("--------------------------\n\n");
					sem_post(centralsem);
					continue;
				}
				if (strcmp(token,"ORDER")==0)			{
					token = strtok(NULL," ");
					if (token==NULL){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (NULL read after ORDER identifier)");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					strcpy(order.name, token);
					token = strtok(NULL," ");
					////////////////////////////////////////////////////////////////////////////
					if (token==NULL){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (NULL read as 'prod:')");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					////////////////////////////////////////////////////////////////////////////
					if (strcmp(token,"prod:")!=0){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (Structure fault ('prod:' unrecognized))");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					token = strtok(NULL," ");
					////////////////////////////////////////////////////////////////////////////
					if (token==NULL){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (NULL read as Product name)");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					////////////////////////////////////////////////////////////////////////////
					token[strlen(token)-1]='\0';
					for(int k=0;k<numProds;k++){
						if(strcmp(token, sharedData->warehouseList[0].products[k].name)==0){
							EXIST=1;
							prodAux=k;
						}
					}
					if (EXIST!=0){
						EXIST=0;
					}
					else{
						logWrite("[PIPE READ]: ERROR: Did not accept order. (Product unrecognized, check Product name)");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					strcpy(order.product, token);
					token = strtok(NULL,", ");
					////////////////////////////////////////////////////////////////////////////
					if (token==NULL){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (NULL read as Product amount)");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					////////////////////////////////////////////////////////////////////////////
					for(int j=0; j<warehouseNum; j++){
							if(warehouseList[j].products[prodAux].stock >= atoi(token)){
								ENOUGH=1;
							}
					}
					if(ENOUGH!=0){
						ENOUGH=0;
					}
					else{
						QUEUE++;
						GOTQUEUED=1;
					}
					order.amount = atoi(token);
					order.PROD_NO = prodAux;
					token = strtok(NULL," ");
					////////////////////////////////////////////////////////////////////////////
					if (token==NULL){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (NULL read as 'to:')");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					////////////////////////////////////////////////////////////////////////////
					if (strcmp(token,"to:")!=0){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (Structure fault ('to:' unrecognized))");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					token = strtok(NULL," ");
					////////////////////////////////////////////////////////////////////////////
					if (token==NULL){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (NULL read as X coordinate)");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					////////////////////////////////////////////////////////////////////////////
					if (atoi(token)>xMax || atoi(token)<0){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (Unavaliable X coordinate)");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					order.x = (double)atoi(token);
					token = strtok(NULL,",");
					////////////////////////////////////////////////////////////////////////////
					if (token==NULL){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (NULL read as Y coordinate)");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					////////////////////////////////////////////////////////////////////////////
					if (atoi(token)>yMax || atoi(token)<0){
						logWrite("[PIPE READ]: ERROR: Did not accept order. (Unavaliable Y coordinate)");
						printf("--------------------------\n\n");
						sem_post(centralsem);
						continue;
					}
					order.y = (double)atoi(token);
					////////////////////////////////////////////////////////////////////////////
					order.ORDER_NO = oNumber;
					oNumber++;
					if (GOTQUEUED==0){
						int dID, wID;
						pathFinder(order,&dID,&wID);
						order.ATTACHED_DRONEID=dID;
						if ((dID==(-1))||(wID==(-1))){
							sprintf(logString,"[ORDER]: FAILED: Order [%d] could no be added, all Drones busy.",order.ORDER_NO);
							logWrite(logString);
							printf("--------------------------\n\n");
							sem_post(centralsem);
							continue;
						}
						sem_post(centralsem);
						droneList[dID].busy++; //OCUPADO
						sharedData->warehouseList[wID].products[order.PROD_NO].stock -= order.amount; //RESERVADO
						sprintf(logString,"[ORDER]: SUCESS: Order [%d] assigned to Drone [%d] to be fetched from %s.",order.ORDER_NO,order.ATTACHED_DRONEID,warehouseList[wID].name);

						globalOrderBuffer = order;
						pthread_cond_broadcast(&condVar);
						usleep(100);
						logWrite(logString);
						printf("--------------------------\n\n");
					}
					else if (GOTQUEUED==1){
						logWrite("[PIPE READ]: QUEUED: Order amount exceeds existing warehouse Stock.");
						printf("--------------------------\n\n");
						orderQueue[orderCounter]=order;
						orderCounter++;
						GOTQUEUED=0;
					}
					sem_post(centralsem);
					continue;
				}
				else if(strcmp(token,"DRONE")==0)	{
					logWrite("[PIPE READ]: WARNING: DRONE SET commands not yet implemented.");
					printf("--------------------------\n\n");
					sem_post(centralsem);
					continue;
				}
				else															{
					logWrite("[PIPE READ]: ERROR: 'ORDER' identifier missing or invalid.");
					printf("--------------------------\n\n");
					sem_post(centralsem);
					continue;
				}
			}
			if (FD_ISSET(fd[1], &read_set))																						{
				read(fd[1], restockBuffer, MAX);
				if (QUEUE==0){
					continue;
				}
				else{
					printf(" %d items!",QUEUE);
				}
			}
		}
	}
	pause();
}

/*
	warehouses comunicate via a pipe, to centralProcess
	when central process reads from a pipe (via select)
	it can either create an order or read from the CONSOLE
	blocks if nothing in there exists
*/

// MAIN //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main()																																			{
	char logString[MAX*3];
	wNumber=0;
	oNumber=0;
	time_t t;

	//MAIN SIGNAL HANDLER
	signal(SIGINT, shutdownMain);

	//SEMAPHORE
	sem_unlink("SEM");
	sem = sem_open("SEM", O_CREAT|O_EXCL, 0766, 1);

	//MESSAGE QUEUE
	mqID = msgget(IPC_PRIVATE, IPC_CREAT|0700);

	//SESSION BEGIN
	printf("\n");
	logWrite("\n");logWrite("--------------\tSESSION STARTED\t-------------");	logWrite("\n");

	//CONFIG READ, UPDATE GLOBALS
	warehouseList = configRead();
	configPrint(warehouseList[0]);

	// CREATING PIPE READING RESTOCK WARNINGS/////////////////////////////////////
	if ((mkfifo("warehousePipe", O_CREAT|O_EXCL|0600|O_NONBLOCK)<0)==0)						{
		logWrite("CENTRAL PROCESS: WAREHOUSE COMMS PIPE: Warehouse communications pipe initialized successfully.");}
	else																																					{
		logWrite("ERROR: CENTRAL PROCESS: WAREHOUSE COMMS PIPE: Warehouse communications pipe initialization failed.");
		if (errno == EEXIST){
			logWrite("ERROR: CENTRAL PROCESS: WAREHOUSE COMMS PIPE: Warehouse communications pipe 'path' already taken.");}
	exit(-1);}
	if ((fd[1]=open("warehousePipe",O_RDWR|O_NONBLOCK))<0)											{
		logWrite("ERROR: CENTRAL PROCESS: WAREHOUSE COMMS PIPE: Warehouse communications pipe could not be read.");exit(-1);}

	//SHARED MEMORY, UPDATE WITH GLOBAL DATA
	shmID = shmget(IPC_PRIVATE, (sizeof(struct SharedData)), IPC_CREAT|0777);
 	sharedData = (SharedData*)shmat(shmID, NULL, 0);
	shmID = shmget(IPC_PRIVATE, (warehouseNum*sizeof(struct Warehouse)), IPC_CREAT|0777);
	sharedData->warehouseList = (Warehouse*)shmat(shmID, NULL, 0);

	for(int i=0; i<warehouseNum; i++){
		sharedData->warehouseList[i].warehousePID = 0;
		sharedData->warehouseList[i].W_NO = warehouseList[i].W_NO;
		sharedData->warehouseList[i].products[0] = warehouseList[i].products[0];
		sharedData->warehouseList[i].products[1] = warehouseList[i].products[1];
		sharedData->warehouseList[i].products[2] = warehouseList[i].products[2];
		strcpy(sharedData->warehouseList[i].name, warehouseList[i].name);
		sharedData->warehouseList[i].x = warehouseList[i].x;
		sharedData->warehouseList[i].y = warehouseList[i].y;
	}

	//INITIATE BASES
	base = malloc(4*sizeof(struct Base));
	baseSpawner();

	//WAREHOUSE PROCESS SPAWNER
	for (int i=0; i<warehouseNum; i++){
		if (fork()==0){
			char restockBuffer[MAX];
			//Handle CTRL as a child process
			signal(SIGINT, shutdownWarehouse);
			sharedData->warehouseList[i].warehousePID = SimManagerPID;
			sem_wait(sem);
			sprintf(logString,"WAREHOUSE PROCESS: Warehouse '%s' process initialized successfully, with PID[%d].", sharedData->warehouseList[i].name, getpid());
			logWrite(logString);
			printWarehouse(sharedData->warehouseList[i]);
			sem_post(sem);
			//////////////////////
			warehouseMessage messageR;
			fd[1] = open("warehousePipe",O_WRONLY);
			while(1){
				close(fd[1]);
				fd[1] = open("warehousePipe",O_WRONLY);
				int aux = warehouseList[i].W_NO + 1;
				int prodaux = rand()%numProds;
				if (sharedData->warehouseList[i].products[prodaux].stock==0){
					continue;
				}
				msgrcv(mqID,&messageR,sizeof(int),aux,0);
				//SENDS MESSAGE TO PIPE FOR RESTOCK VERIFICATION
				char ERR[MAX];
				sprintf(ERR,"%s",sharedData->warehouseList[i].name);
				strcpy(restockBuffer,ERR);
				write(fd[1], restockBuffer, MAX);
				//UPDATES SHARED MEMORY STOCK
				sharedData->warehouseList[i].products[prodaux].stock += restockAmt;
				sprintf(logString,"WAREHOUSE PROCESS: Warehouse '%s' restocked with %d of %s. Now holds %d.",sharedData->warehouseList[i].name, restockAmt, sharedData->warehouseList[i].products[prodaux].name, sharedData->warehouseList[i].products[prodaux].stock);
				logWrite(logString);
			}
			//////////////////////
		}
	}
	logWrite("\n");
	sleep(1);

	//CENTRAL PROCESS SPAWNER
	SimManagerPID = fork();
	if(SimManagerPID==0){
		//Handle CTRL as a different process
		centralProcess();
	}
	else{
		sleep(2);
		while(1){
			warehouseMessage messageS;
			for(int r=0;r<warehouseNum;r++){
				sem_wait(sem);
				messageS.W_NO = sharedData->warehouseList[r].W_NO;
				sem_post(sem);
				messageS.msgtype = r+1;
				msgsnd(mqID,&messageS,sizeof(int),0);
				sleep(supplyRate);
			}
		}
	}
	return EXIT_SUCCESS;
	pause();
}
