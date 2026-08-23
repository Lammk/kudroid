#!/usr/bin/env node

/**
 * KuDroid Debug Bridge (KDB) - Host Server & Interactive Terminal CLI
 * 
 * Functions:
 * 1. Lightweight WebSocket & HTTP server on port 8080 (0 dependencies, pure Node.js).
 * 2. Interactive REPL terminal with commands: help, list, run, stop, install, debug, save, clear.
 * 3. Single Source of Truth: Log stream mirrored byte-for-byte from iPhone.
 * 4. Auto-save session log upon Ctrl+C in debug mode.
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
    // Ưu tiên card mạng vật lý: Wi-Fi (wlan, wlp) hoặc Ethernet (eth, en)
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

    console.log(`\n${C.green}✔ [KDB] iPhone connected via WebSocket!${C.reset}`);
    prompt();
});

function handleDisconnect() {
    if (connectedSocket) {
        connectedSocket = null;
        clientDeviceInfo = null;
        console.log(`\n${C.yellow}⚠ [KDB] iPhone disconnected.${C.reset}`);
        prompt();
    }
}

let isExecutingSo = false;

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
            if (isDebugMode || isExecutingSo) {
                if (isDebugMode) debugSessionLogs.push(line);
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

        case 'run':
            if (args.length === 0) {
                console.log(`${C.red}Usage: run <app_id>${C.reset}`);
            } else {
                await handleRun(args[0]);
            }
            break;

        case 'stop':
            await handleStop();
            break;

        case 'install':
            if (args.length === 0) {
                console.log(`${C.red}Usage: install <path_to_apk_on_pc>${C.reset}`);
            } else {
                await handleInstall(args[0]);
            }
            break;

        case 'debug':
            startDebugMode();
            return;

        case 'test':
            await handleTest(args[0]);
            break;

        case 'so':
        case 'run_so':
            await handleRunSo(args);
            break;

        case 'version':
        case 'ver':
            await handleVersion();
            break;

        case 'dump':
            await handleDump(args[0] || 'kudroid_crash');
            break;

        case 'save':
            handleSave(args[0]);
            break;

        case 'clear':
            handleClear(args);
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
${C.bold}${C.cyan}=== KuDroid Debug Bridge (KDB) Commands ===${C.reset}

  ${C.green}help${C.reset}                     Hiển thị bảng hướng dẫn này
  ${C.green}version / ver${C.reset}            Kiểm tra hash commit / phiên bản build của app trên iPhone
  ${C.green}so <path_to_so> [entry]${C.reset}  🚀 Gửi và chạy trực tiếp file .so test từ PC sang iPhone (${C.yellow}Hot-Reload, không cần cài lại IPA!${C.reset})
  ${C.green}test [name]${C.reset}              Chạy trực tiếp các bài test cô lập có sẵn trên iPhone
                           (ví dụ: ${C.yellow}test gpu${C.reset}, ${C.yellow}test audio${C.reset}, ${C.yellow}test bionic${C.reset}, ${C.yellow}test jni${C.reset}, ${C.yellow}test syscall${C.reset}, ${C.yellow}test vfs${C.reset}, ${C.yellow}test all${C.reset})
  ${C.green}list${C.reset}                     Liệt kê danh sách APK đã cài đặt trên iPhone
  ${C.green}run <app_id>${C.reset}             Mở app trực tiếp trên màn hình iPhone & stream log
  ${C.green}stop${C.reset}                     Đóng app đang chạy, quay về màn hình Launcher
  ${C.green}install <file.apk>${C.reset}       Gửi file APK từ PC sang iPhone và cài đặt
  ${C.green}debug${C.reset}                    Bật chế độ nghe log 'thập cẩm' All-in-One (${C.yellow}Ctrl+C để thoát & lưu log${C.reset})
  ${C.green}dump [file_name]${C.reset}         Kéo trực tiếp file log gốc từ Documents iPhone về PC (vd: dump kudroid_crash, dump test_gpu, dump stderr)
  ${C.green}save [crash|log]${C.reset}         Lưu dump log hoặc crash snapshot về máy tính
  ${C.green}clear <cache|all>${C.reset}        Xóa bộ nhớ đệm / dalvik-cache trên iPhone
  ${C.green}exit / quit${C.reset}              Thoát KDB
`);
}

async function handleVersion() {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected. Connect iPhone to KDB first.${C.reset}`);
        return;
    }

    let localCommit = 'unknown';
    let localShort = 'unknown';
    try {
        const { execSync } = require('child_process');
        localCommit = execSync('git rev-parse HEAD', { encoding: 'utf8' }).trim();
        localShort = execSync('git rev-parse --short HEAD', { encoding: 'utf8' }).trim();
    } catch (e) {}

    console.log(`\n${C.cyan}🔍 Querying build version from iPhone...${C.reset}`);
    const res = await sendCommandWithTimeout({ action: 'version' }, 5000);
    const bInfo = (res && res.buildInfo) ? res.buildInfo : (clientBuildInfo || {});
    const devCommit = bInfo.commit || 'unknown';
    const devShort = bInfo.short_commit || (devCommit !== 'unknown' ? devCommit.substring(0, 7) : 'unknown');
    const devTime = bInfo.build_date || bInfo.build_time || 'N/A';

    console.log(`\n${C.bold}=== KuDroid App Version & Build Status ===${C.reset}`);
    console.log(`  📱 ${C.bold}iPhone Build Hash${C.reset}  : ${C.yellow}${devShort}${C.reset} (${devCommit})`);
    console.log(`  🕒 ${C.bold}Build Timestamp${C.reset}    : ${devTime}`);
    console.log(`  💻 ${C.bold}Local PC Git Hash${C.reset}  : ${C.cyan}${localShort}${C.reset} (${localCommit})\n`);

    if (devCommit !== 'unknown' && localCommit !== 'unknown') {
        if (devCommit === localCommit || devShort === localShort) {
            console.log(`  ${C.green}✔ PERFECT MATCH: iPhone is running the exact latest build!${C.reset}\n`);
        } else {
            console.log(`  ${C.yellow}⚠ MISMATCH: iPhone build (${devShort}) differs from local commit (${localShort}).${C.reset}\n`);
        }
    }
}

async function handleRunSo(args) {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected. Connect iPhone to KDB first.${C.reset}`);
        return;
    }
    if (!args || args.length === 0) {
        console.log(`${C.yellow}Usage: so <path_to_so> [dep2.so ...] [entrypoint] [--then <next_so_group> ...]${C.reset}`);
        return;
    }

    // Tách các stage theo cờ --then hoặc &&
    const stages = [];
    let currentStage = [];
    for (const arg of args) {
        if (arg === '--then' || arg === '&&') {
            if (currentStage.length > 0) {
                stages.push(currentStage);
                currentStage = [];
            }
        } else {
            currentStage.push(arg);
        }
    }
    if (currentStage.length > 0) {
        stages.push(currentStage);
    }

    for (let s = 0; s < stages.length; ++s) {
        const stageArgs = stages[s];
        if (stages.length > 1) {
            console.log(`\n${C.magenta}═════════════════════════════════════════════════════${C.reset}`);
            console.log(`${C.bold}${C.magenta}▶ STAGE [${s + 1}/${stages.length}]: so ${stageArgs.join(' ')}${C.reset}`);
            console.log(`${C.magenta}═════════════════════════════════════════════════════${C.reset}`);
        }
        await executeSingleSoStage(stageArgs);
    }
}

async function uploadAndRunSoChunked(filename, fileData, entrypoint) {
    const CHUNK_SIZE = 32 * 1024; // 32KB per chunk
    const totalChunks = Math.ceil(fileData.length / CHUNK_SIZE);
    const sizeKb = (fileData.length / 1024).toFixed(2);

    console.log(`\n${C.cyan}🚀 Uploading '${filename}' (${sizeKb} KB) in ${totalChunks} chunks to iPhone...${C.reset}`);

    let lastRes = null;
    for (let c = 0; c < totalChunks; ++c) {
        const start = c * CHUNK_SIZE;
        const end = Math.min(start + CHUNK_SIZE, fileData.length);
        const chunkBuf = fileData.slice(start, end);
        const isLast = (c === totalChunks - 1);

        const payload = {
            action: 'run_so_chunk',
            filename: filename,
            chunkIndex: c,
            totalChunks: totalChunks,
            dataBase64: chunkBuf.toString('base64'),
            entrypoint: isLast ? entrypoint : '__none__'
        };

        const timeoutMs = isLast ? 30000 : 10000;
        lastRes = await sendCommandWithTimeout(payload, timeoutMs);
        if (!lastRes || !lastRes.success) {
            console.log(`\n${C.red}❌ Chunk upload failed at [${c + 1}/${totalChunks}]: ${lastRes ? lastRes.error : 'timeout'}${C.reset}`);
            return lastRes;
        }

        if (!isLast) {
            console.log(`${C.dim}   Uploading progress: [${c + 1}/${totalChunks}] ${(((c + 1)/totalChunks)*100).toFixed(0)}%${C.reset}`);
        } else {
            console.log(`${C.dim}   Uploading progress: [${c + 1}/${totalChunks}] 100% -> 🚀 Executing on iPhone...${C.reset}`);
        }
    }
    return lastRes;
}

async function executeSingleSoStage(args) {
    const soFiles = [];
    let entrypoint = null;

    for (const arg of args) {
        if (arg.endsWith('.so') || fs.existsSync(path.resolve(process.cwd(), arg))) {
            soFiles.push(arg);
        } else {
            entrypoint = arg;
        }
    }

    if (soFiles.length === 0) {
        console.log(`${C.red}❌ No valid .so files specified in stage${C.reset}`);
        return;
    }

    // Upload các dependency trước (nếu có)
    for (let i = 0; i < soFiles.length - 1; ++i) {
        const depPath = path.resolve(process.cwd(), soFiles[i]);
        if (!fs.existsSync(depPath)) {
            console.log(`${C.red}❌ Dependency not found on PC: ${depPath}${C.reset}`);
            return;
        }
        const depName = path.basename(depPath);
        const depData = fs.readFileSync(depPath);
        console.log(`${C.cyan}📦 Uploading dependency [${i+1}/${soFiles.length-1}]: '${depName}' (${(depData.length/1024).toFixed(2)} KB)...${C.reset}`);
        await uploadAndRunSoChunked(depName, depData, '__none__');
    }

    // Upload và thực thi file target chính (file cuối cùng)
    const targetFile = soFiles[soFiles.length - 1];
    const resolvedPath = path.resolve(process.cwd(), targetFile);
    if (!fs.existsSync(resolvedPath)) {
        console.log(`${C.red}❌ Target file not found on PC: ${resolvedPath}${C.reset}`);
        return;
    }

    const filename = path.basename(resolvedPath);
    const fileData = fs.readFileSync(resolvedPath);

    isExecutingSo = true;
    let res = null;
    try {
        res = await uploadAndRunSoChunked(filename, fileData, entrypoint);
    } finally {
        isExecutingSo = false;
    }

    if (res && res.log) {
        const statusTag = res.success ? `${C.green}✔ TEST PASSED${C.reset}` : `${C.red}❌ TEST FAILED / RUNTIME ERROR${C.reset}`;
        console.log(`\n=== REMOTE SO EXECUTION [${C.bold}${filename}${C.reset}]: ${statusTag} ===\n`);
        console.log(res.log);

        // 1. Lưu file log riêng biệt theo tên file .so
        const perSoLogName = `${filename.replace('.so', '')}.log`;
        const perSoPath = path.join(LOGS_DIR, perSoLogName);
        fs.writeFileSync(perSoPath, res.log, 'utf8');

        // 2. Đồng thời gom và nối dồn vào file tổng logs/test_so.log
        const collectivePath = path.join(LOGS_DIR, 'test_so.log');
        const header = `\n═══════════════════════════════════════════════════════════════════\n` +
                       `[${new Date().toISOString()}] EXECUTION: ${filename} (Status: ${res.success ? 'PASSED' : 'FAILED'})\n` +
                       `═══════════════════════════════════════════════════════════════════\n`;
        fs.appendFileSync(collectivePath, header + res.log + '\n', 'utf8');

        console.log(`\n${C.green}💾 Log saved to: ${C.bold}${perSoPath}${C.reset} ${C.dim}(and appended to ${collectivePath})${C.reset}\n`);
    } else {
        const err = res && res.error ? res.error : "Timeout or failed to execute SO";
        console.log(`${C.red}❌ Remote execution failed: ${err}${C.reset}`);
    }
}

async function handleTest(testName) {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected. Connect iPhone to KDB first.${C.reset}`);
        return;
    }
    const target = (testName || 'gpu').toLowerCase();
    if (target === 'list' || target === 'help') {
        console.log(`\n${C.bold}Available KuDroid Tests:${C.reset}`);
        console.log(`  - ${C.yellow}gpu${C.reset}        : Host-Native GPU EGL & Shader Compiler Test`);
        console.log(`  - ${C.yellow}audio${C.reset}      : AudioShim OpenSL ES & AAudio Pipeline Test`);
        console.log(`  - ${C.yellow}bionic${C.reset}     : Bionic Execution Test (test_bionic_lib.so)`);
        console.log(`  - ${C.yellow}jni${C.reset}        : JNI Massive 200+ symbols test (test_jni_massive.so)`);
        console.log(`  - ${C.yellow}jvm${C.reset}        : Avian JVM initialization test`);
        console.log(`  - ${C.yellow}syscall${C.reset}    : Syscall Traps Test (test_syscalls.so)`);
        console.log(`  - ${C.yellow}multi_elf${C.reset}  : Multi-ELF Linker Dependency Test`);
        console.log(`  - ${C.yellow}opengl_so${C.reset}  : GPU OpenGL .so module test`);
        console.log(`  - ${C.yellow}vulkan_so${C.reset}  : GPU Vulkan .so module test`);
        console.log(`  - ${C.yellow}vfs${C.reset}        : Virtual File System Extended Path Remapper Test`);
        console.log(`  - ${C.yellow}all${C.reset}        : Run all subsystem tests sequentially\n`);
        return;
    }

    console.log(`\n${C.cyan}🚀 Triggering test '${target}' on iPhone...${C.reset}`);
    const res = await sendCommandWithTimeout({ action: 'test', name: target }, 15000);
    if (res && res.log) {
        const statusTag = res.success ? `${C.green}✔ PASSED${C.reset}` : `${C.red}❌ FAILED / ISSUES DETECTED${C.reset}`;
        console.log(`\n=== TEST RESULT [${C.bold}${target.toUpperCase()}${C.reset}]: ${statusTag} ===\n`);
        console.log(res.log);
        
        // Tự động lưu log về thư mục logs/
        const savedFile = res.file || `test_${target}.log`;
        const targetPath = path.join(LOGS_DIR, savedFile);
        fs.writeFileSync(targetPath, res.log, 'utf8');
        console.log(`\n${C.green}💾 Test log auto-saved to: ${C.bold}${targetPath}${C.reset}\n`);
    } else {
        console.log(`${C.red}❌ Test command timed out or failed to execute.${C.reset}`);
    }
}

async function handleDump(filename) {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected. Connect iPhone to KDB first.${C.reset}`);
        return;
    }
    const target = filename || 'kudroid_crash';
    console.log(`${C.cyan}📥 Fetching '${target}' directly from iPhone Documents...${C.reset}`);
    const res = await sendCommandWithTimeout({ action: 'dump', file: target }, 10000);
    if (res && res.success) {
        const savedFile = res.file || `${target}.log`;
        const targetPath = path.join(LOGS_DIR, savedFile);
        let buffer;
        if (res.dataBase64) {
            buffer = Buffer.from(res.dataBase64, 'base64');
        } else {
            buffer = Buffer.from(res.content || '', 'utf8');
        }
        fs.writeFileSync(targetPath, buffer);
        console.log(`${C.green}✔ Pulled successfully (${(buffer.length / 1024).toFixed(2)} KB)! Saved to: ${C.bold}${targetPath}${C.reset}`);
        
        // In xem trước 20 dòng cuối
        if (res.content) {
            const lines = res.content.trim().split('\n');
            const tail = lines.slice(-25).join('\n');
            console.log(`\n${C.yellow}--- Log Tail (${savedFile}) ---${C.reset}\n${C.gray}${tail}${C.reset}\n`);
        }
    } else {
        const err = res && res.error ? res.error : "Timeout or failed to retrieve file";
        console.log(`${C.red}❌ Dump failed: ${err}${C.reset}`);
    }
}

async function handleList() {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected. Connect iPhone to KDB first.${C.reset}`);
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
        console.log(`${C.red}❌ Failed to retrieve apps list (Timeout or error).${C.reset}`);
    }
}

async function handleRun(appId) {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected.${C.reset}`);
        return;
    }
    console.log(`${C.cyan}🚀 Launching '${appId}' on iPhone screen...${C.reset}`);
    sendToClient({ action: 'run', appId: appId });
    console.log(`${C.green}✔ Command sent! Auto-entering live debug stream (Press Ctrl+C to detach)...${C.reset}\n`);
    startDebugMode();
}

async function handleStop() {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected.${C.reset}`);
        return;
    }
    console.log(`${C.yellow}Stopping guest app and returning to Launcher...${C.reset}`);
    sendToClient({ action: 'stop' });
    console.log(`${C.green}✔ Returned to Launcher.${C.reset}`);
}

async function handleInstall(apkPath) {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected.${C.reset}`);
        return;
    }
    const resolved = path.resolve(apkPath);
    if (!fs.existsSync(resolved)) {
        console.log(`${C.red}❌ File not found: ${resolved}${C.reset}`);
        return;
    }
    const filename = path.basename(resolved);
    const buffer = fs.readFileSync(resolved);
    console.log(`${C.cyan}📦 Uploading ${filename} (${(buffer.length / 1024 / 1024).toFixed(2)} MB) to iPhone...${C.reset}`);
    
    // Gửi header cài đặt
    sendToClient({
        action: 'install',
        filename: filename,
        dataBase64: buffer.toString('base64')
    });
    console.log(`${C.green}✔ APK stream pushed. Auto-monitoring installation progress...${C.reset}\n`);
    startDebugMode();
}

function startDebugMode() {
    isDebugMode = true;
    debugSessionLogs = [];
    debugSessionStart = new Date();
    console.log(`${C.bgBlue} KDB ALL-IN-ONE DEBUG STREAM ACTIVE (Press Ctrl+C to stop & auto-save) ${C.reset}\n`);
}

function stopDebugMode() {
    isDebugMode = false;
    console.log(`\n${C.yellow}⏹ Debug stream stopped.${C.reset}`);
    
    if (debugSessionLogs.length > 0) {
        const nowStr = new Date().toISOString().replace(/[:.]/g, '-');
        const filename = `debug_${nowStr}.log`;
        const targetPath = path.join(LOGS_DIR, filename);
        fs.writeFileSync(targetPath, debugSessionLogs.join('\n'), 'utf8');
        console.log(`${C.green}💾 Session log saved (${debugSessionLogs.length} lines): ${C.bold}${targetPath}${C.reset}\n`);
    } else {
        console.log(`${C.gray}No logs recorded in this session.${C.reset}\n`);
    }
    prompt();
}

function handleSave(type) {
    const nowStr = new Date().toISOString().replace(/[:.]/g, '-');
    const targetFile = path.join(LOGS_DIR, `snapshot_${type || 'all'}_${nowStr}.log`);
    if (debugSessionLogs.length > 0) {
        fs.writeFileSync(targetFile, debugSessionLogs.join('\n'), 'utf8');
        console.log(`${C.green}💾 Snapshot saved: ${targetFile}${C.reset}`);
    } else {
        console.log(`${C.yellow}No recent log buffer to save. Run 'debug' first.${C.reset}`);
    }
}

function handleClear(args) {
    if (!connectedSocket) {
        console.log(`${C.red}❌ No iPhone connected.${C.reset}`);
        return;
    }
    const target = args[0] || 'all';
    sendToClient({ action: 'clear', target: target });
    console.log(`${C.green}✔ Clear command (${target}) sent to iPhone.${C.reset}`);
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
    console.log(`
${C.bold}${C.green}╔═══════════════════════════════════════════════════════════════╗
║             KuDroid Debug Bridge (KDB) Host Server            ║
║              Port: ${PORT} | WebSocket: ws://0.0.0.0:${PORT}          ║
╚═══════════════════════════════════════════════════════════════╝${C.reset}

${C.cyan}Type ${C.bold}'help'${C.reset}${C.cyan} for command list. Waiting for iPhone connection...${C.reset}
`);
    prompt();
});
