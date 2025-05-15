# TicTacToe PowerShell Client
param([string]$Server = "34.168.29.1")

# Configuration
$mosquittoPub = "C:\Program Files\mosquitto\mosquitto_pub.exe"
$mosquittoSub = "C:\Program Files\mosquitto\mosquitto_sub.exe"

# Game variables
$board = @(" ", " ", " ", " ", " ", " ", " ", " ", " ")
$currentPlayer = "X"
$gameMode = 0
$gameOver = $false
$winner = ""

# Display the board and command prompt
function Show-Game {
    Clear-Host
    Write-Host "=== ESP32 TIC-TAC-TOE ===" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  0 1 2"
    Write-Host "0 $($board[0])|$($board[1])|$($board[2])"
    Write-Host "  -+-+-"
    Write-Host "1 $($board[3])|$($board[4])|$($board[5])"
    Write-Host "  -+-+-"
    Write-Host "2 $($board[6])|$($board[7])|$($board[8])"
    Write-Host ""
    Write-Host "Player: $currentPlayer" -ForegroundColor Yellow
    
    if ($gameMode -eq 1) {
        Write-Host "Mode: Human vs Human" -ForegroundColor Green
    } elseif ($gameMode -eq 2) {
        Write-Host "Mode: Human (X) vs AI (O)" -ForegroundColor Green
    } elseif ($gameMode -eq 3) {
        Write-Host "Mode: AI vs AI" -ForegroundColor Green
    }
    
    if ($gameOver) {
        if ($winner -eq "DRAW") {
            Write-Host "Game Over: DRAW!" -ForegroundColor Magenta
        } else {
            Write-Host "Game Over: Player $winner WINS!" -ForegroundColor Magenta
        }
    }
    
    Write-Host ""
    Write-Host "COMMANDS:" -ForegroundColor White
    
    # Show appropriate commands based on mode
    if ($gameMode -eq 3) {
        # AI vs AI mode - only show control commands
        Write-Host "- r = restart game" -ForegroundColor White
        Write-Host "- m = show menu" -ForegroundColor White
        Write-Host "- q = quit" -ForegroundColor White
    } else {
        # Human involved modes - show all commands
        Write-Host "- [0-2],[0-2] = make move (e.g. '1,2')" -ForegroundColor White
        Write-Host "- r = restart game" -ForegroundColor White
        Write-Host "- m = show menu" -ForegroundColor White
        Write-Host "- q = quit" -ForegroundColor White
    }
    
    Write-Host ""
    
    # Show appropriate prompt based on game state and mode
    if ($gameOver) {
        # Game is over - only allow restart/menu/quit
        Write-Host "Enter command (r/m/q): " -NoNewline -ForegroundColor Green
    } elseif ($gameMode -eq 3) {
        # AI vs AI mode - only allow restart/menu/quit and show that it's simulating
        Write-Host "AI vs AI simulation in progress... (r/m/q): " -NoNewline -ForegroundColor Yellow
    } elseif ($gameMode -eq 2 -and $currentPlayer -eq "O") {
        # Human vs AI and it's AI's turn
        Write-Host "Waiting for AI move... (r/m/q): " -NoNewline -ForegroundColor Yellow
    } else {
        # Human's turn - allow regular input
        Write-Host "Enter command: " -NoNewline -ForegroundColor Green
    }
}

# Reset the game
function Reset-Game {
    # Clear MQTT topics
    & $mosquittoPub -h $Server -t "tictactoe/result" -m "" -q 1
    Start-Sleep -Milliseconds 500
    
    # Reset the game on ESP32
    & $mosquittoPub -h $Server -t "tictactoe/command" -m "RESET" -q 1
    Start-Sleep -Seconds 1
    
    # Reset local variables
    $script:board = @(" ", " ", " ", " ", " ", " ", " ", " ", " ")
    $script:gameOver = $false
    $script:winner = ""
    $script:currentPlayer = "X"
}

# Start a new game
function Start-Game($mode) {
    Reset-Game
    $script:gameMode = $mode
    & $mosquittoPub -h $Server -t "tictactoe/command" -m "MODE:$mode" -q 1
    Start-Sleep -Seconds 1

    # Force X's turn for Human vs AI
    if ($mode -eq 2) {
        $script:currentPlayer = "X"
    }

    Show-Game
}

# Show menu
function Show-Menu {
    Clear-Host
    Write-Host "=== SIMPLE TIC-TAC-TOE ===" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "MENU" -ForegroundColor Green
    Write-Host "1. Human vs Human"
    Write-Host "2. Human vs AI"
    Write-Host "3. AI vs AI"
    Write-Host "4. Quit"
    Write-Host ""
    Write-Host "Select option: " -NoNewline
    
    $option = Read-Host
    
    switch ($option) {
        "1" { Start-Game 1 }
        "2" { Start-Game 2 }
        "3" { Start-Game 3 }
        "4" { exit }
        default { Show-Menu }
    }
}

