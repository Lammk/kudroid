import SwiftUI
import UIKit

class AppSession: ObservableObject {
    @Published var activeGuestApp: String = ""
}

@main
struct KuDroidApp: App {
    @StateObject private var session = AppSession()

    var body: some Scene {
        WindowGroup {
            if !session.activeGuestApp.isEmpty {
                DedicatedAppRunnerView(appName: session.activeGuestApp) {
                    session.activeGuestApp = ""
                }
                .ignoresSafeArea()
                .preferredColorScheme(.dark)
                .environmentObject(session)
            } else {
                ContentView()
                    .environmentObject(session)
            }
        }
    }
}