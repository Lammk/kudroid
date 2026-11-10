import SwiftUI
import UIKit
import AVFAudio
import AVFoundation
import Metal
import QuartzCore
import CoreMotion
import Combine
import UniformTypeIdentifiers

struct ContentView: View {
    @State private var fullLog = "KuDroid Core Status"
    @ObservedObject private var dataReset = AppDataResetModel.shared
    
    var body: some View {
        TabView {
            AppsView(fullLog: $fullLog)
                .tabItem {
                    Label("Apps", systemImage: "square.grid.2x2.fill")
                }
            
            PermissionsView()
                .tabItem {
                    Label("Permissions", systemImage: "lock.shield.fill")
                }
            
            ReverseView()
                .tabItem {
                    Label("Reverse", systemImage: "arrow.triangle.2.circlepath")
                }
            ShellSettingsView()
                .tabItem { Label("Settings", systemImage: "gearshape") }
        }
        .tint(ShellStyle.ink)
        .alert("App Data", isPresented: Binding(get: { AppDataResetModel.shared.message != nil }, set: { if !$0 { AppDataResetModel.shared.message = nil } })) {
            Button("OK", role: .cancel) { AppDataResetModel.shared.message = nil }
        } message: { Text(AppDataResetModel.shared.message ?? "") }
        .onAppear {
            setupLogDir()
            activateAudioSession()
            AppDelegate.restorePortraitIfIdle()
        }
    }
}
