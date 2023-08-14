#include "rpc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    int exit_code = 0;

    char* addr;
    int port;

    for (int i = 1; i < argc; i++){
        if (strcmp(argv[i], "-i") == 0){
            addr = argv[i+1];
        }
        if (strcmp(argv[i], "-p") == 0){
            port = atoi(argv[i+1]);
        }
    }
    rpc_client *state = rpc_init_client(addr, port);
    if (state == NULL) {
        exit(EXIT_FAILURE);
    }

    rpc_handle *handle_add2 = rpc_find(state, "add2");
    if (handle_add2 == NULL) {
        fprintf(stderr, "ERROR: Function add2 does not exist\n");
        exit_code = 1;
        goto cleanup;
    }

    for (int i = 0; i < 4; i++) {
        /* Prepare request */
        
        char left_operand = i;
        char right_operand = 100;
        char *right_op = malloc(20); // buffer to encode int as string
        sprintf(right_op, "%d", right_operand);
        rpc_data request_data = {
            .data1 = left_operand, .data2_len = strlen(right_op) + 1, .data2 = right_op};

        /* Call and receive response */
        rpc_data *response_data = rpc_call(state, handle_add2, &request_data);
        if (response_data == NULL) {
            fprintf(stderr, "Function call of add2 failed\n");
            exit_code = 1;
            goto cleanup;
        }

        /* Interpret response */
        assert(response_data->data2_len == 0);
        assert(response_data->data2 == NULL);
        rpc_data_free(response_data);
        free(right_op);


    }

cleanup:
    if (handle_add2 != NULL) {
        free(handle_add2);
    }

    rpc_close_client(state);
    state = NULL;

    return exit_code;
}
