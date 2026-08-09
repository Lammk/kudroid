import SwiftUI
import UIKit

struct ContentView: View {
    @State private var fullLog = "KuDroid Core Status"
    @State private var showCopyAlert = false
    @State private var jitStatus = "JIT: Unknown"
    @State private var showAPKInstaller = false

    /// Show only first 20 lines for readability.
    private var previewLog: String {
        let lines = fullLog.components(separatedBy: "\n")
        if lines.count <= 20 { return fullLog }
        return lines.prefix(20).joined(separator: "\n")
            + "\n\n... (truncated — tap Copy to get full log)"
    }

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: "cpu")
                .font(.system(size: 40))
                .foregroundColor(.green)

            Text("KuDroid v0.1")
                .font(.title)
                .fontWeight(.bold)

            Label(jitStatus, systemImage: jitStatus.contains("Enabled") ? "bolt.fill" : "bolt.slash.fill")
                .font(.subheadline)
                .foregroundColor(jitStatus.contains("Enabled") ? .green : .red)

            ScrollView {
                Text(previewLog)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundColor(.primary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(8)
            }
            .frame(maxHeight: 280)
            .background(Color(.systemGray6))
            .cornerRadius(8)
            .padding(.horizontal)

            // Buttons row 1: Load bundled .so + Self-test
            HStack(spacing: 12) {
                Button("Load Bundled .so") {
                    fullLog = runLoadBundledSO()
                }
                .buttonStyle(.borderedProminent)

                Button("Self-Test") {
                    fullLog = runElfLoaderTest()
                }
                .buttonStyle(.bordered)
            }

            // Buttons row 2: Execution Test
            HStack(spacing: 12) {
                Button("Execution Test") {
                    fullLog = runExecutionTest()
                }
                .buttonStyle(.borderedProminent)
                .tint(.orange)

                Button("Bionic Test") {
                    fullLog = runBionicExecutionTest()
                }
                .buttonStyle(.borderedProminent)
                .tint(.blue)
            }

            HStack(spacing: 12) {
                Button("Multi-ELF Test") {
                    fullLog = runMultiElfTest()
                }
                .buttonStyle(.borderedProminent)
                .tint(.purple)
            }

            HStack(spacing: 12) {
                Button("Run VFS Self-Test") {
                    fullLog = runVFSSelfTest()
                }
                .buttonStyle(.borderedProminent)
                .tint(.green)
            }

            HStack(spacing: 12) {
                Button("VFS Extended Test") {
                    fullLog = runVFSExtendedTest()
                }
                .buttonStyle(.borderedProminent)
                .tint(.teal)
            }

            HStack(spacing: 12) {
                Button("Install APK") {
                    showAPKInstaller = true
                }
                .buttonStyle(.borderedProminent)
                .tint(.indigo)
            }

            // Buttons row 3: Copy
            HStack(spacing: 12) {
                Button("Copy Full Log") {
                    UIPasteboard.general.string = fullLog
                    showCopyAlert = true
                }
                .buttonStyle(.bordered)
            }
        }
        .padding(.vertical)
        .alert("Copied!", isPresented: $showCopyAlert) {
            Button("OK", role: .cancel) {}
        } message: {
            Text("Full log (\(fullLog.count) chars) copied to clipboard.")
        }
        .sheet(isPresented: $showAPKInstaller) {
            APKInstallerView { log in
                fullLog = log
                showAPKInstaller = false
            }
        }
        .onAppear {
            setupLogDir()
            jitStatus = runJitStatus()
        }
    }
}

/// Point kudroid_core at the app's Documents dir for .txt logs + crash dumps.
func setupLogDir() {
    if let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
        kudroid_set_log_dir(docs.path)
        kudroid_set_documents_dir(docs.path)
        let apkInbox = docs.appendingPathComponent("put_apk_here", isDirectory: true)
        try? FileManager.default.createDirectory(at: apkInbox,
                                                 withIntermediateDirectories: true)
    }
}

