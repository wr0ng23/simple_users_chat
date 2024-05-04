#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <linux/unistd.h>
#include <signal.h>
#include <bits/sigaction.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "util.h"

#define MY_ADDR "127.0.0.2"
#define SERVER_ADDR "127.0.0.1"

pthread_t listen_thread_id;
pthread_t recv_thread_id;

struct sockaddr_in cli_addr;

char usernames[3][USERNAME_LEN];
int saved_users_cnt = 0;
struct sockaddr_in client_sockaddrins[3];

int sockfd;
int clisockfd;

char snd_pr_msg_flag = 0;
// char show_chat_messages = 0;

struct sockaddr arr_users_sockets[20];

pthread_mutex_t snd_pr_msg_mut; // locks with start of private message sending, unlocks with recieving list of users in private message sending

void f_obr_user_thread(int sig) {
    printf("SIGPIPE in user thread!\n");
    pthread_mutex_destroy(&snd_pr_msg_mut);
    close(sockfd);
    shutdown(clisockfd, SHUT_RDWR);
    close(clisockfd);
    pthread_cancel(recv_thread_id);
    exit(EXIT_SUCCESS); 
}

// for recieving common messages  
void* recv_thread_routine(void* _){

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = f_obr_user_thread;
    sigaction(SIGPIPE, &sa, 0);

    char exit_thread = 0;
    message msg;
    while(!exit_thread){
        recv(sockfd, &msg, sizeof(msg), 0);
        switch (msg.msg_type)
        {
        case MSG_TRANSFER_MSG:
            msg.username[strlen(msg.username) - 1] = '\0';
            printf("%s: ", msg.username);
            printf("%s", msg.text);
            break;
        
        case MSG_USERS:
            char printflg = 1;
            pthread_mutex_lock(&snd_pr_msg_mut);
            if(snd_pr_msg_flag == 1){
                printflg = 0;
            }
            pthread_mutex_unlock(&snd_pr_msg_mut);
            // check if we are sending private message
            int num_of_users = 0;
            num_of_users = atoi(msg.text);
            if (num_of_users == 0)
                break;

            if (printflg)
                printf("Number of users: %i\n", num_of_users);
            

            char name[MSG_BUFF_SIZE]; 
            struct sockaddr_in adr;
            for (int i = 0; i < num_of_users; i++){
                recv(sockfd, &msg, sizeof(msg), 0);
                strcpy(name, msg.username);
                name[strlen(name) - 1] = '\0';
                adr = msg.client_socket;
                if (printflg)
                    printf("User %i: %s %s  %i\n", i + 1, name, inet_ntoa(adr.sin_addr), ntohs(adr.sin_port));
                // save
                strcpy(usernames[i], name);
                client_sockaddrins[i].sin_family = AF_INET;
                client_sockaddrins[i].sin_port = ntohs(adr.sin_port);
                client_sockaddrins[i].sin_addr.s_addr = inet_addr(inet_ntoa(adr.sin_addr));
                //printf("Saved user %i: %s %s  %i\n", i + 1, usernames[i], inet_ntoa(client_sockaddrins[i].sin_addr), client_sockaddrins[i].sin_port);
            }
            saved_users_cnt = num_of_users;

            // unblock main thread if it is waiting for recieving list of users for private message sending
            pthread_mutex_lock(&snd_pr_msg_mut);
            snd_pr_msg_flag = 0;
            pthread_mutex_unlock(&snd_pr_msg_mut);
            
        break;
        default:
            break;
        }
        
    }

}

void* listen_thread_routine(void* _){

    struct message msg;
    while(1){
        printf("\033[90mListening for whispers in the dark...\033[0m\n");
        if (listen(clisockfd, 3) < 0){
            printf("\033[91mError in listening\033[0m\n");
            continue;
        }
        socklen_t size = sizeof(cli_addr);
        int othersockfd = accept(clisockfd, (struct sockaddr*)&cli_addr, &size);
        if (othersockfd < 0){
            printf("\033[91mError in accepting!\033[0m\n");
            continue;
        }
        //send(othersockfd, "PRIVATE MESSAGE", sizeof("PRIVATE MESSAGE"), 0);
        recv(othersockfd, &msg, sizeof(msg), 0);
        close(othersockfd);

        if (msg.msg_type != MSG_DIRECT_MSG)
            printf("\033[91mLISTEN THREAD CAUGHT NOT PRIVATE MSG!!!\033[0m\n");

        char namebuf[USERNAME_LEN];
        strcpy(namebuf, msg.username);
        namebuf[strlen(namebuf) - 1] = '\0';
        printf("\033[93mPrivate message from %s: %s\033[0m", namebuf, msg.text);
    }
}


