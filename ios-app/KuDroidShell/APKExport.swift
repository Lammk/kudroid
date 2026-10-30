import SwiftUI
import UIKit
import UniformTypeIdentifiers
import Combine

enum APKFileTypes {
    static let extensions: Set<String> = ["apk", "apkm", "xapk", "apks"]
    static func accepts(_ url: URL) -> Bool { url.isFileURL && extensions.contains(url.pathExtension.lowercased()) }
    static var supported: [UTType] {
        ["apk", "xapk", "apks", "apkm"].compactMap { UTType(filenameExtension: $0) } + [.zip, .data]
    }
}

enum JITActivation {
    static func openStikDebug(completion: @escaping (Bool) -> Void) {
        guard let identifier = Bundle.main.bundleIdentifier else { completion(false); return }
        var url = URLComponents()
        url.scheme = "stikdebug"
        url.host = "enable-jit"
        url.queryItems = [URLQueryItem(name: "bundle-id", value: identifier),
                          URLQueryItem(name: "pid", value: String(getpid()))]
        if ProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 26 {
            url.queryItems?.append(URLQueryItem(name: "script-name", value: "universal.js"))
        }
        guard let request = url.url else { completion(false); return }
        UIApplication.shared.open(request, options: [:], completionHandler: completion)
    }

    static func openTrollStore(completion: @escaping (Bool) -> Void) {
        guard let identifier = Bundle.main.bundleIdentifier else { completion(false); return }
        var url = URLComponents()
        url.scheme = "apple-magnifier"
        url.host = "enable-jit"
        url.queryItems = [URLQueryItem(name: "bundle-id", value: identifier)]
        guard let request = url.url else { completion(false); return }
        UIApplication.shared.open(request, options: [:], completionHandler: completion)
    }
}

struct StandaloneConfiguration: Codable {
    let format: Int
    let package: String
    let displayName: String
    let storageName: String
    let installation: String

    static var bundledURL: URL {
        Bundle.main.bundleURL.appendingPathComponent("StandaloneGuest.plist")
    }
    static var isStandalone: Bool { FileManager.default.fileExists(atPath: bundledURL.path) }
    static func load() throws -> StandaloneConfiguration {
        let config = try PropertyListDecoder().decode(Self.self, from: Data(contentsOf: bundledURL))
        guard config.format == 1, validPackage(config.package),
              config.storageName == safeName(config.storageName) else {
            throw ExportFailure("Invalid standalone configuration")
        }
        return config
    }
}

struct ExportFailure: LocalizedError {
    let message: String
    init(_ message: String) { self.message = message }
    var errorDescription: String? { message }
}

private func safeName(_ value: String) -> String {
    let forbidden = CharacterSet(charactersIn: "/\\:\0").union(.controlCharacters)
    let name = String(value.unicodeScalars.map { forbidden.contains($0) ? "_" : String($0) }.joined().prefix(80))
        .trimmingCharacters(in: .whitespacesAndNewlines)
    return name.isEmpty || name == "." || name == ".." ? "Android App" : name
}

