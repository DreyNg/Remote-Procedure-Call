#include "rpc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

rpc_data *add2_i8(rpc_data *);
rpc_data *increment_i8(rpc_data *in);


int main(int argc, char *argv[]) {
    rpc_server *state;
    int port;

    for (int i = 1; i < argc; i++){
        if (strcmp(argv[i], "-p") == 0){
            port = atoi(argv[i+1]);
        }
    }


    state = rpc_init_server(port);
    if (state == NULL) {
        fprintf(stderr, "Failed to init\n");
        exit(EXIT_FAILURE);
    }

    if (rpc_register(state, "add2", add2_i8) == -1) {
        fprintf(stderr, "Failed to register add2\n");
        exit(EXIT_FAILURE);
    }
    if (rpc_register(state, "increment_i8", increment_i8) == -1) {
        fprintf(stderr, "Failed to register increment_i8\n");
        exit(EXIT_FAILURE);
    }

    rpc_serve_all(state);

    return 0;
}

/* Adds 2 signed 8 bit numbers */
/* Uses data1 for left operand, data2 for right operand */
rpc_data *add2_i8(rpc_data *in) {
    /* Check data2 */
    if (in->data2 == NULL || in->data2_len == 0) {
        return NULL;
    }

    /* Parse request */
    int n1 = in->data1;
    int n2;
    sscanf(in->data2, "%d", &n2);

    /* Perform calculation */
    int res = n1 + n2;

    /* Prepare response */
    rpc_data *out = malloc(sizeof(rpc_data));
    assert(out != NULL);
    out->data1 = res;
    out->data2_len = 0;
    out->data2 = NULL;
    return out;
}

rpc_data *increment_i8(rpc_data *in) {

    /* Parse request */
    char n1 = in->data1;

    /* Perform calculation */
    int res = n1 + 1;

    /* Prepare response */
    rpc_data *out = malloc(sizeof(rpc_data));
    assert(out != NULL);
    out->data1 = res;
    out->data2_len = 0;
    out->data2 = NULL;
    return out;
}