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

// Keep every pointer alive until its release has reached the guest.
final class TouchPointerTracker {
    typealias Pointer = (id: Int32, x: Float, y: Float)
    typealias Batch = (action: Int32, primary: Int32, pointers: [Pointer])

    private var ids: [ObjectIdentifier: Int32] = [:]
    // Retain tracked touches so a missing contact cannot share a reused object address.
    private var contacts: [ObjectIdentifier: UITouch] = [:]
    private var order: [ObjectIdentifier] = []
    private var positions: [ObjectIdentifier: (x: Float, y: Float)] = [:]
    private var reconciliations = 0

    var isEmpty: Bool { ids.isEmpty }

    func begin(_ touch: UITouch) -> Int32? {
        let key = ObjectIdentifier(touch)
        if let id = ids[key] { return id }
        let used = Set(ids.values)
        guard let assigned = (Int32(0)..<32).first(where: { !used.contains($0) }) else { return nil }
        ids[key] = assigned
        contacts[key] = touch
        order.append(key)
        return assigned
    }

    private func end(_ key: ObjectIdentifier) {
        order.removeAll { $0 == key }
        ids.removeValue(forKey: key)
        contacts.removeValue(forKey: key)
        positions.removeValue(forKey: key)
    }

    func activePointers(live: Set<UITouch>, in view: UIView, scale: CGFloat) -> [Pointer] {
        for touch in live {
            let key = ObjectIdentifier(touch)
            guard ids[key] != nil else { continue }
            let location = touch.location(in: view)
            positions[key] = (Float(location.x * scale), Float(location.y * scale))
        }
        return order.compactMap { key in
            guard let id = ids[key], let position = positions[key] else { return nil }
            return (id, position.x, position.y)
        }
    }

    func cancel() -> Batch? {
        let pointers: [Pointer] = order.compactMap { key in
            guard let id = ids[key], let position = positions[key] else { return nil }
            return (id, position.x, position.y)
        }
        ids.removeAll()
        contacts.removeAll()
        order.removeAll()
        positions.removeAll()
        guard let first = pointers.first else { return nil }
        return (3, first.id, pointers)
    }

    // Missing contacts must leave through UP/CANCEL before their ids can be reused.
    func reconcile(live: Set<UITouch>, current: Set<UITouch>, in view: UIView,
                   scale: CGFloat) -> [Batch] {
        _ = activePointers(live: live.union(current), in: view, scale: scale)
        var keep = Set(current.map { ObjectIdentifier($0) })
        for touch in live where touch.phase != .ended && touch.phase != .cancelled {
            keep.insert(ObjectIdentifier(touch))
        }
        let missing = order.filter { !keep.contains($0) }
        guard !missing.isEmpty else { return [] }
        if reconciliations < 20 {
            reconciliations += 1
            NSLog("[KuDroidTouch] releasing missing contacts=%d tracked=%d", missing.count, ids.count)
        }
        if missing.count == ids.count { return cancel().map { [$0] } ?? [] }
        var batches: [Batch] = []
        for key in missing {
            if let id = ids[key] {
                let pointers = activePointers(live: live, in: view, scale: scale)
                batches.append((pointers.count > 1 ? 6 : 1, id, pointers))
            }
            end(key)
        }
        return batches
    }

    func release(_ touches: Set<UITouch>, live: Set<UITouch>, in view: UIView,
                 scale: CGFloat, cancelled: Bool) -> [Batch] {
        var batches = reconcile(live: live, current: touches, in: view, scale: scale)
        let keys = Set(touches.map { ObjectIdentifier($0) })
        let leaving = order.filter { keys.contains($0) }
        guard !leaving.isEmpty else { return batches }
        if cancelled && leaving.count == ids.count {
            if let batch = cancel() { batches.append(batch) }
            return batches
        }
        for key in leaving {
            if let id = ids[key] {
                let pointers = activePointers(live: live.union(touches), in: view, scale: scale)
                batches.append((pointers.count > 1 ? 6 : 1, id, pointers))
            }
            end(key)
        }
        return batches
    }
}