private func validPackage(_ value: String) -> Bool {
    !value.isEmpty && value.split(separator: ".", omittingEmptySubsequences: false).allSatisfy {
        !$0.isEmpty && $0.unicodeScalars.allSatisfy { CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_").contains($0) }
    }
}

enum StandaloneStorage {
    static var support: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Kudroid", isDirectory: true)
    }
    static var logs: URL { support.appendingPathComponent("Logs", isDirectory: true) }

    private static func exists(_ url: URL) -> Bool {
        FileManager.default.fileExists(atPath: url.path) ||
            (try? FileManager.default.destinationOfSymbolicLink(atPath: url.path)) != nil
    }

    private static func isDirectory(_ url: URL) throws -> Bool {
        if (try? FileManager.default.destinationOfSymbolicLink(atPath: url.path)) != nil { return false }
        let values = try url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        return values.isDirectory == true && values.isSymbolicLink != true
    }

    private static func archive(_ item: URL, under destination: URL) throws {
        guard exists(item) else { return }
        let fm = FileManager.default
        try fm.createDirectory(at: destination, withIntermediateDirectories: true)
        let target = destination.appendingPathComponent(UUID().uuidString + "-" + item.lastPathComponent)
        try fm.moveItem(at: item, to: target)
    }

    // Move directory entries without following links or replacing existing data.
    private static func merge(_ source: URL, into destination: URL, archive: URL) throws {
        let fm = FileManager.default
        for item in try fm.contentsOfDirectory(at: source, includingPropertiesForKeys: nil) {
            let target = destination.appendingPathComponent(item.lastPathComponent)
            if !exists(target) {
                try fm.moveItem(at: item, to: target)
            } else if try isDirectory(item) && isDirectory(target) {
                try merge(item, into: target, archive: archive)
                try fm.removeItem(at: item)
            } else {
                try self.archive(item, under: archive)
            }
        }
    }

    static func prepare(_ configuration: StandaloneConfiguration) throws -> URL {
        let fm = FileManager.default
        let documents = fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let staging = support.appendingPathComponent("Migration/GuestRoot", isDirectory: true)
        let backup = support.appendingPathComponent("Legacy", isDirectory: true)
        let layout = support.appendingPathComponent("documents-layout-v1")
        try fm.createDirectory(at: documents, withIntermediateDirectories: true)
        if (try? String(contentsOf: layout, encoding: .utf8)) == "1" { return documents }
        try fm.createDirectory(at: support, withIntermediateDirectories: true)
        if !exists(staging) {
            var candidates = [documents.appendingPathComponent(configuration.storageName),
                              documents.appendingPathComponent("android_root")]
            let previous = try fm.contentsOfDirectory(at: documents, includingPropertiesForKeys: nil)
                .filter { fm.fileExists(atPath: $0.appendingPathComponent("installation.txt").path) }
            candidates.append(contentsOf: previous)
            for candidate in candidates {
                let metadata = candidate.appendingPathComponent("data/app/" + configuration.package + "/app_info.json")
                if exists(candidate), try isDirectory(candidate), fm.fileExists(atPath: metadata.path) {
                    try fm.createDirectory(at: staging.deletingLastPathComponent(), withIntermediateDirectories: true)
                    try fm.moveItem(at: candidate, to: staging)
                    break
                }
            }
        }
        try archive(documents.appendingPathComponent("android_root"), under: backup)
        try archive(documents.appendingPathComponent("logs"), under: backup)
        if exists(staging) {
            guard try isDirectory(staging) else { throw ExportFailure("Invalid data migration directory") }
            try archive(staging.appendingPathComponent("logs"), under: backup)
            try archive(staging.appendingPathComponent("android_root"), under: backup)
            try merge(staging, into: documents, archive: backup)
            try fm.removeItem(at: staging)
        }
        try "1".write(to: layout, atomically: true, encoding: .utf8)
        return documents
    }
}

