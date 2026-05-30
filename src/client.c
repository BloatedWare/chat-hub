#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

#define RECV_BUFF_SZ 1024
#define SEND_BUFF_SZ 1024
#define MAX_SEND_ATTEMPTS 10
#define DEFAULT_PSEUDO_LEN 100

bool recv_thread_alive = false;
bool send_thread_alive = false;

int get_port(const char* str);
void send_all(int sd, const char* msg, int n);
char *get_string(const char* prompt);
void* recv_thread_entry(void* args);
void* send_thread_entry(void* arg);
void chat_client(int sd);


//MARK: MAIN
int main(int argc, char **argv) {
    

    int port, sd;
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    if (argc != 3) {
        printf("Usage %s <port> <ip-address>\n", argv[0]);
        exit(1);
    }

    port = get_port(argv[1]);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, argv[2], (void*)&serv_addr.sin_addr) != 1) {
        printf("Bad ip address\n");
        exit(1);
    }

    if ( (sd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ) {
        perror("socket");
        exit(1);
    }

    
    if (connect(sd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect");
        exit(1);
    }
    
    printf("Successfully connected!\nWelcome to ChatHub Client!\n");
    chat_client(sd);

    return 0;
}

int get_port(const char* str) {
    const char* head = str;
    if(str == NULL) {
        printf("Cannot accept null values.\n");
        exit(1);
    }

    while (*str == ' ') str++;//skip white spaces

    do {
        if (*str > '9' || *str < '0') {
            printf("Not port a number.\n");
            exit(1);
        }
        str++;
    }  while (*str != '\0');
    return atoi(head);
}



void* recv_thread_entry(void* args) {
    int sd = *(int*)args, bytes_read;
    char buffer[RECV_BUFF_SZ];

    recv_thread_alive = true;

    while (1) {
        bytes_read = recv(sd, buffer, RECV_BUFF_SZ-1, 0);
        if (bytes_read == -1) {
            perror("recv");
            exit(1);
        } 
    
        buffer[bytes_read] = '\0';// i assume data sent isn't null terminated
        if (bytes_read == 0 || !strncmp(buffer, "/exit", 5)) {
            printf("Server disconnected!\n");
            recv_thread_alive = false;
            return NULL;//just close thread no nead to return value
        }
    
        printf("%s\n", buffer);
        fflush(stdout);
    }
}


void* send_thread_entry(void* arg) {
    int sd = *(int*)arg;
    char* client_msg;//renamed it to client msg, cuz i keep accidently free msg_length when i press TAB
    int msg_len;
    int bytes_sent = 0;
    
    send_thread_alive = true;

    while (1) {
        
        client_msg = get_string(">>> ");
        if(!recv_thread_alive) {
            free(client_msg);
            send_thread_alive = false;
            
            return NULL;
        }
        msg_len = strlen(client_msg);
        if (msg_len > 0 && msg_len < SEND_BUFF_SZ) {
            send_all(sd, client_msg, msg_len);

            if (!strncmp(client_msg, "/exit", 5)) {
                free(client_msg);
                printf("You have ended the session!\n");
                send_thread_alive = false;
                return NULL;
            }
            
        } else {
            printf("BAD MESSAGE! ( 1 <= msg_length <= %d)\n", SEND_BUFF_SZ-1);
        }
        free(client_msg);

    }
}

void send_all(int sd, const char* msg, int n) {
    int bytes_sent = -1;
    int nbr_attempts = 0;

    while ((bytes_sent = send(sd, msg, n, 0)) < n) {
        nbr_attempts++;
        if (bytes_sent == -1 || nbr_attempts > MAX_SEND_ATTEMPTS) {
            printf("Send failed!\n");
            exit(1);
        }
        msg += bytes_sent;
        n -= bytes_sent;
    }
}



char *get_string(const char* prompt) {
    char *str = (char*)malloc(sizeof(char));//for '\0'
    char *temp = NULL;
    int c;
    int size = 1;
    printf("%s", prompt);

    while ((c = getchar()) != '\n') {
        str[size-1] = c;
        temp = (char*)realloc(str ,sizeof(char)*(size+1));
        if (temp == NULL) {
            free(str);
            perror("malloc");
            exit(1);
        }
        str = temp;
        size++;
    }
    str[size-1] = '\0';
    return str;
}

void chat_client(int sd) {
    pthread_t send_thread, recv_thread;
    pthread_create(&recv_thread, NULL, &recv_thread_entry, (void*)&sd);
    pthread_create(&send_thread, NULL, &send_thread_entry, (void*)&sd);
    pthread_join(recv_thread, NULL);
    close(sd);
    pthread_join(send_thread, NULL);
    printf("Goodbye!");
}

