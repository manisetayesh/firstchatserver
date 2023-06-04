#include "friends.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef PORT
  #define PORT 58367
#endif

#define MAX_BACKLOG 5
#define BUFFER_SIZE 256
#define INPUT_ARG_MAX_NUM 12
#define DELIM " \r\n"

//added sockname property: next, to make it suitable for a linked list
typedef struct sockname {
    int sock_fd;
    char *username;
    struct sockname *next;
} Sockname;

/* 
 * Find the network new line in the given character buffer (from lab 10)
 */
int find_network_newline(const char *buf, int n) {
    for(int i=0; i<n; i++){
        if(buf[i] == '\r' && buf[i+1] == '\n'){
            return 2+i;
        }
    }
    return -1;
}

/*
 * a function to handle partial reads - to return a full line.
 * It is extremely similar to lab 10, except it returns the first full line (first net.line)
 * For some reason, this won't work on my own device but it seems to work fine on teach.cs.
 */
char *reader(Sockname *client){
    int fd = client->sock_fd;

    // Receive messages
    char buf[BUFFER_SIZE] = {'\0'};
    char *rtrn;
    int inbuf = 0;           // How many bytes currently in buffer?
    int room = sizeof(buf);  // How many bytes remaining in buffer?
    char *after = buf;       // Pointer to position after the data in buf

    int nbytes;

    //reading loop
    while ((nbytes = read(fd, after, room)) > 0) {

        //put read material in buffer
        inbuf += nbytes;

        //find the first instance of \r\n, return the buffer (without the network newline)
        int where = find_network_newline(buf, inbuf);
        if (where > 0) {
            buf[where-2] = '\0';
            rtrn = malloc(sizeof(char) * (strlen(buf) + 1));
            if(rtrn == NULL){
                perror("malloc");
                exit(1);
            }
            strncpy(rtrn, buf, strlen(buf)+1);
            return rtrn;
        }

        //keep going.
        room = sizeof(buf) - inbuf;  
        after = buf+inbuf;
    }

    //read failure
    if(nbytes == -1){
        perror("read");
        exit(1);
    }

    //if no network newline, the client has disconnected.
    return NULL;
}

/* 
 * Write an error message to the specified fd
 */
void error(int fd, char *msg) {
    write(fd, msg, strlen(msg));
}


/* 
 * Read and process commands (from friendme.c)
 * Return:  -1 for quit command
 *          0 otherwise
 * Added Parameters: The client's socket, and the head pointers of sockname/user lists
 */
