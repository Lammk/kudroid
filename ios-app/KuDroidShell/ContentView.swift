import SwiftUI
import UIKit

struct ContentView: View {
    @State private var fullLog = "KuDroid Core Status"
    @State private var showCopyAlert = false

    /// Show only first 20 lines for readability.
    private var previewLog: String {
        let lines = fullLog.components(separatedBy: "\n")
        if lines.count <= 20 { return fullLog }
        return lines.prefix(20).joined(separator: "\n")
            + "\n\n... (truncated — tap Copy to get full log)"
    }

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "cpu")
                .font(.system(size: 48))
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
            .frame(maxHeight: 300)
            .background(Color(.systemGray6))
            .cornerRadius(8)
            .padding(.horizontal)

            HStack(spacing: 16) {
                Button("Test ELF Loader") {
                    fullLog = runElfLoaderTest()
                }
                .buttonStyle(.borderedProminent)

                Button("Copy Full Log") {
                    UIPasteboard.general.string = fullLog
                    showCopyAlert = true
                }
                .buttonStyle(.bordered)
            }
        }
        .padding()
        .alert("Copied!", isPresented: $showCopyAlert) {
            Button("OK", role: .cancel) {}
        } message: {
            Text("Full log (\(fullLog.count) chars) copied to clipboard.")
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

#Preview {
    ContentView()
}