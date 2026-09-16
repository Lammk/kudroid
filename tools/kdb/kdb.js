#!/usr/bin/env node

/**
 * KuDroid Debug Bridge (KDB): dependency-free WebSocket/HTTP host + REPL
 * (help/list/debug/version). Mirrors the iPhone log stream.
 */

const http = require('http');
const readline = require('readline');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const os = require('os');

const PORT = process.env.KDB_PORT || 8080;
const LOGS_DIR = path.join(__dirname, '../../logs');
const fileRegistry = new Map(); // filename -> absolute path

if (!fs.existsSync(LOGS_DIR)) {
    fs.mkdirSync(LOGS_DIR, { recursive: true });
}

function getLocalIpAddress() {
    const interfaces = os.networkInterfaces();
    // Prefer physical NICs: Wi-Fi (wlan, wlp) or Ethernet (eth, en)
    for (const name of Object.keys(interfaces)) {
        if (name.startsWith('w') || name.startsWith('e')) {
            for (const iface of interfaces[name]) {
                if (iface.family === 'IPv4' && !iface.internal) {
                    return iface.address;
                }
            }
        }
    }
    for (const name of Object.keys(interfaces)) {
        for (const iface of interfaces[name]) {
            if (iface.family === 'IPv4' && !iface.internal && !name.startsWith('tun') && !name.startsWith('docker')) {
                return iface.address;
            }
        }
    }
    return '127.0.0.1';
}

// ── COLOR CODES ─────────────────────────────────────────────────────────────
const C = {
    reset: "\x1b[0m",
    bold: "\x1b[1m",
    dim: "\x1b[2m",
    red: "\x1b[31m",
    green: "\x1b[32m",
    yellow: "\x1b[33m",
    blue: "\x1b[34m",
    magenta: "\x1b[35m",
    cyan: "\x1b[36m",
    white: "\x1b[37m",
    gray: "\x1b[90m",
    bgGreen: "\x1b[42m\x1b[30m",
    bgBlue: "\x1b[44m\x1b[37m"
};

// ── STATE ───────────────────────────────────────────────────────────────────
let connectedSocket = null;
let clientDeviceInfo = null;
let isDebugMode = false;
let debugSessionLogs = [];
let debugSessionStart = null;
let pendingCommandResolver = null;

// ── WEBSOCKET PROTOCOL ENCODER / DECODER (ZERO DEPENDENCY) ───────────────────
function decodeWebSocketFrame(buffer) {
    if (buffer.length < 2) return null;
    const firstByte = buffer[0];
    const secondByte = buffer[1];
    const opcode = firstByte & 0x0f;
    let isMasked = (secondByte & 0x80) === 0x80;
    let payloadLength = secondByte & 0x7f;
    let currentOffset = 2;

    if (payloadLength === 126) {
        if (buffer.length < 4) return null;
        payloadLength = buffer.readUInt16BE(2);
        currentOffset += 2;
    } else if (payloadLength === 127) {
        if (buffer.length < 10) return null;
        payloadLength = Number(buffer.readBigUInt64BE(2));
        currentOffset += 8;
    }

    let maskingKey = null;
    if (isMasked) {
        if (buffer.length < currentOffset + 4) return null;
        maskingKey = buffer.slice(currentOffset, currentOffset + 4);
        currentOffset += 4;
    }

    if (buffer.length < currentOffset + payloadLength) return null;
    const payload = buffer.slice(currentOffset, currentOffset + payloadLength);
    if (isMasked && maskingKey) {
        for (let i = 0; i < payload.length; i++) {
            payload[i] ^= maskingKey[i % 4];
        }
    }

    return { opcode, payload, totalLength: currentOffset + payloadLength };
}

function encodeWebSocketFrame(textOrBuffer, opcode = 0x01) {
    const payload = typeof textOrBuffer === 'string' ? Buffer.from(textOrBuffer, 'utf8') : textOrBuffer;
    let header;
    if (payload.length <= 125) {
        header = Buffer.from([0x80 | opcode, payload.length]);
    } else if (payload.length <= 65535) {
        header = Buffer.alloc(4);
        header[0] = 0x80 | opcode;
        header[1] = 126;
        header.writeUInt16BE(payload.length, 2);
    } else {
        header = Buffer.alloc(10);
        header[0] = 0x80 | opcode;
        header[1] = 127;
        header.writeBigUInt64BE(BigInt(payload.length), 2);
    }
    return Buffer.concat([header, payload]);
}

