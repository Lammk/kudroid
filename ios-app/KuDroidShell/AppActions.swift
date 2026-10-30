import SwiftUI
import UIKit
import Network
import CryptoKit
import Combine

// Package names are single directory components, never arbitrary paths.
enum InstalledAppStorage {
    static func validID(_ value: String) -> Bool {
        !value.isEmpty && value != "." && value != ".." && value.utf8.count <= 255 &&
        value.unicodeScalars.allSatisfy {
            CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-").contains($0)
        }
    }

    static func directory(_ id: String) throws -> URL {
        guard validID(id) else { throw ExportFailure("Invalid installed app ID") }
        let fm = FileManager.default
        let root = fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("android_root/data/app", isDirectory: true).resolvingSymlinksInPath()
        let directory = root.appendingPathComponent(id, isDirectory: true)
        let resolved = directory.resolvingSymlinksInPath()
        guard resolved.deletingLastPathComponent() == root,
              (try? directory.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true,
              fm.fileExists(atPath: directory.appendingPathComponent("app_info.json").path) else {
            throw ExportFailure("This app is no longer installed")
        }
        return directory
    }

    static func launchURL(_ id: String) throws -> URL {
        _ = try directory(id)
        var components = URLComponents()
        components.scheme = "kudroid"
        components.host = "launch"
        components.queryItems = [URLQueryItem(name: "app", value: id)]
        guard let url = components.url else { throw ExportFailure("Could not create app shortcut") }
        return url
    }

    static func id(from url: URL) -> String? {
        guard url.scheme?.lowercased() == "kudroid", url.host == "launch",
              url.path.isEmpty || url.path == "/", url.user == nil, url.password == nil, url.port == nil,
              let components = URLComponents(url: url, resolvingAgainstBaseURL: false) else { return nil }
        let items = components.queryItems?.filter { $0.name == "app" } ?? []
        guard items.count == 1, let id = items.first?.value, validID(id) else { return nil }
        return id
    }
}

final class AppDataResetModel: ObservableObject {
    static let shared = AppDataResetModel()
    private static var queueURL: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Kudroid/pending-data-resets.json")
    }
    @Published private(set) var busy: Bool
    @Published private(set) var pending: Set<String>
    @Published var message: String?
    private var processedStartup = false
    private let worker = DispatchQueue(label: "kudroid.data-reset", qos: .userInitiated)

    private init() {
        var stored: [String] = []
        var failure: String?
        if FileManager.default.fileExists(atPath: Self.queueURL.path) {
            do { stored = try JSONDecoder().decode([String].self, from: Data(contentsOf: Self.queueURL)) }
            catch { failure = "Could not read pending app data resets: " + error.localizedDescription }
        }
        let queued = Set(stored.filter(InstalledAppStorage.validID))
        pending = queued
        busy = !queued.isEmpty
        message = failure
    }

    private func save() throws {
        let file = Self.queueURL
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try JSONEncoder().encode(Array(pending).sorted()).write(to: file, options: .atomic)
    }
    func canLaunch(_ id: String) -> Bool { !busy && !pending.contains(id) }

    func processStartup() {
        guard !processedStartup else { return }
        processedStartup = true
        guard !pending.isEmpty else { busy = false; return }
        run(Array(pending).sorted(), startup: true)
    }

    func request(_ app: AppItem) {
        guard !busy else { return }
        let wasPending = pending.contains(app.id)
        pending.insert(app.id)
        do { try save() }
        catch { if !wasPending { pending.remove(app.id) }; message = error.localizedDescription; return }
        run([app.id], startup: false)
    }

    func forget(_ id: String) {
        guard pending.remove(id) != nil else { return }
        do { try save() }
        catch { message = error.localizedDescription }
    }

    private func run(_ ids: [String], startup: Bool) {
        busy = true
        worker.async {
            var completed: [String] = [], deferred: [String] = [], failed: [String] = []
            for id in ids {
                do { _ = try InstalledAppStorage.directory(id) }
                catch {
                    failed.append(id)
                    continue
                }
                switch kudroid_clear_app_data(id) {
                case 1: completed.append(id)
                case 2: deferred.append(id)
                default: failed.append(id)
                }
            }
            DispatchQueue.main.async {
                for id in completed { self.pending.remove(id) }
                self.busy = false
                do { try self.save() }
                catch { self.message = "Could not save app data reset status: " + error.localizedDescription; return }
                if !failed.isEmpty {
                    self.message = "Could not delete all app data. Reopen KuDroid to retry."
                } else if !deferred.isEmpty {
                    self.message = "Close KuDroid from the app switcher, then reopen it to finish deleting app data."
                } else if !startup {
                    self.message = "App data deleted. The next launch starts fresh."
                }
            }
        }
    }
}

final class ShortcutLaunchModel: ObservableObject, Identifiable {
    let id = UUID()
    let appID: String
    @Published private(set) var ready = false
    @Published private(set) var waiting = false
    @Published var error: String?
    private var checking = false
    private var requestedDebugger = false
    private var cancelled = false
    private let worker = DispatchQueue(label: "kudroid.shortcut-jit", qos: .userInitiated)

