#include "rpc.h"


#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>


#define MAX_REQ_SIZE 10000
#define NOT_FOUND_HANDLE 128
#define FIND_REQUEST 100
#define MAX_HANDLER_NAME_LENGTH 1000  // name is lest than 1000 byte

#define NONBLOCKING

// rpc_serve_all helper
void handle_find_request(rpc_server *srv, rpc_data *rpc_call_params, rpc_data *rpc_response);
void send_rpc_response(int client_sockfd, rpc_data *rpc_response);
void handle_invalid_handle(rpc_data *rpc_response);
void handle_valid_handle(rpc_server *srv, int function_handle, rpc_data *rpc_call_params, rpc_data *rpc_response);
void process_rpc_request(rpc_server *srv, int client_sockfd);

// edian portability
int compress_rpc_data(rpc_data* data, char** code);
void decompress_rpc_data(char *buffer, rpc_data *data);



typedef struct rpc_handle {
    int index;
}rpc_handle;

// instance of registered function/handler
typedef struct {
    char name[MAX_HANDLER_NAME_LENGTH + 1];
    rpc_data *(*handler)(rpc_data *);
} handler_entry;

// wrap the registered func to a node
typedef struct handler_node {
    handler_entry function;
    struct handler_node *next;
} handler_node;

// head of the link list
typedef struct {
    handler_node *head;
    int size;
} handler_linklist;


struct rpc_server {
    int port;
    int sockfd;
    struct sockaddr_in6 socket;

    // all of registered handlers store in a linklist
    handler_linklist handlers;

    pthread_mutex_t mutex;  
};


struct rpc_client {
    char *address;
    int port;
    int sockfd;
    struct sockaddr_in6 serv_addr;

    // if the client finished
    int closed;

};



/* ---------------- */
/* SHARED FUNCTION  */
/* ---------------- */

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}


// This function takes an rpc_data structure data and compresses its contents into a binary buffer code.
int compress_rpc_data(rpc_data* data, char** code) {
    // Encode data1 and data2_len into buffer
    uint64_t encoded_data1 = htobe64(data->data1);
    uint64_t encoded_data2_len = htobe64(data->data2_len);

    // Calculate buffer size
    size_t buffer_size = sizeof(uint64_t) * 2 + data->data2_len;

    // Allocate memory for buffer
    char* buffer = malloc(buffer_size);
    if (buffer == NULL) {
        free(buffer);
        return -1;  // Memory allocation failed
    }

    // Copy encoded data1 and data2_len into buffer
    memcpy(buffer, &encoded_data1, sizeof(uint64_t));
    memcpy(buffer + sizeof(uint64_t), &encoded_data2_len, sizeof(uint64_t));

    // Copy data2 into buffer if data2_len > 0
    if (data->data2_len > 0) {
        memcpy(buffer + sizeof(uint64_t) * 2, data->data2, data->data2_len);
    }

    *code = buffer;  // Assign the buffer to the code pointer

    if (*code == NULL) {
        free(buffer);
        return -1;
    }
    return buffer_size;
}



// This function takes a compressed binary buffer buffer and decompresses its contents 
// into an rpc_data structure data.
void decompress_rpc_data(char *buffer, rpc_data *data) {
    // Extract and convert data1 from the buffer
    uint64_t *data1_ptr = (uint64_t *)buffer;
    data->data1 = be64toh(*data1_ptr);

    // Extract and convert data2_len from the buffer
    uint64_t *data2_len_ptr = (uint64_t *)(buffer + sizeof(uint64_t));
    data->data2_len = be64toh(*data2_len_ptr);

    // Extract and allocate memory for data2 if data2_len > 0
    if (data->data2_len > 0) {
        data->data2 = (char *)malloc(data->data2_len);
        char *data2_buffer = buffer + 2 * sizeof(uint64_t);
        memcpy(data->data2, data2_buffer, data->data2_len);
    } else {
        data->data2 = NULL;
    }
    // Free memory
    if (data->data2_len <= 0) {
        free(data->data2);
    }
}