function sendToClient(obj) {
    if (!connectedSocket || connectedSocket.destroyed) return false;
    try {
        const jsonStr = typeof obj === 'string' ? obj : JSON.stringify(obj);
        const frame = encodeWebSocketFrame(jsonStr);
        connectedSocket.write(frame);
        return true;
    } catch (e) {
        return false;
    }
}

// ── HTTP & WS SERVER ────────────────────────────────────────────────────────
const server = http.createServer((req, res) => {
    if (req.url && req.url.startsWith('/files/')) {
        const reqFile = decodeURIComponent(req.url.replace('/files/', ''));
        const localPath = fileRegistry.get(reqFile);
        if (localPath && fs.existsSync(localPath)) {
            const stat = fs.statSync(localPath);
            res.writeHead(200, {
                'Content-Type': 'application/octet-stream',
                'Content-Length': stat.size,
                'Access-Control-Allow-Origin': '*'
            });
            fs.createReadStream(localPath).pipe(res);
            return;
        } else {
            res.writeHead(404);
            res.end("File not found in registry");
            return;
        }
    }

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
        server: "KuDroid Debug Bridge (KDB)",
        version: "1.0.0",
        device: clientDeviceInfo ? clientDeviceInfo : "Not connected"
    }));
});

server.on('upgrade', (req, socket, head) => {
    const key = req.headers['sec-websocket-key'];
    if (!key) {
        socket.destroy();
        return;
    }
    const acceptKey = crypto
        .createHash('sha1')
        .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
        .digest('base64');

    const headers = [
        'HTTP/1.1 101 Switching Protocols',
        'Upgrade: websocket',
        'Connection: Upgrade',
        `Sec-WebSocket-Accept: ${acceptKey}`
    ];
    socket.write(headers.join('\r\n') + '\r\n\r\n');

    connectedSocket = socket;
    let accumulatedBuffer = Buffer.alloc(0);

    socket.on('data', (chunk) => {
        accumulatedBuffer = Buffer.concat([accumulatedBuffer, chunk]);
        while (accumulatedBuffer.length > 0) {
            const frame = decodeWebSocketFrame(accumulatedBuffer);
            if (!frame) break;
            accumulatedBuffer = accumulatedBuffer.slice(frame.totalLength);

            if (frame.opcode === 0x08) { // Close frame
                handleDisconnect();
                break;
            } else if (frame.opcode === 0x09) { // Ping
                socket.write(encodeWebSocketFrame(frame.payload, 0x0A));
            } else if (frame.opcode === 0x01) { // Text frame
                handleIncomingMessage(frame.payload.toString('utf8'));
            }
        }
    });

    socket.on('close', handleDisconnect);
    socket.on('error', handleDisconnect);

    console.log(`\n${C.green}[KDB] iPhone connected via WebSocket.${C.reset}`);
    prompt();
});

function handleDisconnect() {
    if (connectedSocket) {
        connectedSocket = null;
        clientDeviceInfo = null;
        console.log(`\n${C.yellow}[KDB] iPhone disconnected.${C.reset}`);
        prompt();
    }
}

// ── LOG & EVENT PROCESSOR ───────────────────────────────────────────────────
function handleIncomingMessage(text) {
    try {
        const msg = JSON.parse(text);
        if (msg.type === 'handshake') {
            clientDeviceInfo = msg.device;
            clientBuildInfo = msg.buildInfo || null;
            const bInfo = clientBuildInfo ? ` | Build: ${C.yellow}${clientBuildInfo.short_commit || 'unknown'}${C.cyan} (${clientBuildInfo.build_time || 'N/A'})` : '';
            console.log(`\n${C.cyan}[KDB] Device handshake: ${C.bold}${msg.device.name || 'iOS Device'} (iOS ${msg.device.osVersion || 'Unknown'})${bInfo}${C.reset}`);
            prompt();
        } else if (msg.type === 'log') {
            const line = msg.message;
            if (isDebugMode) {
                debugSessionLogs.push(line);
                formatAndPrintLog(msg.level, msg.tag, line);
            }
        } else if (msg.type === 'response') {
            if (pendingCommandResolver) {
                pendingCommandResolver(msg);
                pendingCommandResolver = null;
            }
        }
    } catch (e) {
        // Raw text line
        if (isDebugMode) {
            debugSessionLogs.push(text);
            console.log(`${C.gray}${text}${C.reset}`);
        }
    }
}

