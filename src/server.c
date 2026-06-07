/*
 * ChatHub - serveur de chat multi-clients (TCP/IPv4)
 * --------------------------------------------------
 * Modele : 1 thread detache par client (client_routine). La liste chainee
 * des clients (client_list) et toute diffusion sont protegees par un unique
 * mutex (client_list_lock).
 *
 * Commandes reconnues (texte brut, cote serveur) :
 *   <texte>              -> message public "pseudo: texte" diffuse a tous
 *   /pseudo <nom>        -> change le pseudo (unique, sans espaces)
 *   /msg <pseudo> <txt>  -> message prive
 *   /exit                -> deconnexion
 *
 * Compilation : gcc -Wall -Wextra -pthread src/server.c -o bin/server
 * Lancement   : bin/server <port> [adresse-ip]   (defaut 0.0.0.0)
 */
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define RECV_BUFF_SZ 1024
#define INITIAL_CLIENT_ALLOC 10
#define MAX_SEND_ATTEMPTS 100
#define DEFAULT_PSEUDO_LEN 100

int pseudo_counter = 0;

struct Client {
    char pseudo_name[DEFAULT_PSEUDO_LEN];
    struct sockaddr_in c_addr;
    socklen_t sk_addr_len;
    int sd;
    struct Client *next;
};

struct Client_head {
    struct Client* head;
    int size;
};
//global
pthread_mutex_t client_list_lock = PTHREAD_MUTEX_INITIALIZER;//single lock guards ALL list traversal + mutation

struct Client_head client_list;

void init_client_array(struct Client_head *list);
struct Client *add_client(struct Client_head *head);
int remove_client(struct Client_head *phead, struct Client *c);
void display_clients(struct Client_head* list);

int get_port(const char* str);
void service(struct Client *c);
void global_msg(const char* msg, struct Client *sender);
void* client_routine(void* args);

int send_all(struct Client *c, const char* msg, int n);
char *extract_pseudo(const char* buffer);

