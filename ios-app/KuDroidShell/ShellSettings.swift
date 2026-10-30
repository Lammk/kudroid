import SwiftUI
import UIKit

enum ShellAppearance: String, CaseIterable, Identifiable {
    case system, light, dark
    var id: String { rawValue }
    var title: String { rawValue.capitalized }
    var colorScheme: ColorScheme? {
        switch self {
        case .system: return nil
        case .light: return .light
        case .dark: return .dark
        }
    }
}

enum ShellStyle {
    static let background = Color(uiColor: .systemGroupedBackground)
    static let surface = Color(uiColor: .secondarySystemGroupedBackground)
    static let ink = Color(uiColor: .label)
    static let inverse = Color(uiColor: .systemBackground)
    static let line = Color(uiColor: .separator).opacity(0.35)
}

struct ShellSettingsView: View {
    @AppStorage("shellAppearance") private var appearance = ShellAppearance.system.rawValue

    var body: some View {
        NavigationView {
            Form {
                Section(header: Text("Appearance")) {
                    Picker("Theme", selection: $appearance) {
                        ForEach(ShellAppearance.allCases) { theme in
                            Text(theme.title).tag(theme.rawValue)
                        }
                    }.pickerStyle(.segmented)
                }
                Section(header: Text("About")) {
                    HStack {
                        Label("Kudroid", systemImage: "square.stack.3d.up")
                        Spacer()
                        Text("v" + appVersion()).foregroundColor(.secondary)
                    }
                }
                Section(header: Text("Developer connection"), footer: Text("Connect to a KDB server for remote debugging.")) {
                    KDBConnectionView()
                }
            }
            .navigationTitle("Settings")
        }
        .navigationViewStyle(.stack)
    }
}

struct KDBConnectionView: View {
    @State private var host = UserDefaults.standard.string(forKey: "kdb_server_ip") ?? ""
    @State private var connected = false

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Label("KDB", systemImage: "antenna.radiowaves.left.and.right")
                    .font(.headline)
                Spacer()
                Text(connected ? "Connected" : "Disconnected")
                    .font(.caption).foregroundColor(.secondary)
            }
            TextField("Server address · 192.168.1.5:8080", text: $host)
                .font(.subheadline.monospaced())
                .textInputAutocapitalization(.never)
                .disableAutocorrection(true)
                .keyboardType(.URL)
                .accessibilityLabel("KDB server address")
            Button {
                UserDefaults.standard.set(host, forKey: "kdb_server_ip")
                if connected { RemoteDebugClient.shared.disconnect() }
                else { RemoteDebugClient.shared.connect(host: host) }
            } label: {
                Text(connected ? "Disconnect" : "Connect")
                    .font(.subheadline.weight(.semibold))
                    .frame(maxWidth: .infinity).padding(.vertical, 12)
                    .background(ShellStyle.ink).foregroundColor(ShellStyle.inverse)
                    .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            }
            .buttonStyle(.plain)
            .disabled(!connected && host.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        }
        .padding(.vertical, 8)
        .onAppear {
            connected = RemoteDebugClient.shared.connectionIsActive
            RemoteDebugClient.shared.onConnectionStatusChanged = { connected = $0 }
        }
    }
}
