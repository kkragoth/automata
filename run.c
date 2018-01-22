#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <mqueue.h>

#include "err.h"
#include "protocol.h"
#include "common.h"
#include "spawner.h"

 void sigusr1_handler(int dummy) {
    // printf("Parent died, child now exiting\n");
    exit(0);
}

int main() {
    int N, A, Q, U, F, initial_state, current_state, validator_pid;
    bool accepting_states[100];
    int transitions[MAX_STATES][ALPHABET_SIZE][MAX_STATES];

    char pid[BUF_SIZE] = { 0 };
    char word[BUF_SIZE] = { 0 };
    char child_response[BUF_SIZE] = { 0 };
    char message[BUF_SIZE] = { 0 };

    read_automata_description_from_stdin(&N, &A, &Q, &U, &F, &initial_state, accepting_states, transitions);

    scanf("%d %s %s %d", &validator_pid,  pid, word, &current_state);

    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        syserr("signal failed");
    }
    if (prctl(PR_SET_PDEATHSIG, SIGUSR1) < 0) {
        syserr("prctl failed");
    }

    int fd[MAX_STATES];

    // ALGORITHM HERE:
    bool is_word_valid = false;
    int children_created = 0;

    if (strcmp(word, END_OF_WORD) == 0) {
        is_word_valid = accepting_states[current_state];
    }
    else {
        int word_offset = 0;
        int transition_letter = 0;
        while (strcmp(word + word_offset + 1, END_OF_WORD) != 0) {
            transition_letter = map_char_to_int(word[word_offset]);

            if (transitions[current_state][transition_letter][1] == INVALID_STATE) {
                current_state = transitions[current_state][transition_letter][0];
                word_offset += 1;
            }
            else {
                break;
            }
        }

        transition_letter = map_char_to_int(word[word_offset]);

        for (int i = 0; i < MAX_STATES; i++) {
            int next_state = transitions[current_state][transition_letter][i];

            if (next_state == INVALID_STATE) {
                break;
            }

            fd[children_created] = spawn_run(validator_pid, &N, &A, &Q, &U, &F, &initial_state, accepting_states, transitions, INVALID_PID, word + word_offset + 1, next_state);
            children_created += 1;
        }

        bool every_state_is_valid = true;
        bool exists_state_that_is_valid = false;

        for (int i = 0; i < children_created; i++ ) {
            memset(child_response, 0 , BUF_SIZE);
            if (read(fd[i], child_response, BUF_SIZE) == -1) {
                kill(validator_pid, SIGINT);
                syserr("read");
            }
            if (close(fd[i])) {
                kill(validator_pid, SIGINT);
                syserr("close");
            }

            bool child_response_is_valid = strcmp(child_response, WORD_IS_VALID) == 0;

            every_state_is_valid = every_state_is_valid && child_response_is_valid;
            exists_state_that_is_valid = exists_state_that_is_valid || child_response_is_valid;

            if (wait(0) == -1) {
                kill(validator_pid, SIGINT);
                syserr("Error in wait");
            }
        }
        if (current_state < U) { // universal state
            is_word_valid = every_state_is_valid;
        } else { //existential state
            is_word_valid = exists_state_that_is_valid;
        }
    }

    if (strcmp(pid, INVALID_PID) != 0) {
        memset(message, 0 , BUF_SIZE);
        mqd_t validator_desc = mq_open(VALIDATOR_MQ, O_WRONLY);

        sprintf(message, "%s#%s#%s", pid, is_word_valid ? WORD_IS_VALID : WORD_IS_INVALID, word);

        if(mq_send(validator_desc, message, strlen(message), 1)) {
            kill(validator_pid, SIGINT);
            syserr("Error in mq_send");
        }
    }
    else {
        if (is_word_valid) {
            printf("%s", WORD_IS_VALID);
        }
        else {
            printf("%s", WORD_IS_INVALID);
        }
    }
}