private func nativeExport(_ call: (UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> Int32) throws {
    var error: UnsafeMutablePointer<CChar>?
    let status = call(&error)
    defer { if let error = error { free(error) } }
    if status != 0 { throw ExportFailure(error.map { String(cString: $0) } ?? "Export failed") }
}

struct ExportCandidate {
    let sourceName: String
    let work: URL
    let guest: URL
    let package: String
    let label: String
    let version: String
    let icon: UIImage?
}

final class APKExportModel: ObservableObject {
    @Published var busy = false
    @Published var stage = "Choose an APK to create a standalone iOS app."
    @Published var name = ""
    @Published var bundleID = ""
    @Published var candidate: ExportCandidate?
    @Published var output: URL?
    @Published var error: String?
    private let worker = DispatchQueue(label: "kudroid.export", qos: .userInitiated)

    deinit { if let work = candidate?.work { try? FileManager.default.removeItem(at: work) } }

    func prepare(_ url: URL) {
        guard !busy else { return }
        busy = true
        error = nil
        output = nil
        let previous = candidate?.work
        candidate = nil
        stage = "Extracting APK…"
        worker.async {
            let fm = FileManager.default
            let access = url.startAccessingSecurityScopedResource()
            defer { if access { url.stopAccessingSecurityScopedResource() } }
            let work = fm.temporaryDirectory.appendingPathComponent("Reverse-" + UUID().uuidString)
            do {
                if let previous = previous { try? fm.removeItem(at: previous) }
                try fm.createDirectory(at: work, withIntermediateDirectories: true)
                let source = work.appendingPathComponent("source." + url.pathExtension)
                try fm.copyItem(at: url, to: source)
                let guest = work.appendingPathComponent("GuestApp", isDirectory: true)
                try nativeExport { kudroid_export_extract(source.path, guest.path, $0) }
                let data = try Data(contentsOf: guest.appendingPathComponent("app_info.json"))
                guard let info = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                      let package = info["package"] as? String, validPackage(package) else {
                    throw ExportFailure("The APK has no valid package ID")
                }
                let rawLabel = info["label"] as? String ?? ""
                let label = rawLabel.isEmpty || rawLabel.hasPrefix("@") ? url.deletingPathExtension().lastPathComponent : rawLabel
                let candidate = ExportCandidate(sourceName: url.lastPathComponent, work: work, guest: guest, package: package,
                    label: label, version: info["version"] as? String ?? "1.0",
                    icon: UIImage(contentsOfFile: guest.appendingPathComponent("app_icon.png").path))
                DispatchQueue.main.async {
                    self.candidate = candidate
                    self.name = label
                    self.bundleID = package.replacingOccurrences(of: "_", with: "-")
                    self.stage = "Ready to export"
                    self.busy = false
                }
            } catch {
                try? fm.removeItem(at: work)
                DispatchQueue.main.async { self.error = error.localizedDescription; self.busy = false; self.stage = "Could not extract APK" }
            }
        }
    }

    func prepareInstalled(_ id: String, displayName: String?) {
        guard !busy else { return }
        busy = true; error = nil; output = nil
        let previous = candidate?.work
        candidate = nil
        stage = "Preparing installed app…"
        worker.async {
            let fm = FileManager.default
            let work = fm.temporaryDirectory.appendingPathComponent("Reverse-" + UUID().uuidString)
            do {
                if let previous = previous { try? fm.removeItem(at: previous) }
                let installed = try InstalledAppStorage.directory(id)
                try fm.createDirectory(at: work, withIntermediateDirectories: true)
                let guest = work.appendingPathComponent("GuestApp", isDirectory: true)
                try fm.copyItem(at: installed, to: guest)
                let info = try JSONSerialization.jsonObject(with: Data(contentsOf: guest.appendingPathComponent("app_info.json"))) as? [String: Any]
                guard let package = info?["package"] as? String, validPackage(package) else {
                    throw ExportFailure("The installed app has no valid package ID")
                }
                let root = installed.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
                let obb = root.appendingPathComponent("sdcard/Android/obb/" + package, isDirectory: true)
                if fm.fileExists(atPath: obb.path) {
                    guard obb.resolvingSymlinksInPath().path.hasPrefix(root.path + "/sdcard/Android/obb/") else {
                        throw ExportFailure("Expansion files point outside app storage")
                    }
                    let seed = guest.appendingPathComponent("SeedSdcard/Android/obb/" + package, isDirectory: true)
                    try fm.createDirectory(at: seed.deletingLastPathComponent(), withIntermediateDirectories: true)
                    if fm.fileExists(atPath: seed.path) { try fm.removeItem(at: seed) }
                    try fm.copyItem(at: obb, to: seed)
                }
                let label = displayName ?? (info?["label"] as? String ?? id)
                let candidate = ExportCandidate(sourceName: id + ".apk", work: work, guest: guest, package: package,
                    label: label, version: info?["version"] as? String ?? "1.0",
                    icon: UIImage(contentsOfFile: guest.appendingPathComponent("app_icon.png").path))
                DispatchQueue.main.async {
                    self.candidate = candidate; self.name = label
                    self.bundleID = package.replacingOccurrences(of: "_", with: "-")
                    self.stage = "Ready to export"; self.busy = false
                }
            } catch {
                try? fm.removeItem(at: work)
                DispatchQueue.main.async { self.error = error.localizedDescription; self.busy = false; self.stage = "Could not prepare installed app" }
            }
        }
    }

    func export() {
        guard !busy, let candidate = candidate else { return }
        let title = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let identifier = bundleID.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !title.isEmpty, identifier.range(of: #"^[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+$"#, options: .regularExpression) != nil else {
            error = "Enter an app name and a valid bundle ID."
            return
        }
        busy = true
        error = nil
        output = nil
        stage = "Preparing standalone launcher…"
        worker.async {
            do {
                let result = try Self.package(candidate, title: title, identifier: identifier) { text in
                    DispatchQueue.main.async { self.stage = text }
                }
                DispatchQueue.main.async { self.output = result; self.stage = "IPA ready — sign it before installation"; self.busy = false }
            } catch {
                DispatchQueue.main.async { self.error = error.localizedDescription; self.busy = false; self.stage = "Export failed" }
            }
        }
    }

    func refreshOutput() {
        guard let previous = output else { return }
        let fm = FileManager.default
        if fm.fileExists(atPath: previous.path) { return }
        let remaining = (try? fm.contentsOfDirectory(at: previous.deletingLastPathComponent(),
            includingPropertiesForKeys: [.isRegularFileKey]))?.filter { $0.pathExtension.lowercased() == "ipa" } ?? []
        output = remaining.count == 1 ? remaining[0] : nil
        if output == nil { stage = "The exported IPA was removed." }
    }

    private static func package(_ candidate: ExportCandidate, title: String, identifier: String,
                                progress: (String) -> Void) throws -> URL {
        let fm = FileManager.default
        let staging = candidate.work.appendingPathComponent("Package-" + UUID().uuidString)
        try fm.createDirectory(at: staging.appendingPathComponent("Payload"), withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: staging) }
        let target = staging.appendingPathComponent("Payload/" + safeName(title) + ".app")
        let preserved = Bundle.main.bundleURL.appendingPathComponent("StandaloneLauncher.bin")
        let hasPreserved = fm.fileExists(atPath: preserved.path)
        guard let executable = hasPreserved ? preserved : Bundle.main.executableURL else { throw ExportFailure("Launcher executable is missing") }
        let handle = try FileHandle(forReadingFrom: executable)
        defer { try? handle.close() }
        if hasPreserved {
            guard try handle.read(upToCount: 8) == Data("KUDLCH01".utf8) else {
                throw ExportFailure("Invalid preserved launcher")
            }
        }
        let header = try handle.read(upToCount: 16) ?? Data()
        guard header.count == 16, header.prefix(4) == Data([0xcf, 0xfa, 0xed, 0xfe]),
              Array(header[12..<16]) == [2, 0, 0, 0] else {
            throw ExportFailure("This installation does not contain an executable app launcher. Export from a standalone installation of Kudroid.")
        }
        try fm.copyItem(at: Bundle.main.bundleURL, to: target)
        for entry in ["_CodeSignature", "embedded.mobileprovision", "GuestApp", "StandaloneGuest.plist", "StandaloneLauncher.bin"] {
            let path = target.appendingPathComponent(entry)
            if fm.fileExists(atPath: path.path) { try fm.removeItem(at: path) }
        }
        if hasPreserved {
            guard let executableName = Bundle.main.object(forInfoDictionaryKey: "CFBundleExecutable") as? String,
                  executableName == URL(fileURLWithPath: executableName).lastPathComponent else {
                throw ExportFailure("Invalid launcher executable name")
            }
            let targetExecutable = target.appendingPathComponent(executableName)
            try fm.removeItem(at: targetExecutable)
            guard fm.createFile(atPath: targetExecutable.path, contents: nil) else { throw ExportFailure("Cannot create launcher") }
            let output = try FileHandle(forWritingTo: targetExecutable)
            defer { try? output.close() }
            try handle.seek(toOffset: 8)
            while let chunk = try handle.read(upToCount: 1024 * 1024), !chunk.isEmpty { try output.write(contentsOf: chunk) }
            try output.close()
            try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: targetExecutable.path)
        }
        progress("Adding Android app files…")
        try fm.copyItem(at: candidate.guest, to: target.appendingPathComponent("GuestApp"))
        let config = StandaloneConfiguration(format: 1, package: candidate.package, displayName: title,
            storageName: safeName(title), installation: UUID().uuidString)
        try PropertyListEncoder().encode(config).write(to: target.appendingPathComponent("StandaloneGuest.plist"), options: .atomic)
        let infoURL = target.appendingPathComponent("Info.plist")
        guard var plist = try PropertyListSerialization.propertyList(from: Data(contentsOf: infoURL), format: nil) as? [String: Any] else {
            throw ExportFailure("Invalid launcher Info.plist")
        }
        plist.removeValue(forKey: "CFBundleURLTypes")
        plist.removeValue(forKey: "CFBundleDocumentTypes")
        plist.removeValue(forKey: "UTImportedTypeDeclarations")
        plist.removeValue(forKey: "UTExportedTypeDeclarations")
        plist["CFBundleDisplayName"] = title
        plist["CFBundleName"] = title
        plist["CFBundleIdentifier"] = identifier
        plist["CFBundleVersion"] = "1"
        let version = candidate.version.range(of: #"\d+(?:\.\d+){0,2}"#, options: .regularExpression).map { String(candidate.version[$0]) } ?? "1.0"
        plist["CFBundleShortVersionString"] = version
        plist["MinimumOSVersion"] = "15.0"
        plist["UIFileSharingEnabled"] = true
        plist["LSSupportsOpeningDocumentsInPlace"] = true
        plist["LSApplicationQueriesSchemes"] = ["stikdebug", "apple-magnifier"]
        if let icon = candidate.icon {
            let format = UIGraphicsImageRendererFormat()
            format.scale = 1
            format.opaque = true
            for pixels in [120, 152, 167, 180] {
                let size = CGSize(width: pixels, height: pixels)
                let image = UIGraphicsImageRenderer(size: size, format: format).image { context in
                    UIColor.black.setFill()
                    context.fill(CGRect(origin: .zero, size: size))
                    icon.draw(in: CGRect(origin: .zero, size: size))
                }
                guard let data = image.pngData() else { throw ExportFailure("Could not encode app icon") }
                try data.write(to: target.appendingPathComponent("GuestIcon\(pixels).png"))
            }
            plist.removeValue(forKey: "CFBundleIconName")
            plist["CFBundleIcons"] = ["CFBundlePrimaryIcon": ["CFBundleIconFiles": ["GuestIcon120", "GuestIcon180"]]]
            plist["CFBundleIcons~ipad"] = ["CFBundlePrimaryIcon": ["CFBundleIconFiles": ["GuestIcon152", "GuestIcon167"]]]
            plist["CFBundleIconFiles"] = ["GuestIcon120", "GuestIcon180", "GuestIcon152", "GuestIcon167"]
        }
        try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0).write(to: infoURL, options: .atomic)
        let outputDir = ReverseHistoryModel.directory.appendingPathComponent(UUID().uuidString)
        try fm.createDirectory(at: outputDir, withIntermediateDirectories: true)
        let output = outputDir.appendingPathComponent(safeName(title) + ".ipa")
        progress("Writing IPA…")
        do {
            try ReverseExportMetadata(format: 1, sourceAPK: candidate.sourceName, package: candidate.package,
                version: candidate.version, created: Date()).write(to: outputDir)
            try nativeExport { kudroid_export_archive(staging.path, output.path, $0) }
        }
        catch { try? fm.removeItem(at: outputDir); throw error }
        return output
    }
}