int main(void){

    char username[USERNAME_LEN];
    message msg; // buffer

    srand(time(NULL));

    if(pthread_mutex_init(&snd_pr_msg_mut, 0) < 0){
        printf("Error in mutex init\n");
        exit(EXIT_FAILURE);
    }

    cli_addr.sin_addr.s_addr = inet_addr(MY_ADDR);
    cli_addr.sin_family = AF_INET;
    clisockfd = socket(AF_INET, SOCK_STREAM, 0);
    int true = 1;
    int codeErr = setsockopt(clisockfd,SOL_SOCKET,SO_REUSEADDR,&true,sizeof(int));
    if (codeErr == -1) {
        perror("setsockopt\n");
        exit(1);
    }
    
    uint16_t my_port;
    int cnt_err = 0;
    for (int i = 0; i < 3; i++){
        //randomize my port 40000-50000 values alllowed
        my_port = rand() % 10000 + 40000;
        printf("Generated port: %u\n", my_port);
        cli_addr.sin_port = htons(my_port);
        if (bind(clisockfd, (struct sockaddr*)&cli_addr, sizeof(cli_addr)) == -1){
            cnt_err++;
            continue;
        }
        break;
    }
    if (cnt_err >= 3){
        printf("error with binding\n");
        exit(EXIT_FAILURE);
    }
    printf("My port is %u\n", my_port);


    if (pthread_create(&listen_thread_id, NULL, listen_thread_routine, NULL) != 0){
        printf("Error in creation of listen thread\n");
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){
        printf("Error in socket opening\n");
        return -1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Error when coonection!\n");
        return -1;
    } else {
        printf("Connection done!\n");
    }

    // authorize
    recv(sockfd, &msg, sizeof(msg), 0);
    if (msg.msg_type == MSG_AUTH) {
        printf("Enter username or login: ");
        fgets(msg.username, USERNAME_LEN, stdin);
        strcpy(username, msg.username);
        printf("You entered: %s\n", msg.username);
        printf("Enter password: ");
        fgets(msg.text, MSG_BUFF_SIZE, stdin);
        printf("You entered: %s\n", msg.text);
        msg.msg_type = MSG_AUTH;
        send(sockfd, &msg, sizeof(msg), 0);
    } else {
        printf("Error!\n");
        exit(1);
    }
    recv(sockfd, &msg, sizeof(msg), 0);
    //printf("%s\n", msg.text);

    switch (msg.msg_type)
    {
        case REGISTRATION:
            printf("%s", msg.text);
            char ynflag = 0;
            char ynchar;
            while(!ynflag){
                // read first character
                ynchar = getchar();
                // skip other characters 
                while(getchar() != '\n');

                if (ynchar == 'n'){
                    strcpy(msg.text, "n\0");
                    ynflag = 1;
                }
                else if (ynchar == 'y'){
                    strcpy(msg.text, "y\0");
                    ynflag = 1;
                }
                else{
                    printf("Error. Enter once again[y/n]\n");
                }
            }
            msg.msg_type = REGISTRATION;
            strcpy(msg.username, username);
            // sending y or n
            send(sockfd, &msg, sizeof(msg), 0);
            if (ynchar == 'n'){
                close(sockfd);
                exit(-1);
            }
            recv(sockfd, &msg, sizeof(msg), 0);
            printf("%s", msg.text);
            //msg.socket = cli_addr;
            //memcpy(&msg.text, &cli_addr, sizeof(cli_addr));
            msg.client_socket = cli_addr;
            send(sockfd, &msg, sizeof(msg), 0);
        break;

        case FORBIDDEN:
            printf("%s", msg.text);
            exit(-1);
        break;


        case MSG_TRANSFER_MSG:
            printf("%s", msg.text);
            //msg.socket = cli_addr;
            //memcpy(&msg.text, &cli_addr, sizeof(cli_addr));
            msg.client_socket = cli_addr;
            send(sockfd, &msg, sizeof(msg), 0);
        break;


    default:
        break;
    }

    // create thread for recv

    if (pthread_create(&recv_thread_id, NULL, recv_thread_routine, NULL) != 0){
        printf("Error in creation of recv thread\n");
    }
    
    // enter message from console
    char exitflag = 0;
    while(!exitflag){
        printf("Enter operation(h for help)\n");
        // read first character
        char option = getchar();
        // skip other characters 
        while(getchar() != '\n');
        //printf("char: %c\n", option);
        switch(option){

            // display help
            case 'h':
            printf("s - Enter chat mode\np - private message for another user\nu - get all users from server\ne - exit\n");
            break;

            // Enter chat mode
            case 's':
            printf("Chat mode entered. q to exit.\n");

            char stopchatting = 0;
            while(!stopchatting){
                //printf("Enter message:\n");
                // send message
                msg.msg_type = MSG_TRANSFER_MSG;
                strcpy(msg.username, username);
                // enter message
                //msg.username[strlen(msg.username) - 1] = '\0';
                //printf("%s: ", msg.username);
                fgets(msg.text, MSG_BUFF_SIZE, stdin);
                if (strcmp(msg.text, "q\n") == 0){
                    stopchatting = 1;
                    break;
                }

                // send
                send(sockfd, &msg, sizeof(msg), 0);
            }

            break;


            // get users
            case 'u':

            msg.msg_type = MSG_USERS;
            strcpy(msg.username, username);

            // send request
            send(sockfd, &msg, sizeof(msg), 0);

            break;


            case 'p':
            // private message for another user

            pthread_mutex_lock(&snd_pr_msg_mut);
            // raise flag of private messages
            snd_pr_msg_flag = 1;
            pthread_mutex_unlock(&snd_pr_msg_mut);

            // get users
            msg.msg_type = MSG_USERS;
            strcpy(msg.username, username);
            // send request
            send(sockfd, &msg, sizeof(msg), 0);

            // wait until recieve
            while(1){
                pthread_mutex_lock(&snd_pr_msg_mut);
                if (snd_pr_msg_flag == 0){
                    // recieved answer with users
                    pthread_mutex_unlock(&snd_pr_msg_mut);
                    break;
                }
                else{
                    // not recieved answer with users
                    pthread_mutex_unlock(&snd_pr_msg_mut);
                    usleep(30000); // sleep 30 millisecs
                }
                
            }
            
            if (saved_users_cnt <= 0){
                printf("There is no other users to send them messages!\n");
                break;
            }

            printf("\nChoose user to send him/her message:\n");
            int idx_usr_chosen = 0;
            for (int i = 0; i < saved_users_cnt; i++){
                printf("User %i: %s\n", i + 1, usernames[i]);
            }
            printf("\nEnter number of user. q to exit.\n");
            char strbuf[10];
            char exit_flag = 0;
            while(1){
                fgets(strbuf, 10, stdin);
                if (strcmp(strbuf, "q\n") == 0){
                    exit_flag = 1;
                    break;
                }
                else if (strbuf[0] >= '1' && strbuf[0] <= '9'){
                    idx_usr_chosen = strbuf[0] - '1';
                    break;
                }
                else{
                    printf("Wrong input. Try once again\n");
                }
            }
            if (exit_flag == 1)
                break;

            printf("User chosen: %i\n", idx_usr_chosen + 1);

            msg.msg_type = MSG_DIRECT_MSG;
            strcpy(msg.username, username);
            // enter message
            printf("Enter message:\n");
            fgets(msg.text, MSG_BUFF_SIZE, stdin);
            
            // connect to another user
            int sockfd2 = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd2 < 0){
                printf("Error in socket2 opening\n");
                break;
            }

            struct sockaddr_in sockaddrbuff = client_sockaddrins[idx_usr_chosen];
            sockaddrbuff.sin_port = htons(sockaddrbuff.sin_port);
            //printf("htons port %u\n", sockaddrbuff.sin_port);
            if (connect(sockfd2, (struct sockaddr*)&sockaddrbuff, sizeof(sockaddrbuff)) < 0){
                printf("Can not connect to user\n");
                break;
            }

            // send
            send(sockfd2, &msg, sizeof(msg), 0);
            // disconnect
            close(sockfd2);

            printf("Message sent\n\n");

            break;


            // exit 
            case 'e':
            msg.msg_type = EXITING;
            strcpy(msg.username, username);
            send(sockfd, &msg, sizeof(msg), 0);
            exitflag = 1;
            break;

            default:
        }
    }

    //shutdown(sockfd, SHUT_RDWR);
    pthread_cancel(recv_thread_id);
    pthread_cancel(listen_thread_id);
    close(sockfd);
    // shutdown(clisockfd, SHUT_RDWR);
    close(clisockfd);
    pthread_mutex_destroy(&snd_pr_msg_mut);

    return 0;
}