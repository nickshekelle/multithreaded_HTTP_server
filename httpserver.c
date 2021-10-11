#include <getopt.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include<fcntl.h>
#include<errno.h>
#include <ctype.h>
#include <pthread.h>
#include <math.h>


// ###############################
// mutexes and condition variables
// ###############################

//queue semaphores
pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t q_cond_var = PTHREAD_COND_INITIALIZER;
pthread_mutex_t q_lock_log = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t q_cond_var_log = PTHREAD_COND_INITIALIZER;

//global semaphores
pthread_mutex_t total_requests_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t total_errors_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t curr_acc_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t curr_acc_update = PTHREAD_COND_INITIALIZER;
pthread_mutex_t curr_end_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;




// ###############################
//             structs
// ###############################
struct node {
    struct node* next;
    int *client_socket; //will hold the accepted connection
};

struct log_node {
    struct log_node* next;
    char *file;
    char* request;
    int *if_error;
    char* status_code;
    char* bad_request_line;
};

typedef struct node node_t;

typedef struct log_node log_node_t;

typedef struct {
    char *filename;
    int req_type;
} Tuple; // for tuples

typedef struct {
            int total_requests;
            int total_errors;
            long curr_end; //used for logging
            Tuple * curr_acc;
            int num_thread;
            int logging;
            char* logfile;
} Global_vars;


typedef struct {
            char *file;
            char* request;
            int *if_error;
            char* status_code;
            char* bad_request_line;
} Log_result;




// ###############################
//             queue
// ###############################
node_t *head = NULL;
node_t *tail = NULL;

log_node_t *log_head = NULL;
log_node_t *log_tail = NULL;

//queue functions
void enqueue(int *client_socket){
    node_t *newnode = malloc(sizeof(node_t));
    newnode->client_socket = client_socket;
    newnode->next = NULL;
    if(tail == NULL){
        head = newnode;
    }else{
        tail->next = newnode;
    }
    tail = newnode;
}


int* dequeue(){ //returns pointer to socket
    if(head == NULL){


        return NULL;
    }else{
        int *result = head->client_socket;
        node_t *temp = head;
        head = head->next;
        if(head == NULL){
            tail = NULL;
        }
        free(temp);
        return result;
    }
}


//queue functions
void enqueue_log(char* request, char* file, int *if_error, char* status_code, char* bad_request_line){
    log_node_t *newnode = calloc(1, sizeof(log_node_t));
    newnode->request = request;
    newnode->file = file;
    newnode->if_error = if_error;
    newnode->status_code = status_code;
    newnode->bad_request_line = bad_request_line;
    newnode->next = NULL;
    if(log_tail == NULL){
        log_head = newnode;
    }else{
        log_tail->next = newnode;
    }
    log_tail = newnode;
    printf("ENQUEUED: Request: %s\nfile: %s\nif_error: %d\nstatus code: %s\n\n", log_tail->request, log_tail->file, log_tail->if_error[0], log_tail->status_code);
}


Log_result* dequeue_log(){
    Log_result *result = calloc(1, sizeof(Log_result));
    result->request = "";
    if(log_head == NULL){
        return result;
    }else{
        // need to assign actual value to result, NOT pointer
        result->request = log_head->request;
        result->file = log_head->file;
        result->if_error = log_head->if_error;
        result->status_code = log_head->status_code;
        result->bad_request_line = log_head->bad_request_line;
        printf("DEQUEUED: Request: %s\nfile: %s\nif_error: %d\nnode status code: %s\n", result->request, result->file, result->if_error[0], result->status_code);
        log_node_t *temp = log_head;
        log_head = log_head->next;
        if(log_head == NULL){
            log_tail = NULL;
        }
        printf("results if_error: %d\n\n", result->if_error[0]);
        free(temp);
        return result;
    }
}