struct ReverseView: View {
    @StateObject private var model = APKExportModel()
    @State private var picker = false
    @State private var share = false
    @State private var history = false
    var installedAppID: String? = nil
    var installedName: String? = nil
    @State private var preparedInstalledAPK = false
    var body: some View {
        NavigationView {
            Form {
                Section {
                    Text("Create a separate iOS app from an APK using the Kudroid runtime.")
                    Button("Add APK") { picker = true }.disabled(model.busy)
                }
                if let candidate = model.candidate {
                    Section(header: Text("App")) {
                        if let icon = candidate.icon {
                            Image(uiImage: icon).resizable().scaledToFit().frame(width: 64, height: 64).cornerRadius(12)
                        }
                        TextField("App name", text: $model.name)
                        TextField("Bundle ID", text: $model.bundleID).autocapitalization(.none).disableAutocorrection(true)
                        Text("App data and saves stay in this app’s Documents folder.").font(.caption)
                    }.disabled(model.busy)
                    Section {
                        Button("Export IPA") { model.export() }.disabled(model.busy)
                        Text("The IPA needs signing before installation. Each exported app has its own data, saves and JIT setup.").font(.caption).foregroundColor(.secondary)
                    }
                }
                Section {
                    if model.busy { ProgressView() }
                    Text(model.stage)
                    if let error = model.error { Text(error).foregroundColor(.red) }
                    if model.output != nil { Button("Save or Share IPA") { share = true } }
                }
            }
            .navigationTitle("Reverse")
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button { history = true } label: { Image(systemName: "folder") }
                        .accessibilityLabel("Export history").disabled(model.busy)
                }
            }
        }
        .navigationViewStyle(.stack)
        .onAppear {
            if !preparedInstalledAPK, let id = installedAppID {
                preparedInstalledAPK = true
                model.prepareInstalled(id, displayName: installedName)
            }
        }
        .fileImporter(isPresented: $picker, allowedContentTypes: APKFileTypes.supported) { result in
            switch result { case .success(let url): model.prepare(url); case .failure(let error): model.error = error.localizedDescription }
        }
        .sheet(isPresented: $share) {
            if let output = model.output { IPAActivitySheet(url: output) }
        }
        .sheet(isPresented: $history, onDismiss: { model.refreshOutput() }) { ReverseHistoryView() }
    }
}