    init(appID: String) { self.appID = appID }
    func cancel() { cancelled = true }

    func check() {
        guard !checking, !ready, !cancelled else { return }
        do { _ = try InstalledAppStorage.directory(appID) }
        catch { self.error = error.localizedDescription; waiting = true; return }
        guard AppDataResetModel.shared.canLaunch(appID) else {
            error = "App data reset is pending. Reopen KuDroid to finish it first."
            waiting = true
            return
        }
        checking = true
        let modern = ProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 26
        worker.async {
            let enabled = modern ? kudroid_is_jit_enabled() != 0 : kudroid_debugger_attached() != 0
            DispatchQueue.main.async {
                self.checking = false
                guard !self.cancelled else { return }
                if enabled { self.ready = true; self.error = nil; return }
                self.waiting = true
                if !self.requestedDebugger { self.requestedDebugger = true; self.openDebugger() }
            }
        }
    }

    func openDebugger() {
        error = nil
        JITActivation.openStikDebug { opened in
            guard !self.cancelled else { return }
            if !opened { self.error = "Could not open StikDebug. Install it and try again." }
        }
    }
}

struct ShortcutLaunchView: View {
    @ObservedObject var model: ShortcutLaunchModel
    let run: () -> Void
    let cancel: () -> Void
    @Environment(\.scenePhase) private var phase
    private let timer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()
    var body: some View {
        ZStack {
            ShellStyle.background.ignoresSafeArea()
            if model.waiting {
                VStack(spacing: 18) {
                    Text("Waiting for JIT").font(.headline)
                    if let error = model.error { Text(error).foregroundColor(.red).multilineTextAlignment(.center) }
                    Button("Open StikDebug") { model.openDebugger() }
                    Button("Check JIT") { model.check() }
                    Button("Cancel", role: .cancel) { model.cancel(); cancel() }
                }.padding(28)
            }
        }
        .onAppear { model.check() }
        .onChange(of: phase) { if $0 == .active { model.check() } }
        .onReceive(timer) { _ in if phase == .active { model.check() } }
        .onChange(of: model.ready) { if $0 { run() } }
        .onDisappear { model.cancel() }
    }
}

// Serve a removable WebClip profile only on the loopback interface.
final class HomeShortcutModel: ObservableObject {
    @Published var message = "Create a Home Screen icon for this app."
    @Published var error: String?
    @Published private(set) var busy = false
    private let queue = DispatchQueue(label: "kudroid.shortcut-profile")
    private var listener: NWListener?
    private var connections: [UUID: NWConnection] = [:]
    private var backgroundTask = UIBackgroundTaskIdentifier.invalid
    private var generation = UUID()

