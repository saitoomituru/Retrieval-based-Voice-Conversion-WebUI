#!/bin/zsh
set -e

SCRIPT_DIR=${0:A:h}
cd "$SCRIPT_DIR"

# 明示指定がない限りWebUIをLANへ公開しない。
export RVC_SERVER_HOST=${RVC_SERVER_HOST:-127.0.0.1}

if [[ ! -x .venv/bin/python ]]; then
  print -u2 "RVC WebUIの実行環境がありません: $SCRIPT_DIR/.venv"
  exit 1
fi

exec .venv/bin/python webui.py "$@"
