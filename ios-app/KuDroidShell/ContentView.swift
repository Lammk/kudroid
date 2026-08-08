import SwiftUI

struct ContentView: View {
    @State private var statusText = "KuDroid Core Status"

    var body: some View {
        VStack(spacing: 24) {
            Image(systemName: "cpu")
                .font(.system(size: 60))
                .foregroundColor(.green)

            Text("KuDroid v0.1")
                .font(.title)
                .fontWeight(.bold)

            Text(statusText)
                .font(.body)
                .foregroundColor(.secondary)

            Button("Test ELF Loader") {
                statusText = runElfLoaderTest()
            }
            .buttonStyle(.borderedProminent)
        }
        .padding()
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