/* ---------------- */
/*   SERVER SIDE    */
/* ---------------- */

// opens listening port
rpc_server *rpc_init_server(int port) {
    // Allocate memory for rpc_server struct
    rpc_server *server = malloc(sizeof(rpc_server));
    int sockfd;

    if (server == NULL) {
        perror("Error allocating memory for rpc_server\n");
        free(server);
        return NULL;
    }

    // Create socket
    sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating server socket\n");
        return NULL;
    }
    // Set socket options to allow reuse of address
    int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Error setting socket options\n");
        return NULL;
    }    
    // Bind socket to specified port
    memset(&server->socket, 0, sizeof(server->socket));
    server->socket.sin6_family = AF_INET6;
    server->socket.sin6_addr = in6addr_any;
    server->socket.sin6_port = htons(port);
    if (bind(sockfd, (struct sockaddr *)&server->socket, sizeof(server->socket)) < 0) {
        perror("Error binding server socket\n");
        return NULL;
    }
    // Listen for incoming connections
    if (listen(sockfd, SOMAXCONN) < 0) {
        perror("Error listening on server socket\n");
        // close(server_socket);
        return NULL;
    }

    // Initialize rpc_server struct
    server->sockfd = sockfd;
    server->port = port;    
    server->handlers.head = NULL;
    server->handlers.size = 0;
    pthread_mutex_init(&server->mutex, NULL);
    return server;
}

// register a handler to name
int rpc_register(rpc_server *srv, char *name, rpc_handler handler){
    if (srv == NULL || name == NULL || handler == NULL) {
        return -1;
    }

    pthread_mutex_lock(&srv->mutex);  // Lock the mutex before accessing the linked list

    //iterate through all registered, if name is found -> update, if not add new node
    handler_node *current = srv->handlers.head;
    while (current != NULL){
        if (strcmp(current->function.name, name) == 0) {
            //overwrite
            current->function.handler = handler;
            return 0;
        }
        current = current->next;
    }

    handler_node *new_node = malloc(sizeof(handler_node));
    if (new_node == NULL) {
        free(new_node);
        return -1; 
    }
    strcpy(new_node->function.name, name);
    new_node->function.handler = handler;
    new_node->next = NULL;

    // Add the new node to the linked list
    if (srv->handlers.head == NULL) {
        // If the linked list is empty, set the new node as the head
        srv->handlers.head = new_node;
    } else {
        // Otherwise, append the new node at the end of the linked list
        handler_node *last = srv->handlers.head;
        while (last->next != NULL) {
            last = last->next;
        }
        last->next = new_node;
    }

    srv->handlers.size++; // Increase the size of the linked list
    pthread_mutex_unlock(&srv->mutex);  // Unlock the mutex after finishing the critical section

    return 0;
}


// searches for a function name in the server's handler list and sets the appropriate response data indicating 
// whether the function was found or not.
void handle_find_request(rpc_server *srv, rpc_data *rpc_call_params, rpc_data *rpc_response) {
    rpc_response->data1 = NOT_FOUND_HANDLE;

    handler_node *current = srv->handlers.head;
    int i = 0;
    while (current != NULL) {
        if (strcmp(current->function.name, rpc_call_params->data2) == 0) {
            rpc_response->data1 = i;
            // rpc_data_free(rpc_call_params)
            free(rpc_call_params->data2);
            break;
        }
        current = current->next;
        ++i;
    }

    // rpc_data_free(rpc_response);
    rpc_response->data2_len = 0;
    rpc_response->data2 = NULL;
    free(rpc_response->data2);
}


