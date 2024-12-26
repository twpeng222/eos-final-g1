#!/bin/bash

# 定義 tmux 會話名稱
SESSION="multi_process"

# 確保舊會話被清理
tmux has-session -t $SESSION 2>/dev/null
if [[ $? -eq 0 ]]; then
    tmux kill-session -t $SESSION
fi

# 創建新會話，並在主窗口執行第一個程序
tmux new-session -d -s $SESSION -n "main"
tmux send-keys -t $SESSION "./HSR_subserver 8881" C-m

# 分裂窗口並執行第二個程序
tmux split-window -h
tmux send-keys -t $SESSION "./train_subserver 8889" C-m

# 分裂窗口（上下分裂）並執行第三個程序
tmux split-window -v
tmux send-keys -t $SESSION "./host_new" C-m

# 切換到第二列，繼續分裂窗口並執行第四個程序
tmux select-pane -t 0
tmux split-window -v -t 1
tmux send-keys -t $SESSION "./host_new_client" C-m

# 調整畫面佈局，讓分裂更加清晰
tmux select-layout tiled

# 附加到會話
tmux -2 attach-session -t $SESSION
