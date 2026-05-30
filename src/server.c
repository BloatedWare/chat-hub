#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define RECV_BUFF_SZ 1024
#define INITIAL_CLIENT_ALLOC 10
#define MAX_SEND_ATTEMPTS 10
#define DEFAULT_PSEUDO_LEN 100

struct Client {
    char pseudo_name[DEFAULT_PSEUDO_LEN];
    struct sockaddr_in c_addr;
    socklen_t sk_addr_len;
    int sd;
};

struct Client_array {
    struct Client *clients;
    int capacity, size;
};

//global
pthread_mutex_t arr_lock = PTHREAD_MUTEX_INITIALIZER;
struct Client_array arr;

void init_client_array(struct Client_array* arr);
void add_client(struct Client_array *arr, struct Client new_client);
int remove_client(struct Client_array *arr, struct Client c);
int get_port(const char* str);
void service(struct Client *c);
void global_msg(const char* msg, struct Client *c);
void* client_routine(void* args);

void global_msg(const char* msg, struct Client *sender);
void send_all(int sd, const char* msg, int n);
char *extract_pseudo(const char* buffer);

// void private_msg(const char* msg, struct Client *sender, struct Client *recipient);

//MARK: MAIN
int main(int argc, char **argv) {
    

    int port, listening_socket;
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    if (argc < 2 || argc > 3) {
        printf("Usage %s <port> [ip-address]\n", argv[0]);
        exit(1);
    }

    port = get_port(argv[1]);


    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if(argc == 3) {
        if (inet_pton(AF_INET, argv[2], (void*)&serv_addr.sin_addr) != 1) {
            printf("Bad ip address\n");
            exit(1);
        }
    } else {
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if ((listening_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    if (bind(listening_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind");
        exit(1);
    }

    
    if (listen(listening_socket, 10) == -1) {
        perror("listen");
        exit(1);
    }

    init_client_array(&arr);

    printf("ChatHub Server listening on port %d...\n[Ctrl + C] to exit\n", port);

    while (1) {
        struct Client new_client;
        memset(&new_client.c_addr, 0, sizeof(new_client.c_addr));

        new_client.sk_addr_len = sizeof(new_client.c_addr);
        if ((new_client.sd = accept(listening_socket, (struct sockaddr*)&new_client.c_addr, &new_client.sk_addr_len))== -1) {
            perror("accept");
            exit(1);
        }

        pthread_mutex_lock(&arr_lock);
        add_client(&arr, new_client);
        pthread_mutex_unlock(&arr_lock);

        service(&arr.clients[arr.size-1]);
    }

   


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

void init_client_array(struct Client_array* arr) {
    arr->clients = (struct Client*)malloc(sizeof(struct Client)*INITIAL_CLIENT_ALLOC);
    if (arr->clients == NULL) {
        perror("malloc");
        exit(1);
    }
    arr->capacity = INITIAL_CLIENT_ALLOC;
    arr->size = 0;
}

void add_client(struct Client_array *arr, struct Client new_client) {
    char *default_pseudo = (char*)malloc(sizeof(char)*DEFAULT_PSEUDO_LEN);
    if (default_pseudo == NULL) {
        perror("add_client:malloc");
        exit(1);
    }

    if (arr->size >= arr->capacity) {
        arr->capacity *= 2;
        void* temp = realloc(arr->clients, sizeof(struct Client)*arr->capacity);
        if (temp == NULL) {
            perror("realloc");
            exit(1);
        }
        arr->clients = (struct Client*)temp;
    }

    arr->clients[arr->size++] =  new_client;
    snprintf(default_pseudo, DEFAULT_PSEUDO_LEN,"default-pseudo[%d]", arr->size);
    strcpy(arr->clients[arr->size-1].pseudo_name, default_pseudo);
    free(default_pseudo);

}

int remove_client(struct Client_array *arr, struct Client c) {

    int index;
    for (index = 0; index < arr->size; index++) {
        if (arr->clients[index].sd == c.sd) {
            break;
        }
    }

    if (index == arr->size) return -1;//failed to find

    for (int i = index; i < arr->size - 1; i++) {
        arr->clients[i] = arr->clients[i+1]; 
    }
    
    arr->size--;
    return 1;
}

void service(struct Client *c) {
    pthread_t client_thread;
    pthread_create(&client_thread, NULL, &client_routine, (void*)c);
}

void* client_routine(void* args) {
    char buffer[RECV_BUFF_SZ];
    int bytes_read = 0;
    struct Client *c = (struct Client*)args;
    
    while (1) {

        bytes_read = recv(c->sd, buffer, RECV_BUFF_SZ-1, 0);
        if (bytes_read == -1) {
            perror("client_routine:recv");
            exit(1);
        } 
        buffer[bytes_read] = '\0';// i assume data sent isn't null terminated
        if (bytes_read == 0 || !strncmp(buffer, "/exit", 5)) {
            printf("%s disconnected!\n", c->pseudo_name);
            pthread_mutex_lock(&arr_lock);
            if (remove_client(&arr, *c)  == -1) {
                printf("Client not found\n");
                exit(1);
            }
            pthread_mutex_unlock(&arr_lock);
            close(c->sd);//close his socket
            return NULL;//just close thread no nead to return value
        }
        
        if (!strncmp(buffer, "/pseudo ", strlen("/pseudo "))) {
            char* new_pseudo = extract_pseudo(buffer);
            if (new_pseudo != NULL) {
                strcpy(c->pseudo_name, new_pseudo);
                free(new_pseudo);
            }
            continue;
        }

        global_msg(buffer, c);
    }

    return NULL;
}

void global_msg(const char* msg, struct Client *sender) {
    int msg_len = strlen(msg)+strlen(sender->pseudo_name)+2;//pseudo_name: msg
    //surgery on msg
    char* final_msg = (char*)malloc(sizeof(char)*(msg_len+1));//+\0
    if (final_msg == NULL) {
        perror("malloc");
        exit(1);
    }
    
    snprintf(final_msg, msg_len+1, "%s: %s",sender->pseudo_name, msg); 

    printf("%s\n", final_msg);

    for (int i = 0; i < arr.size; i++) {
        if (arr.clients[i].sd != sender->sd) {
            send_all(arr.clients[i].sd, final_msg, msg_len);
        }
    }

    
    free(final_msg);
}

//TODO: to implement later
// void private_msg(const char* msg, struct Client *sender, struct Client *recipient) {
//     int msg_len = strlen(msg)+strlen(sender->pseudo_name)+2;//pseudo_name: msg
//     char *final_msg = (char*)malloc(sizeof(char)*(msg_len+1));

//     strcpy(final_msg, sender->pseudo_name);
//     strcpy(final_msg+strlen(sender->pseudo_name), ": ");
//     strcpy(final_msg+strlen(sender->pseudo_name+2), msg);

//     free(msg);
// }

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


char *extract_pseudo(const char* buffer) {
    int buffer_len, copy_len;
    char *pseudo = NULL;
    
    if (buffer == NULL) return NULL;

    buffer_len = strlen(buffer);
    copy_len = buffer_len - strlen("/pseudo ");
    if (copy_len >= 100) {
        printf("Pseudo must be between 1 and 99 characters long!\n");
        return NULL;
    }

    pseudo = (char*)malloc(sizeof(char)*100);
    if (pseudo == NULL) {
        perror("extract_pseudo:malloc");
        exit(1);
    }

    strncpy(pseudo, buffer+strlen("/pseudo "), copy_len);
    pseudo[copy_len] = '\0';
    return pseudo; 
    
}