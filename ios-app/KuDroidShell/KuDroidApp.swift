import SwiftUI
import UIKit

@main
struct KuDroidApp: App {
    @AppStorage("active_guest_app") private var activeGuestApp: String = ""

    var body: some Scene {
        WindowGroup {
            if !activeGuestApp.isEmpty {
                DedicatedAppRunnerView(appName: activeGuestApp) {
                    activeGuestApp = ""
                }
                .ignoresSafeArea()
                .preferredColorScheme(.dark)
            } else {
                ContentView()
            }
        }
    }
}