struct IPAActivitySheet: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

final class StandaloneBootModel: ObservableObject {
    @Published var configuration: StandaloneConfiguration?
    @Published var ready = false
    @Published private(set) var showingJITCheck = true
    @Published var message = ""
    @Published var prompt = false
    @Published var error: String?
    private var started = false
    private var checking = false
    private var prompted = false
    private let worker = DispatchQueue(label: "kudroid.standalone.boot", qos: .userInitiated)

    var modernJIT: Bool { ProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 26 }
    var redirectMessage: String {
        modernJIT ? "Redirecting to stikdebug to enable JIT? (Assign universal.js script too!,hold the app then press assign script)"
                  : "Redirecting to stikdebug to enable JIT?"
    }

    func start() {
        guard !started else { return }
        started = true
        worker.async {
            do {
                let config = try StandaloneConfiguration.load()
                let fm = FileManager.default
                let root = try StandaloneStorage.prepare(config)
                kudroid_set_log_paths(root.path, StandaloneStorage.logs.path)
                kudroid_set_standalone_root(root.path)
                let code = root.appendingPathComponent("data/app/" + config.package)
                let marker = root.appendingPathComponent("installation.txt")
                let installed = try? String(contentsOf: marker, encoding: .utf8)
                if installed != config.installation || !fm.fileExists(atPath: code.appendingPathComponent("app_info.json").path) {
                    let temporary = code.deletingLastPathComponent().appendingPathComponent(".install-" + UUID().uuidString)
                    defer { try? fm.removeItem(at: temporary) }
                    try fm.copyItem(at: Bundle.main.bundleURL.appendingPathComponent("GuestApp"), to: temporary)
                    let backup = code.deletingLastPathComponent().appendingPathComponent(".previous-" + UUID().uuidString)
                    if fm.fileExists(atPath: code.path) { try fm.moveItem(at: code, to: backup) }
                    do { try fm.moveItem(at: temporary, to: code) }
                    catch {
                        if fm.fileExists(atPath: backup.path) { try? fm.moveItem(at: backup, to: code) }
                        throw error
                    }
                    try? fm.removeItem(at: backup)
                    let seed = code.appendingPathComponent("SeedSdcard")
                    if fm.fileExists(atPath: seed.path) {
                        try Self.copyMissingFiles(from: seed, to: root.appendingPathComponent("sdcard"))
                        try fm.removeItem(at: seed)
                    }
                    try config.installation.write(to: marker, atomically: true, encoding: .utf8)
                }
                DispatchQueue.main.async { self.configuration = config; self.checkJIT(showBlackScreen: true) }
            } catch {
                DispatchQueue.main.async {
                    self.error = error.localizedDescription
                    self.message = "Could not start app"
                    self.showingJITCheck = false
                }
            }
        }
    }

