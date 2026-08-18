import Foundation
import UIKit

// mark: - C-Bridge Function để C++ đẩy log sang Swift
@_cdecl("kudroid_remote_log_broadcast")
func kudroid_remote_log_broadcast(level: Int32, tag: UnsafePointer<CChar>?, message: UnsafePointer<CChar>?) {
    let tagStr = tag != nil ? String(cString: tag!) : "KuDroid"
    let msgStr = message != nil ? String(cString: message!) : ""
    RemoteDebugClient.shared.broadcastLog(level: Int(level), tag: tagStr, message: msgStr)
}

// mark: - Client WebSocket KDB trên iOS
class RemoteDebugClient: NSObject {
    static let shared = RemoteDebugClient()

    private var webSocketTask: URLSessionWebSocketTask?
    private var isConnected = false
    private var serverURL: URL?
    private var pingTimer: Timer?
    private var appSession: AppSession?

    var onConnectionStatusChanged: ((Bool) -> Void)?

    private override init() {
        super.init()
    }

    func configure(session: AppSession) {
        self.appSession = session
    }

    func connect(host: String) {
        disconnect()
        let trimmed = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }

        let urlString = trimmed.hasPrefix("ws://") || trimmed.hasPrefix("wss://") ? trimmed : "ws://\(trimmed)"
        guard let url = URL(string: urlString) else { return }
        self.serverURL = url

        let session = URLSession(configuration: .default, delegate: nil, delegateQueue: OperationQueue())
        webSocketTask = session.webSocketTask(with: url)
        webSocketTask?.resume()

