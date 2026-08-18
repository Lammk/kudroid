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
    @Published var isSoRunning: Bool = false
    @Published var soRunningTitle: String = ""
}

struct GlobalMetalViewRepresentable: UIViewRepresentable {
    func makeUIView(context: Context) -> GlobalMetalView {
        let view = GlobalMetalView()
        view.backgroundColor = .black
        view.isMultipleTouchEnabled = true
        view.isUserInteractionEnabled = true
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

    override init(frame: CGRect) {
        super.init(frame: frame)
        self.isMultipleTouchEnabled = true
        self.isUserInteractionEnabled = true
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        self.isMultipleTouchEnabled = true
        self.isUserInteractionEnabled = true
    }

    private func injectTouch(_ touches: Set<UITouch>, action: Int32) {
        let scale = UIScreen.main.scale
        let totalCount = Int32(touches.count)
        var pointerIdx: Int32 = 0
        for touch in touches {
            let location = touch.location(in: self)
            kudroid_inject_touch_event_multi(Float(location.x * scale), Float(location.y * scale), action, pointerIdx, totalCount)
            pointerIdx += 1
        }
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        injectTouch(touches, action: 0) // ACTION_DOWN
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        injectTouch(touches, action: 2) // ACTION_MOVE
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        injectTouch(touches, action: 1) // ACTION_UP
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        injectTouch(touches, action: 3) // ACTION_CANCEL
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
                } else if session.isSoRunning {
                    ZStack {
                        Color.black.ignoresSafeArea()
                        GlobalMetalViewRepresentable()
                            .ignoresSafeArea()
                        VStack {
                            HStack(spacing: 8) {
                                Circle().fill(Color.green).frame(width: 8, height: 8)
                                Text(session.soRunningTitle.isEmpty ? "KuDroid Native Sandbox Active" : session.soRunningTitle)
                                    .font(.caption.monospaced())
                                    .foregroundColor(.green)
                                Spacer()
                            }
                            .padding()
                            Spacer()
                        }
                    }
                    .transition(.opacity)
                    .zIndex(50)
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
            .animation(.easeInOut(duration: 0.25), value: session.isSoRunning)
            .animation(.easeInOut(duration: 0.25), value: session.crashInfo != nil)
        }
    }
}