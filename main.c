#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "ipc.h"
#include "pa1.h"
#include "common.h"

struct proc_pipe {
    int fd[2];
};

struct child_proc {
    local_id id;
    local_id proc_count;
    struct proc_pipe parent_channel_in;
    struct proc_pipe parent_channel_out;
    struct proc_pipe *children_pipes;
};



int log_event(const char *const file_name, const char *msg) {
    FILE *fd = fopen(file_name, "a+");
    int res = fprintf(fd, "%s", msg);
    printf("%s", msg);
    return res;
}

int send_full(int fd, const Message *msg) {
    long need_send = (long)(sizeof(MessageHeader) + msg->s_header.s_payload_len);
    long sent = 0;
    while (sent < need_send) {
        long res = write(fd, msg + sent, need_send - sent);
        if (res < 0) {
            return -1;
        }
        sent += res;
    }
    return 0;
}

int send(void *self, local_id dst, const Message *msg) {
    struct child_proc *proc = self;

    int result = send_full(proc->children_pipes[dst].fd[1], msg);
    if (result < 0) {
        // printf("CANNOT SEND FROM %d TO %d\n", proc->id, dst);
    }
    return result;
}

int send_multicast(void *self, const Message *msg) {
    struct child_proc *proc = self;
    int result = log_event(events_log, msg->s_payload);
    if (result < 0) {
        // printf("CANNOT WRITE TO LOG %d\n", proc->id);
        return -1;
    }

    //sent to parent
    result = send_full(proc->parent_channel_in.fd[1], msg);
    if (result < 0) {
        // printf("CANNOT SEND TO PARENT %d\n", proc->id);
        return -1;
    }
    //send to other
    for (local_id i = 0; i < proc->proc_count; i++) {
        if (i != proc->id) {
            result = send(proc, i, msg);
            if (result < 0) {
                // printf("SEND MULTI ERR %d to sender %d\n", errno, i);
                return -1;
            }
        }
    }
    return 0;
}

int receive_full(int fd, Message *msg, local_id id) {
    long need_receive = sizeof(MessageHeader);
    long received = 0;
    while (received < need_receive) {
        long res = read(fd, msg + received, need_receive - received);
        if (res < 0) {
            return -1;
        }
        received += res;
    }

    need_receive = msg->s_header.s_payload_len;
    received = 0;
    while (received < need_receive) {
        long res = read(fd, msg->s_payload + received, need_receive - received);
        if (res < 0) {
            return -1;
        }
        received += res;
    }
    if(id == 15) {
        printf("LOG: %d %s\n", msg->s_header.s_payload_len, msg->s_payload);
    }
    return 0;
}

int receive(void *self, local_id from, Message *msg) {
    struct child_proc *proc = self;

    int result = receive_full(proc->children_pipes[from].fd[0], msg, proc->id);
    if (result < 0) {
        //printf("CANNOT RECEIVE FROM %d TO %d\n", from, proc->id);
    }
    return result;
}

int receive_all(void *self, Message *msg) {
    struct child_proc *proc = self;
    int result = log_event(events_log, msg->s_payload);
    if (result < 0) {
        //printf("CANNOT WRITE TO LOG %d\n", proc->id);
        return -1;
    }
    result = send_full(proc->parent_channel_in.fd[1], msg);
    if (result < 0) {
        // printf("CANNOT SEND TO PARENT %d\n", proc->id);
        return -1;
    }

    //send to other
    for (local_id i = 0; i < proc->proc_count; i++) {
        if (i != proc->id) {
            result = receive(proc, i, msg);
            if (result < 0) {
                // printf("RECEIVE MULTI ERR %d from sender %d\n", errno, i);
                return -1;
            }
        }
    }
    return 0;
}

int child_proc_work(struct child_proc *child) {
    Message message;
    message.s_header.s_type = STARTED;
    pid_t p = getpid();
    pid_t pp = getppid();
    sprintf(message.s_payload, log_started_fmt, child->id, p, pp);
    message.s_header.s_payload_len = strlen(message.s_payload);
    int result = send_multicast(child, &message);
    if (result < 0) {
        return -1;
    }
    char log_all_fmt[MAX_PAYLOAD_LEN];
    sprintf(log_all_fmt, log_received_all_started_fmt, child->id);
    result = log_event(events_log, log_all_fmt);
    if (result < 0) {
        return -1;
    }

    // 3 step;
    Message done_message;
    done_message.s_header.s_type = DONE;
    sprintf(done_message.s_payload, log_done_fmt, child->id);
    done_message.s_header.s_payload_len = strlen(message.s_payload);
    result = receive_all(child, &done_message);
    if (result < 0) {
        return -1;
    }

    char log_all_done_fmt[MAX_PAYLOAD_LEN];
    sprintf(log_all_done_fmt, log_received_all_done_fmt, child->id);
    result = log_event(events_log, log_all_done_fmt);
    if (result < 0) {
        return -1;
    }

    return 0;
}