// encrypts the response data, sends it through the client socket, and checks for any send errors.
void send_rpc_response(int client_sockfd, rpc_data *rpc_response) {
    char *rpc_response_buffer;
    int rpc_response_buffer_len;

    rpc_response_buffer_len = compress_rpc_data(rpc_response, &rpc_response_buffer);
    int res = send(client_sockfd, rpc_response_buffer, rpc_response_buffer_len, 0);
    if (res < rpc_response_buffer_len) {
        perror("send error\n");
        free(rpc_response_buffer);  // Free the compressed response buffer

        return;
    }
    free(rpc_response_buffer);
}


// This function is called when the client provides an invalid function handle.
// It sets the appropriate response data to indicate that the handle was not found.
void handle_invalid_handle(rpc_data *rpc_response) {
    perror("bad handle\n");
    rpc_response->data1 = NOT_FOUND_HANDLE;
    rpc_response->data2_len = 0;
    rpc_response->data2 = NULL;
}


// This function handles a valid function handle provided by the client.
//  It retrieves the corresponding handler function from the server's handler list, invokes it with the RPC call parameters,
//  and sets the response data based on the returned value.
void handle_valid_handle(rpc_server *srv, int function_handle, rpc_data *rpc_call_params, rpc_data *rpc_response) {
    handler_node *current = srv->handlers.head;
    for (int i = 0; i < function_handle; i++) {
        current = current->next;
    }

    rpc_handler handler = current->function.handler;
    rpc_data *ret = (*handler)(rpc_call_params);
    assert(ret);
    rpc_response->data1 = ret->data1;
    rpc_response->data2_len = ret->data2_len;
    rpc_response->data2 = ret->data2;
    free(ret);
    free(rpc_call_params->data2);
}


// receives the request data, decompress_rpc_datas it, extracts the function handle and call parameters. 
// It then dispatches the request to the appropriate handler based on the function handle and prepares the response data. 
// Finally, it sends the response back to the client.
void process_rpc_request(rpc_server *srv, int client_sockfd) {
    char buffer[MAX_REQ_SIZE];
    int ret = recv(client_sockfd, buffer, sizeof(buffer), 0);
    if (ret <= 0) {
        close(client_sockfd);
        return;
    }

    rpc_data rpc_call_req;
    decompress_rpc_data(buffer, &rpc_call_req);
    int function_handle = rpc_call_req.data1;
    char *rpc_call_params_buffer = rpc_call_req.data2;

    rpc_data rpc_call_params;
    decompress_rpc_data(rpc_call_params_buffer, &rpc_call_params);
    free(rpc_call_params_buffer);
    rpc_data rpc_response;

    if (function_handle == FIND_REQUEST) {
        handle_find_request(srv, &rpc_call_params, &rpc_response);
    } else if (function_handle > srv->handlers.size) {
        handle_invalid_handle(&rpc_response);
    } else {
        handle_valid_handle(srv, function_handle, &rpc_call_params, &rpc_response);
    }

    send_rpc_response(client_sockfd, &rpc_response);
}


// It accepts client connections, calls process_rpc_request to handle the requests, 
// and continues listening for more requests until a stop signal is received.
void rpc_serve_all(rpc_server *srv) {
    int sockfd = srv->sockfd;
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int flag = 0;
    int client_sockfd;


    volatile sig_atomic_t stop_serving = 0;

    while (!stop_serving) {
        if (flag == 0) {
            client_sockfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len);
            flag = 1;
        }
        process_rpc_request(srv, client_sockfd);
    }
}



/* ---------------- */
/*   CLIENT SIDE    */
/* ---------------- */

// connect to server using IP address and port
rpc_client *rpc_init_client(char *addr, int port) {
    rpc_client *client = (rpc_client *)malloc(sizeof(rpc_client));
    if (client == NULL) {
        perror("Error allocating memory for client");
        free(client);
        return NULL;
    }

    client->address = strdup(addr);
    client->port = port;

    client->sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (client->sockfd < 0) {
        perror("Error creating socket");
        free(client->address);
        free(client);
        return NULL;
    }

    memset(&(client->serv_addr), 0, sizeof(client->serv_addr));
    client->serv_addr.sin6_family = AF_INET6;
    client->serv_addr.sin6_port = htons(port);
    
    if (inet_pton(AF_INET6, addr, &(client->serv_addr.sin6_addr)) <= 0) {
        perror("inet_pton");
        free(client->address);
        free(client);
        return NULL;
    }

    if (connect(client->sockfd, (struct sockaddr *)&(client->serv_addr),
                sizeof(client->serv_addr)) < 0) {
        perror("Error connecting to server");
        free(client->address);
        free(client);
        return NULL;
    }

    client->closed = 0;
    return client;
}

