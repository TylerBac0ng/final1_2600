#include <WiFi.h>
#include <PubSubClient.h>

// WiFi and MQTT settings
const char* ssid = "iPhone";
const char* password = "ezdubsWW";
const char* mqtt_server = "34.168.29.1";
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_TicTacToe";
const char* mqtt_topic_command = "tictactoe/command";
const char* mqtt_topic_state = "tictactoe/state";
const char* mqtt_topic_result = "tictactoe/result";

// Game state - use a flat 1D array for the board
char board[9];
char currentPlayer = 'X';
bool gameOver = false;
int gameMode = 0;

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Function declarations
void initBoard();
void connectMQTT();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void processCommand(const char* cmd);
void makeMove(int row, int col);
void makeRandomMove();
void sendGameState();
bool checkWin();
bool isBoardFull();
void printBoardDebug();

void setup() {
  Serial.begin(115200);
  
  // Print a clear separator
  Serial.println("\n\n==================================");
  Serial.println("  ESP32 TICTACTOE - FIXED VERSION");
  Serial.println("==================================\n");
  
  // Initialize with an empty board
  initBoard();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  
  // Setup MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqtt_callback);
  
  // Connect to MQTT
  connectMQTT();
  
  // Clear any existing game state
  mqttClient.publish(mqtt_topic_result, "");
  delay(100);
  
  // Send a reset command to clear any previous state
  mqttClient.publish(mqtt_topic_command, "RESET");
  delay(500);
  
  // Send initial game state
  sendGameState();
  
  // Print debug info
  Serial.println("Initial game state:");
  printBoardDebug();
}

void loop() {
  // Maintain MQTT connection
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();
  
  // Check for Serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    processCommand(cmd.c_str());
  }
  
  // Handle AI moves in modes 2 and 3
  static unsigned long lastAIMoveTime = 0;
  unsigned long currentTime = millis();
  
  if (!gameOver && (currentTime - lastAIMoveTime > 3000)) {
    if ((gameMode == 2 && currentPlayer == 'O') || 
        (gameMode == 3)) {
      lastAIMoveTime = currentTime;
      makeRandomMove();
    }
  }
  
  delay(10);
}

void initBoard() {
  // Initialize to empty board
  for (int i = 0; i < 9; i++) {
    board[i] = ' ';  // Important: use space character, not empty or null
  }
  currentPlayer = 'X';
  gameOver = false;
  
  Serial.println("Board initialized with empty spaces");
}

