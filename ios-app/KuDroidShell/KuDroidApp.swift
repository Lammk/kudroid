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

final class SharedMetalContainer {
    static let shared = SharedMetalContainer()
    let view = GlobalMetalView()
    private init() {
        view.backgroundColor = .clear
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
    }
}

/// Stable multi-touch pointer identities for the guest.
///
/// UIKit hands us unordered *changed* sets per callback, so neither the set order
/// nor `touches.count` identifies a finger: the second finger down arrived as
/// another ACTION_DOWN with count 1, and a finger's slot flipped between callbacks.
/// The guest (and Android's contract) needs: one stable id per finger, DOWN only
/// for the first finger (POINTER_DOWN after), and the true total finger count.
/// Ids are the smallest free slot, so they stay dense; the Java layer additionally
/// clamps count >= index+1 as a backstop.
final class TouchPointerTracker {
    private var ids: [ObjectIdentifier: Int32] = [:]
    private var next: Int32 = 0

    var activeCount: Int32 { Int32(ids.count) }

    /// Assign (or recall) the slot for a finger going down. Returns (id, totalAfter).
    func begin(_ touch: UITouch) -> (Int32, Int32) {
        let key = ObjectIdentifier(touch)
        if let existing = ids[key] {
            return (existing, Int32(ids.count))
        }
        // Smallest free slot: scan from 0 so ids stay dense.
        var candidate: Int32 = 0
        let used = Set(ids.values)
        while used.contains(candidate) { candidate += 1 }
        if candidate == next { next += 1 }
        var hole = candidate
        if ids.count > 10 {
            // Safety: UIKit reliably pairs began/ended; a leak here means stale
            // entries, so reset rather than grow unbounded.
            ids.removeAll()
            next = 0
            hole = 0
        }
        ids[key] = hole
        return (hole, Int32(ids.count))
    }

    func id(of touch: UITouch) -> Int32? {
        return ids[ObjectIdentifier(touch)]
    }

    /// Release a finger. Returns (id, totalBeforeRemoval) or nil if unknown.
    func end(_ touch: UITouch) -> (Int32, Int32)? {
        let key = ObjectIdentifier(touch)
        guard let id = ids.removeValue(forKey: key) else { return nil }
        return (id, Int32(ids.count) + 1)
    }
}

struct GlobalMetalViewRepresentable: UIViewRepresentable {    func makeUIView(context: Context) -> GlobalMetalView {
        let v = SharedMetalContainer.shared.view
        if let metalLayer = v.layer as? CAMetalLayer {
            let scale = UIScreen.main.scale
            let bounds = UIScreen.main.bounds
            let w = Int(bounds.width * scale)
            let h = Int(bounds.height * scale)
            metalLayer.drawableSize = CGSize(width: w, height: h)
            kudroid_set_metal_layer(Unmanaged.passUnretained(metalLayer).toOpaque(), Int32(w), Int32(h), Float(scale))
        }
        return v
    }

    func updateUIView(_ uiView: GlobalMetalView, context: Context) {}
}

class GlobalMetalView: UIView {
    override class var layerClass: AnyClass {
        return CAMetalLayer.self
    }

    private let touchTracker = TouchPointerTracker()

    override func layoutSubviews() {
        super.layoutSubviews()
        if let metalLayer = self.layer as? CAMetalLayer {
            let scale = UIScreen.main.scale
            metalLayer.contentsScale = scale
            let w = Int(self.bounds.width * scale)
            let h = Int(self.bounds.height * scale)
            if w > 0 && h > 0 {
                metalLayer.drawableSize = CGSize(width: w, height: h)
                kudroid_set_metal_layer(Unmanaged.passUnretained(metalLayer).toOpaque(), Int32(w), Int32(h), Float(scale))
            }
        }
    }

    private func injectTouch(_ touches: Set<UITouch>, action: Int32) {
        let scale = UIScreen.main.scale
        for touch in touches {
            let location = touch.location(in: self)
            let x = Float(location.x * scale)
            let y = Float(location.y * scale)
            switch action {
            case 0: // began: first finger DOWN, later fingers POINTER_DOWN via shim
                let (id, count) = touchTracker.begin(touch)
                kudroid_inject_touch_event_multi(x, y, 0, id, count)
            case 2: // moved: Android MOVE carries no index; count is authoritative
                let id = touchTracker.id(of: touch) ?? 0
                kudroid_inject_touch_event_multi(x, y, 2, id, touchTracker.activeCount)
            case 1, 3: // ended/cancelled: last finger UP, others POINTER_UP via shim
                guard let (id, count) = touchTracker.end(touch) else { continue }
                kudroid_inject_touch_event_multi(x, y, action, id, count)
            default:
                continue
            }
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

class AppDelegate: NSObject, UIApplicationDelegate {
    static var orientationLock = UIInterfaceOrientationMask.all

    func application(_ application: UIApplication, supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        let req = kudroid_get_requested_orientation()
        if req == 0 || req == 6 || req == 8 || req == 11 {
            return .landscape
        } else if req == 1 || req == 7 || req == 9 || req == 12 {
            return .portrait
        }
        return AppDelegate.orientationLock
    }
}

@main
@available(iOS 15.0, *)
struct KuDroidApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject private var session = AppSession()

    init() {
        IOSDiagnostics.shared.start()
        IOSDiagnostics.shared.applicationPhase("app-init")
    }

    var body: some Scene {
        WindowGroup {
            ZStack {
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
                                Button(action: {
                                    session.isSoRunning = false
                                }) {
                                    HStack(spacing: 4) {
                                        Image(systemName: "xmark.circle.fill")
                                        Text("Exit")
                                    }
                                    .font(.caption.bold())
                                    .foregroundColor(.white)
                                    .padding(.horizontal, 10)
                                    .padding(.vertical, 5)
                                    .background(Color.white.opacity(0.2))
                                    .cornerRadius(12)
                                }
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
                IOSDiagnostics.shared.applicationPhase("window-on-appear")
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