int process_args(Sockname *client, int cmd_argc, char **cmd_argv, User **user_list_ptr, Sockname *sock_head) {
    User *user_list = *user_list_ptr;
    int fd = client->sock_fd;

    //if statement for controlling the command
    if (cmd_argc <= 0) {
        return 0;
    } else if (strcmp(cmd_argv[0], "quit") == 0 && cmd_argc == 1) {
        return -1;
    } else if (strcmp(cmd_argv[0], "list_users") == 0 && cmd_argc == 1) {
        //get the user list, write to client's fd, free the malloc'd user list
        char *buf = list_users(user_list);
        write(fd, buf, strlen(buf));
        free(buf);
    } else if (strcmp(cmd_argv[0], "make_friends") == 0 && cmd_argc == 2) {
        
        // only one parameter - the second friend is assumed to be the client
        // error cases considered below
        switch (make_friends(cmd_argv[1], client->username, user_list)) {
            case 1:
                error(fd, "users are already friends\r\n");
                break;
            case 2:
                error(fd, "at least one user you entered has the max number of friends\r\n");
                break;
            case 3:
                error(fd, "you must enter two different users\r\n");
                break;
            case 4:
                error(fd, "at least one user you entered does not exist\r\n");
                break;
        }
    } else if (strcmp(cmd_argv[0], "post") == 0 && cmd_argc >= 3) {
        // first determine how long a string we need
        int space_needed = 0;
        for (int i = 2; i < cmd_argc; i++) {
            space_needed += strlen(cmd_argv[i]) + 1;
        }

        // allocate the space
        char *contents = malloc(space_needed);
        if (contents == NULL) {
            perror("malloc");
            exit(1);
        }

        // copy in the bits to make a single string
        strcpy(contents, cmd_argv[2]);
        for (int i = 3; i < cmd_argc; i++) {
            strcat(contents, " ");
            strcat(contents, cmd_argv[i]);
        }

        //find the author/target users, make the post.
        char message[2 * BUFFER_SIZE + 8];
        User *author = find_user(client->username, user_list);
        User *target = find_user(cmd_argv[1], user_list);
        switch (make_post(author, target, contents)) {
            case 0:
                
                //create a message - Format:  From author: contents
                strncpy(message, "From ", 6);
                strcat(message, author->name);
                strcat(message, ": ");
                strcat(message, contents);
                strcat(message, "\r\n");

                //find the target's socket fd, write message to the target
                Sockname *curr = sock_head;
                while(curr != NULL){
                    if(!strcmp(curr->username, target->name)){
                        write(curr->sock_fd, message, strlen(message));
                    }
                    curr = curr->next;
                }
                break;
            case 1:
                error(fd, "the users are not friends\r\n");
                break;
            case 2:
                error(fd, "at least one user you entered does not exist\r\n");
                break;
        }
        
    } else if (strcmp(cmd_argv[0], "profile") == 0 && cmd_argc == 2) {
        //get user's profile (string form), then write it to given fd
        User *user = find_user(cmd_argv[1], user_list);
        char *buf = print_user(user);
        if (buf == NULL) {
            error(fd, "user not found\r\n");
        } else {
            write(fd, buf, strlen(buf));
        }
        free(buf);
    } else {
        error(fd, "Incorrect syntax\r\n");
    }
    return 0;
}

/*
 * Tokenize the string stored in cmd.
 * Return the number of tokens, and store the tokens in cmd_argv.
 * Returns -1 if there are too many arguments.
 */
int tokenize(char *cmd, char **cmd_argv) {
    int cmd_argc = 0;
    char *next_token = strtok(cmd, DELIM);    
    while (next_token != NULL) {
        if (cmd_argc >= INPUT_ARG_MAX_NUM - 1) {
            cmd_argc = -1;
            break;
        }
        cmd_argv[cmd_argc] = next_token;
        cmd_argc++;
        next_token = strtok(NULL, DELIM);
    }

    return cmd_argc;
}

/*
 * Read a message from client_index and echo it back to them.
 * Return the fd if it has been closed or 0 otherwise.
 *
 * Task 1: There are now two different types of input that we could
 * receive: a username, or a message. If we have not yet received
 * the username, then we should copy buf to username.  Otherwise, the
 * input will be a message to write to the socket.
 *
 * Task 2: Change the code to broadcast to all connected clients
 */
