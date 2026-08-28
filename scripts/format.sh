#!/usr/bin/env sh
set -eu

CHECK_ONLY=0
IGNORED_FILES='
src/clog.h
'

usage() {
    cat <<'EOF'
Usage: scripts/format.sh [--check]

Format tracked and untracked C/C++ source files.

Options:
  --check     Check formatting without modifying files
  -h, --help  Show this help message
EOF
}

case "${1:-}" in
    --check)
        CHECK_ONLY=1
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    '')
        ;;
    *)
        echo "unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
esac

if [ "$#" -gt 1 ]; then
    echo "only one option is supported" >&2
    usage >&2
    exit 1
fi

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format not found" >&2
    exit 1
fi

files=$({
    git ls-files
    git ls-files --others --exclude-standard
} | sort -u | grep -E '\.(c|h|cc|hh|cpp|hpp)$' | while IFS= read -r file; do
    if ! printf '%s\n' "$IGNORED_FILES" | grep -Fqx "$file"; then
        printf '%s\n' "$file"
    fi
done)

if [ -z "$files" ]; then
    exit 0
fi

if [ "$CHECK_ONLY" -eq 1 ]; then
    printf '%s\n' "$files" | xargs clang-format --dry-run --Werror --style=file
else
    printf '%s\n' "$files" | xargs clang-format -i --style=file
fi
