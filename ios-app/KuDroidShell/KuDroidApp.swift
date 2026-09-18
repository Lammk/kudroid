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
/// Stable finger identities for the guest.
///
/// The table is keyed by the touch object, not by a slot count: whatever the digitiser
/// reports is passed on. Entries leave when the finger lifts, and anything the event no
/// longer reports is reconciled away (see `prune`) instead of being capped.
final class TouchPointerTracker {
    private var ids: [ObjectIdentifier: Int32] = [:]
    private var next: Int32 = 0

    /// Assign (or recall) the slot for a finger going down.
    func begin(_ touch: UITouch) -> Int32 {
        let key = ObjectIdentifier(touch)
        if let existing = ids[key] {
            return existing
        }
        // Smallest free slot: scan from 0 so ids stay dense and the oldest finger keeps
        // the lowest id, which is the order Android's pointer array uses.
        var candidate: Int32 = 0
        let used = Set(ids.values)
        while used.contains(candidate) { candidate += 1 }
        if candidate == next { next += 1 }
        ids[key] = candidate
        return candidate
    }

    func id(of touch: UITouch) -> Int32? {
        return ids[ObjectIdentifier(touch)]
    }

    /// Release a finger. Returns its id, or nil if it was not tracked.
    func end(_ touch: UITouch) -> Int32? {
        return ids.removeValue(forKey: ObjectIdentifier(touch))
    }

    /// Reconcile with the event's own touch set.
    ///
    /// `live` is every touch the event reports; `current` is the batch being handled.
    /// A touch in `current` may already be ended while it is still the finger this event
    /// is about (that is what an ended callback is), so it is kept until the caller
    /// releases it explicitly. Anything neither live nor current is gone.
    func prune(live: Set<UITouch>, current: Set<UITouch>) {
        guard !ids.isEmpty else { return }
        var keep = Set<ObjectIdentifier>()
        keep.reserveCapacity(live.count + current.count)
        for touch in live where touch.phase != .ended && touch.phase != .cancelled {
            keep.insert(ObjectIdentifier(touch))
        }
        for touch in current { keep.insert(ObjectIdentifier(touch)) }
        if keep.count >= ids.count && ids.keys.allSatisfy({ keep.contains($0) }) { return }
        ids = ids.filter { keep.contains($0.key) }
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

    /// Forward one callback as one event carrying every live finger.
    ///
    /// `action` is the guest action base (0 DOWN, 1 UP, 2 MOVE, 3 CANCEL); the shim picks
    /// DOWN vs POINTER_DOWN and UP vs POINTER_UP from how many fingers are down. A lifting
    /// finger stays in the table sent for UP — that is the slot POINTER_UP's index refers
    /// to — and leaves right after.
    private func injectTouch(_ touches: Set<UITouch>, action: Int32, event: UIEvent?) {
        let scale = UIScreen.main.scale
        // Every live finger, not only the ones this callback names: a MOVE has to carry the
        // other fingers' positions too, and UIKit reports them all through allTouches.
        let live = event?.allTouches ?? touches
        touchTracker.prune(live: live, current: touches)
        if action == 0 {
            for touch in touches { _ = touchTracker.begin(touch) }
        }
        var entries: [(id: Int32, x: Float, y: Float)] = []
        entries.reserveCapacity(live.count)
        for touch in live {
            guard let id = touchTracker.id(of: touch) else { continue }
            let location = touch.location(in: self)
            entries.append((id, Float(location.x * scale), Float(location.y * scale)))
        }
        guard !entries.isEmpty else { return }
        // Pointer 0 is the finger that has been down longest and the action's index refers
        // to this order, so it must not be left to Set iteration order.
        entries.sort { $0.id < $1.id }
        var ids = [Int32]()
        var xs = [Float]()
        var ys = [Float]()
        ids.reserveCapacity(entries.count)
        xs.reserveCapacity(entries.count)
        ys.reserveCapacity(entries.count)
        for entry in entries {
            ids.append(entry.id)
            xs.append(entry.x)
            ys.append(entry.y)
        }
        let primary = touches.first.flatMap { touchTracker.id(of: $0) } ?? ids[0]
        kudroid_inject_touch_batch(action, primary, Int32(ids.count), ids, xs, ys)
        if action == 1 || action == 3 {
            for touch in touches { _ = touchTracker.end(touch) }
        }
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        injectTouch(touches, action: 0, event: event) // ACTION_DOWN
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        injectTouch(touches, action: 2, event: event) // ACTION_MOVE
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        injectTouch(touches, action: 1, event: event) // ACTION_UP
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        injectTouch(touches, action: 3, event: event) // ACTION_CANCEL
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
