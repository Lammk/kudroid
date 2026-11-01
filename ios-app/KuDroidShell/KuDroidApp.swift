import SwiftUI
import UIKit

struct CrashInfo: Identifiable {
    let id = UUID()
    let appName: String
    let tailLog: String
}

class AppSession: ObservableObject {
    @Published var activeGuestApp: String = ""
    @Published var shortcutLaunch: ShortcutLaunchModel?
    @Published var shortcutError: String?
    @Published var libraryBusy = false
    @Published var libraryDeletionInProgress = false
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

// Share the same gesture lifecycle across rendering backends.
class GuestTouchView: UIView {
    var guestGeometryActive = true
    var guestDisplayScale: CGFloat { window?.screen.scale ?? UIScreen.main.scale }
    var guestDrawableSize: CGSize {
        let scale = guestDisplayScale
        return CGSize(width: (bounds.width * scale).rounded(), height: (bounds.height * scale).rounded())
    }
    private let touchTracker = TouchPointerTracker()
    private var inactiveObserver: NSObjectProtocol?

    override func safeAreaInsetsDidChange() {
        super.safeAreaInsetsDidChange()
        reportSafeAreaInsets()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        reportSafeAreaInsets()
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        reportSafeAreaInsets()
    }

    private func reportSafeAreaInsets() {
        guard guestGeometryActive, window != nil else { return }
        let scale = guestDisplayScale
        let edges = safeAreaInsets
        kudroid_set_safe_area_insets(Int32((edges.left * scale).rounded()),
                                    Int32((edges.top * scale).rounded()),
                                    Int32((edges.right * scale).rounded()),
                                    Int32((edges.bottom * scale).rounded()))
    }

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
        guard guestGeometryActive else { return }
        if let metalLayer = self.layer as? CAMetalLayer {
            let scale = guestDisplayScale
            metalLayer.contentsScale = scale
            let size = guestDrawableSize
            let w = Int(size.width)
            let h = Int(size.height)
            if w > 0 && h > 0 {
                metalLayer.drawableSize = CGSize(width: w, height: h)
                kudroid_set_metal_layer(Unmanaged.passUnretained(metalLayer).toOpaque(), Int32(w), Int32(h), Float(scale))
            }
        }
    }


}

class AppDelegate: NSObject, UIApplicationDelegate {
    static var guestRunnerActive = false
    static var nativeSandboxActive = false
    static var guestOrientationActive: Bool { guestRunnerActive || nativeSandboxActive }

    static func guestOrientationMask(_ requested: Int32) -> UIInterfaceOrientationMask {
        switch requested {
        case 0, 6, 8, 11: return .landscape
        case 1, 7, 9, 12: return .portrait
        default: return .allButUpsideDown
        }
    }

    static func restorePortraitIfIdle() {
        DispatchQueue.main.async {
            guard !guestOrientationActive else { return }
            let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
                .filter { $0.activationState == .foregroundActive }
            if #available(iOS 16.0, *) {
                for scene in scenes {
                    for window in scene.windows {
                        window.rootViewController?.setNeedsUpdateOfSupportedInterfaceOrientations()
                    }
                }
            }
            UIViewController.attemptRotationToDeviceOrientation()
            if #available(iOS 16.0, *) {
                for scene in scenes where scene.interfaceOrientation != .portrait {
                    scene.requestGeometryUpdate(.iOS(interfaceOrientations: .portrait)) { error in
                        NSLog("[Orientation] Could not restore portrait: %@", error.localizedDescription)
                    }
                }
            } else {
                UIDevice.current.setValue(UIInterfaceOrientation.portrait.rawValue, forKey: "orientation")
                UIViewController.attemptRotationToDeviceOrientation()
            }
        }
    }

    func application(_ application: UIApplication, supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        guard AppDelegate.guestOrientationActive else { return .portrait }
        return AppDelegate.guestOrientationMask(kudroid_get_requested_orientation())
    }
}

/// A held finger near a screen edge must not be taken over by the system (home / app switcher /
/// control centre cancel the touch). The SwiftUI hosting controller owns these preferences, so
/// they are set here rather than on the guest's view controller.
private struct GuestEdgeGestures: ViewModifier {
    func body(content: Content) -> some View {
        if #available(iOS 16.0, *) {
            content
                .statusBar(hidden: true)
                .defersSystemGestures(on: .all)
                .persistentSystemOverlays(.hidden)
        } else {
            content.statusBar(hidden: true)
        }
    }
}

