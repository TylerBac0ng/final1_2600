#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <mosquitto.h>

// MQTT settings
#define SERVER "34.168.29.1"  // Server address
#define TOPIC_CMD "tictactoe/command"
#define TOPIC_STATE "tictactoe/state"
#define TOPIC_RESULT "tictactoe/result"
#define QOS 1

// Game variables
char board[9];
char current_player;
time_t last_move_time = 0;
int game_active = 1;
char last_board_state[20] = "";

// Delay between moves in seconds
#define MOVE_DELAY 3

// Function declarations
void make_move(struct mosquitto *mosq);
void print_board();
void log_message(const char *format, ...);

// Signal handler for graceful shutdown
void handle_signal(int signum) {
    printf("C player shutting down\n");
    exit(0);
}

// Generate unique client ID
char* generate_client_id() {
    static char client_id[50];
    snprintf(client_id, sizeof(client_id), "c_player_%ld_%d", (long)time(NULL), rand() % 1000);
    return client_id;
}

// Logging function with timestamp
void log_message(const char *format, ...) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "[%H:%M:%S]", tm);
    printf("%s ", timestamp);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

// Print current board state (for debugging)
void print_board() {
    log_message("Current board:");
    for (int i = 0; i < 3; i++) {
        printf("%c|%c|%c\n",
               board[i*3] == ' ' ? '_' : board[i*3],
               board[i*3+1] == ' ' ? '_' : board[i*3+1],
               board[i*3+2] == ' ' ? '_' : board[i*3+2]);
        if (i < 2) printf("-+-+-\n");
    }
    printf("Player: %c\n", current_player);
}

// Make a random move
void make_move(struct mosquitto *mosq) {
    // Only make moves for player X and when the game is active
    if (current_player != 'X' || !game_active) return;
    // Measure time since last move to add a delay
    time_t current_time = time(NULL);
    if (current_time - last_move_time < MOVE_DELAY) return;
    // Count empty spots
    int empty_spots[9];
    int count = 0;
    for (int i = 0; i < 9; i++) {
        if (board[i] == ' ') {
            empty_spots[count++] = i;
        }
    }
    if (count == 0) {
        log_message("No valid moves available");
        return; // No moves available
    }
    // Choose a random empty spot
    int idx = empty_spots[rand() % count];
    int row = idx / 3;
    int col = idx % 3;
    log_message("Making random move: %d,%d", row, col);
    char move_message[20];
    sprintf(move_message, "MOVE:%d,%d", row, col);
    mosquitto_publish(mosq, NULL, TOPIC_CMD, strlen(move_message), move_message, QOS, false);
    last_move_time = current_time;
}

// MQTT message callback
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    if (!msg->payload) return;
    char *payload = (char *)msg->payload;
    if (strcmp(msg->topic, TOPIC_STATE) == 0) {
        if (strncmp(payload, "STATE:", 6) == 0 && strlen(payload) >= 16) {
            // Check if this is a new state to avoid processing duplicates
            if (strcmp(payload, last_board_state) != 0) {
                strcpy(last_board_state, payload);
                // Parse state message
                for (int i = 0; i < 9; i++) {
                    board[i] = payload[6 + i];
                }
                current_player = payload[15];
                log_message("Received state update");
                print_board();
                // Add a small delay then make a move if it's our turn
                sleep(1);
                make_move(mosq);
            }
        }
    }
    else if (strcmp(msg->topic, TOPIC_RESULT) == 0) {
        if (strncmp(payload, "WIN:", 4) == 0 || strcmp(payload, "DRAW") == 0) {
            log_message("Game over: %s", payload);
            game_active = 0;
            log_message("Game finished - waiting for new game");
        }
    }
}

void on_connect(struct mosquitto *mosq, void *obj, int reason_code) {
    if (reason_code != 0) {
        log_message("Failed to connect to MQTT broker: %s", mosquitto_connack_string(reason_code));
        mosquitto_disconnect(mosq);
        return;
    }
    log_message("Connected to MQTT broker");
    // Subscribe to game state and results
    mosquitto_subscribe(mosq, NULL, TOPIC_STATE, QOS);
    mosquitto_subscribe(mosq, NULL, TOPIC_RESULT, QOS);
}

void on_disconnect(struct mosquitto *mosq, void *obj, int reason_code) {
    log_message("Disconnected from MQTT broker: %s", mosquitto_strerror(reason_code));
    // Sleep to prevent rapid reconnect cycles
    sleep(3);
}

void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos) {
    log_message("Subscription succeeded");
}

int main() {
    struct mosquitto *mosq;
    int rc;
    // Set up signal handler
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    // Initialize game variables
    for (int i = 0; i < 9; i++) {
        board[i] = ' ';
    }
    current_player = 'X';
    game_active = 1;
    last_move_time = 0;
    // Initialize random number generator
    srand(time(NULL));
    // Initialize mosquitto library
    mosquitto_lib_init();
    // Create new client instance with unique ID
    char* client_id = generate_client_id();
    log_message("Using client ID: %s", client_id);
    mosq = mosquitto_new(client_id, true, NULL);
    if (!mosq) {
        log_message("Error: Out of memory");
        return 1;
    }
    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    mosquitto_subscribe_callback_set(mosq, on_subscribe);
    // Connect to broker
    log_message("Connecting to MQTT broker at %s...", SERVER);
    rc = mosquitto_connect(mosq, SERVER, 1883, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        log_message("Unable to connect to broker: %s", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }
    log_message("C player started (X)");
    // Start network loop
    rc = mosquitto_loop_forever(mosq, -1, 1);
    if (rc != MOSQ_ERR_SUCCESS) {
        log_message("Error in network loop: %s", mosquitto_strerror(rc));
    }
    // Clean up
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}