    private static func copyMissingFiles(from source: URL, to destination: URL) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: destination, withIntermediateDirectories: true)
        for item in try fm.contentsOfDirectory(at: source, includingPropertiesForKeys: [.isDirectoryKey]) {
            let target = destination.appendingPathComponent(item.lastPathComponent)
            if try item.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true {
                try copyMissingFiles(from: item, to: target)
            } else if !fm.fileExists(atPath: target.path) {
                let staging = destination.appendingPathComponent(".seed-" + UUID().uuidString)
                defer { try? fm.removeItem(at: staging) }
                try fm.copyItem(at: item, to: staging)
                try fm.moveItem(at: staging, to: target)
            }
        }
    }

    func checkJIT(showBlackScreen: Bool = false) {
        guard configuration != nil, !ready else { return }
        if showBlackScreen { showingJITCheck = true }
        guard !checking else { return }
        checking = true
        let modern = modernJIT
        worker.async {
            let debugged = kudroid_debugger_attached() != 0
            let entitled = !modern && kudroid_has_jit_entitlement() != 0
            let enabled = modern ? kudroid_is_jit_enabled() != 0
                : debugged || (entitled && kudroid_is_jit_enabled() != 0)
            DispatchQueue.main.async {
                self.checking = false
                if enabled { self.prompt = false; self.error = nil; self.ready = true; return }
                self.showingJITCheck = false
                self.message = modern ? "Assign universal.js script on stikdebug to continue" : "JIT is required to start this app."
                if !self.prompted { self.prompted = true; self.prompt = true }
            }
        }
    }

    func openTrollStore() {
        JITActivation.openTrollStore { opened in
            if !opened { self.error = "Could not open TrollStore." }
        }
    }

    func openDebugger() {
        JITActivation.openStikDebug { opened in
            if !opened { self.error = "Could not open StikDebug. Install it and try again." }
        }
    }
}