// all of these assume the caller already holds client_list_lock
void broadcast(const char* full_msg, struct Client *exclude);
void send_line(struct Client *c, const char* text);
struct Client *find_by_pseudo(const char* name);
int valid_pseudo(const char* name);
void private_msg(const char* buffer, struct Client *sender);
void announce(const char* text, struct Client *exclude);

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

    init_client_array(&client_list);

    printf("ChatHub Server listening on port %d...\n[Ctrl + C] to exit\n", port);

    while (1) {
        struct sockaddr_in conn_addr;
        socklen_t conn_len = sizeof(conn_addr);
        memset(&conn_addr, 0, sizeof(conn_addr));

        //accept BEFORE linking the node, so no thread ever sees a client with an uninitialized sd
        int conn_sd = accept(listening_socket, (struct sockaddr*)&conn_addr, &conn_len);
        if (conn_sd == -1) {
            perror("accept");
            continue;//one bad accept shouldn't kill the whole server
        }

        pthread_mutex_lock(&client_list_lock);
        struct Client *new_client = add_client(&client_list);
        new_client->c_addr = conn_addr;
        new_client->sk_addr_len = conn_len;
        new_client->sd = conn_sd;
        pthread_mutex_unlock(&client_list_lock);

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


// void init_client_array(struct Client *head);
// void add_client(struct Client *head, struct new_client) {
//     struct Client *temp = NULL;
    
// }

void init_client_array(struct Client_head *list) {
    if (list == NULL) return;
    list->head = NULL;
    list->size = 0;
}

struct Client *add_client(struct Client_head *head) {
    if (head == NULL) {
        printf("Cannot add to NULL Client_head!\n");
        return NULL;
    }

    char *default_pseudo = (char*)malloc(sizeof(char)*DEFAULT_PSEUDO_LEN);// temp buffer for pseudo 
    struct Client *curr = NULL, *new_node = (struct Client*)malloc(sizeof(struct Client));
    if (new_node == NULL) {
        perror("new_client_node: malloc");
        exit(1);
    }

    if (default_pseudo == NULL) {
        perror("add_client:malloc");
        exit(1);
    }
    
    new_node->next = NULL;


    if (head->head == NULL) {
        head->head = new_node;
    } else {
        curr = head->head;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = new_node;
    }
    head->size++;

    snprintf(default_pseudo, DEFAULT_PSEUDO_LEN,"default-pseudo[%d]", pseudo_counter);
    pseudo_counter++;
    strcpy(new_node->pseudo_name, default_pseudo);
    free(default_pseudo);
    return new_node;
}

void display_clients(struct Client_head *list) {
    struct Client *curr;
    if (list == NULL || list->head == NULL) {
        printf("Empty client list!\n");
        return;
    } 
    curr = list->head;
    while (curr != NULL) {
        printf("pseudo name: %s\n", curr->pseudo_name);
        printf("sd: %d\n", curr->sd);
        printf("socket len: %d\n", curr->sk_addr_len);
        printf("port: %d\n", ntohs(curr->c_addr.sin_port));
        printf("ip: %s\n", inet_ntoa(curr->c_addr.sin_addr));
        printf("-----------------------------------------------------------------------\n");
        curr = curr->next;
    }

    return;
}

int remove_client(struct Client_head *phead, struct Client *c) {
    if (phead == NULL || phead->head == NULL) return -1;

    struct Client *prev = NULL, *curr = phead->head;
    while (curr != NULL && curr != c) {
        prev = curr;//store prev
        curr = curr->next;
    }
    if (curr == NULL) return -1;//no client found
    if (prev == NULL) {//we are still at phead
        phead->head = phead->head->next;
    } else {
        prev->next = curr->next;
    }
    free(curr);
    return 1;
}

void service(struct Client *c) {
    pthread_t client_thread;
    pthread_create(&client_thread, NULL, &client_routine, (void*)c);
    pthread_detach(client_thread);//fire-and-forget: reclaim thread resources on exit, no leak
}

// Announce a client leaving, remove it from the list, and close its socket.
// Captures the pseudo BEFORE freeing the node so we never touch freed memory.
void disconnect_client(struct Client *c) {
    int sd = c->sd;
    char who[DEFAULT_PSEUDO_LEN];
    char leave_msg[DEFAULT_PSEUDO_LEN + 48];

    pthread_mutex_lock(&client_list_lock);
    strncpy(who, c->pseudo_name, sizeof(who) - 1);
    who[sizeof(who) - 1] = '\0';
    snprintf(leave_msg, sizeof(leave_msg), "*** %s has left the chat ***", who);
    announce(leave_msg, c);            // tell everyone else first...
    remove_client(&client_list, c);    // ...then free the node
    pthread_mutex_unlock(&client_list_lock);

    printf("%s disconnected!\n", who);
    close(sd);
}

void* client_routine(void* args) {
    char buffer[RECV_BUFF_SZ];
    int bytes_read = 0;
    struct Client *c = (struct Client*)args;

    // announce the new arrival to everyone already connected
    pthread_mutex_lock(&client_list_lock);
    char join_msg[DEFAULT_PSEUDO_LEN + 48];
    snprintf(join_msg, sizeof(join_msg), "*** %s has joined the chat ***", c->pseudo_name);
    announce(join_msg, c);
    pthread_mutex_unlock(&client_list_lock);

    while (1) {

        bytes_read = recv(c->sd, buffer, RECV_BUFF_SZ-1, 0);
        if (bytes_read <= 0) {//0 = clean close, -1 = error/reset: treat both as a disconnect (don't kill the server)
            disconnect_client(c);
            return NULL;
        }
        buffer[bytes_read] = '\0';// i assume data sent isn't null terminated

        if (!strncmp(buffer, "/exit", strlen("/exit"))) {
            disconnect_client(c);
            return NULL;//just close thread, no need to return a value
        }

        if (!strncmp(buffer, "/pseudo ", strlen("/pseudo "))) {
            char* new_pseudo = extract_pseudo(buffer);
            if (new_pseudo == NULL) continue;//too long: extract_pseudo already reported it

            pthread_mutex_lock(&client_list_lock);
            struct Client *clash = find_by_pseudo(new_pseudo);
            if (!valid_pseudo(new_pseudo)) {
                send_line(c, "*** Pseudo can't be empty or contain spaces. ***");
            } else if (clash != NULL && clash != c) {
                send_line(c, "*** That pseudo is already taken. Pick another. ***");
            } else if (clash == c) {
                send_line(c, "*** That's already your pseudo. ***");
            } else {
                char notice[DEFAULT_PSEUDO_LEN * 2 + 48];
                snprintf(notice, sizeof(notice), "*** %s is now known as %s ***", c->pseudo_name, new_pseudo);
                strcpy(c->pseudo_name, new_pseudo);
                announce(notice, c);   // tell the others about the rename
                send_line(c, notice);  // and confirm to the renamer
            }
            pthread_mutex_unlock(&client_list_lock);
            free(new_pseudo);
            continue;
        }

        if (!strncmp(buffer, "/msg ", strlen("/msg "))) {
            pthread_mutex_lock(&client_list_lock);
            private_msg(buffer, c);
            pthread_mutex_unlock(&client_list_lock);
            continue;
        }

        pthread_mutex_lock(&client_list_lock);
        global_msg(buffer, c);
        pthread_mutex_unlock(&client_list_lock);
    }

    return NULL;
}

void broadcast(const char* full_msg, struct Client *exclude) {
    int len = strlen(full_msg);
    struct Client *curr = client_list.head;
    while (curr != NULL) {
        struct Client *next = curr->next;//save next first: curr may be removed below
        if (curr != exclude) {
            if (send_all(curr, full_msg, len) == -1) {//dead client: clean up under the lock we hold
                printf("%s dropped (send failed). Cleaning up...\n", curr->pseudo_name);
                close(curr->sd);
                remove_client(&client_list, curr);
            }
        }
        curr = next;
    }
}

void send_line(struct Client *c, const char* text) {
    send_all(c, text, strlen(text));
}


void announce(const char* text, struct Client *exclude) {
    printf("[announce] %s\n", text);
    broadcast(text, exclude);
}


struct Client *find_by_pseudo(const char* name) {
    struct Client *curr = client_list.head;
    while (curr != NULL) {
        if (strcmp(curr->pseudo_name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}


int valid_pseudo(const char* name) {
    if (name == NULL || name[0] == '\0') return 0;
    for (const char* p = name; *p != '\0'; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') return 0;
    }
    return 1;
}


void global_msg(const char* msg, struct Client *sender) {
    int msg_len = strlen(msg) + strlen(sender->pseudo_name) + 2;
    char* final_msg = (char*)malloc(sizeof(char) * (msg_len + 1));
    if (final_msg == NULL) {
        perror("malloc");
        exit(1);
    }
    snprintf(final_msg, msg_len + 1, "%s: %s", sender->pseudo_name, msg);
    broadcast(final_msg, sender);
    free(final_msg);
}


void private_msg(const char* buffer, struct Client *sender) {
    const char *p = buffer + strlen("/msg ");
    while (*p == ' ') p++;                       

    const char *pseudo_start = p;
    while (*p != '\0' && *p != ' ') p++;         
    int pseudo_len = (int)(p - pseudo_start);

    if (pseudo_len <= 0 || pseudo_len >= DEFAULT_PSEUDO_LEN) {
        send_line(sender, "*** Usage: /msg <pseudo> <message> ***");
        return;
    }
    char target[DEFAULT_PSEUDO_LEN];
    memcpy(target, pseudo_start, pseudo_len);
    target[pseudo_len] = '\0';

    while (*p == ' ') p++;                        
    if (*p == '\0') {                             
        send_line(sender, "*** Usage: /msg <pseudo> <message> ***");
        return;
    }
    const char *message = p;

    struct Client *recipient = find_by_pseudo(target);
    if (recipient == NULL) {
        char notice[DEFAULT_PSEUDO_LEN + 64];
        snprintf(notice, sizeof(notice), "*** No user named '%s' ***", target);
        send_line(sender, notice);
        return;
    }

    char line[RECV_BUFF_SZ + DEFAULT_PSEUDO_LEN + 32];
    snprintf(line, sizeof(line), "[private] %s: %s", sender->pseudo_name, message);
    send_line(recipient, line);

    snprintf(line, sizeof(line), "[private -> %s] %s", target, message);
    send_line(sender, line);
}


int send_all(struct Client *c, const char* msg, int n) {
    int bytes_sent = -1;
    int nbr_attempts = 0;

    while ((bytes_sent = send(c->sd, msg, n, MSG_NOSIGNAL)) < n) {
        nbr_attempts++;
        if (bytes_sent == -1 || nbr_attempts > MAX_SEND_ATTEMPTS) {
            return -1;
        }
        msg += bytes_sent;
        n -= bytes_sent;
    }
    return 0;
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
        perror("extract_pseudo: malloc");
        exit(1);
    }

    strncpy(pseudo, buffer+strlen("/pseudo "), copy_len);
    pseudo[copy_len] = '\0';
    return pseudo; 
    
}