// ###############################
//        helper funcitons
// ###############################
int content_length(char* string){
    char* save_ptr;
    char request_copy[5000];
    strcpy(request_copy, string);
    char* request = strtok_r(request_copy, "\n", &save_ptr);
    while(strstr(request, "Content-Length:") == 0){
        request = strtok_r(NULL, "\n", &save_ptr);

    }
    return atoi(request+16);
}

char* content_length_str(char* string){
    char* save_ptr;
    char request_copy[5000];
    strcpy(request_copy, string);
    char* request = strtok_r(request_copy, "\n", &save_ptr);
    while(strstr(request, "Content-Length:") == 0){
        request = strtok_r(NULL, "\n", &save_ptr);

    }
    return request+16;
}

int start_of_content(char* string){
    char* flag = "\r\n\r\n";
    int i=0,k=0,c,index=-5;
    while(string[i]!='\0'){
        if(string[i]==flag[0]){
            k=1;
            for(c=1;flag[c]!='\0';c++){
                if(string[i+c]!=flag[c]){
                 k=0;
                 break;
                }
            }
        }
        if(k==1){
            index=i;
        }
        i++;
        k=0;
    }
    return index + 4; //to account for the 4 leading chars (\r\n\r\n)
    // returns -1 if flag not found
}

int end_of_first_line(char* string){
    char* flag = "\n";
    int i=0,k=0,c,index=-5;
    while(string[i]!='\0'){
        if(string[i]==flag[0]){
            return i-1;
        }
        i++;
    }
    return -1;
}

int open_slot(Tuple* arr, int size){
    for (int i =0; i<size; i++){
        if(arr[i].req_type == -1){
            return i;
        }
    }

    return -1;

}

// ###############################
//         logging process
// ###############################
int log_write(int log_file_descriptor, unsigned char* buffer, long amount_read, long my_offset, long total_read){
    long i;
    int amount_to_log = 0;
    char big_write[15000] = {0};
    char log_beg_line[20] = {0};
    char logbuffer[10] = {0};

    // Process every byte in the data.

    for (i = 0; i < amount_read; i++) {
        // Multiple of 20 means new line (with line offset).



        if ((i % 20) == 0) {

            // Output the offset.
            sprintf (log_beg_line, "\n%08ld", i+total_read);
            strcat(big_write, log_beg_line);
            amount_to_log += 9;

        }

        // Now the hex code for the specific character.
        sprintf (logbuffer, " %02x", buffer[i]);
        strcat(big_write, logbuffer);
        amount_to_log += 3;


    }
    my_offset += pwrite(log_file_descriptor, big_write, amount_to_log, my_offset);



    return my_offset;
}


void log_write_wrapper(char* request, char* file, char* logfile_name, int size_of_log){
    int logfile = open(logfile_name, O_WRONLY);
    long my_offset = size_of_log;
    struct stat buf;
    stat(file, &buf);
    int size = buf.st_size;
    float size_for_log_math = buf.st_size;

    //convert content length to string
    char len[12];
    sprintf(len, "%d", size);


    //how much will we need to log
    int log_header_size = strlen(request) + strlen(file) + strlen(len) + 11;
    int log_content_length = (ceil(size_for_log_math/20.0) * 9) + ((size_for_log_math) * 3);
    int log_end_line = 9;
    int log_total_space = log_header_size + log_content_length + log_end_line;


    //write log header to prepare for logging
    char log_header[log_header_size+1];
    if(strcmp(request, "healthcheck") == 0){
        sprintf(log_header, "GET /healthcheck length %ld", strlen(file));
        my_offset += pwrite(logfile, log_header, strlen(log_header), my_offset);
        my_offset = log_write(logfile, (unsigned char*) file, strlen(file), my_offset, 0);
        pwrite(logfile, "\n========\n", log_end_line+1, my_offset);
        return;
    }else{
        sprintf(log_header, "%s /%s length %s", request, file, len);
        my_offset += pwrite(logfile, log_header, log_header_size-1, my_offset);
    }


    if(strcmp(request, "HEAD") != 0){
        int sz;
        int fd = open(file, O_RDONLY);
        char *c = (char *) calloc(4080, 1);
        long total_read = 0;
        while(total_read < size){
            sz = read(fd, c, 4080);
            my_offset = log_write(logfile, (unsigned char*) c, sz, my_offset, total_read);
            total_read += sz;


        }

        free(c);
    }


    pwrite(logfile, "\n========\n", log_end_line+1, my_offset);
}