int init_parent_pipes(struct proc_pipe *pipes_array, local_id count) {
    for (local_id i = 0; i < count; i++) {
        int res = pipe(pipes_array[i].fd);
        if (res < 0) {
            //  printf("ERROR IN init_pipes %d\n", errno);
            return -1;
        }
    }
    return 0;
}


void wait_all(local_id cnt, struct proc_pipe* fd) {

    for (local_id i = 0; i < cnt; i++) {
        Message start_message;
        receive_full(fd[i].fd[0], &start_message, 15);
        Message done_message;
        receive_full(fd[i].fd[0], &done_message, 15);
        wait(NULL);
    }
}

void log_pipes(struct proc_pipe *pipes, local_id id, local_id count) {
    FILE *f = fopen(pipes_log, "a+");
    for(local_id i = 0; i < count; i++) {
        if(i != id) {
            fprintf(f, "pipes: fd[0] %d fd[1]: %d processes id: %d %d\n", pipes[i].fd[0], pipes[i].fd[1], id, i);
        }
    }
    fclose(f);
}
int main(int argc, char **argv) {
    local_id proc_count = 1;
    if (argc == 3) {
        char *arg_end;
        proc_count = (local_id) strtol(argv[2], &arg_end, 10);
    } else if (argc != 1) {
        return 0;
    }


    // init parent pipes
    struct proc_pipe parent_pipes_in[proc_count];
    init_parent_pipes(parent_pipes_in, proc_count);
    log_pipes(parent_pipes_in, proc_count, proc_count);
    struct proc_pipe parent_pipes_out[proc_count];
    init_parent_pipes(parent_pipes_out, proc_count);
    log_pipes(parent_pipes_out, proc_count, proc_count);
    // open logs
    FILE *f = fopen(events_log, "a+");


    // init children pipes
    struct proc_pipe children_pipes[proc_count][proc_count];
    for (local_id i = 0; i < proc_count; i++) {
        for (local_id j = 0; j < proc_count; j++) {
            if (i < j) {
                int first_pipe[2];
                int second_pipe[2];

                pipe(first_pipe);
                pipe(second_pipe);

                children_pipes[i][j].fd[0] = first_pipe[0];
                children_pipes[i][j].fd[1] = second_pipe[1];

                children_pipes[j][i].fd[1] = first_pipe[1];
                children_pipes[j][i].fd[0] = second_pipe[0];
            }
        }
    }

    for(local_id i = 0; i < proc_count; i++) {
        log_pipes(children_pipes[i], i, proc_count);
    }
    local_id created = 0;
    pid_t proc_id = 0;
    for (local_id i = 0; i < proc_count; i++) {
        proc_id = fork();
        if (proc_id < 0) {
            break;
        }
        if (proc_id == 0) {
            struct child_proc child;
            child.id = i;
            child.proc_count = proc_count;
            child.children_pipes = children_pipes[i];
            child.parent_channel_in = parent_pipes_in[i];
            child.parent_channel_out = parent_pipes_out[i];
            close(parent_pipes_in[i].fd[0]);
            close(parent_pipes_out[i].fd[1]);
            for (local_id j = 0; j < proc_count; j++) {
                if (i != j) {
                    close(children_pipes[j][i].fd[0]);
                    close(children_pipes[j][i].fd[1]);

                    close(parent_pipes_in[j].fd[0]);
                    close(parent_pipes_in[j].fd[1]);
                    close(parent_pipes_out[j].fd[0]);
                    close(parent_pipes_out[j].fd[1]);
                    for (local_id k = 0; k < proc_count; k++) {
                        if (i != k && k != j) {
                            close(children_pipes[j][k].fd[0]);
                            close(children_pipes[j][k].fd[1]);
                            close(children_pipes[k][j].fd[0]);
                            close(children_pipes[k][j].fd[1]);
                        }
                    }
                }
            }
            child_proc_work(&child);
            break;
        } else {
            created++;
        }
    }

    if (proc_id != 0) {
        for(local_id i = 0; i < proc_count; i ++) {
            for(local_id j = 0; j < proc_count; j++) {
                if(i < j) {
                    close(children_pipes[i][j].fd[0]);
                    close(children_pipes[i][j].fd[1]);
                    close(children_pipes[j][i].fd[0]);
                    close(children_pipes[j][i].fd[1]);
                }
            }
            close(parent_pipes_in[i].fd[1]);
            close(parent_pipes_out[i].fd[0]);
        }

        wait_all(created, parent_pipes_in);

        fclose(f);
    }
    return 0;
}