function formatAndPrintLog(level, tag, message) {
    let tagColor = C.cyan;
    if (tag && tag.includes("GPU")) tagColor = C.magenta;
    if (tag && tag.includes("Syscall")) tagColor = C.yellow;
    if (tag && tag.includes("Core")) tagColor = C.green;

    let lvlTag = "[INFO]";
    if (level >= 5 || (message && message.includes("ERROR"))) lvlTag = `${C.red}[ERR]${C.reset}`;
    else if (level === 4 || (message && message.includes("WARN"))) lvlTag = `${C.yellow}[WRN]${C.reset}`;
    else lvlTag = `${C.gray}[DBG]${C.reset}`;

    console.log(`${lvlTag} ${tagColor}[${tag || 'KuDroid'}]${C.reset} ${message}`);
}

// ── INTERACTIVE CLI REPL ────────────────────────────────────────────────────
let rl = null;

function setupReadline() {
    if (rl && !rl.closed) return;
    rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout,
        prompt: `${C.bold}${C.green}kudroid>${C.reset} `
    });

    rl.on('line', handleCommandLine);
    rl.on('SIGINT', handleSigInt);
    rl.on('close', () => {
        // Prevent fatal crash on EOF or temporary close
        rl = null;
    });
}

function prompt() {
    if (!isDebugMode) {
        setupReadline();
        if (rl && !rl.closed) {
            try {
                rl.prompt(true);
            } catch (e) {}
        }
    }
}

async function handleCommandLine(line) {
    const raw = line.trim();
    if (!raw) {
        prompt();
        return;
    }
    const parts = raw.split(/\s+/);
    const cmd = parts[0].toLowerCase();
    const args = parts.slice(1);

    switch (cmd) {
        case 'help':
            printHelp();
            break;

        case 'list':
            await handleList();
            break;

        case 'debug':
            startDebugMode();
            return;

        case 'version':
        case 'ver':
            await handleVersion();
            break;

        case 'exit':
        case 'quit':
            console.log(`${C.yellow}Shutting down KDB Server...${C.reset}`);
            process.exit(0);
            break;

        default:
            console.log(`${C.red}Unknown command: '${cmd}'. Type 'help' for available commands.${C.reset}`);
            break;
    }
    prompt();
}

function handleSigInt() {
    if (isDebugMode) {
        stopDebugMode();
    } else {
        console.log(`\n${C.yellow}Use 'exit' or Ctrl+D to quit KDB.${C.reset}`);
        prompt();
    }
}

// ── COMMAND HANDLERS ────────────────────────────────────────────────────────
function printHelp() {
    console.log(`
  ${C.green}help${C.reset}              Show this command list
  ${C.green}version / ver${C.reset}     Check the build commit hash / version of the app on the iPhone
  ${C.green}list${C.reset}              List APKs installed on the iPhone
  ${C.green}debug${C.reset}             Start the all-in-one log stream (${C.yellow}Ctrl+C to stop & save logs${C.reset})
  ${C.green}exit / quit${C.reset}       Exit KDB
`);
}