// Share the same gesture lifecycle across rendering backends.
class GuestTouchView: UIView {
    private let touchTracker = TouchPointerTracker()
    private var inactiveObserver: NSObjectProtocol?

    override init(frame: CGRect) {
        super.init(frame: frame)
        setupInput()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupInput()
    }

    private func setupInput() {
        isMultipleTouchEnabled = true
        inactiveObserver = NotificationCenter.default.addObserver(
            forName: UIApplication.willResignActiveNotification, object: nil, queue: .main
        ) { [weak self] _ in self?.cancelActiveTouches() }
    }

    deinit {
        if let observer = inactiveObserver { NotificationCenter.default.removeObserver(observer) }
    }

    override func willMove(toWindow newWindow: UIWindow?) {
        if newWindow == nil { cancelActiveTouches() }
        super.willMove(toWindow: newWindow)
    }

    func cancelActiveTouches() {
        if let batch = touchTracker.cancel() { dispatchBatch(batch) }
    }

    private func dispatchBatch(_ batch: TouchPointerTracker.Batch) {
        guard !batch.pointers.isEmpty else { return }
        let ids = batch.pointers.map { $0.id }
        let xs = batch.pointers.map { $0.x }
        let ys = batch.pointers.map { $0.y }
        kudroid_inject_touch_batch(batch.action, batch.primary, Int32(ids.count), ids, xs, ys)
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        let live = (event?.allTouches ?? touches).union(touches)
        let scale = UIScreen.main.scale
        for batch in touchTracker.reconcile(live: live, current: touches, in: self, scale: scale) {
            dispatchBatch(batch)
        }
        for touch in touches {
            let wasEmpty = touchTracker.isEmpty
            guard let id = touchTracker.begin(touch) else { continue }
            let pointers = touchTracker.activePointers(live: live, in: self, scale: scale)
            dispatchBatch((wasEmpty ? 0 : 5, id, pointers))
        }
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        let live = (event?.allTouches ?? touches).union(touches)
        let scale = UIScreen.main.scale
        for batch in touchTracker.reconcile(live: live, current: touches, in: self, scale: scale) {
            dispatchBatch(batch)
        }
        let pointers = touchTracker.activePointers(live: live, in: self, scale: scale)
        guard let first = pointers.first else { return }
        dispatchBatch((2, first.id, pointers))
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        forwardRelease(touches, event: event, cancelled: false)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        forwardRelease(touches, event: event, cancelled: true)
    }

    private func forwardRelease(_ touches: Set<UITouch>, event: UIEvent?, cancelled: Bool) {
        let live = (event?.allTouches ?? touches).union(touches)
        for batch in touchTracker.release(touches, live: live, in: self,
                                         scale: UIScreen.main.scale, cancelled: cancelled) {
            dispatchBatch(batch)
        }
    }
}

struct GlobalMetalViewRepresentable: UIViewRepresentable {
    func makeUIView(context: Context) -> GlobalMetalView {
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

class GlobalMetalView: GuestTouchView {
    override class var layerClass: AnyClass {
        return CAMetalLayer.self
    }

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

/// A held finger near a screen edge must not be taken over by the system (home / app switcher /
/// control centre cancel the touch). The SwiftUI hosting controller owns these preferences, so
/// they are set here rather than on the guest's view controller.
private struct GuestEdgeGestures: ViewModifier {
    func body(content: Content) -> some View {
        if #available(iOS 16.0, *) {
            content
                .defersSystemGestures(on: .all)
                .persistentSystemOverlays(.hidden)
        } else {
            content
        }
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
        activateAudioSession()
        KuDroidNetworkMonitor.shared.start()
    }

    var body: some Scene {
        WindowGroup {
            ZStack {
                if !session.activeGuestApp.isEmpty {
                    DedicatedAppRunnerView(
                        appName: session.activeGuestApp,
                        onCrash: { appName, tailLog in
                            session.activeGuestApp = ""
                            session.crashInfo = CrashInfo(appName: appName, tailLog: tailLog)
                        }
                    )
                    .ignoresSafeArea()
                    .modifier(GuestEdgeGestures())
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
