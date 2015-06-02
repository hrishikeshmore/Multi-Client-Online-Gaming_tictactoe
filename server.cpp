/*
 * server_main.cpp
 *
 *  Created on: Apr 16, 2015
 *      Author: dkh/hm
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <queue>
#include <semaphore.h>
#include "AllCommands.h"

using namespace std;

/********* Define macros************************/
#define INTERVAL 	5 // seconds
#define MAX_BUFFER_SIZE  1024


/*************************************************
********************* STRUCTURE ******************
***************HIT, FAILED, FILESIZE**************
*************************************************/
typedef struct {
	pthread_mutex_t st_mutex;
	unsigned int st_concount;
	unsigned int st_contotal;
	unsigned long st_contime;
	unsigned long st_totalregusers;
	unsigned long st_totalgames;
}stats_t;

typedef enum{
	game_tie,
	game_lost,
	game_win,
	game_in_progress,
	game_ended
}game_status_t;

typedef struct{
	bool validGame;
	int player1_index;
	int player2_index;
	char gameArray[9];// 0 for empty, '*' or 'o'
	game_status_t player1_status;
	game_status_t player2_status;
}game_t;



/**********Function Declarations**************/
void *newClientThread(int fd);
void prstats(void);
int errexit(const char *format, ...);
void error(char *msg);
void updateStartThreadStats();
void updateEndThreadStats(time_t startTime);
bool parseClientMessage(char* message, char* command, char* arg);
int handleRegUser(int fd);
int handleNewGame(int fd,int player);
void handleEndGame(int fd,int gameIndex);
void sendCommandNotSupported(int fd, int error_code);
void makeAMove(int fd,int gameIndex, int row,int column);

/**********Global Variables*******************/
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; //THREAD INITIALIZE
int sockfd, portno, clilen, count;

char sendata[1024];
char *buffs;
char length[1024];
char *datatype;

struct sockaddr_in serv_addr, cli_addr; // serv_addr - Server Structure
                                        // cli_addr  - Client Structure
int n, ret;
long lengthOfFile;
FILE *fp;                               // File pointer initialize
char data[10240];

stats_t stats;

vector<int> regUsersList;
pthread_mutex_t regUsersList_mutex;

vector<game_t> runningGameList;
pthread_mutex_t runningGameList_mutex;

vector<string> regUserQueue;
pthread_mutex_t regUserQueue_mutex;


