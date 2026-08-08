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
/// Returns a debug string indicating loader status.
func runElfLoaderTest() -> String {
    let result = kudroid_self_test()
    let code = Int(result)
    return code == 0 ? "✅ ELF Loader OK (stub)" : "❌ Error code: \(code)"
}

#Preview {
    ContentView()
}