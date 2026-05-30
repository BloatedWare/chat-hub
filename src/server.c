#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define RECV_BUFF_SZ 1024
#define INITIAL_CLIENT_ALLOC 10

struct Client {
    char pseudo_name[100];
    struct sockaddr_in c_addr;
    socklen_t sk_addr_len;
    int sd;
};

struct Client_array {
    struct Client *clients;
    int capacity, size;
};

//global
struct Client_array arr;

void init_client_array(struct Client_array* arr);
void add_client(struct Client_array *arr, struct Client new_client);
int remove_client(struct Client_array *arr, int index);
int get_port(const char* str);
void service(struct Client c);
void global_msg(const char* msg);
void* client_routine(void* args);


//MARK: MAIN
int main(int argc, char **argv) {
    int port, listening_socket, client_count = 0;
    int *client_sockets;
    
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

    if (bind(listening_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr) == -1)) {
        perror("bind");
        exit(1);
    }

    
    if (listen(listening_socket, 10) == -1) {
        perror("listen");
        exit(1);
    }

    init_client_array(&arr);

    printf("ChatHub Server listening on port %d...\n", port);

    while (1) {
        struct Client new_client;
        memset(&new_client.c_addr, 0, sizeof(new_client.c_addr));

        new_client.sk_addr_len = sizeof(new_client.c_addr);
        if ((new_client.sd = accept(listening_socket, &new_client.c_addr, &new_client.sk_addr_len))== -1) {
            perror("accept");
            exit(1);
        }
        add_client(&arr, new_client);
        service(new_client);
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
}

void add_client(struct Client_array *arr, struct Client new_client) {
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
}

int remove_client(struct Client_array *arr, int index) {
    if (index < 0 || index >= arr->size) {
        return -1;
    } 

    for (int i = index; i < arr->size - 1; i++) {
        arr->clients[i] = arr->clients[i+1]; 
    }
    
    arr->size--;
    return 1;
}

void service(struct Client c) {
    pthread_t client_thread;
    pthread_create(&client_thread, NULL, &client_routine, (void*)&c);
}

void* client_routine(void* args) {
    char buffer[RECV_BUFF_SZ];
    int bytes_read = 0;
    struct Client *c = (struct Client*)args;
    bytes_read = recv(c->sd, buffer, RECV_BUFF_SZ-1, 0);
    if (bytes_read == -1) {
        perror("recv");
        //do magic
    } else if (bytes_read == 0 || !strncmp(buffer, "/exit", 5)) {
        printf("%s disconnected!\n", c->pseudo_name);
    }

    //actualy data arrived here
    global_msg(buffer);

    return NULL;
}

void global_msg(const char* msg) {

}