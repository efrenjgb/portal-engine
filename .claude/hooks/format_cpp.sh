#!/bin/bash
# PostToolUse hook: clang-format the C/C++ file Claude just wrote/edited.
# Claude Code passes the tool event as JSON on stdin (not as $1).
input=$(cat)
FILE_PATH=$(printf '%s' "$input" | jq -r '.tool_input.file_path // empty')
[ -z "$FILE_PATH" ] && exit 0

# Skip vendored stb headers and anything that isn't a C/C++ source/header.
case "$FILE_PATH" in
    *stb_image*) exit 0 ;;
esac
if [[ "$FILE_PATH" =~ \.(cpp|cc|cxx|h|hpp|hxx)$ ]] && command -v clang-format &>/dev/null; then
    before=$(cat "$FILE_PATH" 2>/dev/null)
    clang-format -i "$FILE_PATH"
    # Only tell Claude if the file actually changed, so it re-reads before editing.
    if [ "$before" != "$(cat "$FILE_PATH" 2>/dev/null)" ]; then
        echo "Formatted $FILE_PATH with clang-format (re-read before further edits)" >&2
        exit 2
    fi
fi
exit 0
