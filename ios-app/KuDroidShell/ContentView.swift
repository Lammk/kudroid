import SwiftUI
import UIKit
import UniformTypeIdentifiers

struct ContentView: View {
    @State private var fullLog = "KuDroid Core Status"
    @State private var showCopyAlert = false
    @State private var soPath = "No file selected"
    @State private var showFilePicker = false
    /// Hold the security-scoped URL so it stays accessible
    @State private var selectedURL: URL? = nil

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

            ScrollView {
                Text(previewLog)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundColor(.primary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(8)
            }
            .frame(maxHeight: 250)
            .background(Color(.systemGray6))
            .cornerRadius(8)
            .padding(.horizontal)

            // Selected file path display
            Text(soPath)
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(.secondary)
                .lineLimit(2)
                .padding(.horizontal)

            // Buttons row 1: Browse + Load .so
            HStack(spacing: 12) {
                Button(action: { showFilePicker = true }) {
                    Label("Browse .so", systemImage: "folder")
                }
                .buttonStyle(.bordered)

                Button("Load .so") {
                    fullLog = runLoadElf(path: soPath)
                }
                .buttonStyle(.borderedProminent)
                .disabled(soPath == "No file selected")
            }

            // Buttons row 2: Self-test + Copy
            HStack(spacing: 12) {
                Button("Self-Test") {
                    fullLog = runElfLoaderTest()
                }
                .buttonStyle(.bordered)

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
        .fileImporter(
            isPresented: $showFilePicker,
            allowedContentTypes: [
                .item,
                UTType(filenameExtension: "so") ?? .unixExecutable,
                .unixExecutable
            ].compactMap { $0 },
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first {
                    _ = url.startAccessingSecurityScopedResource()
                    selectedURL = url
                    soPath = url.lastPathComponent
                }
            case .failure(let error):
                soPath = "Error: \(error.localizedDescription)"
            }
        }
    }

    private func loadSO() {
        guard let url = selectedURL else {
            fullLog = "❌ No file selected"
            return
        }
        let tmpDir = FileManager.default.temporaryDirectory
        let tmpURL = tmpDir.appendingPathComponent("loaded.so")
        do {
            if FileManager.default.fileExists(atPath: tmpURL.path) {
                try FileManager.default.removeItem(at: tmpURL)
            }
            try FileManager.default.copyItem(at: url, to: tmpURL)
            fullLog = runLoadElf(path: tmpURL.path)
        } catch {
            fullLog = "❌ Failed to copy file: \(error.localizedDescription)"
        }
    }
}

/// Calls into kudroid_core C++ library.
/// Returns a detailed debug log string.
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

#Preview {
    ContentView()
}