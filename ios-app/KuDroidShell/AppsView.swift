import SwiftUI
import UIKit
import AVFAudio
import AVFoundation
import Metal
import QuartzCore
import CoreMotion
import Combine
import UniformTypeIdentifiers

struct AppItem: Identifiable {
    let id: String // folder name (e.g. "rolling-sky-5-5-8")
    let displayName: String // display name (e.g. "Rolling Sky")
    let version: String // version (e.g. "5.5.8")
    let iconImage: UIImage?
}

// mark: - application tab
struct AppsView: View {
    @EnvironmentObject private var session: AppSession
    @Binding var fullLog: String
    @State private var installedApps: [AppItem] = []
    @State private var showAPKPicker = false
    @ObservedObject private var installer = APKInstallModel.shared
    @ObservedObject private var dataReset = AppDataResetModel.shared
    @State private var reverseApp: AppItem?
    @State private var shortcutApp: AppItem?
    @State private var resetApp: AppItem?
    @State private var showDataReset = false
    @State private var showRenameAlert = false
    // App deletion status — shows progress instead of freeze UI.
    @State private var deletingApp: String? = nil
    @State private var deletePhase = ""
    @State private var deletePercent: Double = 0
    // [DEBUG] count callbacks from native — watchdog based on this instead of percent
    // (percent remains 0 when deleting many small files even though native is running).
    @State private var deleteCbCount = 0
    @State private var renamingAppId: String = ""
    @State private var newAppName: String = ""
    
