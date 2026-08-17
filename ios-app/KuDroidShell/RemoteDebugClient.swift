import Foundation
import UIKit

// mark: - C-Bridge Function để C++ đẩy log sang Swift
@_cdecl("kudroid_remote_log_broadcast")
public func kudroid_remote_log_broadcast(level: Int32, tag: UnsafePointer<CChar>?, message: UnsafePointer<CChar>?) {
    let tagStr = tag != nil ? String(cString: tag!) : "KuDroid"
    let msgStr = message != nil ? String(cString: message!) : ""
    RemoteDebugClient.shared.broadcastLog(level: Int(level), tag: tagStr, message: msgStr)
}

// mark: - Client WebSocket KDB trên iOS
public class RemoteDebugClient: NSObject {
    public static let shared = RemoteDebugClient()

    private var webSocketTask: URLSessionWebSocketTask?
    private var isConnected = false
    private var serverURL: URL?
    private var pingTimer: Timer?
    private var appSession: AppSession?

    public var onConnectionStatusChanged: ((Bool) -> Void)?

    private override init() {
        super.init()
    }

    public func configure(session: AppSession) {
        self.appSession = session
    }

    public func connect(host: String) {
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

    public func disconnect() {
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

    private func sendHandshake() {
        let device = [
            "name": UIDevice.current.name,
            "model": UIDevice.current.model,
            "systemName": UIDevice.current.systemName,
            "osVersion": UIDevice.current.systemVersion
        ]
        let payload: [String: Any] = [
            "type": "handshake",
            "device": device
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
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
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

        default:
            break
        }
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
                let cString = kudroid_install_apk(tempURL.path)
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

    public func broadcastLog(level: Int, tag: String, message: String) {
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