int read_from(Sockname *client, User **user_head, Sockname *sock_head) {
    //initializing empty buffers (for reading arguments and writing messages)
    int fd = client->sock_fd;
    char buf[BUFFER_SIZE];
    char message[2 * BUFFER_SIZE + 3];

    // using the partial reader
    char *rtrn = reader(client);
    if(rtrn == NULL){
        client->sock_fd = -1;
        return fd;
    }
    strncpy(buf, rtrn, strlen(rtrn)+1);

    /* This code segment worked on my own device, however not on teach.cs. I commented it out.
    int num_read = read(fd, &buf, BUFFER_SIZE);
    if(num_read == -1){
        perror("read");
        exit(1);
    }
    buf[num_read-1] = '\0';
    */

    if(client->username == NULL){
        //Truncate the name if too long
        if(strlen(buf) >= MAX_NAME){
            buf[MAX_NAME] = '\0';
        }

        //create user, append at the end of the list
        switch(create_user((const char *) buf, user_head)){
            case 0: 
                strcpy(message, "Welcome.\r\nEnter Commands.\r\n\0");
                break;
            case 1:
                strcpy(message, "Welcome Back.\r\nEnter Commands.\r\n\0");   
                break;
        }

        //write message to the user's socket
        client->username = malloc(sizeof(char) * strlen(buf) + 1);
        if(client->username == NULL){
            perror("malloc");
            exit(1);
        }

        strncpy(client->username, buf, strlen(buf) + 1);
        write(fd, message , strlen(message));

    } else {

        char *cmd_argv[INPUT_ARG_MAX_NUM];
        int argc = tokenize(buf, cmd_argv);
        if(argc == -1){
            write(fd, "Too many arguments.\r\n", strlen("Too many arguments.\r\n"));
        }
        if (process_args(client, argc, cmd_argv, user_head, sock_head) < 0) {
            client->sock_fd = -1;
            return fd;
        }                
    }
    return 0;
}

/*
 * Accept a connection and create a new file desc. for client. (lab 11)
 * Return the new client's file descriptor or -1 on error.
 * Added parameter: Head of the sockname list
 */
int accept_connection(int fd, Sockname **head) {

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }

    //ask client for username
    write(client_fd, "Please enter your username:\r\n", strlen("Please enter your username:\r\n"));    
   
    //initialize new empty Sockname
    Sockname *new_sock = malloc(sizeof(Sockname));
    new_sock->sock_fd = client_fd;
    new_sock->username = NULL;
    new_sock->next = NULL;

    //if there are no Socknames, set as first sockname
    Sockname *curr = *head;
    if(*head == NULL){
        *head = new_sock;
        return client_fd;
    }

    //else append to the end of the Sockname list.
    while(curr->next != NULL){
        curr = curr->next;
    }
    curr->next = new_sock;
    return client_fd;
}

int main(){

    // (lab 11 helpers)
    // Create the socket FD.
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }

    // Set information about the port (and IP) we want to be connected to.
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    // Option to reuse ports right away
    int on = 1;
    int status = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on));
    if (status == -1) {
        perror("setsockopt -- REUSEADDR");
    }
    memset(&server.sin_zero, 0, 8);

    // Bind the selected port to the socket.
    if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("server: bind");
        close(sock_fd);
        exit(1);
    }

    // Announce willingness to accept connections on this socket.
    if (listen(sock_fd, MAX_BACKLOG) < 0) {
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }

    // Create the heads of the empty data structures (one for users, one for Socknames of users)
    Sockname *user_socks = NULL;
    User *user_list = NULL;

    // Initialize a set of file descriptors.
    int max_fd = sock_fd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);

    while (1) {

        // copy the fd set
        fd_set listen_fds = all_fds;
        if (select(max_fd + 1, &listen_fds, NULL, NULL, NULL) == -1) {
            perror("server: select");
            exit(1);
        }

        // Create a new connetion if it is the initial socket
        if (FD_ISSET(sock_fd, &listen_fds)) {
            int client_fd = accept_connection(sock_fd, &user_socks);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            printf("Accepted connection\n");
        }

        // Check existing clients. Loop through the Sockname list
        Sockname *curr = user_socks;
        while(curr != NULL){
            if((curr->sock_fd) > -1 && FD_ISSET(curr->sock_fd, &listen_fds)) {

                int client_closed = read_from(curr, &user_list, user_socks);
                if (client_closed > 0) {

                    //if client disconnected, shutdown their writing pipe and close the socket
                    FD_CLR(client_closed, &all_fds);
                    printf("Client %d disconnected\n", client_closed);
                    shutdown(client_closed, SHUT_WR);
                    close(client_closed);
                    close(curr->sock_fd);
                }
            }
            curr = curr->next;
        }
    }
    return 1;
    
}