#!/bin/bash

# Settings
SERVER="34.168.29.1"
# Generate a unique client ID
CLIENT_ID="bash_player_$(date +%s)_$RANDOM"
TOPIC_STATE="tictactoe/state"
TOPIC_CMD="tictactoe/command"
TOPIC_RESULT="tictactoe/result"

# Game variables
LAST_MOVE_TIME=0
MOVE_DELAY=3  # Seconds between moves
GAME_ACTIVE=true
CURRENT_PLAYER="X"
LAST_BOARD_STATE=""

# Initialize board
declare -a BOARD
for ((i = 0; i < 9; i++)); do
    BOARD[$i]=" "
done

# Function to log with timestamp
log() {
    echo "[$(date '+%H:%M:%S')] $1"
}

# Function to output current state for debugging
print_board() {
    log "Current board:"
    echo "${BOARD[0]}|${BOARD[1]}|${BOARD[2]}"
    echo "-+-+-"
    echo "${BOARD[3]}|${BOARD[4]}|${BOARD[5]}"
    echo "-+-+-"
    echo "${BOARD[6]}|${BOARD[7]}|${BOARD[8]}"
    echo "Player: $CURRENT_PLAYER"
}

# Make a random move if it's player O's turn
make_move() {
    if [[ "$CURRENT_PLAYER" != "O" || "$GAME_ACTIVE" != "true" ]]; then
        return
    fi
    # Add delay before making a move
    CURRENT_TIME=$(date +%s)
    if (( CURRENT_TIME - LAST_MOVE_TIME < MOVE_DELAY )); then
        return
    fi
    # Find empty spots
    EMPTY_SPOTS=()
    for ((i = 0; i < 9; i++)); do
        if [[ "${BOARD[$i]}" == " " ]]; then
            EMPTY_SPOTS+=($i)
        fi
    done
    # Check if there are any valid moves
    if [[ ${#EMPTY_SPOTS[@]} -eq 0 ]]; then
        log "No valid moves available"
        return
    fi
    # Make a random move
    RANDOM_INDEX=$((RANDOM % ${#EMPTY_SPOTS[@]}))
    POSITION=${EMPTY_SPOTS[$RANDOM_INDEX]}
    ROW=$((POSITION / 3))
    COL=$((POSITION % 3))
    log "Making random move: $ROW,$COL"
    mosquitto_pub -h $SERVER -t $TOPIC_CMD -m "MOVE:$ROW,$COL" -q 1
    LAST_MOVE_TIME=$CURRENT_TIME
}

# Parse state message
parse_state() {
    MESSAGE="$1"
    if [[ "$MESSAGE" == STATE:* ]]; then
        BOARD_STR=${MESSAGE:6:9}
        NEW_PLAYER=${MESSAGE:15:1}
        # Only process if this is a new state
        if [[ "$BOARD_STR$NEW_PLAYER" != "$LAST_BOARD_STATE" ]]; then
            LAST_BOARD_STATE="$BOARD_STR$NEW_PLAYER"
            for ((i = 0; i < 9; i++)); do
                BOARD[$i]="${BOARD_STR:$i:1}"
            done
            CURRENT_PLAYER="$NEW_PLAYER"
            log "Received state update"
            print_board
            # Wait a moment before trying to make a move
            sleep 0.5
            make_move
        fi
    elif [[ "$MESSAGE" == "WIN:"* || "$MESSAGE" == "DRAW" ]]; then
        log "Game result: $MESSAGE"
        GAME_ACTIVE=false
        log "Game over - will exit in 5 seconds"
        sleep 5
        exit 0
    fi
}

# Setup signal handling
cleanup() {
    log "Bash player shutting down"
    exit 0
}
trap cleanup SIGINT SIGTERM

# Main loop
log "Bash player started (O) with client ID: $CLIENT_ID"
log "Subscribing to MQTT topics..."

# Subscribe to state and result topics with unique client ID
mosquitto_sub -h $SERVER -t $TOPIC_STATE -t $TOPIC_RESULT -q 1 -i "$CLIENT_ID" | while read -r TOPIC MESSAGE; do
    if [[ "$TOPIC" == "$TOPIC_STATE" ]]; then
        parse_state "$MESSAGE"
    elif [[ "$TOPIC" == "$TOPIC_RESULT" ]]; then
        parse_state "$MESSAGE"
    fi
done