void log_error(char* status_code, char* request, char* file, char* logfile_name, int size,  char* bad_request_line){
    //open logfile and prepare for logging
    int logfile = open(logfile_name, O_WRONLY);
    long my_offset = size;

    //how much will we need to log
    int log_header_size = strlen(bad_request_line) + 24;
    int log_content_length = 0;
    int log_end_line = 9;
    int log_total_space = log_header_size + log_content_length + log_end_line;

    //write log header to prepare for logging
    char log_header[log_header_size+1];
    sprintf(log_header, "FAIL: %s --- response %s\n", bad_request_line, status_code);
    my_offset += pwrite(logfile, log_header, log_header_size, my_offset);
    pwrite(logfile, "========\n", log_end_line, my_offset);



}




// ###############################
//          main process
// ###############################
void * process_request(void* client_socket_pointer, Global_vars* global){
    // prevent all the casting
    int client_sockd = *((int*)client_socket_pointer);






    long valread;
    uint8_t request_buffer[4097] = {0};
    uint8_t request_buffer_copy[4098] = {0};
    int offset = 0;
    while(start_of_content((char*) request_buffer_copy) == -1){
        uint8_t buffer[4096] = {0};
        valread = read( client_sockd , buffer, 4096);  // buffer now holds the first 4096 bytes of whatever was sent in the most recent stream
        // copy buffer contents into request_buffer
        int i;
        for(i = 0; i<valread; i++){
            request_buffer[offset+i] = buffer[i];
            request_buffer_copy[offset+i] = buffer[i];
        }
        offset += i;

    }

    request_buffer_copy[sizeof(request_buffer)/sizeof(request_buffer[0])] = '\0';

    //printf("OUR REQUEST HEADER:\n%s\n", request_buffer_copy);
    char *request = calloc(1, 256);
    char *file = calloc(1, 256);
    char *http_version = calloc(1, 256);
    sscanf((char*) request_buffer_copy, "%s /%s %s", request, file, http_version);    // sscanf is threadsafe

    //printf("Request: %s\n", request);
    //printf("Filename: %s\n", file);
    //printf("http_version: %s\n", http_version);



    int bad_request = 0;
    if(strcmp(http_version, "HTTP/1.1") != 0 ){
        char* get_response = "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n";
        write(client_sockd, get_response, strlen(get_response));
        close(client_sockd);
        //update total_errors
        pthread_mutex_lock(&total_errors_lock);
        global->total_errors++;
        pthread_mutex_unlock(&total_errors_lock);

        bad_request = 1;
    }else if(strlen(file) > 27){
        char* get_response = "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n";
        write(client_sockd, get_response, strlen(get_response));
        close(client_sockd);
        //update total_errors
        pthread_mutex_lock(&total_errors_lock);
        global->total_errors++;
        pthread_mutex_unlock(&total_errors_lock);

        bad_request = 1;



    }else{
        for(int i =0; i<strlen(file); i++){
            if(isalnum(file[i]) == 0 && (file[i] != '-') && (file[i] != '_')){
                char* get_response = "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n";
                write(client_sockd, get_response, strlen(get_response));
                close(client_sockd);
                //update total_errors
                pthread_mutex_lock(&total_errors_lock);
                global->total_errors++;
                pthread_mutex_unlock(&total_errors_lock);

                bad_request = 1;

            }

        }
    }

    if (bad_request == 1){
        if(global->logging == 1){
            char *status_code = "400";
            pthread_mutex_lock(&q_lock_log);
            char *bad_request_line = calloc(1, 300);
            int end_first_line = end_of_first_line((char*) request_buffer_copy);
            memcpy(bad_request_line, request_buffer_copy, end_first_line);
            bad_request_line[end_first_line] = '\0';
            int* if_error = calloc(1, sizeof(int));
            if_error[0] = 1;
            enqueue_log(request, file, if_error, status_code, bad_request_line);
            pthread_cond_signal(&q_cond_var_log);
            pthread_mutex_unlock(&q_lock_log);
            close(client_sockd);


        }


        return NULL;
    }

    // check for call to healthcheck TODO for some reason not the write response
        if((strcmp(file, "healthcheck") == 0)){
            if(global->logging == 1){
                if((strcmp(request, "GET") == 0)){
                    char total_req_string[12];
                    char total_errors_string[12];
                    char len[12];
                    sprintf(total_req_string, "%d", global->total_requests);
                    sprintf(total_errors_string, "%d", global->total_errors);
                    int size = strlen(total_req_string) + strlen(total_errors_string) + 1;
                    sprintf(len, "%d", size);
                    char content_length[20] = {0};
                    strcpy(content_length, len);
                    strcat(content_length, "\r\n\r\n");

                    char health_content[30] = {0};
                    strcpy(health_content, total_errors_string);
                    strcat(health_content, "\n");
                    strcat(health_content, total_req_string);

                    char* get_response = "HTTP/1.1 200 OK\r\nContent-Length: ";
                    write(client_sockd, get_response, strlen(get_response));
                    write(client_sockd, content_length, strlen(content_length));
                    write(client_sockd, health_content, strlen(health_content));
                    close(client_sockd);
                    pthread_mutex_lock(&q_lock_log);
                    int* if_error = calloc(1, sizeof(int));
                    if_error[0] = 0;
                    enqueue_log("healthcheck", health_content, if_error, "", "HTTP/1.1");
                    pthread_cond_signal(&q_cond_var_log);
                    pthread_mutex_unlock(&q_lock_log);


                }else{
                    char *status_code = "403";
                    char* get_response = "HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n";

                    write(client_sockd, get_response, strlen(get_response));
                    close(client_sockd);
                    //update total_errors
                    pthread_mutex_lock(&total_errors_lock);
                    global->total_errors++;
                    pthread_mutex_unlock(&total_errors_lock);

                    pthread_mutex_lock(&q_lock_log);
                    char *bad_request_line = calloc(1, 300);
                    int end_first_line = end_of_first_line((char*) request_buffer_copy);
                    memcpy(bad_request_line, request_buffer_copy, end_first_line);
                    bad_request_line[end_first_line] = '\0';
                    int* if_error = calloc(1, sizeof(int));
                    if_error[0] = 1;
                    enqueue_log(request, file, if_error, status_code, bad_request_line);
                    pthread_cond_signal(&q_cond_var_log);
                    pthread_mutex_unlock(&q_lock_log);
                }
            }else{
                char *status_code = "404";
                char* get_response = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n";
                write(client_sockd, get_response, strlen(get_response));
                close(client_sockd);
                //update total_errors
                pthread_mutex_lock(&total_errors_lock);
                global->total_errors++;
                pthread_mutex_unlock(&total_errors_lock);
            }


            close(client_sockd);
            //increment total_requests
            pthread_mutex_lock(&total_requests_lock);
            global->total_requests++;
            pthread_mutex_unlock(&total_requests_lock);



            return NULL;
        }

    //increment total_requests
    pthread_mutex_lock(&total_requests_lock);
    global->total_requests++;
    pthread_mutex_unlock(&total_requests_lock);



    // construct response
    if(strcmp(request, "GET") == 0){
        int fd;
        int sz = 1;
        fd = open(file, O_RDONLY);
        if(fd == -1){
            char *status_code = "404";
            char* get_response = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n";

            if(strcmp(strerror(errno), "Permission denied") == 0){
                char *status_code = "403";
                get_response = "HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n";
            }
            write(client_sockd, get_response, strlen(get_response));
            //update total_errors
            pthread_mutex_lock(&total_errors_lock);
            global->total_errors++;
            pthread_mutex_unlock(&total_errors_lock);

            if(global->logging == 1){
                pthread_mutex_lock(&q_lock_log);
                char *bad_request_line = calloc(1, 300);
                int end_first_line = end_of_first_line((char*) request_buffer_copy);
                memcpy(bad_request_line, request_buffer_copy, end_first_line);
                bad_request_line[end_first_line] = '\0';
                int* if_error = calloc(1, sizeof(int));
                if_error[0] = 1;
                enqueue_log(request, file, if_error, status_code, bad_request_line);
                pthread_cond_signal(&q_cond_var_log);
                pthread_mutex_unlock(&q_lock_log);
                close(client_sockd);


                return NULL;

            }

        }else{

        struct stat buf;
        stat(file, &buf);
        int size = buf.st_size;
        float size_for_log_math = buf.st_size;


        //convert content length to string
        char len[12];
        sprintf(len, "%d", size);

        // create return strings
        char* get_response = "HTTP/1.1 200 OK\r\nContent-Length: ";
        char content_length[20] = {0};
        strcpy(content_length, len);
        strcat(content_length, "\r\n\r\n");

        write(client_sockd, get_response, strlen(get_response));
        write(client_sockd, content_length, strlen(content_length));


        char *c = (char *) calloc(4080, 1);
        while(sz != 0){
            sz = read(fd, c, 4080);
            write(client_sockd, c, sz);
        }
        free(c);

        if(global->logging == 1){
            pthread_mutex_lock(&q_lock_log);
            int* if_error = calloc(1, sizeof(int));
            if_error[0] = 0;
            enqueue_log(request, file, if_error, "", "HTTP/1.1");
            pthread_cond_signal(&q_cond_var_log);
            pthread_mutex_unlock(&q_lock_log);


        }




        // free allocated memory


    }


    }else if(strcmp(request, "PUT") == 0){ // TODO failing tests
        // parse length of input
        int length = content_length((char*) request_buffer_copy);

        printf("length: %d\n", length);


        // open file and write content
        int created = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if(created == -1){
            char *status_code = "500";
            char* get_response = "HTTP/1.1 500 INTERNAL SERVER ERROR\r\nContent-Length: 0\r\n\r\n";
            if(strcmp(strerror(errno), "Permission denied") == 0){
                char *status_code = "403";
                get_response = "HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n";
            }
            write(client_sockd, get_response, strlen(get_response));
            //update total_errors
            pthread_mutex_lock(&total_errors_lock);
            global->total_errors++;
            pthread_mutex_unlock(&total_errors_lock);

            pthread_mutex_lock(&q_lock_log);
            char *bad_request_line = calloc(1, 300);
            int end_first_line = end_of_first_line((char*) request_buffer_copy);
            memcpy(bad_request_line, request_buffer_copy, end_first_line);
            bad_request_line[end_first_line] = '\0';
            int* if_error = calloc(1, sizeof(int));
            if_error[0] = 1;
            enqueue_log(request, file, if_error, status_code, bad_request_line);
            pthread_cond_signal(&q_cond_var_log);
            pthread_mutex_unlock(&q_lock_log);

        }else{
        uint8_t* content = request_buffer + start_of_content((char*) request_buffer_copy);
        int start_of_content_buffer = start_of_content((char*) request_buffer_copy);
        int length_of_request_buffer = strlen((char*)request_buffer_copy);

        int total_read = 0;
        if(start_of_content_buffer < length_of_request_buffer){ //if there is more in the buffer than just the request headers
            int difference = length_of_request_buffer - start_of_content_buffer;
            if(difference > length){ // if we already have all the content to be sent in our request buffer
                write(created, content, length); // write all the content, we are done TODO this is working!!!
                total_read = length;
        }else{
                write(created, content, difference);  // write the current amount of content we have
                total_read = difference;
            }
        }





        while(0 < (length - total_read)){ // there is more content to be written
            uint8_t buffer[4080] = {0};
            valread = read( client_sockd , buffer, 4080);
            write(created, buffer, valread);
            total_read += valread;

        }

        if(global->logging == 1){
            pthread_mutex_lock(&q_lock_log);
            int* if_error = calloc(1, sizeof(int));
            if_error[0] = 0;
            enqueue_log(request, file, if_error, "", "HTTP/1.1");
            pthread_cond_signal(&q_cond_var_log);
            pthread_mutex_unlock(&q_lock_log);

        }

        char* get_response = "HTTP/1.1 201 CREATED\r\nContent-Length: 0\r\n\r\n";
        write(client_sockd, get_response, strlen(get_response));


    }

    }else if(strcmp(request, "HEAD") == 0){
        int fd;
        fd = open(file, O_RDONLY);
        if(fd == -1){
            char *status_code = "404";
            char* get_response = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n";

            if(strcmp(strerror(errno), "Permission denied") == 0){
                char *status_code = "403";
                get_response = "HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n";
            }
            write(client_sockd, get_response, strlen(get_response));
            //update total_errors
            pthread_mutex_lock(&total_errors_lock);
            global->total_errors++;
            pthread_mutex_unlock(&total_errors_lock);

            if(global->logging == 1){
                pthread_mutex_lock(&q_lock_log);
                char *bad_request_line = calloc(1, 300);
                int end_first_line = end_of_first_line((char*) request_buffer_copy);
                memcpy(bad_request_line, request_buffer_copy, end_first_line);
                bad_request_line[end_first_line] = '\0';
                int* if_error = calloc(1, sizeof(int));
                if_error[0] = 1;
                enqueue_log(request, file, if_error, status_code, bad_request_line);
                pthread_cond_signal(&q_cond_var_log);
                pthread_mutex_unlock(&q_lock_log);
                close(client_sockd);



                return NULL;

            }

        }else{
        struct stat buf;
        stat(file, &buf);
        int size = buf.st_size;

        //convert content length to string
        char len[12];
        sprintf(len, "%d", size);


        if(global->logging == 1){
            pthread_mutex_lock(&q_lock_log);
            int* if_error = calloc(1, sizeof(int));
            if_error[0] = 0;
            enqueue_log(request, file, if_error, "", "HTTP/1.1");
            pthread_cond_signal(&q_cond_var_log);
            pthread_mutex_unlock(&q_lock_log);


        }

        char* get_response = "HTTP/1.1 200 OK\r\nContent-Length: ";
        char content_length[20] = {0};
        strcpy(content_length, len);
        strcat(content_length, "\r\n\r\n");

        write(client_sockd, get_response, strlen(get_response));
        write(client_sockd, content_length, strlen(content_length));


    }

    }else {
        char* get_response = "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n";
        char *status_code = "400";
        write(client_sockd, get_response, strlen(get_response));
        //update total_errors
        pthread_mutex_lock(&total_errors_lock);
        global->total_errors++;
        pthread_mutex_unlock(&total_errors_lock);

        if(global->logging == 1){
                pthread_mutex_lock(&q_lock_log);
                char *bad_request_line = calloc(1, 300);
                int end_first_line = end_of_first_line((char*) request_buffer_copy);
                memcpy(bad_request_line, request_buffer_copy, end_first_line);
                bad_request_line[end_first_line] = '\0';
                int* if_error = calloc(1, sizeof(int));
                if_error[0] = 1;
                enqueue_log(request, file, if_error, status_code, bad_request_line);
                pthread_cond_signal(&q_cond_var_log);
                pthread_mutex_unlock(&q_lock_log);
                close(client_sockd);



                return NULL;

            }
    }

    close(client_sockd);





    return NULL;
}