    func create(_ app: AppItem) {
        guard !busy else { return }
        stop()
        do {
            let launchURL = try InstalledAppStorage.launchURL(app.id)
            let profile = try Self.profile(app, launchURL: launchURL)
            let token = UUID().uuidString
            let parameters = NWParameters.tcp
            parameters.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: .any)
            let server = try NWListener(using: parameters)
            listener = server
            let requestGeneration = generation
            busy = true
            error = nil
            backgroundTask = UIApplication.shared.beginBackgroundTask(withName: "HomeShortcut") { [weak self] in self?.stop() }
            server.newConnectionHandler = { [weak self] connection in
                guard let self = self else { connection.cancel(); return }
                let id = UUID()
                guard self.connections.count < 8 else { connection.cancel(); return }
                self.connections[id] = connection
                connection.start(queue: self.queue)
                self.receive(connection, id: id, token: token, profile: profile, data: Data())
                self.queue.asyncAfter(deadline: .now() + 10) {
                    connection.cancel(); self.connections.removeValue(forKey: id)
                }
            }
            server.stateUpdateHandler = { [weak self, weak server] state in
                DispatchQueue.main.async {
                    guard let self = self, self.generation == requestGeneration else { return }
                    switch state {
                    case .ready:
                        guard let port = server?.port,
                              let url = URL(string: "http://127.0.0.1:\(port.rawValue)/\(token).mobileconfig") else { return }
                        self.message = "Allow the profile download in Safari, then open Settings → Profile Downloaded → Install. The profile adds only this app’s icon."
                        UIApplication.shared.open(url, options: [:]) { opened in
                            if !opened { self.error = "Open Safari to download the shortcut profile."; self.stop() }
                        }
                    case .failed(let error): self.error = error.localizedDescription; self.stop()
                    default: break
                    }
                }
            }
            server.start(queue: queue)
            DispatchQueue.main.asyncAfter(deadline: .now() + 60) { [weak self] in
                guard self?.generation == requestGeneration else { return }
                self?.stop()
            }
        } catch { self.error = error.localizedDescription; busy = false }
    }

    private func receive(_ connection: NWConnection, id: UUID, token: String, profile: Data, data: Data) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 4096) { [weak self] bytes, _, complete, error in
            guard let self = self else { connection.cancel(); return }
            var request = data
            if let bytes = bytes { request.append(bytes) }
            guard request.count <= 8192, error == nil else { connection.cancel(); self.connections.removeValue(forKey: id); return }
            if request.range(of: Data("\r\n\r\n".utf8)) == nil {
                if complete { connection.cancel(); self.connections.removeValue(forKey: id) }
                else { self.receive(connection, id: id, token: token, profile: profile, data: request) }
                return
            }
            let line = String(data: request, encoding: .utf8)?.components(separatedBy: "\r\n").first ?? ""
            let ok = line == "GET /\(token).mobileconfig HTTP/1.1" || line == "GET /\(token).mobileconfig HTTP/1.0"
            let body = ok ? profile : Data()
            let status = ok ? "200 OK" : "404 Not Found"
            var response = Data("HTTP/1.1 \(status)\r\nContent-Type: application/x-apple-aspen-config\r\nContent-Disposition: attachment; filename=Shortcut.mobileconfig\r\nCache-Control: no-store\r\nContent-Length: \(body.count)\r\nConnection: close\r\n\r\n".utf8)
            response.append(body)
            connection.send(content: response, completion: .contentProcessed { _ in
                connection.cancel(); self.connections.removeValue(forKey: id)
            })
        }
    }

    func stop() {
        generation = UUID()
        let old = listener
        listener = nil
        old?.cancel()
        queue.async {
            for connection in self.connections.values { connection.cancel() }
            self.connections.removeAll()
        }
        if backgroundTask != .invalid {
            UIApplication.shared.endBackgroundTask(backgroundTask)
            backgroundTask = .invalid
        }
        busy = false
    }

    private static func profile(_ app: AppItem, launchURL: URL) throws -> Data {
        let namespace = (Bundle.main.bundleIdentifier ?? "kudroid") + ".shortcut."
        let suffix = SHA256.hash(data: Data(app.id.utf8)).map { String(format: "%02x", $0) }.joined()
        var clip: [String: Any] = [
            "PayloadType": "com.apple.webClip.managed", "PayloadVersion": 1,
            "PayloadIdentifier": namespace + suffix + ".webclip", "PayloadUUID": UUID().uuidString,
            "PayloadDisplayName": app.displayName, "Label": app.displayName,
            "URL": launchURL.absoluteString, "IsRemovable": true, "FullScreen": false, "Precomposed": true
        ]
        let format = UIGraphicsImageRendererFormat(); format.scale = 1; format.opaque = true
        let image = UIGraphicsImageRenderer(size: CGSize(width: 180, height: 180), format: format).image { context in
            UIColor.black.setFill(); context.fill(CGRect(x: 0, y: 0, width: 180, height: 180))
            if let icon = app.iconImage { icon.draw(in: CGRect(x: 0, y: 0, width: 180, height: 180)) }
            else { UIImage(systemName: "cube.fill")?.withTintColor(.systemGreen, renderingMode: .alwaysOriginal).draw(in: CGRect(x: 30, y: 30, width: 120, height: 120)) }
        }
        clip["Icon"] = image.pngData()
        let profile: [String: Any] = [
            "PayloadType": "Configuration", "PayloadVersion": 1,
            "PayloadIdentifier": namespace + suffix, "PayloadUUID": UUID().uuidString,
            "PayloadDisplayName": app.displayName + " Shortcut",
            "PayloadDescription": "Adds a Home Screen icon that launches this app in KuDroid.",
            "PayloadRemovalDisallowed": false, "PayloadContent": [clip]
        ]
        return try PropertyListSerialization.data(fromPropertyList: profile, format: .xml, options: 0)
    }
}

struct HomeShortcutView: View {
    let app: AppItem
    @StateObject private var model = HomeShortcutModel()
    @Environment(\.presentationMode) private var presentation
    var body: some View {
        NavigationView {
            VStack(spacing: 22) {
                if let icon = app.iconImage { Image(uiImage: icon).resizable().frame(width: 72, height: 72).cornerRadius(16) }
                Text(app.displayName).font(.title2)
                Text(model.message).multilineTextAlignment(.center)
                if let error = model.error { Text(error).foregroundColor(.red).multilineTextAlignment(.center) }
                Button("Create Home Shortcut") { model.create(app) }.disabled(model.busy)
                Text("When opened, KuDroid checks JIT and requests StikDebug if needed. On iOS 26+, it requests universal.js automatically.")
                    .font(.caption).foregroundColor(.secondary).multilineTextAlignment(.center)
                Spacer()
            }.padding()
            .navigationTitle("Home Shortcut")
            .toolbar { ToolbarItem(placement: .navigationBarTrailing) { Button("Done") { model.stop(); presentation.wrappedValue.dismiss() } } }
        }.navigationViewStyle(.stack)
        .onDisappear { model.stop() }
    }
}