/// Run VFS path remapping and redirected file I/O checks.
func runVFSSelfTest() -> String {
    guard let cString = kudroid_vfs_self_test_log() else {
        return "Error: null result from kudroid_vfs_self_test_log"
    }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runVFSExtendedTest() -> String {
    guard let cString = kudroid_vfs_extended_test_log() else {
        return "Error: null result from kudroid_vfs_extended_test_log"
    }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func installAPK(at apkURL: URL) -> String {
    guard let cString = kudroid_install_apk(apkURL.path) else {
        return "[kudroid_apk] Native installer returned no log"
    }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

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
                                Text(apk.lastPathComponent)
                                Text(fileSize(apk))
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                            Spacer()
                            if selectedAPK == apk {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundColor(.green)
                            }
                        }
                    }
                    .foregroundColor(.primary)
                }
                .overlay {
                    if apkFiles.isEmpty {
                        VStack(spacing: 8) {
                            Image(systemName: "folder.badge.plus")
                                .font(.largeTitle)
                            Text("Put .apk files in Documents/put_apk_here")
                                .multilineTextAlignment(.center)
                            Text("Then tap Refresh")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                        .padding()
                    }
                }

                if !status.isEmpty {
                    Text(status)
                        .font(.caption)
                        .foregroundColor(.red)
                        .padding(.horizontal)
                }

                HStack {
                    Button("Refresh") { refresh() }
                        .buttonStyle(.bordered)
                    Button("Install Selected") {
                        guard let selectedAPK else { return }
                        onInstall(installAPK(at: selectedAPK))
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(selectedAPK == nil)
                }
                .padding()
            }
            .navigationTitle("Install APK")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") { dismiss() }
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
            try FileManager.default.createDirectory(at: inboxURL,
                                                    withIntermediateDirectories: true)
            apkFiles = try FileManager.default.contentsOfDirectory(
                at: inboxURL,
                includingPropertiesForKeys: [.fileSizeKey],
                options: [.skipsHiddenFiles]
            )
            .filter { $0.pathExtension.lowercased() == "apk" }
            .sorted { $0.lastPathComponent.localizedCaseInsensitiveCompare(
                $1.lastPathComponent) == .orderedAscending }
            if let selectedAPK, !apkFiles.contains(selectedAPK) {
                self.selectedAPK = nil
            }
            status = ""
        } catch {
            status = "Cannot scan put_apk_here: \(error.localizedDescription)"
        }
    }

    private func fileSize(_ url: URL) -> String {
        let values = try? url.resourceValues(forKeys: [.fileSizeKey])
        return ByteCountFormatter.string(fromByteCount: Int64(values?.fileSize ?? 0),
                                         countStyle: .file)
    }
}

/// Query JIT availability from kudroid_core.
func runJitStatus() -> String {
    guard let cString = kudroid_jit_status() else {
        return "JIT: Unknown"
    }
    let status = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return status
}

/// Load test_lib.so bundled inside the app.
func runLoadBundledSO() -> String {
    guard let bundledURL = Bundle.main.url(forResource: "test_lib", withExtension: "so") else {
        return "❌ test_lib.so not found in bundle"
    }
    // Copy to tmp so C++ ifstream can read it
    let tmpURL = FileManager.default.temporaryDirectory.appendingPathComponent("test_lib.so")
    do {
        if FileManager.default.fileExists(atPath: tmpURL.path) {
            try FileManager.default.removeItem(at: tmpURL)
        }
        try FileManager.default.copyItem(at: bundledURL, to: tmpURL)
    } catch {
        return "❌ Failed to copy bundled .so: \(error.localizedDescription)"
    }
    return runLoadElf(path: tmpURL.path)
}

/// Calls into kudroid_core C++ library (self-test).
func runElfLoaderTest() -> String {
    guard let cString = kudroid_self_test_log() else {
        return "❌ Error: null result"
    }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

/// Load an ELF .so file via kudroid_core.
func runLoadElf(path: String) -> String {
    guard let cString = kudroid_load_elf(path) else {
        return "❌ Error: null result from kudroid_load_elf"
    }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

/// Run execution test on the already-loaded .so (Phase 2).
func runExecutionTest() -> String {
    // First ensure the .so is loaded via the bundled file
    guard let bundledURL = Bundle.main.url(forResource: "test_lib", withExtension: "so") else {
        return "❌ test_lib.so not found in bundle"
    }
    let tmpURL = FileManager.default.temporaryDirectory.appendingPathComponent("test_lib.so")
    // Always refresh: LiveContainer keeps tmp across updates, so a stale .so would persist.
    do {
        if FileManager.default.fileExists(atPath: tmpURL.path) {
            try FileManager.default.removeItem(at: tmpURL)
        }
        try FileManager.default.copyItem(at: bundledURL, to: tmpURL)
    } catch {
        return "❌ Failed to copy bundled .so: \(error.localizedDescription)"
    }

    guard let cString = kudroid_execution_test(tmpURL.path) else {
        return "❌ Error: null result from kudroid_execution_test"
    }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

/// Run the bundled ARM64 library that imports Bionic libc/liblog symbols.
func runBionicExecutionTest() -> String {
    guard let bundledURL = Bundle.main.url(forResource: "test_bionic_lib", withExtension: "so") else {
        return "❌ test_bionic_lib.so not found in bundle"
    }
    let tmpURL = FileManager.default.temporaryDirectory.appendingPathComponent("test_bionic_lib.so")
    do {
        if FileManager.default.fileExists(atPath: tmpURL.path) {
            try FileManager.default.removeItem(at: tmpURL)
        }
        try FileManager.default.copyItem(at: bundledURL, to: tmpURL)
    } catch {
        return "❌ Failed to copy bundled Bionic .so: \(error.localizedDescription)"
    }

    guard let cString = kudroid_bionic_execution_test(tmpURL.path) else {
        return "❌ Error: null result from kudroid_bionic_execution_test"
    }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

/// Load both bundled ELF files and test dependency/global symbol resolution.
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
            if FileManager.default.fileExists(atPath: url.path) {
                try FileManager.default.removeItem(at: url)
            }
        }
        try FileManager.default.copyItem(at: consumer, to: consumerURL)
        try FileManager.default.copyItem(at: provider, to: providerURL)
    } catch {
        return "❌ Failed to prepare multi-ELF test: \(error.localizedDescription)"
    }

    guard let cString = kudroid_multi_elf_test(consumerURL.path, providerURL.path) else {
        return "❌ Error: null result from kudroid_multi_elf_test"
    }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

#Preview {
    ContentView()
}