// find a module
rpc_handle *rpc_find(rpc_client *cl, char *name) {
    if (cl == NULL || name == NULL) {
        return NULL;
    }

    // Send request to server to find the function
    rpc_data request;
    request.data1 = FIND_REQUEST;
    request.data2_len = strlen(name) + 1; // include null terminator
    request.data2 = malloc(request.data2_len);
    memcpy(request.data2, name, request.data2_len);

    // Call the remote procedure call to send the find request and get the response
    rpc_handle find_request;
    find_request.index = FIND_REQUEST;
    rpc_data *response = rpc_call(cl, &find_request, &request);

    // rpc_data *response = rpc_call(cl, &(rpc_handle){FIND_REQUEST}, &request);
    free(request.data2);
    // Check if the response is NULL
    if (response == NULL) {
        return NULL;
    }
    
    // Check if the handle is not found
    if (response->data1 == NOT_FOUND_HANDLE) {
        free(response->data2);
        free(response);
        return NULL;
    }

    // Allocate memory for the RPC handle and assign the response data1 value
    rpc_handle *result = malloc(sizeof(rpc_handle));
    result->index = response->data1;

    // Free memories
    free(response->data2);
    free(response);

    return result;
}

// excute a module
rpc_data* rpc_call(rpc_client* cl, rpc_handle* h, rpc_data* params) {
    if (cl == NULL || h == NULL || params == NULL) {
        return NULL;
    }

    // Encode request parameters
    char* request_code;
    int request_code_len = compress_rpc_data(params, &request_code);
    
    // Create and populate the request data structure
    rpc_data* rpc_call_req = malloc(sizeof(rpc_data));
    rpc_call_req->data1 = h->index;
    rpc_call_req->data2_len = request_code_len;
    rpc_call_req->data2 = request_code;

    // Convert the request data structure to a byte buffer
    char* rpc_call_req_buffer;
    int rpc_call_req_buffer_len = compress_rpc_data(rpc_call_req, &rpc_call_req_buffer);

    // Send the request buffer
    int res = send(cl->sockfd, rpc_call_req_buffer, rpc_call_req_buffer_len, 0);
    if (res < 0) {
        perror("Error writing to socket\n");
        free(rpc_call_req_buffer);
        free(rpc_call_req);
        return NULL;
    }

    // Receive the response buffer
    char* response_buffer = malloc(MAX_REQ_SIZE);
    if (recv(cl->sockfd, response_buffer, MAX_REQ_SIZE, 0) == -1) {
        free(rpc_call_req_buffer);
        free(rpc_call_req);
        free(response_buffer);
        return NULL;
    }

    // Decode the response buffer
    rpc_data* rpc_response = malloc(sizeof(rpc_data));
    decompress_rpc_data(response_buffer, rpc_response);

    // Clean up and return the response data
    free(response_buffer);
    free(rpc_call_req_buffer);
    // free(rpc_call_req);
    rpc_data_free(rpc_call_req);
    return rpc_response;
}

// close connection from client side and free all memories
void rpc_close_client(rpc_client *cl) {
    if (cl == NULL || cl->closed) {
        return;  // Return if client is already closed or cl is NULL
    }
        
    close(cl->sockfd);
    free(cl->address);
    memset(&cl->serv_addr, 0, sizeof(struct sockaddr_in6));
    cl->closed = 1;

    free(cl);
}
