#!/bin/zsh

pwd=$(cd "$(dirname "$0")" && pwd)

for file in "$pwd"/test_*.log; do
  if [ -f "$file" ]; then
    echo "\n\n\n============================================================================================"
    echo "File: $file"
    cat "$file"
    echo ""
  fi
done