// ###############################
//         thread function
// ###############################
void * look_for_work(void* arg){
    Global_vars *global = (Global_vars*) arg;

    while(1){
        int *client_socket_pointer;
        pthread_mutex_lock(&q_lock);
        client_socket_pointer = dequeue();
        while(client_socket_pointer == NULL){
            pthread_cond_wait(&q_cond_var, &q_lock);
            client_socket_pointer = dequeue();
        }
        pthread_mutex_unlock(&q_lock);
        if(client_socket_pointer != NULL){ // we found work
            process_request(client_socket_pointer, global);
        }
    }
}


void * look_for_work_log(void* arg){
    char *logfile = (char*) arg;



    while(1){
        Log_result* results;
        pthread_mutex_lock(&q_lock_log);
        results = dequeue_log();
        while(strcmp(results->request, "") == 0){
            pthread_cond_wait(&q_cond_var_log, &q_lock_log);
            results = dequeue_log();
        }
        pthread_mutex_unlock(&q_lock_log);
        if(strcmp(results->request, "") != 0){ // we found work

            struct stat buf;
            stat(logfile, &buf);
            int size = buf.st_size;
            if(results->if_error[0] == 1){
                log_error(results->status_code, results->request, results->file, logfile, size, results->bad_request_line);
            }else{
                log_write_wrapper(results->request, results->file, logfile, size);
            }
            free(results);
        }
    }
}