/*************************************************
********************* MAIN ***********************
*************************************************/
int main(int argc, char *argv[])
{
	int enable = 1;
	int newsockfd;

	// Stats thread declaration
	pthread_t stats_th;
	pthread_attr_t stats_th_ta;

	/**************Client THREAD declaration***********/
    pthread_t tid;
    pthread_attr_t attr;

    /************************************************/
    //CHECK FOR NO. OF ARGUEMENTS
     if (argc < 2)
     {
         fprintf(stderr,"ERROR, no port provided\n");
         exit(1);
     }

     //sleep(120);

     printf("Server Starting up\n");

     /**************STATS THREAD INITIALISATION*************/
 	(void) pthread_attr_init(&stats_th_ta);
 	(void) pthread_attr_setdetachstate(&stats_th_ta, PTHREAD_CREATE_DETACHED);
 	pthread_mutex_init(&stats.st_mutex, 0);

 	if (pthread_create(&stats_th, &stats_th_ta, (void * (*)(void *))prstats, 0) < 0)
 			errexit("pthread_create(prstats): %s\n", strerror(errno));


 	// Initialize mutex for list of registered users
 	pthread_mutex_init(&regUsersList_mutex, 0);

 	// Initialize mutex for list of running games
 	pthread_mutex_init(&runningGameList_mutex, 0);

 	// Initialize mutex of queue
 	pthread_mutex_init(&regUserQueue_mutex, 0);


 	/**********CREATE SERVER SOCKET & INITIALIZE************/
     sockfd = socket(AF_INET, SOCK_STREAM, 0);

     if (sockfd < 0)
        errexit("ERROR opening socket");

     bzero((char *) &serv_addr, sizeof(serv_addr));
     portno = atoi(argv[1]);
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);

	// Set socket option to reuse local port
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&enable, sizeof(enable)) < 0)
	{
		perror(" Failed to set sock option SO_REUSEADDR");
	}

     if (bind(sockfd, (struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
              errexit("ERROR on binding");

     listen(sockfd,20);

     // Listen for new client connection
     while(1)
     {
		 clilen = sizeof(cli_addr);

		 // Accept new connection
		 newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, (unsigned int*)&clilen);
		 if (newsockfd < 0) {
			if (errno == EINTR)
				continue;
			errexit("accept: %s\n", strerror(errno));
			}

		 /***************CREATE A THREAD FOR NEW CLIENT******************/
		 pthread_attr_init(&attr);
		 (void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		 if (pthread_create(&tid, &attr, (void * (*)(void *))newClientThread,
		 		    (void *)(long)newsockfd) < 0)
		 			errexit("pthread_create: %s\n", strerror(errno));

     }

     // Close listening socket
     close(sockfd);
     return 0;
}
/*************************************************
*********************THREAD***********************
*************************************************/
// Try and use fixed size messages always to avoid confusion
void *newClientThread(int socketDesc)
{
	uint8_t buffer[MAX_BUFFER_SIZE];
	uint8_t* bptr = buffer;
	int buflen = MAX_BUFFER_SIZE;
	time_t start = time(0);
	const int max_msg_size = 50;
	int regUserListIndex = -1;
	int gameIndex = -1;

	// Increment total clients count
	updateStartThreadStats();

	printf("Client Socket: %d\n",socketDesc);

	// Initialize the client queue that is used for passing messages
	(void) pthread_mutex_lock(&regUserQueue_mutex);
	regUserQueue.push_back("no data");
	//printf("QueueSize after new client:%d\n",regUserQueue.size());
	(void) pthread_mutex_unlock(&regUserQueue_mutex);

	// Server code handling each client remains in loop till client disconnects or ends session
	while(1)
	{
		//Initialize buffer to 0
		bzero(buffer,MAX_BUFFER_SIZE);

		buflen = max_msg_size;
		//Check if connection is closed
		if (recv(socketDesc, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT) == 0)
		{
			// if recv returns zero, that means the connection has been closed:
			break;
		}

		/**********START: HANDLING INTER_THREAD COMMUNICATION***********************/

		// Check for new item in queue (message from another server thread
		string queueString = "no data";
		(void) pthread_mutex_lock(&regUserQueue_mutex);
			if(regUserListIndex != -1){
				queueString = regUserQueue.at(regUserListIndex);
			}
		(void) pthread_mutex_unlock(&regUserQueue_mutex);


		if((strcmp(queueString.c_str(),"no data") != 0) &&
				(regUserListIndex != -1))
		{
			printf("Received command from thread\n");

			// receive and reset string in queue
			(void) pthread_mutex_lock(&regUserQueue_mutex);
			string localCommand = regUserQueue.at(regUserListIndex);
			regUserQueue.at(regUserListIndex) = "no data";
			(void) pthread_mutex_unlock(&regUserQueue_mutex);

			char threadCommand[50];
			char threadArg[50];
			char local[50];
			strcpy(local,localCommand.c_str());


			// There is a message in the queue, handle it
			if(parseClientMessage(local,threadCommand,threadArg))
			{
				printf("Parsed message from thread: %s %s\n",threadCommand,threadArg);

				if(strcmp(threadCommand,"newgamereq") == 0)
				{
					// new game
					printf("Another player has requested for new game");
					if(threadArg == NULL)
						printf("New game request: No argument received\n");
					else{
						if(regUserListIndex == -1)
						{
							// User needs to register first
							printf("USer not registered yet, send no\n");
							string queueSize;
							int userIndex = atoi(threadArg);


							// Wait till the other thread's queue is empty
							while(queueSize != "no data")
							{
								(void) pthread_mutex_lock(&regUserQueue_mutex);
								//printf("Queue Size: %d\n",regUserQueue.size());
								queueSize = regUserQueue.at(userIndex);

								(void) pthread_mutex_unlock(&regUserQueue_mutex);

							}

							printf("Storing no in Queue \n");

							(void) pthread_mutex_lock(&regUserQueue_mutex);
							regUserQueue.at(userIndex) = "no";
							(void) pthread_mutex_unlock(&regUserQueue_mutex);
						}
						else
						{
							// Automatic yes, client is not asked

							string queueSize;
							int userIndex = atoi(threadArg);
							printf("USer registered, send yes to %d\n",userIndex);

							// Wait till the other thread's queue is empty
							while(queueSize != "no data")
							{
								(void) pthread_mutex_lock(&regUserQueue_mutex);
								//printf("Queue Size: %d\n",regUserQueue.size());
								queueSize = regUserQueue.at(userIndex);

								(void) pthread_mutex_unlock(&regUserQueue_mutex);

							}

							printf("Storing yes in Queue \n");

							(void) pthread_mutex_lock(&regUserQueue_mutex);
							regUserQueue.at(userIndex) = "yes";
							(void) pthread_mutex_unlock(&regUserQueue_mutex);

						}
					}
				}
				else if(strcmp(threadCommand,"newgamecreated") == 0)
				{
					// new game
					gameIndex = atoi(threadArg);
					printf("A new game has opened up with another user");
					printf("New game created with player: %d\n",gameIndex);
					message_t msg;

					memset(&msg,0, sizeof(msg));
					msg.header = 0xA5;
					msg.msgid = GAME_STARTED;
					msg.msglen = 1;
					msg.data[0]= gameIndex;
					msg.eom=0x5A;
					memcpy(buffer, &msg, (int)sizeof(msg));
					send(socketDesc, buffer, sizeof(msg), 0);



				}
			}
		}

		/**********END: HANDLING INTER_THREAD COMMUNICATION***********************/


		/**********START: HANDLING CLIENT SOCKET COMMUNICATION***********************/
		//printf("In while before read\n");
		message_t msg;

		// Wait here till data is received from client
		if(recv(socketDesc,bptr,buflen,MSG_DONTWAIT) > 0)
		{
			printf("Message received at socketDesc: %d , message is %d\n",socketDesc,sizeof(message_t));

			//printf("%x %x %x\n",(unsigned char) buffer[0],buffer[1],buffer[2]);

			memcpy(&msg, &buffer, sizeof(msg));
			printf("%X %X %X %X \n",msg.header,msg.msgid,msg.msglen,msg.eom);

			// Parse client Command
			switch(msg.msgid)
			{
				// Handle parsed command
			case REGUSER:
				{
					// got reguser
					printf("Handling Reg user command");
					if(regUserListIndex == -1)
						regUserListIndex = handleRegUser(socketDesc);
					else{
						printf("User has already registered\n");
						sendCommandNotSupported(socketDesc,REPEAT_REGISTRATION);
					}
				}
				break;
			case NEWGAME:
				{
					// new game
					printf("Handling New Game command");
					if(msg.msglen == 0)
						sendCommandNotSupported(socketDesc,COMMAND_INCOMPLETE);
					else{
						if(regUserListIndex == -1)
						{
							// User needs to register first
							printf("User needs to register before new game\n");
							sendCommandNotSupported(socketDesc,USER_NOT_REG);
						}
						else
						{
							gameIndex = handleNewGame(regUserListIndex,(msg.data[0]));
							printf("Game handled with index %d\n",gameIndex);
						}
					}
				}
				break;
			case ENDGAME:
				{
					// end game
					printf("Handling End game command");
					if(regUserListIndex == -1)
					{
						// User needs to register first
						printf("User needs to register first\n");
						sendCommandNotSupported(socketDesc,USER_NOT_REG);
					}
					else if(gameIndex == -1)
					{
						// User needs to start game first
						printf("No game running\n");
						sendCommandNotSupported(socketDesc,NO_GAME);

					}
					else{
						printf("Handling End Game of index %d\n",gameIndex);
						handleEndGame(regUserListIndex,gameIndex);
					}
				}
				break;
			case SELECTMOVE:
				{
					if(regUserListIndex == -1)
					{
						// User needs to register first
						printf("User needs to register first\n");
						sendCommandNotSupported(socketDesc,USER_NOT_REG);
					}
					else if(gameIndex == -1)
					{
						// User needs to start game first
						printf("No game running\n");
						sendCommandNotSupported(socketDesc,NO_GAME);

					}
					else{
						// make a move
						printf("Handling Make a move command");

						if(msg.msglen == 0)
							sendCommandNotSupported(socketDesc,COMMAND_INCOMPLETE);
						else
						{

							makeAMove(regUserListIndex,gameIndex,msg.data[0],msg.data[1]);
						}
					}
				}
				break;
			default:
				{

					sendCommandNotSupported(socketDesc,COMMAND_NOT_SUPPORTED);
				}
				break;
			}


		}




	}

	if(regUserListIndex != -1)
	{
		(void) pthread_mutex_lock(&stats.st_mutex);
		stats.st_totalregusers--;
		(void) pthread_mutex_unlock(&stats.st_mutex);

		(void) pthread_mutex_lock(&regUsersList_mutex);
		regUsersList.at(regUserListIndex) = -1;
		(void) pthread_mutex_unlock(&regUsersList_mutex);
	}
	// Update stats before closing socket
	updateEndThreadStats(start);




	close(socketDesc);
    pthread_exit(NULL);
    return EXIT_SUCCESS;

}

/*------------------------------------------------------------------------
 * prstats - print server statistical data
 *------------------------------------------------------------------------
 */
void prstats(void)
{
	time_t	now;

	while (1) {
		(void) sleep(INTERVAL);

		(void) pthread_mutex_lock(&stats.st_mutex);
		now = time(0);
		(void) printf("--- %s", ctime(&now));
		(void) printf("%-32s: %u\n", "Current connections",
			stats.st_concount);
		(void) printf("%-32s: %u\n", "Completed connections",
			stats.st_contotal);
		if (stats.st_contotal) {
			(void) printf("%-32s: %.2f (secs)\n",
				"Average complete connection time",
				(float)stats.st_contime /
				(float)stats.st_contotal);

		}

		(void) printf("%-32s: %lu\n\n", "Total Running games",
							stats.st_totalgames);
		(void) printf("%-32s: %lu\n\n", "Total registered Users",
							stats.st_totalregusers);
		(void) pthread_mutex_unlock(&stats.st_mutex);

	}
}

/*------------------------------------------------------------------------
 * errexit - print an error message and exit
 *------------------------------------------------------------------------
 */
int errexit(const char *format, ...)
{
	va_list	args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(1);
}

void error(char *msg)
{
    perror(msg);
    exit(1);
}


/*------------------------------------------------------------------------
 * updateStartThreadStats - Updates statistics every time a thread starts
 *------------------------------------------------------------------------
 */
void updateStartThreadStats()
{
	(void) pthread_mutex_lock(&stats.st_mutex);
	stats.st_concount++;
	(void) pthread_mutex_unlock(&stats.st_mutex);
}

/*------------------------------------------------------------------------
 * updateEndThreadStats - Updates statistics every time a thread ends
 *------------------------------------------------------------------------
 */
void updateEndThreadStats(time_t startTime)
{
	(void) pthread_mutex_lock(&stats.st_mutex);
	stats.st_contime += time(0) - startTime;
	stats.st_concount--;
	stats.st_contotal++;
	(void) pthread_mutex_unlock(&stats.st_mutex);
}

/*------------------------------------------------------------------------
 * parseClientMessage - Parses client message and extracts command
 *                        and arguments
 *------------------------------------------------------------------------
 */
bool parseClientMessage(char* message, char* command, char* arg)
{
	bool retVal = false;
	char* local;

	if(message != NULL){
		local = strtok(message," ");
		//printf("Cmd: %s, message: %s\n",local,message);
		if(local != NULL){

			sprintf(command,"%s",local);
			printf("Command has: %s",command);
			retVal = true;
		}


		local = strtok (NULL, " ");
		//printf("Arg: %s, message: %s\n",local,message);
		if(local != NULL){
			sprintf(arg,"%s",local);
			retVal = true;
		}



	}

	return retVal;

}


/*------------------------------------------------------------------------
 * sendCommandNotSupported - Send command not supported to client
 *------------------------------------------------------------------------
 */
void sendCommandNotSupported(int fd, int error_code)
{
	char buffer[MAX_BUFFER_SIZE];
	message_t msg;

	memset(&msg,0, sizeof(msg));
	msg.header = 0xA5;
	msg.msgid = ERROR_MSG;
	msg.msglen = 1;
	msg.data[0]= error_code;
	msg.eom=0x5A;
	memcpy(buffer, &msg, (int)sizeof(msg));
	int x = send(fd, buffer, sizeof(msg), 0);

	// Command not supported
	printf("Sending Command not supported %d\n",x);


}

/*------------------------------------------------------------------------
 * handleRegUser - Handles reguser command from client
 *------------------------------------------------------------------------
 */
int handleRegUser(int fd)
{
	// TODO: Add support for registration failed if max users exceeded
	int index = -1;
	char buffer[MAX_BUFFER_SIZE];

	(void) pthread_mutex_lock(&regUsersList_mutex);
	regUsersList.push_back(fd);
	index = regUsersList.size() - 1;
	(void) pthread_mutex_unlock(&regUsersList_mutex);

	(void) pthread_mutex_lock(&stats.st_mutex);
	stats.st_totalregusers++;
	(void) pthread_mutex_unlock(&stats.st_mutex);

	message_t msg;

	memset(&msg,0, sizeof(msg));
	msg.header = 0xA5;
	msg.msgid = USERREG_SUCCESS;
	msg.msglen = 1;
	msg.data[0]= index;
	msg.eom=0x5A;
	memcpy(buffer, &msg, (int)sizeof(msg));
	send(fd, buffer, sizeof(msg), 0);

	printf("New user registered with id: %d\n",index);

	// Sending user list
	memset(&msg,0, sizeof(msg));
	msg.header = 0xA5;
	msg.msgid = USERLIST;
	msg.msglen = 0;
	int j=0;
	for(unsigned int i=0;i<9;i++)
	{
		(void) pthread_mutex_lock(&regUsersList_mutex);
		if(i< regUsersList.size()){
			if(regUsersList.at(i) != -1){
				msg.data[j]= i;
				msg.msglen++;
				j++;
			}
		}

		(void) pthread_mutex_unlock(&regUsersList_mutex);

	}

	msg.eom=0x5A;
	memcpy(buffer, &msg, (int)sizeof(msg));
	send(fd, buffer, sizeof(msg), 0);

	return index;

}

/*------------------------------------------------------------------------
 * handleNewGame - Handles newuser XX command from client
 *------------------------------------------------------------------------
 */
int handleNewGame(int fd,int player)
{
	int game_index = -1;// -1 indicates unsuccessful game creation
	int sockdes;
	game_t local_game = {0};
	int playerSockDes;
	int RegUserListSize;
	char buffer[MAX_BUFFER_SIZE];

//
//	if(fd == player)
//	{
//		printf("Cannot create game with yourself\n");
//		sendCommandNotSupported(fd,COMMAND_NOT_SUPPORTED);
//		return game_index;
//	}

	// Get socket descriptor of the current player and total registered users
	(void) pthread_mutex_lock(&regUsersList_mutex);
		sockdes = regUsersList.at(fd);
		RegUserListSize = regUsersList.size();
	(void) pthread_mutex_unlock(&regUsersList_mutex);

	if(fd == player)
	{
		printf("Cannot create game with yourself\n");
		sendCommandNotSupported(sockdes,COMMAND_NOT_SUPPORTED);
		return game_index;
	}

	// Check if other player is a valid player
	if(player >= RegUserListSize)
	{
		printf("Player doesnt exist\n");
		sendCommandNotSupported(fd,PLAYER_DOESNOT_EXIST);
		return game_index;
	}

	// If player exists, get player's socket descriptor
	(void) pthread_mutex_lock(&regUsersList_mutex);
		playerSockDes = regUsersList.at(player);
	(void) pthread_mutex_unlock(&regUsersList_mutex);

	printf("Checking if player: %d socket is closed\n",player);

	//Check if connection is closed
	if (recv(playerSockDes, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT) == 0)
	{
		// if recv returns zero, that means the connection has been closed:
		printf("Player socket is closed\n");
		sendCommandNotSupported(fd,PLAYER_DOESNOT_EXIST);

	}
	else
	{
		char buffer[20];
		printf("Sending new game request to socket %d \n",playerSockDes);
		sprintf(buffer,"newgamereq %d",fd);

		string queueSize = "nothing";

		//Wait till queue size is 0
		while(queueSize != "no data")
		{
			(void) pthread_mutex_lock(&regUserQueue_mutex);
			queueSize = regUserQueue.at(player);
			(void) pthread_mutex_unlock(&regUserQueue_mutex);

		}


		// Store new game request in queue
		(void) pthread_mutex_lock(&regUserQueue_mutex);
		regUserQueue.at(player) = buffer;
		(void) pthread_mutex_unlock(&regUserQueue_mutex);

		printf("Waiting on thread response\n");


		//Wait till there is a response
		while(queueSize == "no data")
		{
			(void) pthread_mutex_lock(&regUserQueue_mutex);
			queueSize = regUserQueue.at(fd);
			(void) pthread_mutex_unlock(&regUserQueue_mutex);

		}

		printf("Data received %s\n",queueSize.c_str());

		// reset queue data
		(void) pthread_mutex_lock(&regUserQueue_mutex);
		regUserQueue.at(fd) = "no data";
		(void) pthread_mutex_unlock(&regUserQueue_mutex);

		// Check if thread replied with yes or no
		if(queueSize == "yes")
		{
			printf("response is yes\n");


			// Create a new game
			(void) pthread_mutex_lock(&runningGameList_mutex);
				local_game.player1_index = fd;
				local_game.player2_index = player;
				memset(&local_game.gameArray,0,sizeof(local_game.gameArray));
				local_game.player1_status = game_in_progress;
				local_game.player2_status = game_in_progress;
				local_game.validGame = true;

				runningGameList.push_back(local_game);
				game_index = runningGameList.size() - 1;
			(void) pthread_mutex_unlock(&runningGameList_mutex);

			(void) pthread_mutex_lock(&stats.st_mutex);
				stats.st_totalgames++;
			(void) pthread_mutex_unlock(&stats.st_mutex);

		}
		else if(queueSize == "no")
		{
			printf("response is no\n");
		}
	}

	message_t msg;

	if(game_index != -1){
		printf("New game created with player: %d\n",player);


		memset(&msg,0, sizeof(msg));
		msg.header = 0xA5;
		msg.msgid = GAME_STARTED;
		msg.msglen = 1;
		msg.data[0]= player;
		msg.eom=0x5A;
		memcpy(buffer, &msg, (int)sizeof(msg));
		send(sockdes, buffer, sizeof(msg), 0);

		// Also send command to other thread
		sprintf(buffer,"newgamecreated %d",game_index);

		string queueSize = "nothing";

		//Wait till players queue is empty
		while(queueSize != "no data")
		{
			(void) pthread_mutex_lock(&regUserQueue_mutex);
			queueSize = regUserQueue.at(player);
			(void) pthread_mutex_unlock(&regUserQueue_mutex);

		}
		(void) pthread_mutex_lock(&regUserQueue_mutex);
		regUserQueue.at(player) = buffer;
		(void) pthread_mutex_unlock(&regUserQueue_mutex);

		printf("Sending make a move command to player %d\n",fd);
		memset(&msg,0, sizeof(msg));
		msg.header = 0xA5;
		msg.msgid = MAKE_A_MOVE;
		msg.msglen = 9;
		msg.eom=0x5A;
		memcpy(buffer, &msg, (int)sizeof(msg));
		send(sockdes, buffer, sizeof(msg), 0);
	}
	else
	{
		printf("New game NOT created with player: %d\n",player);

		memset(&msg,0, sizeof(msg));
		msg.header = 0xA5;
		msg.msgid = GAME_START_FAIL;
		msg.msglen = 0;
		msg.eom=0x5A;
		memcpy(buffer, &msg, (int)sizeof(msg));
		send(fd, buffer, sizeof(msg), 0);
	}

	return game_index;

}

/*------------------------------------------------------------------------
 * handleEndGame - Handles endgame command from client
 *------------------------------------------------------------------------
 */
void handleEndGame(int fd, int gameIndex)
{
	int sockdes;
	game_t game;
	int player2;

	char buffer[MAX_BUFFER_SIZE];
	message_t msg;

	(void) pthread_mutex_lock(&regUsersList_mutex);
	sockdes = regUsersList.at(fd);
	(void) pthread_mutex_unlock(&regUsersList_mutex);

	(void) pthread_mutex_lock(&runningGameList_mutex);
	if((unsigned int)gameIndex < runningGameList.size())
	{
		game = runningGameList.at(gameIndex);
		printf("Valid id\n");
	}
	else{
		gameIndex = -1;
		printf("Game index not valid\n");
	}
	(void) pthread_mutex_unlock(&runningGameList_mutex);

	if(game.validGame == false)
	{
		printf("Game flag not valid\n");
		gameIndex = -1;
	}

	if(gameIndex == -1)
	{
		//In valid game
		printf("Cannot end game. Index is -1\n");
		sendCommandNotSupported(sockdes,NO_GAME);
	}
	else{

		player2 = (game.player1_index == fd)? game.player2_index:game.player1_index;

		(void) pthread_mutex_lock(&stats.st_mutex);
			stats.st_totalgames--;
		(void) pthread_mutex_unlock(&stats.st_mutex);

		printf("Game ended with player\n");

		(void) pthread_mutex_lock(&runningGameList_mutex);
			runningGameList.at(gameIndex).validGame = false;
		(void) pthread_mutex_unlock(&runningGameList_mutex);



		memset(&msg,0, sizeof(msg));
		msg.header = 0xA5;
		msg.msgid = GAME_ENDED;
		msg.msglen = 0;
		msg.eom=0x5A;
		memcpy(buffer, &msg, (int)sizeof(msg));
		send(sockdes, buffer, sizeof(msg), 0);
		(void) pthread_mutex_lock(&regUsersList_mutex);
		sockdes = regUsersList.at(player2);
		(void) pthread_mutex_unlock(&regUsersList_mutex);
		send(sockdes, buffer, sizeof(msg), 0);
	}
}

/*------------------------------------------------------------------------
 * makeAMove - client makes a move
 *------------------------------------------------------------------------
 */
void makeAMove(int fd, int gameIndex,int row,int column)
{
	game_t localGame;
	int sockdes;
	message_t msg;
	char buffer[MAX_BUFFER_SIZE];


	(void) pthread_mutex_lock(&regUsersList_mutex);
	sockdes = regUsersList.at(fd);
	(void) pthread_mutex_unlock(&regUsersList_mutex);

	if(row >= 3 ||
			column >= 3)
	{
		sendCommandNotSupported(sockdes,INVALID_OPTION);
		return;
	}

	(void) pthread_mutex_lock(&runningGameList_mutex);
	localGame = runningGameList.at(gameIndex);
	(void) pthread_mutex_unlock(&runningGameList_mutex);

	int index = 3*row + column;
	printf("Index is : %d\n",index);
	// Update clients tic tac toe table
	if(localGame.gameArray[index] == 0)
	{
		if(fd == localGame.player1_index)
			localGame.gameArray[index] = '*';
		else
			localGame.gameArray[index] = 'o';
		printf("Making a move\n");
		for(unsigned int i=0;i<9;i++){
			(void) pthread_mutex_lock(&runningGameList_mutex);
				runningGameList.at(gameIndex).gameArray[i] = localGame.gameArray[i];
			(void) pthread_mutex_unlock(&runningGameList_mutex);
		}

		int i;
		char winner = 0;
		for (i = 0; i < 3; i++) {
			if ((localGame.gameArray[i*3] != 0) &&
					localGame.gameArray[i*3 + 1] == localGame.gameArray[i*3] &&
					localGame.gameArray[i*3 + 2] == localGame.gameArray[i*3])
				winner = localGame.gameArray[i*3];
			if ((localGame.gameArray[i+3]!= 0) &&
					localGame.gameArray[i+3] == localGame.gameArray[i] &&
					localGame.gameArray[i+6] == localGame.gameArray[i])
				winner = localGame.gameArray[i];
		}
		if(winner == 0){
			if (localGame.gameArray[4] == 0)
				winner = 0;
			else if (localGame.gameArray[4] == localGame.gameArray[0] &&
					localGame.gameArray[8] == localGame.gameArray[0])
				winner = localGame.gameArray[0];
			else if (localGame.gameArray[4] == localGame.gameArray[6] &&
					localGame.gameArray[2] == localGame.gameArray[4])
				winner = localGame.gameArray[4];
		}

		if(winner != 0){
			printf("There is a winner and it is %c\n",winner);

			int winnersock = (winner == '*')?localGame.player1_index:localGame.player2_index;
			int losersock = (winner == '*')?localGame.player2_index:localGame.player1_index;

			(void) pthread_mutex_lock(&regUsersList_mutex);
			sockdes = regUsersList.at(winnersock);
			(void) pthread_mutex_unlock(&regUsersList_mutex);
			// Send GAME_WON to one and GAME_LOST to one
			memset(&msg,0, sizeof(msg));
			msg.header = 0xA5;
			msg.msgid = GAME_WON;
			msg.msglen = 0;
			msg.eom=0x5A;
			memcpy(buffer, &msg, (int)sizeof(msg));
			send(sockdes, buffer, sizeof(msg), 0);
			msg.msgid = GAME_ENDED;
			memcpy(buffer, &msg, (int)sizeof(msg));
			send(sockdes, buffer, sizeof(msg), 0);

			(void) pthread_mutex_lock(&regUsersList_mutex);
			sockdes = regUsersList.at(losersock);
			(void) pthread_mutex_unlock(&regUsersList_mutex);
			// Send GAME_WON to one and GAME_LOST to one
			memset(&msg,0, sizeof(msg));
			msg.header = 0xA5;
			msg.msgid = GAME_LOST;
			msg.msglen = 0;
			msg.eom=0x5A;
			memcpy(buffer, &msg, (int)sizeof(msg));
			send(sockdes, buffer, sizeof(msg), 0);
			msg.msgid = GAME_ENDED;
			memcpy(buffer, &msg, (int)sizeof(msg));
			send(sockdes, buffer, sizeof(msg), 0);

			//and game ended to both

			(void) pthread_mutex_lock(&stats.st_mutex);
				stats.st_totalgames--;
			(void) pthread_mutex_unlock(&stats.st_mutex);
		}
		else{
			//char tie = 0;
			bool tieflag = true;
			for (i = 0; i < 9; i++)
			{
				if (localGame.gameArray[i] == 0){
					tieflag = false;
					break;
				}
			}

			if(tieflag==true)
			{
				printf("There is a tie");
				int sock1 = localGame.player1_index;
				int sock2 = localGame.player2_index;

				(void) pthread_mutex_lock(&regUsersList_mutex);
				sockdes = regUsersList.at(sock1);
				(void) pthread_mutex_unlock(&regUsersList_mutex);

				memset(&msg,0, sizeof(msg));
				msg.header = 0xA5;
				msg.msgid = GAME_TIE;
				msg.msglen = 0;
				msg.eom=0x5A;
				memcpy(buffer, &msg, (int)sizeof(msg));
				send(sockdes, buffer, sizeof(msg), 0);
				msg.msgid = GAME_ENDED;
				memcpy(buffer, &msg, (int)sizeof(msg));
				send(sockdes, buffer, sizeof(msg), 0);

				msg.msgid = GAME_TIE;
				(void) pthread_mutex_lock(&regUsersList_mutex);
				sockdes = regUsersList.at(sock2);
				(void) pthread_mutex_unlock(&regUsersList_mutex);
				memcpy(buffer, &msg, (int)sizeof(msg));
				send(sockdes, buffer, sizeof(msg), 0);

				msg.msgid = GAME_ENDED;
				memcpy(buffer, &msg, (int)sizeof(msg));
				send(sockdes, buffer, sizeof(msg), 0);
			}
			else
			{
				memset(&msg,0, sizeof(msg));
				msg.header = 0xA5;
				msg.msgid = MOVE_SUCCESS;
				msg.msglen = 9;
				for(unsigned int i=0;i<9;i++)
				{
					msg.data[i] = localGame.gameArray[i];
				}
				msg.eom=0x5A;
				memcpy(buffer, &msg, (int)sizeof(msg));
				send(sockdes, buffer, sizeof(msg), 0);

				int player2 = (localGame.player1_index == fd)?localGame.player2_index:localGame.player1_index;

				printf("Sending make a move command to player %d\n",player2);

				(void) pthread_mutex_lock(&regUsersList_mutex);
				sockdes = regUsersList.at(player2);
				(void) pthread_mutex_unlock(&regUsersList_mutex);

				msg.msgid = MAKE_A_MOVE;

				memcpy(buffer, &msg, (int)sizeof(msg));
				send(sockdes, buffer, sizeof(msg), 0);

			}
		}
	}
	else{
		// Invalid move
		printf("Move not possible\n");
		sendCommandNotSupported(sockdes,INVALID_OPTION);
	}

	// Check if a win/loss/tie decision is made

	//Send response to client and to server socket of opponent



}