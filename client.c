#include<stdio.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/time.h>
#include<string.h>
#include<errno.h>
#include "AllCommands.h"
#define MAXBUF 1024

void about()
{
     printf("Tic-Tac-Toe (Version 5.0) \n");
     printf("Copyright © 2015. All rights reserved. \n");
}

void help()
{
    printf("1.Rules of the game \n");
    printf("2. Top 5 scorers \n");
    printf("3. My statistics \n");

}

void viewstats()
{
    printf("Statistics currently not available");
}

void print_tic_tac_toe(char* data)
{
    int z;

    printf("\n");
    printf("----------\n");
    for (z = 0; z < 9; z=z+3)
    {
        printf("| %c | %c | %c |\n", data[z], data[z+1], data[z+2]);
        if (z != 8)
            printf("----------\n");
    }
    printf("\n");

}
void new_msg_create(message_t *msg, int msgid, int msglen, int data)
{
    msg->header = 0xA5;
    msg->msgid = msgid;
    msg->msglen = msglen;
    msg->data[0] = data;
    msg->eom=0x5A;
    /*Add::::: on the basis of confirmation message user is going to start a new game*/

}
void msg_append(message_t *msg, int index, int data)
{
    msg->msglen++;
    msg->data[index] = data;

}
void newgame(int id, int new_sd)
{
    char buffer[MAXBUF];
    message_t msg;
    new_msg_create(&msg,NEWGAME, 1, id);
    memcpy(buffer, &msg, (int)sizeof(msg));
    send(new_sd, buffer, sizeof(msg), 0);
}

void game_start()
{
    printf("A new game has been started\n");

}

void make_a_move(int new_sd, message_t *msg_rcv)
{
    int row, col;
    message_t msg;
    char buffer[MAXBUF];
    print_tic_tac_toe((char*)msg_rcv->data);
    printf("Make a move now\n");
    printf("Select row number (0-2)\n");
    scanf("%d", &row);
    printf("Select column number (0-2)\n");
    scanf("%d", &col);

    new_msg_create(&msg,SELECTMOVE, 1, row);
    msg_append(&msg, 1, col);
    memcpy(buffer, &msg, (int)sizeof(msg));
    send(new_sd, buffer, sizeof(msg), 0);

}

void move_success()
{

}

int main()
{
    int new_sd=0;
    int id, choice;
    char ch[20];
    struct sockaddr_in serveraddr;
    char buffer[MAXBUF];
    memset(buffer, '0', sizeof(buffer));
    message_t msg;
    memset(&msg, '0', sizeof(msg));
    /*Set all bits of padding to zero*/
    memset(&serveraddr, '0', sizeof(serveraddr));

    /*create socket*/
    new_sd = socket(AF_INET, SOCK_STREAM, 0);
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(7993);
    serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");


    /*Connect*/
    socklen_t addr_size=sizeof(serveraddr);
    if(connect(new_sd,(struct sockaddr *) &serveraddr,sizeof(serveraddr))<0)
    {
        printf("connect failed %s\n", strerror(errno));
    }


    /*Print welcome message*/
    printf("Welcome to Tic-Tac-Toe v207 \n");
    printf("Use \n");
    printf("- 'reguser' to register \n");
    printf("- 'help' for help commands \n");
    printf("- 'about' to read about the game and this software \n");

    /*"Scan the choices")*/
    scanf("%s",ch);
    if(strncmp(ch,"help",10) == 0)
        help();

    else if(strncmp(ch,"about",10) == 0)
            about();

    else if(strncmp(ch,"reguser",10) == 0)

    {
        new_msg_create(&msg,REGUSER, 0, 0);
        memcpy(buffer, &msg, (int)sizeof(msg));
        send(new_sd, buffer, sizeof(msg), 0);
        int len=0;

       /*Receive registration success and userids  from user*/
        while(len != 2*sizeof(message_t))
        {
             len += recv(new_sd, buffer+len, 1024, 0);
        }
        message_t *msg1;
        msg1 = (message_t *)buffer;
        if(msg1[0].msgid== USERREG_SUCCESS)
        {
            printf("You are a registered user with user ID : %d\n", msg1[0].data[0]);
           int i;
            //Read the 2nd message
            printf("List of Registered Users: %d\n", msg1[1].msglen);
            for(i=0;i<msg1[1].msglen;i++)
            {
                if(msg1[1].data[i] != msg1[0].data[0])
                printf("%d\n", msg1[1].data[i]);
            }

            printf("Select a number to choose the option \n");
            printf("1.‘newgame xx’ to initiate game with player xx \n");
            printf("2.‘viewstats’ to view game statistics \n");
            printf("3.‘help’ for help commands \n");
            printf("4.‘about’ to read about the game and this software \n");
            printf("5 ‘wait’ to wait for another player to initiate a game \n");
            scanf("%d", &choice);



            switch (choice){
                case 1:
                    printf("select an ID to start a new game : ");
                    scanf("%d",&id);
                    newgame(id, new_sd);
                case 5:
                    while(1)
                    {

                        len = recv(new_sd, buffer, sizeof(message_t), 0);
                        //printf("shanu len recvd before game start= %d\n", len);
                        msg1 = (message_t *)buffer;

                        switch(msg1->msgid)
                        {
                            case GAME_STARTED :
                            game_start();
                            print_tic_tac_toe((char*)(msg1->data));
                            break;

                            case GAME_START_FAIL :
                            break;

                            case MAKE_A_MOVE :
                            make_a_move(new_sd, msg1);
                            break;

                            case MOVE_SUCCESS :
                            	printf("Received move success \n");
                            	//printf("%c %c \n",msg.data[0],msg.data[1]);
                            	print_tic_tac_toe((char*)(msg1->data));
                            break;

                            case ERROR_MSG :
                            break;

                            case GAME_WON :
                            	printf("Game won\n");
                            break;

                            case GAME_LOST :
                            	printf("Game Lost\n");
                            break;

                            case GAME_ENDED :
                            	printf("Game ended\n");
                            break;

                            case GAME_TIE :
                            	printf("Game tie\n");
                            break;

                            default:
                            break;

                        }
                    }
                case 2:
                    viewstats();
                    break;

                case 3:
                    help();
                    break;

                case 4:
                    about();
                    break;

                default:
                    printf("Wrong Choice\n");
                    break;
            }


        }
        else if(msg1->msgid== USERREG_FAIL)
        printf("Registration not done");

        else
        printf("Try again later");
        }
    else
        printf("Wrong choice");

    close(new_sd);
   return 0;
}