struct StandaloneGuestView: View {
    @StateObject private var model = StandaloneBootModel()
    @Environment(\.scenePhase) private var scenePhase
    private let timer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()
    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            if model.ready, let config = model.configuration {
                DedicatedAppRunnerView(appName: config.package) { _, log in
                    model.error = String(log.suffix(4000)); model.message = "The app stopped."
                }.ignoresSafeArea()
            } else if !model.showingJITCheck {
                VStack(spacing: 20) {
                    Text(model.message).multilineTextAlignment(.center)
                    if let error = model.error { Text(error).foregroundColor(.red) }
                    if model.configuration != nil {
                        Button("Open StikDebug") { model.openDebugger() }
                        if !model.modernJIT { Button("TrollStore JIT") { model.openTrollStore() } }
                        Button("Check JIT") { model.checkJIT(showBlackScreen: true) }
                    }
                }.padding()
            }
            if model.ready, let error = model.error {
                VStack { Text(error).font(.caption).padding(); Button("Close") { exit(0) } }
                    .background(Color.black.opacity(0.9)).padding()
            }
        }
        .statusBar(hidden: true)
        .onAppear { model.start() }
        .onReceive(timer) { _ in if scenePhase == .active { model.checkJIT() } }
        .onChange(of: scenePhase) { phase in if phase == .active { model.checkJIT(showBlackScreen: true) } }
        .alert(model.redirectMessage, isPresented: $model.prompt) {
            Button("Yes") { model.openDebugger() }
            if !model.modernJIT { Button("TrollStore JIT") { model.openTrollStore() } }
            Button("No", role: .cancel) { exit(0) }
        } message: { Text(model.message) }
    }
}
