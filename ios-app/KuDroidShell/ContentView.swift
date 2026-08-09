import SwiftUI
import UIKit

// MARK: - Core View
struct ContentView: View {
    @State private var fullLog = "KuDroid Core Status"
    @State private var jitStatus = "JIT: Unknown"
    
    var body: some View {
        TabView {
            AppsView(fullLog: $fullLog)
                .tabItem {
                    Label("Apps", systemImage: "square.grid.2x2.fill")
                }
            
            DebugView(fullLog: $fullLog, jitStatus: $jitStatus)
                .tabItem {
                    Label("Debug", systemImage: "terminal.fill")
                }
        }
        .preferredColorScheme(.dark)
        .onAppear {
            setupLogDir()
            jitStatus = runJitStatus()
        }
    }
}

// MARK: - Apps Tab
struct AppsView: View {
    @Binding var fullLog: String
    @State private var installedApps: [String] = []
    @State private var showAPKInstaller = false
    
    private var androidRootAppsURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("android_root/data/app", isDirectory: true)
    }
    
    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                
                VStack(spacing: 0) {
                    // Header
                    HStack {
                        Image(systemName: "cpu")
                            .font(.title)
                            .foregroundColor(.green)
                        Text("KuDroid")
                            .font(.title2)
                            .fontWeight(.black)
                        Spacer()
                        Button(action: { showAPKInstaller = true }) {
                            Image(systemName: "plus.circle.fill")
                                .font(.title2)
                                .foregroundColor(.green)
                        }
                    }
                    .padding()
                    
                    if installedApps.isEmpty {
                        VStack(spacing: 16) {
                            Spacer()
                            Image(systemName: "cube.box.fill")
                                .font(.system(size: 64))
                                .foregroundColor(.secondary)
                            Text("No Android Apps Installed")
                                .font(.headline)
                            Text("Tap the + button to install an APK")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                            Spacer()
                        }
                    } else {
                        List(installedApps, id: \.self) { appName in
                            HStack {
                                Image(systemName: "app.dashed")
                                    .font(.title)
                                    .foregroundColor(.green)
                                    .padding(.trailing, 8)
                                
                                VStack(alignment: .leading) {
                                    Text(appName)
                                        .font(.headline)
                                    Text("Native Arm64 Libraries")
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                                
                                Spacer()
                                
                                Button(action: {
                                    runApp(name: appName)
                                }) {
                                    Text("RUN")
                                        .font(.caption)
                                        .fontWeight(.bold)
                                        .padding(.horizontal, 16)
                                        .padding(.vertical, 8)
                                        .background(Color.green.opacity(0.2))
                                        .foregroundColor(.green)
                                        .cornerRadius(16)
                                }
                            }
                            .padding(.vertical, 4)
                            .listRowBackground(Color(.systemGray6))
                            .contextMenu {
                                Button(role: .destructive) {
                                    clearAppCache(name: appName)
                                } label: {
                                    Label("Clear Cache", systemImage: "trash")
                                }
                                Button(role: .destructive) {
                                    deleteApp(name: appName)
                                } label: {
                                    Label("Delete App", systemImage: "xmark.bin")
                                }
                            }
                        }
                        .onAppear {
                            // Pre-iOS 16 workaround
                            UITableView.appearance().backgroundColor = .clear
                        }
                    }
                }
            }
            .navigationBarHidden(true)
            .onAppear(perform: loadInstalledApps)
            .sheet(isPresented: $showAPKInstaller, onDismiss: loadInstalledApps) {
                APKInstallerView { log in
                    fullLog = log
                    showAPKInstaller = false
                }
            }
        }
    }
    
    private func loadInstalledApps() {
        guard let url = androidRootAppsURL else { return }
        do {
            let contents = try FileManager.default.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isDirectoryKey])
            installedApps = contents.filter { $0.hasDirectoryPath }.map { $0.lastPathComponent }.sorted()
        } catch {
            print("Failed to load apps: \(error)")
            installedApps = []
        }
    }
    
    private func runApp(name: String) {
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
        let success = kudroid_delete_app(name)
        fullLog = success == 1 ? "Deleted app \(name)" : "Failed to delete app \(name)"
        loadInstalledApps()
    }
}