@main
@available(iOS 15.0, *)
struct KuDroidApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @AppStorage("shellAppearance") private var appearance = ShellAppearance.system.rawValue
    @StateObject private var session = AppSession()
    @ObservedObject private var dataReset = AppDataResetModel.shared
    @ObservedObject private var installer = APKInstallModel.shared
    @StateObject private var documentImport = ExternalAPKImportModel()

    init() {
        if StandaloneConfiguration.isStandalone,
           let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
            kudroid_set_log_paths(documents.path, StandaloneStorage.logs.path)
        } else if !StandaloneConfiguration.isStandalone {
            setupLogDir()
        }
        kudroid_log_vm_startup_probe()
        IOSDiagnostics.shared.start()
        IOSDiagnostics.shared.applicationPhase("app-init")
        activateAudioSession()
        KuDroidNetworkMonitor.shared.start()
        KuDroidThermalMonitor.shared.start()
    }

    var body: some Scene {
        WindowGroup {
            ZStack {
                if StandaloneConfiguration.isStandalone {
                    StandaloneGuestView()
                        .modifier(GuestEdgeGestures())
                } else if dataReset.busy {
                    ZStack {
                        Color.black.ignoresSafeArea()
                        VStack(spacing: 16) { ProgressView(); Text("Deleting app data…") }
                    }
                } else if let launch = session.shortcutLaunch {
                    ShortcutLaunchView(model: launch, run: {
                        session.activeGuestApp = launch.appID
                        session.shortcutLaunch = nil
                    }, cancel: { session.shortcutLaunch = nil }).id(launch.id)
                } else if !session.activeGuestApp.isEmpty {
                    DedicatedAppRunnerView(
                        appName: session.activeGuestApp,
                        onEnded: { session.activeGuestApp = "" },
                        onCrash: { appName, tailLog in
                            session.activeGuestApp = ""
                            session.crashInfo = CrashInfo(appName: appName, tailLog: tailLog)
                        }
                    )
                    .ignoresSafeArea()
                    .modifier(GuestEdgeGestures())
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
                    .modifier(GuestEdgeGestures())
                    .transition(.opacity)
                    .zIndex(50)
                } else {
                    ContentView()
                        .environmentObject(session)
                }

                if installer.busy {
                    Color.black.opacity(0.65).ignoresSafeArea()
                    VStack(spacing: 16) {
                        ProgressView()
                        Text(installer.stage).multilineTextAlignment(.center)
                    }.padding(28).background(Color(.systemGray6)).cornerRadius(18).padding()
                }
                if let crashInfo = session.crashInfo {
                    CrashAlertView(crashInfo: crashInfo) {
                        session.crashInfo = nil
                    }
                    .transition(.opacity.combined(with: .scale(scale: 0.95)))
                    .zIndex(100)
                }
            }
            .preferredColorScheme((ShellAppearance(rawValue: appearance) ?? .system).colorScheme)
            .tint(ShellStyle.ink)
            .alert("Could not install APK", isPresented: $installer.showError) {
                Button("OK", role: .cancel) {}
            } message: { Text(installer.error) }
            .sheet(isPresented: $documentImport.presented, onDismiss: { documentImport.cancel() }) {
                ExternalAPKImportView(model: documentImport, installationAllowed: {
                    session.activeGuestApp.isEmpty && !session.isSoRunning && !session.libraryBusy && !dataReset.busy
                })
            }
            .alert("KuDroid", isPresented: Binding(get: { session.shortcutError != nil }, set: { if !$0 { session.shortcutError = nil } })) {
                Button("OK", role: .cancel) { session.shortcutError = nil }
            } message: { Text(session.shortcutError ?? "") }
            .onOpenURL { url in
                guard !StandaloneConfiguration.isStandalone else { return }
                if APKFileTypes.accepts(url) {
                    guard session.activeGuestApp.isEmpty, !session.isSoRunning,
                          !session.libraryBusy, !dataReset.busy else {
                        session.shortcutError = "Close the running app and wait for any file operation to finish before installing an APK."
                        return
                    }
                    documentImport.open(url)
                    return
                }
                guard !documentImport.presented,
                      let id = InstalledAppStorage.id(from: url),
                      !session.isSoRunning else { return }
                guard !session.libraryBusy else {
                    session.shortcutError = "Wait for the app installation or deletion to finish."
                    return
                }
                guard session.activeGuestApp.isEmpty else {
                    if session.activeGuestApp != id { session.shortcutError = "Close the current app before opening another app shortcut." }
                    return
                }
                session.shortcutLaunch?.cancel()
                session.shortcutLaunch = ShortcutLaunchModel(appID: id)
            }
            .onReceive(installer.$busy) { busy in
                session.libraryBusy = busy || session.libraryDeletionInProgress
            }
            .onChange(of: session.isSoRunning) { running in
                AppDelegate.nativeSandboxActive = running
                SharedMetalContainer.shared.view.guestGeometryActive = running
                if running {
                    kudroid_notify_orientation_change(orientation: kudroid_get_requested_orientation())
                } else {
                    AppDelegate.restorePortraitIfIdle()
                }
            }
            .onAppear {
                if !StandaloneConfiguration.isStandalone { dataReset.processStartup() }
                IOSDiagnostics.shared.applicationPhase("window-on-appear")
                RemoteDebugClient.shared.configure(session: session)
                let savedIP = UserDefaults.standard.string(forKey: "kdb_server_ip") ?? ""
                if !savedIP.isEmpty {
                    RemoteDebugClient.shared.connect(host: savedIP)
                }
            }
            .animation(.easeInOut(duration: 0.25), value: session.isSoRunning)
            .animation(.easeInOut(duration: 0.25), value: session.crashInfo != nil)
        }
    }
}
