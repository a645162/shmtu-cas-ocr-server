#!/usr/bin/env bash
# ===========================================================================
# SHMTU CAS OCR Server Comparison Test
#
# Starts the C++ OCR server, runs local OCR and remote API calls on a set of
# test images, then compares results and outputs statistics.
#
# Usage:
#   ./ocr_compare_test.sh [OPTIONS]
#
# Options:
#   --images <dir>        Directory containing test images (default: ./test_images)
#   --server-bin <path>   Path to shmtu_cas_ocr_server binary
#   --cli-bin <path>      Path to shmtu_cas_ocr_cli binary
#   --model-dir <path>    Model directory
#   --host <addr>         Server bind address (default: 127.0.0.1)
#   --http-port <port>    HTTP port (default: 21600)
#   --tcp-port <port>     TCP port (default: 21601)
#   --output <file>       Write JSON results to file
#   --stop-only           Stop a previously started server and exit
#   -h, --help            Show this help
# ===========================================================================

set -euo pipefail

# ---- Defaults ----
IMAGES_DIR="./test_images"
SERVER_BIN=""
CLI_BIN=""
MODEL_DIR="./models"
HOST="127.0.0.1"
HTTP_PORT=21600
TCP_PORT=21601
OUTPUT_FILE=""
STOP_ONLY=false

# ---- Colors (if terminal) ----
if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' NC=''
fi

# ---- Parse arguments ----
while [[ $# -gt 0 ]]; do
    case "$1" in
        --images)       IMAGES_DIR="$2"; shift 2 ;;
        --server-bin)   SERVER_BIN="$2"; shift 2 ;;
        --cli-bin)      CLI_BIN="$2"; shift 2 ;;
        --model-dir)    MODEL_DIR="$2"; shift 2 ;;
        --host)         HOST="$2"; shift 2 ;;
        --http-port)    HTTP_PORT="$2"; shift 2 ;;
        --tcp-port)     TCP_PORT="$2"; shift 2 ;;
        --output)       OUTPUT_FILE="$2"; shift 2 ;;
        --stop-only)    STOP_ONLY=true; shift ;;
        -h|--help)
            head -25 "$0" | tail -20
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ---- Resolve binary paths ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "$SERVER_BIN" ]]; then
    # Try common locations relative to script
    for candidate in \
        "$SCRIPT_DIR/build/ocr/shmtu-cas-ocr-server/shmtu_cas_ocr_server" \
        "$SCRIPT_DIR/../build/ocr/shmtu-cas-ocr-server/shmtu_cas_ocr_server" \
        "$(command -v shmtu_cas_ocr_server 2>/dev/null)"; do
        if [[ -x "$candidate" ]]; then
            SERVER_BIN="$candidate"
            break
        fi
    done
fi

if [[ -z "$CLI_BIN" ]]; then
    for candidate in \
        "$SCRIPT_DIR/build/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli" \
        "$SCRIPT_DIR/../build/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli" \
        "$(command -v shmtu_cas_ocr_cli 2>/dev/null)"; do
        if [[ -x "$candidate" ]]; then
            CLI_BIN="$candidate"
            break
        fi
    done
fi