int main(int argc, char *argv[]) {

    // TODO use getopt to get these values from the command line
    int num_thread = 4; //TODO substitue with parsed thread number
    int logging = 0;
    char* logfile;
    char* port;/* server operating port */

    if(argc < 2){
        fprintf(stderr, "%s\n", "YOU MUST GIVE A PORT NUMBER GREATER THAN 0");
        exit(0);
    }

    int opt;
    while (1) {
        opt = getopt(argc, argv, "N:l:");
        if (opt == -1){
            break;
        }
        switch (opt) {
            case 'N':
                num_thread = atoi(optarg);
                break;
            case 'l':
                logging = 1;
                logfile = optarg;
                break;
            }
    }

    if(optind != argc){
        port = argv[optind];
        if(atoi(port) <= 0){
            fprintf(stderr, "%s\n", "YOU MUST GIVE A PORT NUMBER GREATER THAN 0");
            exit(0);
        }
    }

    printf("Server started with:\nPort: %s\nNumber of threads: %d\nLogging: %d\nLogfile: %s\n", port, num_thread, logging, logfile);



    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t ai_addrlen = sizeof server_addr;
    /* create server socket */
    int server_sockd = socket(AF_INET, SOCK_STREAM, 0);
    /* configure server socket */
    int enable = 1;
    /* this allows you to avoid 'Bind: Address Already in Use' error */
    int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    /* bind server address to socket that is open */
    ret = bind(server_sockd, (struct sockaddr *) &server_addr, ai_addrlen);
    /* listen for incoming connctions */
    ret = listen(server_sockd, 5); /* 5 should be enough, if not use SOMAXCONN */




    pthread_t threads[num_thread];
    pthread_t logger_thread;

    //initialize global vars and thread args
    Global_vars g;
    g.total_requests = 0;
    g.total_errors = 0;
    g.curr_end = 0;
    /*g.curr_acc = malloc(sizeof(Tuple)*num_thread);
    for(int i = 0; i<num_thread; i++){
        g.curr_acc[i].filename = "";
        g.curr_acc[i].req_type = -1;
    } */
    g.num_thread = num_thread;
    g.logfile = logfile;
    g.logging = logging;

    if (logging == 1){
        int logfd = open(logfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        close(logfd);
        pthread_create(&logger_thread, NULL, look_for_work_log, logfile);
    }


    //create our thread pool
    for(int i=0; i<num_thread; i++){
        pthread_create(&threads[i], NULL, look_for_work, &g);
    }


    while(1){

        /* connecting with a client */
        struct sockaddr client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);

        int *client_socket_pointer = malloc(sizeof(int));
        *client_socket_pointer = client_sockd;

        //lock enqueue to ensure no race condidtions
        pthread_mutex_lock(&q_lock);
        enqueue(client_socket_pointer);
        pthread_cond_signal(&q_cond_var);
        pthread_mutex_unlock(&q_lock);


    }
        free(g.curr_acc);

        return 0;
}