// MARK: - Debug Tab
struct DebugView: View {
    @Binding var fullLog: String
    @Binding var jitStatus: String
    @State private var showCopyAlert = false
    
    private var previewLog: String {
        let lines = fullLog.components(separatedBy: "\n")
        if lines.count <= 25 { return fullLog }
        return lines.prefix(25).joined(separator: "\n")
            + "\n\n... (truncated — tap Copy to get full log)"
    }
    
    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                
                VStack(spacing: 12) {
                    HStack {
                        Text("Terminal Log")
                            .font(.headline)
                            .foregroundColor(.white)
                        Spacer()
                        Label(jitStatus, systemImage: jitStatus.contains("Enabled") ? "bolt.fill" : "bolt.slash.fill")
                            .font(.caption)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(jitStatus.contains("Enabled") ? Color.green.opacity(0.2) : Color.red.opacity(0.2))
                            .foregroundColor(jitStatus.contains("Enabled") ? .green : .red)
                            .cornerRadius(8)
                    }
                    .padding(.horizontal)
                    
                    ScrollView {
                        Text(previewLog)
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundColor(.green)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(12)
                    }
                    .frame(maxHeight: .infinity)
                    .background(Color(.systemGray6))
                    .cornerRadius(12)
                    .padding(.horizontal)
                    
                    // Buttons
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 8) {
                            Button("Copy Log") {
                                UIPasteboard.general.string = fullLog
                                showCopyAlert = true
                            }
                            .buttonStyle(.bordered)
                            
                            Button("Test JNI JVM") { fullLog = runJniJvmTest() }
                                .buttonStyle(.bordered)
                            
                            Button("Bionic Test") { fullLog = runBionicExecutionTest() }
                                .buttonStyle(.bordered)
                            Button("Multi-ELF") { fullLog = runMultiElfTest() }
                                .buttonStyle(.bordered)
                            Button("VFS Test") { fullLog = runVFSExtendedTest() }
                                .buttonStyle(.bordered)
                        }
                        .padding(.horizontal)
                    }
                    .padding(.bottom, 8)
                }
            }
            .navigationBarHidden(true)
            .alert("Copied!", isPresented: $showCopyAlert) {
                Button("OK", role: .cancel) {}
            } message: {
                Text("Full log (\(fullLog.count) chars) copied to clipboard.")
            }
        }
    }
}

// MARK: - Native Bridge Helpers

func setupLogDir() {
    if let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
        kudroid_set_log_dir(docs.path)
        kudroid_set_documents_dir(docs.path)
        let apkInbox = docs.appendingPathComponent("put_apk_here", isDirectory: true)
        try? FileManager.default.createDirectory(at: apkInbox, withIntermediateDirectories: true)
    }
}