# ---- Stop-only mode ----
SERVER_PID_FILE="/tmp/shmtu_ocr_server_${HTTP_PORT}.pid"
if [[ "$STOP_ONLY" == true ]]; then
    if [[ -f "$SERVER_PID_FILE" ]]; then
        PID=$(cat "$SERVER_PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo "Stopping server (PID $PID)..."
            kill "$PID"
            rm -f "$SERVER_PID_FILE"
            echo "Server stopped."
        else
            echo "Server process $PID not running. Cleaning up PID file."
            rm -f "$SERVER_PID_FILE"
        fi
    else
        echo "No PID file found at $SERVER_PID_FILE"
    fi
    exit 0
fi

# ---- Validate ----
if [[ -z "$SERVER_BIN" ]]; then
    echo -e "${RED}Error: Cannot find shmtu_cas_ocr_server binary.${NC}"
    echo "Use --server-bin to specify the path."
    exit 1
fi

if [[ -z "$CLI_BIN" ]]; then
    echo -e "${RED}Error: Cannot find shmtu_cas_ocr_cli binary.${NC}"
    echo "Use --cli-bin to specify the path."
    exit 1
fi

if [[ ! -d "$IMAGES_DIR" ]]; then
    echo -e "${RED}Error: Test images directory not found: $IMAGES_DIR${NC}"
    echo "Create a directory with CAPTCHA images for testing."
    exit 1
fi

# Count test images
IMAGE_COUNT=$(find "$IMAGES_DIR" -maxdepth 1 -type f \( -name "*.png" -o -name "*.jpg" -o -name "*.jpeg" -o -name "*.bmp" \) | wc -l)
if [[ "$IMAGE_COUNT" -eq 0 ]]; then
    echo -e "${RED}Error: No image files found in $IMAGES_DIR${NC}"
    exit 1
fi

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}SHMTU CAS OCR Server Comparison Test${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo "Server binary:   $SERVER_BIN"
echo "CLI binary:      $CLI_BIN"
echo "Model directory: $MODEL_DIR"
echo "Test images:     $IMAGES_DIR ($IMAGE_COUNT files)"
echo "Server address:  $HOST:$HTTP_PORT (HTTP), $HOST:$TCP_PORT (TCP)"
echo ""

# ---- Step 1: Start the OCR server ----
echo -e "${YELLOW}[1/5] Starting OCR server...${NC}"

# Check if server is already running on this port
if curl -s "http://$HOST:$HTTP_PORT/api/health" > /dev/null 2>&1; then
    echo "Server already running on $HOST:$HTTP_PORT."
    if [[ -f "$SERVER_PID_FILE" ]]; then
        echo "PID file exists: $(cat "$SERVER_PID_FILE")"
    fi
else
    # Start server in background
    "$SERVER_BIN" \
        --model-dir "$MODEL_DIR" \
        --http-port "$HTTP_PORT" \
        --tcp-port "$TCP_PORT" \
        &

    SERVER_PID=$!
    echo "$SERVER_PID" > "$SERVER_PID_FILE"
    echo "Server started (PID $SERVER_PID)."

    # Wait for server to be ready
    echo -n "Waiting for server to become ready"
    MAX_WAIT=60
    WAITED=0
    while ! curl -s "http://$HOST:$HTTP_PORT/api/health" > /dev/null 2>&1; do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo ""
            echo -e "${RED}Server process died unexpectedly.${NC}"
            rm -f "$SERVER_PID_FILE"
            exit 1
        fi
        sleep 1
        WAITED=$((WAITED + 1))
        if [[ $WAITED -ge $MAX_WAIT ]]; then
            echo ""
            echo -e "${RED}Server did not become ready within ${MAX_WAIT}s.${NC}"
            kill "$SERVER_PID" 2>/dev/null || true
            rm -f "$SERVER_PID_FILE"
            exit 1
        fi
        echo -n "."
    done
    echo " ready!"
fi

# Show server health
echo ""
echo "Server health:"
curl -s "http://$HOST:$HTTP_PORT/api/health" | python3 -m json.tool 2>/dev/null || \
    curl -s "http://$HOST:$HTTP_PORT/api/health"
echo ""

# ---- Step 2: Run local OCR ----
echo ""
echo -e "${YELLOW}[2/5] Running local OCR on test images...${NC}"
LOCAL_JSON=$("$CLI_BIN" --model-dir "$MODEL_DIR" --json "$IMAGES_DIR" 2>/tmp/ocr_local_stderr.log)
LOCAL_ERR=$?
if [[ $LOCAL_ERR -ne 0 ]]; then
    echo -e "${RED}Local OCR failed (exit code $LOCAL_ERR).${NC}"
    cat /tmp/ocr_local_stderr.log
fi

# ---- Step 3: Run remote OCR via server API ----
echo -e "${YELLOW}[3/5] Running remote OCR via server API...${NC}"
REMOTE_JSON=$("$CLI_BIN" --server "$HOST:$HTTP_PORT" --json "$IMAGES_DIR" 2>/tmp/ocr_remote_stderr.log)
REMOTE_ERR=$?
if [[ $REMOTE_ERR -ne 0 ]]; then
    echo -e "${RED}Remote OCR failed (exit code $REMOTE_ERR).${NC}"
    cat /tmp/ocr_remote_stderr.log
fi

# ---- Step 4: Run compare mode ----
echo ""
echo -e "${YELLOW}[4/5] Running comparison (local vs remote)...${NC}"
COMPARE_OUTPUT=$("$CLI_BIN" --model-dir "$MODEL_DIR" --server "$HOST:$HTTP_PORT" --compare "$IMAGES_DIR" 2>&1)
echo "$COMPARE_OUTPUT"

# ---- Step 5: Parse and summarize ----
echo ""
echo -e "${YELLOW}[5/5] Generating summary...${NC}"

# Extract comparison statistics from compare output
CONSISTENCY=$(echo "$COMPARE_OUTPUT" | grep "Consistency rate:" || echo "N/A")
MATCHING=$(echo "$COMPARE_OUTPUT" | grep "Matching:" || echo "N/A")
DIFFERING=$(echo "$COMPARE_OUTPUT" | grep "Differing:" || echo "N/A")

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Test Results Summary${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Test images:   $IMAGE_COUNT"
echo "$CONSISTENCY"
echo "$MATCHING"
echo "$DIFFERING"
echo ""

# ---- Optional: save JSON output ----
if [[ -n "$OUTPUT_FILE" ]]; then
    cat > "$OUTPUT_FILE" <<EOF
{
  "timestamp": "$(date -Iseconds)",
  "imageCount": $IMAGE_COUNT,
  "imagesDir": "$IMAGES_DIR",
  "serverAddress": "$HOST:$HTTP_PORT",
  "localResults": $LOCAL_JSON,
  "remoteResults": $REMOTE_JSON,
  "comparisonOutput": $(printf '%s' "$COMPARE_OUTPUT" | python3 -c 'import sys,json; print(json.dumps(sys.stdin.read()))')
}
EOF
    echo "Full results saved to: $OUTPUT_FILE"
fi

# ---- Cleanup ----
echo ""
read -p "Stop the OCR server? [Y/n] " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    if [[ -f "$SERVER_PID_FILE" ]]; then
        PID=$(cat "$SERVER_PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            kill "$PID"
            echo "Server stopped (PID $PID)."
        fi
        rm -f "$SERVER_PID_FILE"
    fi
else
    echo "Server still running (PID file: $SERVER_PID_FILE)."
    echo "Stop later with: $0 --stop-only"
fi

echo ""
echo -e "${GREEN}Done.${NC}"
