#include <netinet/in.h>

#define USERNAME_LEN 32
#define SERVER_PORT 48124
#define MSG_BUFF_SIZE 256
#define USER_NUMBER 3

typedef enum msg_types{
    MSG_AUTH, // usage: server requests to enter name and password
    MSG_TRANSFER_MSG, // usage: transfer message from one user to another
    MSG_DIRECT_MSG, // usage: direct message to another user
    MSG_USERS, // usage: get list of users
    REGISTRATION, // usage: registration of user
    FORBIDDEN, // usage: forbidden in access
    EXITING // usage: client sends this when exit
} msg_types;

typedef struct message{
    msg_types msg_type;
    char username[USERNAME_LEN];
    char text[MSG_BUFF_SIZE];
    struct sockaddr_in client_socket;
} message;