func runJniJvmTest() -> String {
    guard let cString = kudroid_test_jvm() else { return "Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runVFSSelfTest() -> String {
    guard let cString = kudroid_vfs_self_test_log() else { return "Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runVFSExtendedTest() -> String {
    guard let cString = kudroid_vfs_extended_test_log() else { return "Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func installAPK(at apkURL: URL) -> String {
    guard let cString = kudroid_install_apk(apkURL.path) else { return "[kudroid_apk] Native installer returned no log" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

// MARK: - APK Installer View
struct APKInstallerView: View {
    let onInstall: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var apkFiles: [URL] = []
    @State private var selectedAPK: URL?
    @State private var status = ""

    private var inboxURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("put_apk_here", isDirectory: true)
    }

    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                VStack(spacing: 0) {
                    if let inboxURL {
                        Text(inboxURL.path)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundColor(.secondary)
                            .textSelection(.enabled)
                            .padding()
                    }

                    List(apkFiles, id: \.path) { apk in
                        Button {
                            selectedAPK = apk
                        } label: {
                            HStack {
                                Image(systemName: "shippingbox")
                                VStack(alignment: .leading) {
                                    Text(apk.lastPathComponent).foregroundColor(.white)
                                    Text(fileSize(apk)).font(.caption).foregroundColor(.secondary)
                                }
                                Spacer()
                                if selectedAPK == apk {
                                    Image(systemName: "checkmark.circle.fill").foregroundColor(.green)
                                }
                            }
                        }
                        .listRowBackground(Color(.systemGray6))
                    }
                    .onAppear {
                        // Pre-iOS 16 workaround
                        UITableView.appearance().backgroundColor = .clear
                    }
                    .overlay {
                        if apkFiles.isEmpty {
                            VStack(spacing: 8) {
                                Image(systemName: "folder.badge.plus").font(.largeTitle).foregroundColor(.secondary)
                                Text("Put .apk files in Documents/put_apk_here").multilineTextAlignment(.center).foregroundColor(.white)
                                Text("Then tap Refresh").font(.caption).foregroundColor(.secondary)
                            }
                            .padding()
                        }
                    }

                    if !status.isEmpty {
                        Text(status).font(.caption).foregroundColor(.red).padding(.horizontal)
                    }

                    HStack {
                        Button("Refresh") { refresh() }
                            .buttonStyle(.bordered)
                        Button("Install Selected") {
                            guard let selectedAPK else { return }
                            onInstall(installAPK(at: selectedAPK))
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(.green)
                        .disabled(selectedAPK == nil)
                    }
                    .padding()
                }
            }
            .navigationTitle("Install APK")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") { dismiss() }.foregroundColor(.green)
                }
            }
            .onAppear { refresh() }
        }
    }

    private func refresh() {
        guard let inboxURL else {
            status = "Documents directory unavailable"
            return
        }
        do {
            try FileManager.default.createDirectory(at: inboxURL, withIntermediateDirectories: true)
            apkFiles = try FileManager.default.contentsOfDirectory(
                at: inboxURL, includingPropertiesForKeys: [.fileSizeKey], options: [.skipsHiddenFiles]
            ).filter { $0.pathExtension.lowercased() == "apk" }
             .sorted { $0.lastPathComponent.localizedCaseInsensitiveCompare($1.lastPathComponent) == .orderedAscending }
            if let selectedAPK, !apkFiles.contains(selectedAPK) { self.selectedAPK = nil }
            status = ""
        } catch {
            status = "Cannot scan put_apk_here: \(error.localizedDescription)"
        }
    }

    private func fileSize(_ url: URL) -> String {
        let values = try? url.resourceValues(forKeys: [.fileSizeKey])
        return ByteCountFormatter.string(fromByteCount: Int64(values?.fileSize ?? 0), countStyle: .file)
    }
}

// MARK: - Other Native Helpers
func runJitStatus() -> String {
    guard let cString = kudroid_jit_status() else { return "JIT: Unknown" }
    let status = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return status
}

func runBionicExecutionTest() -> String {
    guard let bundledURL = Bundle.main.url(forResource: "test_bionic_lib", withExtension: "so") else {
        return "❌ test_bionic_lib.so not found in bundle"
    }
    let tmpURL = FileManager.default.temporaryDirectory.appendingPathComponent("test_bionic_lib.so")
    do {
        if FileManager.default.fileExists(atPath: tmpURL.path) { try FileManager.default.removeItem(at: tmpURL) }
        try FileManager.default.copyItem(at: bundledURL, to: tmpURL)
    } catch {
        return "❌ Failed to copy bundled Bionic .so: \(error.localizedDescription)"
    }
    guard let cString = kudroid_bionic_execution_test(tmpURL.path) else { return "❌ Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runMultiElfTest() -> String {
    guard let consumer = Bundle.main.url(forResource: "libkudroid_consumer", withExtension: "so"),
          let provider = Bundle.main.url(forResource: "libkudroid_provider", withExtension: "so") else {
        return "❌ Multi-ELF provider/consumer libraries are missing from bundle"
    }
    let directory = FileManager.default.temporaryDirectory
    let consumerURL = directory.appendingPathComponent("libkudroid_consumer.so")
    let providerURL = directory.appendingPathComponent("libkudroid_provider.so")
    do {
        for url in [consumerURL, providerURL] {
            if FileManager.default.fileExists(atPath: url.path) { try FileManager.default.removeItem(at: url) }
        }
        try FileManager.default.copyItem(at: consumer, to: consumerURL)
        try FileManager.default.copyItem(at: provider, to: providerURL)
    } catch {
        return "❌ Failed to prepare multi-ELF test: \(error.localizedDescription)"
    }
    guard let cString = kudroid_multi_elf_test(consumerURL.path, providerURL.path) else { return "❌ Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

#Preview {
    ContentView()
}