async function handleVersion() {
    if (!connectedSocket) {
        console.log(`${C.red}No iPhone connected. Connect iPhone to KDB first.${C.reset}`);
        return;
    }

    let localCommit = 'unknown';
    let localShort = 'unknown';
    try {
        const { execSync } = require('child_process');
        localCommit = execSync('git rev-parse HEAD', { encoding: 'utf8' }).trim();
        localShort = execSync('git rev-parse --short HEAD', { encoding: 'utf8' }).trim();
    } catch (e) {}

    console.log(`\n${C.cyan}Querying build version from iPhone...${C.reset}`);
    const res = await sendCommandWithTimeout({ action: 'version' }, 5000);
    const bInfo = (res && res.buildInfo) ? res.buildInfo : (clientBuildInfo || {});
    const devCommit = bInfo.commit || 'unknown';
    const devShort = bInfo.short_commit || (devCommit !== 'unknown' ? devCommit.substring(0, 7) : 'unknown');
    const devTime = bInfo.build_date || bInfo.build_time || 'N/A';

    console.log(`\n${C.bold}KuDroid App Version & Build Status${C.reset}`);
    console.log(`  ${C.bold}iPhone Build Hash${C.reset}  : ${C.yellow}${devShort}${C.reset} (${devCommit})`);
    console.log(`  ${C.bold}Build Timestamp${C.reset}    : ${devTime}`);
    console.log(`  ${C.bold}Local PC Git Hash${C.reset}  : ${C.cyan}${localShort}${C.reset} (${localCommit})\n`);

    if (devCommit !== 'unknown' && localCommit !== 'unknown') {
        if (devCommit === localCommit || devShort === localShort) {
            console.log(`  ${C.green}PERFECT MATCH: iPhone is running the exact latest build.${C.reset}\n`);
        } else {
            console.log(`  ${C.yellow}MISMATCH: iPhone build (${devShort}) differs from local commit (${localShort}).${C.reset}\n`);
        }
    }
}

async function handleList() {
    if (!connectedSocket) {
        console.log(`${C.red}No iPhone connected. Connect iPhone to KDB first.${C.reset}`);
        return;
    }
    console.log(`${C.gray}Fetching installed apps from iPhone...${C.reset}`);
    const res = await sendCommandWithTimeout({ action: 'list' }, 5000);
    if (res && res.apps) {
        if (res.apps.length === 0) {
            console.log(`${C.yellow}No Android apps installed on device.${C.reset}`);
        } else {
            console.log(`\n${C.bold}Installed Android Apps (${res.apps.length}):${C.reset}`);
            res.apps.forEach((app, idx) => {
                console.log(`  [${C.green}${idx + 1}${C.reset}] ${C.bold}${app.displayName || app.id}${C.reset} (${C.cyan}${app.id}${C.reset}) - v${app.version || '1.0.0'}`);
            });
            console.log();
        }
    } else {
        console.log(`${C.red}Failed to retrieve apps list (Timeout or error).${C.reset}`);
    }
}

function startDebugMode() {
    isDebugMode = true;
    debugSessionLogs = [];
    debugSessionStart = new Date();
    console.log(`${C.bgBlue} KDB ALL-IN-ONE DEBUG STREAM ACTIVE (Press Ctrl+C to stop & auto-save) ${C.reset}\n`);
}

function stopDebugMode() {
    isDebugMode = false;
    console.log(`\n${C.yellow}Debug stream stopped.${C.reset}`);

    if (debugSessionLogs.length > 0) {
        const nowStr = new Date().toISOString().replace(/[:.]/g, '-');
        const filename = `debug_${nowStr}.log`;
        const targetPath = path.join(LOGS_DIR, filename);
        fs.writeFileSync(targetPath, debugSessionLogs.join('\n'), 'utf8');
        console.log(`${C.green}Session log saved (${debugSessionLogs.length} lines): ${C.bold}${targetPath}${C.reset}\n`);
    } else {
        console.log(`${C.gray}No logs recorded in this session.${C.reset}\n`);
    }
    prompt();
}

function sendCommandWithTimeout(payload, timeoutMs = 5000) {
    return new Promise((resolve) => {
        pendingCommandResolver = resolve;
        const success = sendToClient(payload);
        if (!success) {
            pendingCommandResolver = null;
            resolve(null);
            return;
        }
        setTimeout(() => {
            if (pendingCommandResolver === resolve) {
                pendingCommandResolver = null;
                resolve(null);
            }
        }, timeoutMs);
    });
}

// ── START SERVER ────────────────────────────────────────────────────────────
server.listen(PORT, '0.0.0.0', () => {
    console.clear();
    console.log(`\n${C.bold}${C.green}${getLocalIpAddress()}:${PORT}${C.reset}\n`);
    console.log(`${C.cyan}Type ${C.bold}'help'${C.reset}${C.cyan} for command list. Waiting for iPhone connection...${C.reset}\n`);
    prompt();
});
