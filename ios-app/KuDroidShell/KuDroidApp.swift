import SwiftUI
import UIKit

struct CrashInfo: Identifiable {
    let id = UUID()
    let appName: String
    let tailLog: String
}

class AppSession: ObservableObject {
    @Published var activeGuestApp: String = ""
    @Published var crashInfo: CrashInfo? = nil
}

struct GlobalMetalViewRepresentable: UIViewRepresentable {
    func makeUIView(context: Context) -> GlobalMetalView {
        let view = GlobalMetalView()
        view.backgroundColor = .clear
        view.isUserInteractionEnabled = false
        if let metalLayer = view.layer as? CAMetalLayer {
            metalLayer.device = MTLCreateSystemDefaultDevice()
            metalLayer.pixelFormat = .bgra8Unorm
            metalLayer.framebufferOnly = false
            metalLayer.allowsNextDrawableTimeout = false
            metalLayer.maximumDrawableCount = 3
            let scale = UIScreen.main.scale
            let bounds = UIScreen.main.bounds
            let w = Int(bounds.width * scale)
            let h = Int(bounds.height * scale)
            metalLayer.drawableSize = CGSize(width: w, height: h)
            kudroid_set_metal_layer(Unmanaged.passUnretained(metalLayer).toOpaque(), Int32(w), Int32(h), Float(scale))
        }
        return view
    }

    func updateUIView(_ uiView: GlobalMetalView, context: Context) {}
}

class GlobalMetalView: UIView {
    override class var layerClass: AnyClass {
        return CAMetalLayer.self
    }
}

@main
struct KuDroidApp: App {
    @StateObject private var session = AppSession()

    var body: some Scene {
        WindowGroup {
            ZStack {
                GlobalMetalViewRepresentable()
                    .ignoresSafeArea()

                if !session.activeGuestApp.isEmpty {
                    DedicatedAppRunnerView(
                        appName: session.activeGuestApp,
                        onExit: {
                            session.activeGuestApp = ""
                        },
                        onCrash: { appName, tailLog in
                            session.activeGuestApp = ""
                            session.crashInfo = CrashInfo(appName: appName, tailLog: tailLog)
                        }
                    )
                    .ignoresSafeArea()
                    .preferredColorScheme(.dark)
                    .environmentObject(session)
                } else {
                    ContentView()
                        .environmentObject(session)
                }

                if let crashInfo = session.crashInfo {
                    CrashAlertView(crashInfo: crashInfo) {
                        session.crashInfo = nil
                    }
                    .transition(.opacity.combined(with: .scale(scale: 0.95)))
                    .zIndex(100)
                }
            }
            .preferredColorScheme(.dark)
            .onAppear {
                RemoteDebugClient.shared.configure(session: session)
                let savedIP = UserDefaults.standard.string(forKey: "kdb_server_ip") ?? ""
                if !savedIP.isEmpty {
                    RemoteDebugClient.shared.connect(host: savedIP)
                }
            }
            .animation(.easeInOut(duration: 0.25), value: session.activeGuestApp)
            .animation(.easeInOut(duration: 0.25), value: session.crashInfo != nil)
        }
    }
}