# Process state update
function Update-State($message) {
    if ($message -match "^STATE:(.{9})(.{1})$") {
        $boardStr = $matches[1]
        $script:currentPlayer = $matches[2]
        
        # Update board
        for ($i = 0; $i -lt 9; $i++) {
            $script:board[$i] = $boardStr[$i]
        }
        
        Show-Game
    }
    elseif ($message -match "^WIN:(.+)$") {
        $script:winner = $matches[1]
        $script:gameOver = $true
        Show-Game
    }
    elseif ($message -eq "DRAW") {
        $script:winner = "DRAW"
        $script:gameOver = $true
        Show-Game
    }
}

# Make a move
function Make-Move($row, $col) {
    if ($row -lt 0 -or $row -gt 2 -or $col -lt 0 -or $col -gt 2) {
        Write-Host "Invalid move! Row and column must be between 0-2." -ForegroundColor Red
        Start-Sleep -Seconds 1
        Show-Game
        return
    }
    
    # Calculate board index
    $idx = $row * 3 + $col
    
    # Check if cell is already occupied
    if ($board[$idx] -ne " ") {
        Write-Host "Invalid move! Cell already occupied." -ForegroundColor Red
        Start-Sleep -Seconds 1
        Show-Game
        return
    }
    
    # Send move command
    & $mosquittoPub -h $Server -t "tictactoe/command" -m "MOVE:$row,$col" -q 1
}

# MAIN SCRIPT
try {
    # Check if mosquitto is installed
    if (-not (Test-Path $mosquittoPub) -or -not (Test-Path $mosquittoSub)) {
        Write-Host "Error: Mosquitto client not found at $mosquittoPub" -ForegroundColor Red
        Write-Host "Please install Mosquitto or modify the script to point to the correct location." -ForegroundColor Yellow
        Write-Host "Press any key to exit..."
        $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        exit
    }
    
    # Initial reset
    Reset-Game
    
    # Show menu
    Show-Menu
    
    # Main loop
    $running = $true
    while ($running) {
        # Check for MQTT messages
        $state = & $mosquittoSub -h $Server -t "tictactoe/state" -C 1 -W 1 2>$null
        $result = & $mosquittoSub -h $Server -t "tictactoe/result" -C 1 -W 1 2>$null
        
        if ($state) {
            Update-State $state
        }
        
        if ($result) {
            Update-State $result
        }
        
        # Check if we should allow move inputs
        $allowMoves = !$gameOver -and ($gameMode -eq 1 -or ($gameMode -eq 2 -and $currentPlayer -eq "X"))
        
        # Ensure console buffer is clear before reading
        while ([Console]::KeyAvailable) {
            [Console]::ReadKey($true) | Out-Null
        }
        
        # Get user input with timeout to allow processing MQTT messages
        $input = $null
        if ([Console]::KeyAvailable) {
            $input = Read-Host
        } else {
            Start-Sleep -Milliseconds 500
            # Only prompt for input if we haven't received new state
            if (!$state -and !$result) {
                $input = Read-Host
            }
        }
        
        # Skip empty input
        if ([string]::IsNullOrEmpty($input)) {
            continue
        }
        
        # Process command
        switch ($input) {
            "q" { 
                $running = $false 
            }
            "r" { 
                Reset-Game
                Start-Game $gameMode 
            }
            "m" { 
                Show-Menu 
            }
            default {
                # Check for move command in format row,col
                if ($input -match "^([0-2]),([0-2])$") {
                    if ($allowMoves) {
                        $row = [int]$matches[1]
                        $col = [int]$matches[2]
                        Make-Move -row $row -col $col
                    } else {
                        if ($gameMode -eq 3) {
                            Write-Host "Cannot make moves in AI vs AI mode. Use r/m/q commands only." -ForegroundColor Red
                        } elseif ($gameOver) {
                            Write-Host "Game is over. Press 'r' to restart or 'm' for menu." -ForegroundColor Red
                        } else {
                            Write-Host "Not your turn. Please wait..." -ForegroundColor Red
                        }
                        Start-Sleep -Seconds 1
                        Show-Game
                    }
                } else {
                    # Invalid input
                    if ($gameMode -eq 3) {
                        Write-Host "Invalid command! Use r/m/q only in AI vs AI mode." -ForegroundColor Red
                    } else {
                        Write-Host "Invalid command! Use format 'row,col' (e.g. '1,2') or r/m/q" -ForegroundColor Red
                    }
                    Start-Sleep -Seconds 1
                    Show-Game
                }
            }
        }
    }
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor DarkGray
    Write-Host "Press any key to exit..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}