void connectMQTT() {
  Serial.print("Connecting to MQTT broker...");
  
  int retryCount = 0;
  while (!mqttClient.connected() && retryCount < 10) {
    if (mqttClient.connect(mqtt_client_id)) {
      Serial.println("connected");
      mqttClient.subscribe(mqtt_topic_command);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying...");
      delay(1000);
      retryCount++;
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // Convert payload to string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.print("MQTT received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // Process command
  processCommand(message);
}

void processCommand(const char* cmd) {
  Serial.print("Processing command: ");
  Serial.println(cmd);
  
  if (strncmp(cmd, "RESET", 5) == 0) {
    Serial.println("RESET command received");
    
    // Clear the board
    initBoard();
    
    // Clear any existing result
    mqttClient.publish(mqtt_topic_result, "");
    delay(100);
    
    // Send updated state
    sendGameState();
  }
  else if (strncmp(cmd, "MODE:", 5) == 0) {
    int newMode = cmd[5] - '0';
    
    Serial.print("MODE command received: ");
    Serial.println(newMode);
    
    // Only accept valid modes (1, 2, 3)
    if (newMode >= 1 && newMode <= 3) {
      gameMode = newMode;
      
      // Reset the game
      initBoard();
      
      // Clear any existing result
      mqttClient.publish(mqtt_topic_result, "");
      delay(100);
      
      // Send updated state
      sendGameState();
    } else {
      Serial.println("Invalid game mode");
    }
  }
  else if (strncmp(cmd, "MOVE:", 5) == 0) {
    int row, col;
    
    // Parse the move coordinates
    if (sscanf(cmd + 5, "%d,%d", &row, &col) == 2) {
      Serial.print("MOVE command received: ");
      Serial.print(row);
      Serial.print(",");
      Serial.println(col);
      
      // Validate and make the move
      if (row >= 0 && row < 3 && col >= 0 && col < 3) {
        makeMove(row, col);
      } else {
        Serial.println("Invalid move coordinates");
      }
    } else {
      Serial.println("Invalid move format");
    }
  }
  else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
  }
}

void makeMove(int row, int col) {
  int index = row * 3 + col;
  
  // Validate the move
  if (index < 0 || index >= 9) {
    Serial.println("Move index out of bounds");
    return;
  }
  
  if (board[index] != ' ') {
    Serial.println("Cell already occupied");
    return;
  }
  
  if (gameOver) {
    Serial.println("Game is already over");
    return;
  }
  
  // Make the move
  Serial.print("Making move at row=");
  Serial.print(row);
  Serial.print(", col=");
  Serial.println(col);
  
  board[index] = currentPlayer;
  
  // Print board after move
  printBoardDebug();
  
  // Check for win AFTER the move is made
  if (checkWin()) {
    Serial.print("Player ");
    Serial.print(currentPlayer);
    Serial.println(" wins!");
    
    gameOver = true;
    
    // Send the win result
    char result[10];
    sprintf(result, "WIN:%c", currentPlayer);
    mqttClient.publish(mqtt_topic_result, result);
  }
  // Check for draw if no win
  else if (isBoardFull()) {
    Serial.println("Board is full - Draw!");
    
    gameOver = true;
    
    // Send the draw result
    mqttClient.publish(mqtt_topic_result, "DRAW");
  }
  // No win or draw, switch players
  else {
    // Switch to the other player
    currentPlayer = (currentPlayer == 'X') ? 'O' : 'X';
  }
  
  // Send updated state
  sendGameState();
}

void makeRandomMove() {
  if (gameOver) return;
  
  // Only act in the right game mode and for the right player
  if ((gameMode == 2 && currentPlayer != 'O') ||
      (gameMode == 3 && !(currentPlayer == 'X' || currentPlayer == 'O'))) {
    return;
  }
  
  Serial.print("AI making move for player ");
  Serial.println(currentPlayer);
  
  // Find empty cells
  int emptyCells[9];
  int count = 0;
  
  for (int i = 0; i < 9; i++) {
    if (board[i] == ' ') {
      emptyCells[count++] = i;
    }
  }
  
  // If there are empty cells, choose one randomly
  if (count > 0) {
    int randomIndex = random(count);
    int cellIndex = emptyCells[randomIndex];
    
    // Convert to row, col
    int row = cellIndex / 3;
    int col = cellIndex % 3;
    
    // Make the move
    makeMove(row, col);
  }
}

void sendGameState() {
  // Format: STATE:[board][currentPlayer]
  char state[20];
  char boardStr[10];
  
  // Convert board to string
  for (int i = 0; i < 9; i++) {
    boardStr[i] = board[i];
  }
  boardStr[9] = '\0';
  
  // Create the state message
  sprintf(state, "STATE:%s%c", boardStr, currentPlayer);
  
  // Send it
  mqttClient.publish(mqtt_topic_state, state);
  
  Serial.print("Sent state: ");
  Serial.println(state);
}

// Check if current player has won - FIXED VERSION
bool checkWin() {
  Serial.println("Checking for win condition...");
  
  // Debug - print raw board
  for (int i = 0; i < 9; i++) {
    Serial.print("[");
    Serial.print(board[i]);
    Serial.print("]");
    if ((i + 1) % 3 == 0) Serial.println();
  }
  
  // Check rows
  for (int i = 0; i < 3; i++) {
    int rowStart = i * 3;
    // Must check that cells are not empty AND all the same
    if (board[rowStart] != ' ' && 
        board[rowStart] == board[rowStart+1] && 
        board[rowStart+1] == board[rowStart+2]) {
      Serial.print("Win found in row ");
      Serial.println(i);
      return true;
    }
  }
  
  // Check columns
  for (int i = 0; i < 3; i++) {
    // Must check that cells are not empty AND all the same
    if (board[i] != ' ' && 
        board[i] == board[i+3] && 
        board[i+3] == board[i+6]) {
      Serial.print("Win found in column ");
      Serial.println(i);
      return true;
    }
  }
  
  // Check diagonal (top-left to bottom-right)
  if (board[0] != ' ' && 
      board[0] == board[4] && 
      board[4] == board[8]) {
    Serial.println("Win found in diagonal \\");
    return true;
  }
  
  // Check diagonal (top-right to bottom-left)
  if (board[2] != ' ' && 
      board[2] == board[4] && 
      board[4] == board[6]) {
    Serial.println("Win found in diagonal /");
    return true;
  }
  
  // No win found
  Serial.println("No win condition detected");
  return false;
}

bool isBoardFull() {
  for (int i = 0; i < 9; i++) {
    if (board[i] == ' ') {
      return false; // Found at least one empty cell
    }
  }
  return true; // All cells are filled
}

void printBoardDebug() {
  Serial.println("\nCurrent board state:");
  
  for (int i = 0; i < 3; i++) {
    Serial.print(" ");
    for (int j = 0; j < 3; j++) {
      char cell = board[i*3 + j];
      if (cell == ' ') {
        Serial.print(".");  // Use dots for empty cells in debug output
      } else {
        Serial.print(cell);
      }
      
      if (j < 2) Serial.print(" | ");
    }
    Serial.println();
    
    if (i < 2) Serial.println(" -----------");
  }
  
  Serial.print("Current player: ");
  Serial.println(currentPlayer);
  Serial.print("Game mode: ");
  Serial.println(gameMode);
  Serial.print("Game over: ");
  Serial.println(gameOver ? "YES" : "NO");
  Serial.println();
}
