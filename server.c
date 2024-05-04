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

#define SERVER_ADDR "127.0.0.1"

int is_user_exists(char* login);
int check_password(char* password);
int write_to_file(char* login, char* password);
void f_obr_user_thread(int sig);
void f_obr_listen_thread(int sig);
void f_obr_sig_pipe(int sig);
void* user_thread(void* param);
int send_list_of_users(int sock);
int find_index_of_empty_element(int* arr);

char user_names[USER_NUMBER][USERNAME_LEN] = {0};
int user_sockets_discriptors[USER_NUMBER] = {0};
struct sockaddr_in addresses_of_users[USER_NUMBER] = {0};

int current_number_of_users = 0;
int cnt_of_threads = 0;
pthread_mutex_t mutex;
int sock1;

int main () {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = f_obr_listen_thread;
    sigaction(SIGINT, &sa, 0);

    int codeErr = 0;
    struct sockaddr_in server_addr, client_addr; 
    pthread_mutex_init(&mutex, NULL);

    sock1 = socket(AF_INET, SOCK_STREAM, 0);
    if (sock1 == -1) {
        printf("Error when socket was in process of creating!\n");
        exit(EXIT_FAILURE);
    }

    int true = 1;
    codeErr = setsockopt(sock1,SOL_SOCKET,SO_REUSEADDR,&true,sizeof(int));
    if (codeErr == -1) {
        perror("setsockopt\n");
        exit(1);
    }
    
    memset((char *)&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
    server_addr.sin_port = htons(SERVER_PORT);
    codeErr = bind(sock1, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (codeErr == -1) {
        printf("Error when bind function was called!\n");
        exit(1);
    }

    printf("Server is running on addr: %s\n", SERVER_ADDR);
    pthread_t thread;
    while (1) {
        codeErr = listen(sock1, 3);
        if (codeErr == -1) {
            printf("Error when listening function was activated!\n");
            exit(EXIT_FAILURE);
        }
        if (cnt_of_threads == USER_NUMBER + 1) {
            printf("Limit of users!\n");
            continue;
        }

        socklen_t ans_len = sizeof(client_addr);  
        int sock2 = accept(sock1, (struct sockaddr*)&client_addr, &ans_len);
        if (sock2 == -1) {
            printf("Error when accept connection with sock2!\n");
            exit(EXIT_FAILURE);
        } else {
            printf("Connection done!\n");
        }

        pthread_create(&thread, NULL, user_thread, (void*)&sock2);
        pthread_mutex_lock(&mutex);
        cnt_of_threads++;
        pthread_mutex_unlock(&mutex);
    }

    close(sock1);

    return 0;
} 

void* user_thread(void* param) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = f_obr_user_thread;
    sigaction(SIGINT, &sa, 0);

    int* sock2_addr = (int*) param;
    int sock2 = *sock2_addr;

    // auth
    message auth_message;
    auth_message.msg_type = MSG_AUTH;
    send(sock2, &auth_message, sizeof(auth_message), 0);

    // getting message
    message message;
    recv(sock2, &message, sizeof(message), 0);

    printf("Username: %s", message.username);
    printf("Password: %s\n", message.text);
    printf("User socket fd: %d\n", sock2);

    struct sockaddr_in client_socket;

    if (is_user_exists(message.username)) {
        if (check_password(message.text)) {
            strcpy(message.text, "Successfully authorization!\n");
            printf("Successfully authorization of %s", message.username);
            message.msg_type = MSG_TRANSFER_MSG;
            send(sock2, &message, sizeof(message), 0);

            recv(sock2, &message, sizeof(message), 0); // waiting for client socket for direct messaging
            printf("Client addr: %s\n", inet_ntoa(message.client_socket.sin_addr));
            printf("Client addr with func: %u\n", ntohs(message.client_socket.sin_port));
            client_socket = message.client_socket;

        } else {
            strcpy(message.text, "Wrong password entered!\n");
            message.username[strlen(message.username) - 1] = '\0';
            printf("User %s entered a wrong password!\n", message.username);
            message.msg_type = FORBIDDEN;
            send(sock2, &message, sizeof(message), 0);
            pthread_mutex_lock(&mutex);
            cnt_of_threads--;
            pthread_mutex_unlock(&mutex);
            shutdown(sock2, SHUT_RDWR);
            close(sock2);
            pthread_exit(NULL);
        }
    } else {
        char login_from_user[32];
        strcpy(login_from_user, message.username);
        char password_from_user[256];
        strcpy(password_from_user, message.text);

        strcpy(message.text, "You're not registered! Do you want to register?(Y/N)\n");
        message.msg_type = REGISTRATION;
        send(sock2, &message, sizeof(message), 0); 

        // wating asnwer from client
        recv(sock2, &message, sizeof(message), 0);
        if (strcmp(message.text, "y") == 0) {
            write_to_file(login_from_user, password_from_user);
            printf("Success registration!\n");
            strcpy(message.text, "Success registration!\n");
            send(sock2, &message, sizeof(message), 0);

            recv(sock2, &message, sizeof(message), 0); // waiting for client socket for direct messaging

            printf("Client direct addr: %s\n", inet_ntoa(message.client_socket.sin_addr));
            printf("Client direct port: %u\n", ntohs(message.client_socket.sin_port));
            client_socket = message.client_socket;
        } else {
            printf("User doesn't want register on server!\n");
            pthread_mutex_lock(&mutex);
            cnt_of_threads--;
            pthread_mutex_unlock(&mutex);
            shutdown(sock2, SHUT_RDWR);
            close(sock2);
            pthread_exit(NULL);
        }
    }

    pthread_mutex_lock(&mutex);
    int index_of_empty_element = find_index_of_empty_element(user_sockets_discriptors);
    strcpy(user_names[index_of_empty_element], message.username); // filling array of user names 
    user_sockets_discriptors[index_of_empty_element] = sock2; // filling array of user socket descriptors
    addresses_of_users[index_of_empty_element] = client_socket; // filling array of user direct sockets
    current_number_of_users++;
    pthread_mutex_unlock(&mutex);

    while (1) {
        recv(sock2, &message, sizeof(message), 0);
        if (message.msg_type == MSG_TRANSFER_MSG) { // sending messages to all clients in chat
            pthread_mutex_lock(&mutex);
            for (int i = 0; i < USER_NUMBER; ++i) {
                if (sock2 != user_sockets_discriptors[i] && (user_sockets_discriptors[i] != 0)) { 
                    send(user_sockets_discriptors[i], &message, sizeof(message), 0);
                }
            }
            pthread_mutex_unlock(&mutex);

            message.username[strlen(message.username) - 1] = '\0';
            printf("%s: %s", message.username, message.text);

        } else if(message.msg_type == MSG_USERS) { // user want to get list of users
            int code = send_list_of_users(sock2);
            printf("Succes sending of list users code: %d\n", code);

        } else if(message.msg_type == EXITING) {
            shutdown(sock2, SHUT_RDWR);
            close(sock2);
            pthread_mutex_lock(&mutex);
            int i = 0;
            for (; i < USER_NUMBER; ++i) {
                if (user_sockets_discriptors[i] == sock2) {
                    user_sockets_discriptors[i] = 0;
                    memset((char *)&addresses_of_users[i], '\0', sizeof(addresses_of_users[i]));
                    memset((char *)& user_names[i], '\0', sizeof user_names[i]);
                    printf("User was exit and his data was deleted!\n");
                    break;
                }
            }
            current_number_of_users--;
            cnt_of_threads--;
            pthread_mutex_unlock(&mutex);
            pthread_exit(NULL);
        }
    }
}

int find_index_of_empty_element(int* arr) {
    for (int i = 0; i < USER_NUMBER; ++i) {
        if (arr[i] == 0) return i;
    }
    return -1;
}

int send_list_of_users(int user_socket_fd) {    
    message number_of_users_message;
    number_of_users_message.msg_type = MSG_USERS;
    char c = (current_number_of_users - 1) + '0';
    number_of_users_message.text[0] = c;
    number_of_users_message.text[1] = '\0';
    send(user_socket_fd, &number_of_users_message, sizeof(message), 0);

    message sockets_message;
    sockets_message.msg_type = MSG_USERS;
    // memcpy(&number_of_users_message.text, &addresses_of_users, sizeof(addresses_of_users));
    for (int i = 0; i < USER_NUMBER; ++i) {
        if ((user_sockets_discriptors[i] == 0) || (user_sockets_discriptors[i] == user_socket_fd)) continue;
        sockets_message.client_socket = addresses_of_users[i];
        strcpy(sockets_message.username, user_names[i]);
        printf("Username: %s", sockets_message.username);
        printf("Address of user %d: %s\n", i + 1, inet_ntoa(sockets_message.client_socket.sin_addr));
        printf("Port of user %d: %u\n",  i+ 1, ntohs(sockets_message.client_socket.sin_port));
        printf("User socket fd: %d\n", user_socket_fd);
        send(user_socket_fd, &sockets_message, sizeof(message), 0);
    }
    return 1;
}

int is_user_exists(char* login) {
    FILE* file;
    file = fopen("db.txt", "r");
    char buf[MSG_BUFF_SIZE];
    int i = 0;
    while ((fgets(buf, MSG_BUFF_SIZE, file)) != NULL) {
        ++i;
        if (i % 2 != 0) {
            if (strcmp(buf, login) == 0) {
                fclose(file);
                return 1;
            }
        } else continue;
    }
    fclose(file);
    return 0;
}

int check_password(char* password) {
    FILE* file;
    file = fopen("db.txt", "r");
    char buf[MSG_BUFF_SIZE];
    int i = 0;
    while ((fgets(buf, MSG_BUFF_SIZE, file)) != NULL) {
        ++i;
        if (i % 2 == 0) {
            if (strcmp(buf, password) == 0) {
                fclose(file);
                return 1;
            }
        } else continue;
    }
    fclose(file);
    return 0;
}

int write_to_file(char* login, char* password) {
    FILE* file;
    file = fopen("db.txt", "a");
    fprintf(file, "%s%s", login, password);
    fclose(file);
    return 1;
}

void f_obr_user_thread(int sig) {
    printf("SIGINT in user thread!\n");
    pthread_mutex_destroy(&mutex);
    close(sock1);
    for (int i = 0; i < USER_NUMBER; ++i) {
        if (user_sockets_discriptors[i] != 0) {
            close(user_sockets_discriptors[i]);
        }
    }
    exit(EXIT_SUCCESS); 
}

void f_obr_listen_thread(int sig) {
    printf("SIGINT in listen thread!\n");
    pthread_mutex_destroy(&mutex);
    close(sock1);
    for (int i = 0; i < USER_NUMBER; ++i) {
        if (user_sockets_discriptors[i] != 0) {
            close(user_sockets_discriptors[i]);
        }
    }
    exit(EXIT_SUCCESS); 
}

void f_obr_sig_pipe(int sig) {
    printf("SIGPIPE in user thread!\n");
    // exit(EXIT_SUCCESS); 
}