    private var androidRootAppsURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("android_root/data/app", isDirectory: true)
    }
    
    var body: some View {
        NavigationView {
            ZStack {
                ShellStyle.background.ignoresSafeArea()
                
                VStack(spacing: 0) {
                    HStack(alignment: .center, spacing: 20) {
                        VStack(alignment: .leading, spacing: 6) {
                            Text("Kudroid")
                                .font(.system(size: 34, weight: .bold, design: .rounded))
                                .tracking(-1)
                            Text("YOUR ANDROID LIBRARY")
                                .font(.system(size: 10, weight: .semibold))
                                .tracking(2).foregroundColor(.secondary)
                        }
                        Spacer()
                        Button { showAPKPicker = true } label: {
                            Image(systemName: "plus")
                                .font(.system(size: 21, weight: .medium))
                                .frame(width: 48, height: 48)
                                .foregroundColor(ShellStyle.inverse)
                                .background(ShellStyle.ink)
                                .clipShape(Circle())
                        }
                        .accessibilityLabel("Install APK")
                        .disabled(installer.busy)
                    }
                    .padding(.horizontal, 24).padding(.top, 20).padding(.bottom, 14)

                    if installedApps.isEmpty {
                        VStack(spacing: 16) {
                            Spacer()
                            Image(systemName: "cube.box.fill")
                                .font(.system(size: 56, weight: .ultraLight))
                                .foregroundColor(.secondary)
                            Text("Your library starts here")
                                .font(.headline)
                            Text("Install an APK to bring your Android apps here.")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                            Spacer()
                        }
                    } else {
                        List(installedApps) { app in
                            HStack(spacing: 12) {
                                if let icon = app.iconImage {
                                    Image(uiImage: icon)
                                        .resizable()
                                        .aspectRatio(contentMode: .fill)
                                        .frame(width: 48, height: 48)
                                        .clipShape(RoundedRectangle(cornerRadius: 11, style: .continuous))
                                        .overlay(
                                            RoundedRectangle(cornerRadius: 11, style: .continuous)
                                                .stroke(ShellStyle.ink.opacity(0.15), lineWidth: 0.5)
                                        )
                                        .shadow(color: .black.opacity(0.3), radius: 4, x: 0, y: 2)
                                } else {
                                    ZStack {
                                        RoundedRectangle(cornerRadius: 11, style: .continuous)
                                            .fill(ShellStyle.ink.opacity(0.06))
                                            .frame(width: 48, height: 48)
                                            .overlay(
                                                RoundedRectangle(cornerRadius: 11, style: .continuous)
                                                    .stroke(ShellStyle.ink.opacity(0.3), lineWidth: 1)
                                            )
                                        Image(systemName: "cube.fill")
                                            .font(.system(size: 22))
                                            .foregroundColor(.primary)
                                    }
                                }
                                
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(app.displayName)
                                        .font(.headline)
                                        .foregroundColor(.primary)
                                        .lineLimit(1)
                                    
                                    HStack(spacing: 6) {
                                        Text("v\(app.version)")
                                            .font(.caption2.bold())
                                            .padding(.horizontal, 6)
                                            .padding(.vertical, 2)
                                            .background(ShellStyle.ink.opacity(0.2))
                                            .foregroundColor(.primary)
                                            .cornerRadius(6)
                                    }
                                }
                                
                                Spacer()
                                
                                Button(action: {
                                    if dataReset.canLaunch(app.id) { session.activeGuestApp = app.id }
                                }) {
                                    Text(dataReset.pending.contains(app.id) ? "Pending" : "Open")
                                        .font(.caption.bold())
                                        .padding(.horizontal, 16)
                                        .padding(.vertical, 8)
                                        .background(ShellStyle.ink)
                                        .foregroundColor(ShellStyle.inverse)
                                        .cornerRadius(16)
                                }.disabled(!dataReset.canLaunch(app.id))
                                .buttonStyle(.borderless)
                                Menu {
                                    Button { reverseApp = app } label: {
                                        Label("Reverse app", systemImage: "arrow.triangle.2.circlepath")
                                    }
                                    Button { shortcutApp = app } label: {
                                        Label("Create a home shortcut", systemImage: "plus.app")
                                    }
                                    Button(role: .destructive) {
                                        resetApp = app
                                        showDataReset = true
                                    } label: {
                                        Label("Delete all app data", systemImage: "trash.slash")
                                    }
                                    Button {
                                        renamingAppId = app.id
                                        newAppName = app.displayName
                                        showRenameAlert = true
                                    } label: {
                                        Label("Rename", systemImage: "pencil")
                                    }
                                    Button(role: .destructive) {
                                        clearAppCache(name: app.id)
                                    } label: {
                                        Label("Clear Cache", systemImage: "trash")
                                    }
                                    Button(role: .destructive) {
                                        deleteApp(name: app.id)
                                    } label: {
                                        Label("Delete App", systemImage: "xmark.bin")
                                    }
                                } label: {
                                    Image(systemName: "ellipsis")
                                        .font(.system(size: 18, weight: .semibold))
                                        .frame(width: 44, height: 44)
                                        .contentShape(Rectangle())
                                }
                                .buttonStyle(.borderless)
                                .accessibilityLabel("Actions for " + app.displayName)
                            }
                            .padding(.vertical, 12)
                            .background(ImmediateScrollTouches().allowsHitTesting(false))
                            .listRowBackground(ShellStyle.surface)
                        }
                        .listStyle(.insetGrouped)
                        .onAppear {
                            DispatchQueue.global(qos: .userInitiated).async {
                                _ = kudroid_is_jit_enabled()
                            }
                            // Keep the grouped background visible.
                            UITableView.appearance().backgroundColor = .clear
                        }
                    }
                }

                // Overlay the app deletion process — block interaction while deleting
                // but still displays phase + % instead of freezing the screen.
                if let name = deletingApp {
                    ZStack {
                        Color.black.opacity(0.65).ignoresSafeArea()
                        VStack(spacing: 16) {
                            ProgressView()
                                .progressViewStyle(.circular)
                                .scaleEffect(1.4)
                            Text("Deleting \(name)")
                                .font(.headline)
                                .foregroundColor(.primary)
                            Text(deletePhase)
                                .font(.caption)
                                .foregroundColor(.secondary)
                                .lineLimit(1)
                            ProgressView(value: deletePercent, total: 100)
                                .progressViewStyle(.linear)
                                .frame(width: 220)
                            Text("\(Int(deletePercent))%")
                                .font(.caption2.monospacedDigit())
                                .foregroundColor(.primary)
                        }
                        .padding(28)
                        .background(
                            RoundedRectangle(cornerRadius: 18, style: .continuous)
                                .fill(ShellStyle.surface.opacity(0.97))
                        )
                    }
                }
            }
            .navigationBarHidden(true)
            .onAppear(perform: loadInstalledApps)
            .sheet(item: $reverseApp) { app in
                ReverseView(installedAppID: app.id, installedName: app.displayName)
            }
            .sheet(item: $shortcutApp) { app in HomeShortcutView(app: app) }
            .alert("Delete all app data?", isPresented: $showDataReset) {
                Button("Cancel", role: .cancel) { resetApp = nil }
                Button("Delete Data", role: .destructive) {
                    if let app = resetApp { dataReset.request(app) }
                    resetApp = nil
                }
            } message: {
                Text("This deletes saves, settings, accounts and cache for \(resetApp?.displayName ?? "this app"). The installed app stays. If a restart is needed, deletion finishes when KuDroid reopens.")
            }
            .fileImporter(isPresented: $showAPKPicker, allowedContentTypes: APKFileTypes.supported) { result in
                switch result {
                case .success(let url): installer.install(url)
                case .failure(let error): installer.report(error.localizedDescription)
                }
            }
            .onReceive(installer.$busy) { busy in session.libraryBusy = busy || deletingApp != nil }
            .onReceive(installer.$result) { log in
                guard let log = log else { return }
                fullLog = log
                loadInstalledApps()
            }
            .alert("Rename App", isPresented: $showRenameAlert) {
                TextField("App Name", text: $newAppName)
                Button("Cancel", role: .cancel) {}
                Button("Save") {
                    renameApp(id: renamingAppId, newName: newAppName)
                }
            } message: {
                Text("Enter a new display name for this app.")
            }
        }
    }

    private func loadInstalledApps() {
        guard let url = androidRootAppsURL else { return }
        do {
            let contents = try FileManager.default.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isDirectoryKey])
            var items: [AppItem] = []
            for folder in contents.filter({ $0.hasDirectoryPath }) {
                var folderName = folder.lastPathComponent
                var currentFolderURL = folder
                
                // 1. Automatically normalize immediately if the directory name has a version underscore (e.g. ru.zdevs.zarchiver_1.0.10)
                if folderName.contains("_") {
                    let cleanPkg = String(folderName.split(separator: "_")[0])
                    if cleanPkg.contains(".") {
                        let targetURL = url.appendingPathComponent(cleanPkg, isDirectory: true)
                        if !FileManager.default.fileExists(atPath: targetURL.path) {
                            try? FileManager.default.moveItem(at: currentFolderURL, to: targetURL)
                            folderName = cleanPkg
                            currentFolderURL = targetURL
                        } else {
                            try? FileManager.default.removeItem(at: currentFolderURL)
                            folderName = cleanPkg
                            currentFolderURL = targetURL
                        }
                    }
                }

                // Read app_info.json if available
                var infoURL = currentFolderURL.appendingPathComponent("app_info.json")
                var displayName = prettifyAppName(folderName)
                var version = extractVersionFromFolderName(folderName) ?? "1.0.0"

                if let infoData = try? Data(contentsOf: infoURL),
                   let rawObj = try? JSONSerialization.jsonObject(with: infoData),
                   let json = rawObj as? [String: Any] {
                    if let realPkg = json["package"] as? String, !realPkg.isEmpty && realPkg != folderName {
                        let targetURL = url.appendingPathComponent(realPkg, isDirectory: true)
                        if !FileManager.default.fileExists(atPath: targetURL.path) {
                            try? FileManager.default.moveItem(at: currentFolderURL, to: targetURL)
                            folderName = realPkg
                            currentFolderURL = targetURL
                            infoURL = currentFolderURL.appendingPathComponent("app_info.json")
                        }
                    }
                    if let label = json["label"] as? String, !label.isEmpty {
                        if label.contains(".") || label.lowercased().hasSuffix(".apk") {
                            displayName = prettifyAppName(label)
                        } else {
                            displayName = label
                        }
                    }
                    if let ver = json["version"] as? String, !ver.isEmpty && ver != "1.0.0" {
                        version = ver
                    }
                }

                // Version from AndroidManifest.xml is STANDARD SOURCE — app_info.json
                // may be stale (written from an old version when the AXML parser still had errors → "1.0.0").
                // Read the binary manifest directly each time the app is listed.
                if let mfData = try? Data(contentsOf: currentFolderURL.appendingPathComponent("AndroidManifest.xml")),
                   let mfVer = extractVersionNameFromAxml(mfData), !mfVer.isEmpty {
                    version = mfVer
                }
                
                // Read app_icon.png if available
                var iconImg: UIImage? = nil
                let iconURL = currentFolderURL.appendingPathComponent("app_icon.png")
                if FileManager.default.fileExists(atPath: iconURL.path) {
                    iconImg = UIImage(contentsOfFile: iconURL.path)
                }
                
                items.append(AppItem(id: folderName, displayName: displayName, version: version, iconImage: iconImg))
            }
            installedApps = items.sorted { $0.displayName.localizedCaseInsensitiveCompare($1.displayName) == .orderedAscending }
        } catch {
            print("Failed to load apps: \(error)")
            installedApps = []
        }
    }
    
    private func prettifyAppName(_ raw: String) -> String {
        if raw.isEmpty { return "Android App" }
        var s = raw

        // 1. Separate package name if any (eg "com.discord" -> "discord")
        if s.contains(".") {
            let parts = s.split(separator: ".")
            var chosen = parts.last
            let flavorSuffixes: Set<String> = [
                "google", "android", "store", "play", "release",
                "debug", "beta", "alpha", "taptap", "bilibili", "amazon", "dist"
            ]
            if parts.count > 1, let lastStr = chosen, flavorSuffixes.contains(lastStr.lowercased()) {
                chosen = parts[parts.count - 2]
            }
            if let chosen = chosen {
                s = String(chosen)
            }
        }

        // 2. Remove the .apk extension
        if s.lowercased().hasSuffix(".apk") {
            s = String(s.dropLast(4))
        }

        // The cleanup below derives a readable name from any package or file name. A
        // table mapping four specific apps to their branded spelling used to sit here,
        // which mislabelled anything whose name merely contained one of those words.

        // 3. Separate common junk prefixes/suffixes in mod/port APK file names
        let junkWords: Set<String> = [
            "apk", "arm64", "arm64v8a", "v8a", "vulkan", "gles", "mod",
            "signed", "release", "debug", "beta", "alpha", "jakitomzed",
            "bandishare", "apkpure", "moddroid", "an1", "apkmirror"
        ]
        
        let components = s.split(whereSeparator: { $0 == "-" || $0 == "_" || $0 == " " })
        var validWords: [String] = []
        for comp in components {
            let str = String(comp)
            let wLower = str.lowercased()
            if junkWords.contains(wLower) { continue }
            // Ignore pure numeric version e.g. "5.5.8" or "v202"
            if str.allSatisfy({ $0.isNumber || $0 == "." }) || (str.hasPrefix("v") && str.dropFirst().allSatisfy({ $0.isNumber || $0 == "." })) {
                continue
            }
            validWords.append(str)
        }
        
        if !validWords.isEmpty {
            s = validWords.joined(separator: " ")
        } else {
            s = s.replacingOccurrences(of: "-", with: " ").replacingOccurrences(of: "_", with: " ")
        }

        // 4. Capitalize first letter and separate CamelCase (e.g. "rollingsky" -> "Rolling Sky", "Discord" -> "Discord")
        var result = ""
        for (i, char) in s.enumerated() {
            if i > 0 && char.isUppercase && s[s.index(s.startIndex, offsetBy: i - 1)].isLowercase && !result.hasSuffix(" ") {
                result += " "
            }
            result.append(char)
        }

        return result.capitalized
    }
    
    private func runApp(name: String) {
        // Re-activate on every launch: the onAppear session can be lost to
        // interruptions or backgrounding long before the user taps an app,
        // and an inactive session plays AudioQueue output silently.
        activateAudioSession()
        guard let cString = kudroid_run_apk(name) else {
            fullLog = "[kudroid_core] ERROR: null result from kudroid_run_apk"
            return
        }
        fullLog = String(cString: cString)
        free(UnsafeMutablePointer(mutating: cString))
    }
    
    private func clearAppCache(name: String) {
        let success = kudroid_clear_app_cache(name)
        fullLog = success == 1 ? "Cleared cache for \(name)" : "Failed to clear cache for \(name)"
    }
    
    private func deleteApp(name: String) {
        guard deletingApp == nil else { return } // Deleting another app
        deletingApp = name
        session.libraryDeletionInProgress = true
        session.libraryBusy = true
        deletePhase = "Preparing…"
        deletePercent = 0
        // [DEBUG UNINSTALL] marks the start — watchdog below
        // Use it to detect frozen deletions (stuck at 0% with no callbacks).
        let start = Date()
        DispatchQueue.main.async {
            self.fullLog += "\n[uninstall] START \(name) @ \(start)"
        }
        DispatchQueue.global(qos: .userInitiated).async {
            final class Ctx {
                let phase: (String) -> Void
                let pct: (Double) -> Void
                init(_ p: @escaping (String) -> Void, _ q: @escaping (Double) -> Void) { phase = p; pct = q }
            }
            let ctx = Ctx({ text in
                DispatchQueue.main.async { self.deletePhase = text }
            }, { v in
                DispatchQueue.main.async {
                    self.deletePercent = v
                    self.deleteCbCount += 1
                }
            })
            let cb: @convention(c) (UnsafePointer<CChar>?, Int32, UnsafeMutableRawPointer?) -> Void = {
                phaseC, percent, ud in
                guard let raw = ud else { return }
                let ctx = Unmanaged<Ctx>.fromOpaque(raw).takeUnretainedValue()
                let text = phaseC.map { String(cString: $0) } ?? ""
                ctx.phase(text)
                ctx.pct(Double(percent))
            }
            let opaque = Unmanaged.passRetained(ctx).toOpaque()

            // [DEBUG] watchdog: if after 15 seconds there is NO callback from native
            // (not counting percent because 1% = ~22MB for large apps) → native stream
            // Almost certainly stuck. Runs on main queue to read @State safely.
            DispatchQueue.main.asyncAfter(deadline: .now() + 15) {
                if self.deletingApp == name && self.deleteCbCount == 0 {
                    self.fullLog += "\n[uninstall] ⚠️ WATCHDOG: no callback after 15s — native thread likely STUCK (permission or filesystem)"
                    self.deletePhase = "Stuck? (>15s, no activity)"
                }
            }

            let success = kudroid_delete_app_progress(name, cb, opaque)
            Unmanaged<Ctx>.fromOpaque(opaque).release()
            // [DEBUG] pull the native-side trace into the Swift log for viewing/copying
            // (also saved to kudroid_uninstall_debug.txt in Documents).
            if let cTrace = kudroid_uninstall_debug_log() {
                let trace = String(cString: cTrace)
                free(UnsafeMutablePointer(mutating: cTrace))
                DispatchQueue.main.async { self.fullLog += "\n" + trace }
            }
            let elapsed = Int(Date().timeIntervalSince(start))
            DispatchQueue.main.async {
                self.fullLog += success == 1
                    ? "\n[uninstall] DONE \(name) in \(elapsed)s"
                    : "\n[uninstall] FAILED \(name) after \(elapsed)s"
                if success == 1 { self.dataReset.forget(name) }
                self.deletingApp = nil
                self.session.libraryDeletionInProgress = false
                self.session.libraryBusy = self.installer.busy
                self.loadInstalledApps()
            }
        }
    }
    
    private func renameApp(id: String, newName: String) {
        let trimmed = newName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, let root = androidRootAppsURL else { return }
        let infoURL = root.appendingPathComponent(id).appendingPathComponent("app_info.json")
        var json: [String: Any] = [:]
        if let data = try? Data(contentsOf: infoURL),
           let rawObj = try? JSONSerialization.jsonObject(with: data),
           let existing = rawObj as? [String: Any] {
            json = existing
        }
        json["label"] = trimmed
        if let outData = try? JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted]) {
            try? outData.write(to: infoURL)
        }
        loadInstalledApps()
    }
}

// mark: - application permission management tab (PermissionsView)