        listenForMessages()
        sendHandshake()
        startPingTimer()
    }

    func disconnect() {
        pingTimer?.invalidate()
        pingTimer = nil
        webSocketTask?.cancel(with: .goingAway, reason: nil)
        webSocketTask = nil
        isConnected = false
        DispatchQueue.main.async { [weak self] in
            self?.onConnectionStatusChanged?(false)
        }
    }

    private func startPingTimer() {
        DispatchQueue.main.async { [weak self] in
            self?.pingTimer?.invalidate()
            self?.pingTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
                self?.webSocketTask?.sendPing { error in
                    if let _ = error {
                        self?.isConnected = false
                        self?.onConnectionStatusChanged?(false)
                    }
                }
            }
        }
    }

    private func getBuildInfo() -> [String: Any] {
        if let url = Bundle.main.url(forResource: "build_info", withExtension: "json"),
           let data = try? Data(contentsOf: url),
           let json = (try? JSONSerialization.jsonObject(with: data, options: [])) as? [String: Any] {
            return json
        }
        return [
            "short_commit": "legacy/manual",
            "commit": "unknown",
            "message": "N/A",
            "build_time": "N/A"
        ]
    }

    private func sendHandshake() {
        let device = [
            "name": UIDevice.current.name,
            "model": UIDevice.current.model,
            "systemName": UIDevice.current.systemName,
            "osVersion": UIDevice.current.systemVersion
        ]
        let payload: [String: Any] = [
            "type": "handshake",
            "device": device,
            "buildInfo": getBuildInfo()
        ]
        sendJson(payload)
        isConnected = true
        DispatchQueue.main.async { [weak self] in
            self?.onConnectionStatusChanged?(true)
        }
    }

    private func listenForMessages() {
        webSocketTask?.receive { [weak self] result in
            guard let self = self else { return }
            switch result {
            case .failure(let error):
                NSLog("[KDBClient] WebSocket receive error: %@", error.localizedDescription)
                self.disconnect()
            case .success(let message):
                switch message {
                case .string(let text):
                    self.handleCommand(text)
                case .data(let data):
                    if let text = String(data: data, encoding: .utf8) {
                        self.handleCommand(text)
                    }
                @unknown default:
                    break
                }
                // Tiếp tục lắng nghe lệnh tiếp theo
                self.listenForMessages()
            }
        }
    }

    private func handleCommand(_ text: String) {
        guard let data = text.data(using: .utf8),
              let rawObj = try? JSONSerialization.jsonObject(with: data, options: []),
              let json = rawObj as? [String: Any],
              let action = json["action"] as? String else {
            return
        }

        NSLog("[KDBClient] Received command from PC: %@", action)

        switch action {
        case "run":
            if let appId = json["appId"] as? String {
                DispatchQueue.main.async { [weak self] in
                    self?.appSession?.activeGuestApp = appId
                }
            }

        case "stop":
            DispatchQueue.main.async { [weak self] in
                self?.appSession?.activeGuestApp = ""
            }

        case "list":
            handleListCommand()

        case "install":
            if let filename = json["filename"] as? String,
               let dataBase64 = json["dataBase64"] as? String,
               let apkData = Data(base64Encoded: dataBase64) {
                handleInstallCommand(filename: filename, data: apkData)
            }

        case "clear":
            let target = json["target"] as? String ?? "all"
            handleClearCommand(target: target)

        case "dump":
            let file = json["file"] as? String ?? "kudroid_crash"
            handleDumpCommand(targetFile: file)

        case "test":
            let name = json["name"] as? String ?? "gpu"
            handleTestCommand(testName: name)

        case "run_so_chunk":
            let filename = json["filename"] as? String ?? "test.so"
            let chunkIdx = json["chunkIndex"] as? Int ?? 0
            let totalChunks = json["totalChunks"] as? Int ?? 1
            let entry = json["entrypoint"] as? String ?? ""
            let b64 = json["dataBase64"] as? String ?? ""
            guard let chunkData = Data(base64Encoded: b64) else {
                sendResponse(["success": false, "error": "Invalid base64 in chunk \(chunkIdx)"])
                return
            }
            handleChunkStream(filename: filename, chunkIndex: chunkIdx, totalChunks: totalChunks, chunkData: chunkData, entrypoint: entry)

        case "run_so":
            let filename = json["filename"] as? String ?? "test.so"
            let entry = json["entrypoint"] as? String ?? ""

            if let dataBase64 = json["dataBase64"] as? String,
               let soData = Data(base64Encoded: dataBase64) {
                handleRunSoCommand(filename: filename, data: soData, entrypoint: entry)
            } else {
                sendResponse(["success": false, "error": "Invalid run_so payload"])
            }

        case "version":
            sendResponse([
                "success": true,
                "buildInfo": getBuildInfo()
            ])

        default:
            break
        }
    }

    private func handleChunkStream(filename: String, chunkIndex: Int, totalChunks: Int, chunkData: Data, entrypoint: String) {
        guard let docURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else {
            sendResponse(["success": false, "error": "Cannot access Documents directory"])
            return
        }
        let microDir = docURL.appendingPathComponent("micro_tests", isDirectory: true)
        try? FileManager.default.createDirectory(at: microDir, withIntermediateDirectories: true)
        let targetURL = microDir.appendingPathComponent(filename)

        if chunkIndex == 0 {
            try? FileManager.default.removeItem(at: targetURL)
            do {
                try chunkData.write(to: targetURL)
            } catch {
                sendResponse(["success": false, "error": "Failed to create chunk file: \(error.localizedDescription)"])
                return
            }
        } else {
            if let fileHandle = try? FileHandle(forWritingTo: targetURL) {
                fileHandle.seekToEndOfFile()
                fileHandle.write(chunkData)
                fileHandle.closeFile()
            } else {
                sendResponse(["success": false, "error": "Cannot append chunk \(chunkIndex)"])
                return
            }
        }

        if chunkIndex == totalChunks - 1 {
            if entrypoint == "__none__" {
                sendResponse(["success": true, "log": "✔ Dependency '\(filename)' uploaded successfully (\(totalChunks) chunks)."])
            } else {
                executeSoFile(targetURL: targetURL, entrypoint: entrypoint)
            }
        } else {
            sendResponse(["success": true, "ackChunk": chunkIndex])
        }
    }

    private func handleDownloadAndRunSo(filename: String, url: URL, entrypoint: String) {
        guard let docURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else {
            sendResponse(["success": false, "error": "Cannot access Documents directory"])
            return
        }

        let microDir = docURL.appendingPathComponent("micro_tests", isDirectory: true)
        try? FileManager.default.createDirectory(at: microDir, withIntermediateDirectories: true)
        let targetURL = microDir.appendingPathComponent(filename)

        let task = URLSession.shared.downloadTask(with: url) { [weak self] (tempURL, _, error) in
            if let error = error {
                self?.sendResponse(["success": false, "error": "LAN HTTP download failed: \(error.localizedDescription)"])
                return
            }
            guard let tempURL = tempURL else {
                self?.sendResponse(["success": false, "error": "Downloaded temp file is missing"])
                return
            }
            do {
                if FileManager.default.fileExists(atPath: targetURL.path) {
                    try FileManager.default.removeItem(at: targetURL)
                }
                try FileManager.default.moveItem(at: tempURL, to: targetURL)
                self?.executeSoFile(targetURL: targetURL, entrypoint: entrypoint)
            } catch {
                self?.sendResponse(["success": false, "error": "Failed to move downloaded SO: \(error.localizedDescription)"])
            }
        }
        task.resume()
    }

    private func handleRunSoCommand(filename: String, data: Data, entrypoint: String) {
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let docURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else {
                self?.sendResponse(["success": false, "error": "Cannot access Documents directory"])
                return
            }

            let microDir = docURL.appendingPathComponent("micro_tests", isDirectory: true)
            try? FileManager.default.createDirectory(at: microDir, withIntermediateDirectories: true)

            let targetURL = microDir.appendingPathComponent(filename)
            do {
                try data.write(to: targetURL)
                self?.executeSoFile(targetURL: targetURL, entrypoint: entrypoint)
            } catch {
                self?.sendResponse(["success": false, "error": "Failed to write SO: \(error.localizedDescription)"])
                return
            }
        }
    }

    private func executeSoFile(targetURL: URL, entrypoint: String) {
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let cString = kudroid_run_so_test(targetURL.path, entrypoint.isEmpty ? nil : entrypoint) else {
                self?.sendResponse(["success": false, "error": "Native runner returned null"])
                return
            }

            let log = String(cString: cString)
            free(UnsafeMutablePointer(mutating: cString))

            // Lưu log
            saveTestLog(filename: "test_so.log", content: log)

            let isSuccess = !log.contains("❌") && !log.contains("FAILED")
            self?.sendResponse([
                "success": isSuccess,
                "file": "test_so.log",
                "log": log
            ])
        }
    }

    private func handleTestCommand(testName: String) {
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let res = executeKuDroidTest(name: testName)
            self?.sendResponse([
                "success": res.success,
                "test": testName,
                "file": res.logFilename,
                "log": res.log
            ])
        }
    }

    private func handleDumpCommand(targetFile: String) {
        guard let docURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else {
            sendResponse(["success": false, "error": "Cannot access Documents directory"])
            return
        }

        let candidates = [
            targetFile,
            "\(targetFile).log",
            "\(targetFile).txt",
            targetFile.replacingOccurrences(of: "kudroid_android", with: "kudroid_android_logs.txt"),
            targetFile.replacingOccurrences(of: "kudroid_crash", with: "kudroid_crash.log"),
            targetFile.replacingOccurrences(of: "stderr", with: "stderr.log")
        ]

        var foundURL: URL? = nil
        for name in candidates {
            let u = docURL.appendingPathComponent(name)
            if FileManager.default.fileExists(atPath: u.path) {
                foundURL = u
                break
            }
        }

        guard let targetURL = foundURL, let data = try? Data(contentsOf: targetURL) else {
            sendResponse(["success": false, "file": targetFile, "error": "File not found in Documents directory: \(targetFile)"])
            return
        }

        let textContent = String(data: data, encoding: .utf8) ?? ""
        sendResponse([
            "success": true,
            "file": targetURL.lastPathComponent,
            "size": data.count,
            "content": textContent,
            "dataBase64": data.base64EncodedString()
        ])
    }

    private func handleListCommand() {
        guard let rootURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("android_root/data/app", isDirectory: true) else {
            sendResponse(["apps": []])
            return
        }

        var appsList: [[String: String]] = []
        if let contents = try? FileManager.default.contentsOfDirectory(at: rootURL, includingPropertiesForKeys: nil) {
            for folder in contents where folder.hasDirectoryPath {
                let folderName = folder.lastPathComponent
                var displayName = folderName
                var version = "1.0.0"

                let infoURL = folder.appendingPathComponent("app_info.json")
                if let infoData = try? Data(contentsOf: infoURL),
                   let rawObj = try? JSONSerialization.jsonObject(with: infoData),
                   let json = rawObj as? [String: Any] {
                    if let label = json["label"] as? String, !label.isEmpty {
                        displayName = label
                    }
                    if let ver = json["version"] as? String, !ver.isEmpty {
                        version = ver
                    }
                }
                appsList.append([
                    "id": folderName,
                    "displayName": displayName,
                    "version": version
                ])
            }
        }
        sendResponse(["apps": appsList])
    }

    private func handleInstallCommand(filename: String, data: Data) {
        let tempURL = FileManager.default.temporaryDirectory.appendingPathComponent(filename)
        do {
            try data.write(to: tempURL)
            NSLog("[KDBClient] Saved incoming APK to %@. Triggering native extraction...", tempURL.path)
            
            DispatchQueue.global(qos: .userInteractive).async {
                let cString = tempURL.path.withCString { kudroid_install_apk($0) }
                var log = ""
                if let cString = cString {
                    log = String(cString: cString)
                    free(UnsafeMutablePointer(mutating: cString))
                }
                NSLog("[KDBClient] APK installation completed: %@", log)
                try? FileManager.default.removeItem(at: tempURL)
            }
        } catch {
            NSLog("[KDBClient] Failed to save incoming APK: %@", error.localizedDescription)
        }
    }

    private func handleClearCommand(target: String) {
        guard let rootURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("android_root/data/dalvik-cache", isDirectory: true) else { return }
        if target == "all" {
            try? FileManager.default.removeItem(at: rootURL)
        } else {
            let targetDir = rootURL.appendingPathComponent(target)
            try? FileManager.default.removeItem(at: targetDir)
        }
    }

    func broadcastLog(level: Int, tag: String, message: String) {
        guard isConnected else { return }
        let payload: [String: Any] = [
            "type": "log",
            "level": level,
            "tag": tag,
            "message": message,
            "timestamp": Date().timeIntervalSince1970
        ]
        sendJson(payload)
    }

    private func sendResponse(_ data: [String: Any]) {
        var resp = data
        resp["type"] = "response"
        sendJson(resp)
    }

    private func sendJson(_ dict: [String: Any]) {
        guard let jsonData = try? JSONSerialization.data(withJSONObject: dict),
              let jsonStr = String(data: jsonData, encoding: .utf8) else {
            return
        }
        let message = URLSessionWebSocketTask.Message.string(jsonStr)
        webSocketTask?.send(message) { _ in }
    }
}
