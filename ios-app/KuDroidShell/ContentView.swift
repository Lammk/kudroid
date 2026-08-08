import SwiftUI
import UIKit

struct ContentView: View {
    @State private var fullLog = "KuDroid Core Status"
    @State private var showCopyAlert = false
    @State private var jitStatus = "JIT: Unknown"

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
        .onAppear {
            jitStatus = runJitStatus()
        }
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

#Preview {
    ContentView()
}