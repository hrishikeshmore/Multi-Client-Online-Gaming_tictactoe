/*
 * AllCommands.h
 *
 *  Created on: Apr 18, 2015
 *      Author: dkh
 */

#ifndef ALLCOMMANDS_H_
#define ALLCOMMANDS_H_

// MESSAGE IDs for messages passed between client and server

/********** Client to Server Messages*******************/
#define REGUSER 0x1000
#define NEWGAME 0x1001
#define ENDGAME 0x1002
#define SELECTMOVE 0x1003

/********** Server to Client Messages*******************/
#define USERREG_SUCCESS 0x0101
#define USERREG_FAIL	0x0102
#define USERLIST		0x0100
#define GAME_STARTED	0x0103
#define GAME_START_FAIL 0x0104
#define GAME_WON		0x0105
#define GAME_LOST		0x0106
#define GAME_TIE		0x0107
#define GAME_ENDED     	0x0108
#define ERROR_MSG 		0x0110
#define MOVE_SUCCESS	0x0109
#define MAKE_A_MOVE 	0x010A


//Note: Errors code will be in data[0] field of ERROR_MSG and msg len =1

/******** Error Codes for message ERROR_MSG***********/
#define COMMAND_NOT_SUPPORTED 	0x01
#define REPEAT_REGISTRATION 	0x02
#define COMMAND_INCOMPLETE     	0x03
#define USER_NOT_REG	     	0x04
#define PLAYER_DOESNOT_EXIST   	0x05
#define NO_GAME				   	0x06
#define INVALID_OPTION		   	0x07

typedef struct {
    uint8_t header;
    uint16_t msgid;
    uint8_t msglen;
    uint8_t data[9];
    uint8_t eom;
}message_t;


#endif /* ALLCOMMANDS_H_ */
