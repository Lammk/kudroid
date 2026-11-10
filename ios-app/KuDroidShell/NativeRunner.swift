import SwiftUI
import UIKit
import AVFAudio
import AVFoundation
import Metal
import QuartzCore
import CoreMotion
import Combine
import UniformTypeIdentifiers

// mark: - Full screen exclusive executable (Dedicated Clean Container)
struct DedicatedAppRunnerView: UIViewControllerRepresentable {
    let appName: String
    let onCrash: (String, String) -> Void
    var onEnded: (() -> Void)? = nil

    init(appName: String, onEnded: (() -> Void)? = nil, onCrash: @escaping (String, String) -> Void) {
        self.appName = appName
        self.onEnded = onEnded
        self.onCrash = onCrash
    }

    func makeUIViewController(context: Context) -> NativeMetalViewController {
        let vc = NativeMetalViewController(appName: appName, onCrash: onCrash, onEnded: onEnded)
        return vc
    }

    func updateUIViewController(_ uiViewController: NativeMetalViewController, context: Context) {
        DispatchQueue.main.async {
            uiViewController.startAppIfNeeded()
        }
    }
}

private func onKuDroidSoftInputShow(_ flags: Int32) {
    DispatchQueue.main.async {
        guard let vc = NativeMetalViewController.sCurrentRunnerVC else { return }
        NSLog("[KuDroid] guest requested soft keyboard (flags=0x%x)", flags)
        // The focused field's type decides the layout; a keyboard that is already up
        // must be rebuilt for a field of a different kind.
        vc.metalView.applyGuestInputTraits(inputType: kudroid_get_soft_input_type(),
                                           imeOptions: kudroid_get_soft_input_ime_options())
        if vc.metalView.isFirstResponder {
            vc.metalView.reloadInputViews()
        } else {
            vc.metalView.becomeFirstResponder()
        }
        kudroid_set_soft_input_visible(vc.metalView.isFirstResponder ? 1 : 0)
    }
}

// Opens links the guest asks for (browser, mail, phone) in the system handler. Local-file
// and script schemes are never opened.
private func onKuDroidOpenURL(_ urlC: UnsafePointer<CChar>?) {
    guard let urlC = urlC else { return }
    let text = String(cString: urlC)
    DispatchQueue.main.async {
        guard let url = URL(string: text),
              let scheme = url.scheme?.lowercased(),
              !["file", "content", "javascript", "data"].contains(scheme) else {
            NSLog("[KuDroid] refused to open url: %@", text)
            return
        }
        NSLog("[KuDroid] opening url: %@", text)
        UIApplication.shared.open(url, options: [:], completionHandler: nil)
    }
}

private func onKuDroidSoftInputHide() {
    DispatchQueue.main.async {
        guard let vc = NativeMetalViewController.sCurrentRunnerVC else { return }
        NSLog("[KuDroid] guest dismissed soft keyboard")
        vc.metalView.resignFirstResponder()
        kudroid_set_soft_input_visible(0)
    }
}

private func onKuDroidPermissionPrompt(pkgNameC: UnsafePointer<CChar>?,
                                      permsCsvC: UnsafePointer<CChar>?,
                                      reqCode: Int32,
                                      actHandle: UnsafeMutableRawPointer?) {
    // Copied now: the C strings are only valid for the duration of this call.
    let csv = permsCsvC != nil ? String(cString: permsCsvC!) : ""
    let appTitle = NativeMetalViewController.sCurrentRunnerVC?.appName ?? "Android App"
    let respond: (Int32) -> Void = { granted in
        csv.withCString { kudroid_submit_permission_response(actHandle, reqCode, $0, granted) }
    }
    DispatchQueue.main.async {
        let perms = csv.split(separator: ",").map(String.init)
        // iOS never asks twice: once the user has refused, only Settings can undo it.
        let refusedByHost = perms.contains {
            ($0 == "android.permission.RECORD_AUDIO" && kudroid_get_host_authorization(0) == 2) ||
            ($0 == "android.permission.CAMERA" && kudroid_get_host_authorization(1) == 2)
        }
        if refusedByHost {
            let alert = UIAlertController(
                title: "Access turned off",
                message: "\(appTitle) needs the microphone or camera, but access is turned off for KuDroid in iOS Settings.",
                preferredStyle: .alert
            )
            alert.addAction(UIAlertAction(title: "Cancel", style: .cancel) { _ in respond(0) })
            alert.addAction(UIAlertAction(title: "Open Settings", style: .default) { _ in
                if let url = URL(string: UIApplication.openSettingsURLString) {
                    UIApplication.shared.open(url)
                }
                respond(0)
            })
            NativeMetalViewController.sCurrentRunnerVC?.present(alert, animated: true)
            return
        }
        let items = perms.map { p -> String in
            if p.contains("CAMERA") { return "📷 Camera Access" }
            if p.contains("AUDIO") || p.contains("RECORD") { return "🎙️ Microphone & Audio" }
            if p.contains("STORAGE") || p.contains("EXTERNAL") { return "📁 Storage & Files (/sdcard)" }
            if p.contains("LOCATION") { return "📍 Location (GPS)" }
            if p.contains("BLUETOOTH") { return "🎮 Bluetooth & Gamepads" }
            return "🔒 " + (p.components(separatedBy: ".").last ?? p)
        }
        let listText = items.isEmpty ? "Permissions" : items.joined(separator: "\n")
        let alert = UIAlertController(
            title: "Permission Request",
            message: "\(appTitle) requests permission to access:\n\n\(listText)\n\nAllow access?",
            preferredStyle: .alert
        )
        alert.addAction(UIAlertAction(title: "Deny", style: .destructive) { _ in
            respond(0)
        })
        alert.addAction(UIAlertAction(title: "Allow", style: .default) { _ in
            // The app's grant is not enough: iOS has its own prompt for the mic and camera.
            requestHostAuthorizations(for: perms) { respond(1) }
        })
        NativeMetalViewController.sCurrentRunnerVC?.present(alert, animated: true)
    }
}

/// Show a camera preview layer over the running guest's surface, or remove it.
func kudroidSetGuestPreviewLayer(_ layer: CALayer?) {
    DispatchQueue.main.async {
        NativeMetalViewController.sCurrentRunnerVC?.metalView.setGuestPreviewLayer(layer)
    }
}

func kudroidSetGuestVideoLayer(_ handle: Int64, _ layer: CALayer?) {
    NativeMetalViewController.sCurrentRunnerVC?.metalView.setGuestVideoLayer(handle, layer)
}

class NativeMetalViewController: UIViewController {
    let appName: String
    let onCrash: (String, String) -> Void
    private let onEnded: (() -> Void)?
    private var isStarted = false
    private var didCleanup = false
    fileprivate var metalView: NativeMetalView!
    private var crashCheckTimer: Timer?
    private var keyboardObservers: [NSObjectProtocol] = []
    // True while the guest Activity was paused because iOS resigned active; gates
    // the matching resume so a fresh didBecomeActive never re-enters launch state.
    private var guestPausedForBackground = false
    private var statusLabel: UILabel!
    private var lastRequestedOrientation: Int32 = -1

    init(appName: String, onCrash: @escaping (String, String) -> Void, onEnded: (() -> Void)? = nil) {
        self.appName = appName
        self.onCrash = onCrash
        self.onEnded = onEnded
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        let root = UIView(frame: UIScreen.main.bounds)
        root.backgroundColor = .black
        metalView = NativeMetalView(frame: root.bounds)
        metalView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        root.addSubview(metalView)
        self.view = root
    }

    override var supportedInterfaceOrientations: UIInterfaceOrientationMask {
        guard isStarted, !didCleanup else { return .portrait }
        return AppDelegate.guestOrientationMask(kudroid_get_requested_orientation())
    }

    override var preferredScreenEdgesDeferringSystemGestures: UIRectEdge {
        return .all
    }

    override var prefersHomeIndicatorAutoHidden: Bool {
        return true
    }

    override var prefersStatusBarHidden: Bool { true }

    private func syncDarkMode() {
        if #available(iOS 13.0, *) {
            let systemStyle = view.window?.windowScene?.screen.traitCollection.userInterfaceStyle
                ?? UIScreen.main.traitCollection.userInterfaceStyle
            let style = systemStyle == .unspecified ? traitCollection.userInterfaceStyle : systemStyle
            kudroid_set_dark_mode(style == .dark ? 1 : 0)
        }
    }

    override func traitCollectionDidChange(_ previousTraitCollection: UITraitCollection?) {
        super.traitCollectionDidChange(previousTraitCollection)
        syncDarkMode()
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        syncDarkMode()
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        syncDarkMode()
        activateAudioSession()
        self.view.backgroundColor = .black
        metalView.backgroundColor = .black

        // Status line stays hidden until the session ends.
        statusLabel = UILabel()
        statusLabel.text = ""
        statusLabel.alpha = 0.0
        statusLabel.textColor = UIColor.green.withAlphaComponent(0.8)
        statusLabel.font = UIFont.monospacedSystemFont(ofSize: 12, weight: .semibold)
        statusLabel.backgroundColor = UIColor.black.withAlphaComponent(0.6)
        statusLabel.layer.cornerRadius = 8
        statusLabel.layer.masksToBounds = true
        statusLabel.textAlignment = .center
        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(statusLabel)

        NSLayoutConstraint.activate([
            statusLabel.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -16),
            statusLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            statusLabel.heightAnchor.constraint(equalToConstant: 28),
            statusLabel.widthAnchor.constraint(greaterThanOrEqualToConstant: 180)
        ])

        KuDroidSensorBridge.shared.register()

        NativeMetalViewController.sCurrentRunnerVC = self

        // Soft keyboard: the guest's InputMethodManager reaches these, and they make
        // the Metal view first responder so iOS shows the system keyboard. Registered
        // here rather than in the C++ layer because kudroid_core is a static library
        // and cannot touch UIKit.
        kudroid_set_soft_input_callbacks(onKuDroidSoftInputShow, onKuDroidSoftInputHide)
        kudroid_set_open_url_callback(onKuDroidOpenURL)

        // Track the real keyboard state rather than assuming it followed the request:
        // iOS can dismiss it on its own (interactive dismiss, scene change), and the
        // guest reads this to lay out around the keyboard.
        for token in keyboardObservers {
            NotificationCenter.default.removeObserver(token)
        }
        keyboardObservers = [
            NotificationCenter.default.addObserver(
                forName: NSNotification.Name(rawValue: "UIKeyboardDidShowNotification"), object: nil, queue: .main
            ) { _ in
                kudroid_set_soft_input_visible(1)
            },
            NotificationCenter.default.addObserver(
                forName: NSNotification.Name(rawValue: "UIKeyboardDidHideNotification"), object: nil, queue: .main
            ) { _ in
                kudroid_set_soft_input_visible(0)
            },
        ]

        // Set up Permission Request Dialog Callback
        kudroid_set_permission_prompt_callback(onKuDroidPermissionPrompt)
        registerKuDroidCameraHost()
        registerKuDroidMediaPlayerHost()
        // The user can change microphone/camera access in Settings while the app is away.
        keyboardObservers.append(NotificationCenter.default.addObserver(
            forName: UIApplication.didBecomeActiveNotification, object: nil, queue: .main
        ) { [weak self] _ in
            refreshHostAuthorizations()
            self?.metalView?.setNeedsLayout()
            if let self = self, self.guestPausedForBackground {
                self.guestPausedForBackground = false
                kudroid_send_lifecycle_event(102)
            }
        })
        // Android delivers onPause/onResume with app foreground; iOS must mirror it or
        // Unity never runs OnApplicationPause, so save-on-pause paths (settings commit)
        // never execute and the engine keeps pumping frames into a suspended process.
        keyboardObservers.append(NotificationCenter.default.addObserver(
            forName: UIApplication.willResignActiveNotification, object: nil, queue: .main
        ) { [weak self] _ in
            guard let self = self, self.isStarted else { return }
            self.metalView?.cancelActiveTouches()
            self.guestPausedForBackground = true
            kudroid_send_lifecycle_event(101)
        })

        startAppIfNeeded()
    }

    fileprivate weak static var sCurrentRunnerVC: NativeMetalViewController?
    private static var isGlobalAppRunning = false

    private func cleanupAndResetOrientation() {
        guard !didCleanup else { return }
        didCleanup = true
        metalView?.guestGeometryActive = false
        if NativeMetalViewController.sCurrentRunnerVC === self {
            NativeMetalViewController.sCurrentRunnerVC = nil
            AppDelegate.guestRunnerActive = false
        }
        UIApplication.shared.isIdleTimerDisabled = false
        NativeMetalViewController.isGlobalAppRunning = false
        KuDroidSensorBridge.shared.stop()
        crashCheckTimer?.invalidate()
        for token in keyboardObservers {
            NotificationCenter.default.removeObserver(token)
        }
        keyboardObservers = []
        kudroid_set_soft_input_callbacks(nil, nil)
        kudroid_set_open_url_callback(nil)
        metalView?.cancelActiveTouches()
        metalView?.resignFirstResponder()
        kudroid_set_soft_input_visible(0)
        kudroid_stop_app()

        if #available(iOS 16.0, *) {
            setNeedsUpdateOfSupportedInterfaceOrientations()
        }
        AppDelegate.restorePortraitIfIdle()
    }

    deinit {
        cleanupAndResetOrientation()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        guard !didCleanup else { return }
        if let metalLayer = metalView?.layer as? CAMetalLayer {
            let scale = metalView.guestDisplayScale
            metalLayer.contentsScale = scale
            let targetSize = metalView.guestDrawableSize
            if metalLayer.drawableSize != targetSize && targetSize.width > 0 && targetSize.height > 0 {
                metalLayer.drawableSize = targetSize
                if isStarted {
                    let unmanaged = Unmanaged.passUnretained(metalLayer)
                    kudroid_set_metal_layer(unmanaged.toOpaque(), Int32(targetSize.width),
                                            Int32(targetSize.height), Float(scale))
                }
            }
        }
        startAppIfNeeded()
    }

    override func viewWillTransition(to size: CGSize, with coordinator: UIViewControllerTransitionCoordinator) {
        super.viewWillTransition(to: size, with: coordinator)
        coordinator.animate(alongsideTransition: nil) { [weak self] _ in
            self?.view.setNeedsLayout()
            self?.view.layoutIfNeeded()
        }
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        startAppIfNeeded()
    }

    func startAppIfNeeded() {
        guard !didCleanup else { return }
        guard !isStarted else { return }
        if NativeMetalViewController.isGlobalAppRunning { return }
        loadViewIfNeeded()
        guard !didCleanup, !isStarted, !NativeMetalViewController.isGlobalAppRunning,
              let mv = metalView, let metalLayer = mv.layer as? CAMetalLayer else { return }
        let scale = mv.guestDisplayScale
        let size = mv.guestDrawableSize
        guard size.width > 0, size.height > 0 else { return }
        activateAudioSession()
        NativeMetalViewController.isGlobalAppRunning = true
        AppDelegate.guestRunnerActive = true
        isStarted = true
        if #available(iOS 16.0, *) {
            setNeedsUpdateOfSupportedInterfaceOrientations()
        }

        // By default, No Sleep is enabled (keeps the screen on when playing games)
        UIApplication.shared.isIdleTimerDisabled = true
        NSLog("[KuDroid] >>> Launching guest app: %@ (isIdleTimerDisabled = true) <<<", appName)

        let width = Int32(size.width)
        let height = Int32(size.height)

        if metalLayer.device == nil {
            metalLayer.device = MTLCreateSystemDefaultDevice()
        }
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = false
        metalLayer.allowsNextDrawableTimeout = false
        metalLayer.maximumDrawableCount = 3
        metalLayer.presentsWithTransaction = false
        metalLayer.isOpaque = true
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = CGSize(width: Double(width), height: Double(height))
        let unmanaged = Unmanaged.passUnretained(metalLayer)
        kudroid_set_metal_layer(unmanaged.toOpaque(), width, height, Float(scale))

        // Timer periodically scans crash status, screen orientation, and No Sleep from the C++ bridge
        crashCheckTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] timer in
            guard let self = self else { return }
            if kudroid_has_crashed() != 0 {
                timer.invalidate()
                self.handleCrash()
                return
            }

            // Sync No Sleep (Keep Screen On) state from the Android app
            let keepScreenOn = kudroid_get_keep_screen_on() != 0
            if UIApplication.shared.isIdleTimerDisabled != keepScreenOn {
                UIApplication.shared.isIdleTimerDisabled = keepScreenOn
                NSLog("[KuDroid] Synchronized isIdleTimerDisabled = %@", keepScreenOn ? "true" : "false")
            }

            // Poll the requested screen orientation from the Android guest app
            let reqOri = kudroid_get_requested_orientation()
            if reqOri != self.lastRequestedOrientation {
                self.lastRequestedOrientation = reqOri
                // requestGeometryUpdate refuses any orientation the view
                // controller does not declare (observed: "Requested: portrait;
                // Supported: landscapeLeft, landscapeRight" — the VC had
                // already rotated to landscape, so its supported set was the
                // OLD one and the back-to-portrait request was rejected,
                // leaving the swap chain in a dead geometry: black screen
                // with healthy eglSwapBuffers). Announcing the change first
                // re-evaluates supportedInterfaceOrientations (which reads
                // the same kudroid_get_requested_orientation value) so the
                // request and the mask always agree.
                if #available(iOS 16.0, *) {
                    self.setNeedsUpdateOfSupportedInterfaceOrientations()
                }
                UIViewController.attemptRotationToDeviceOrientation()
                if reqOri == 0 || reqOri == 6 || reqOri == 8 || reqOri == 11 {
                    // Landscape
                    NSLog("[KuDroid] Guest app requested LANDSCAPE orientation (%d)", reqOri)
                    // Skip when the scene already sits in this geometry: a
                    // redundant request triggers the guest's pause/resume
                    // handshake (a 2s Unity pause timeout per occurrence) for
                    // no visual change. See the portrait branch for the storm
                    // this stopped.
                    if let scene = self.view.window?.windowScene,
                       scene.interfaceOrientation.isLandscape {
                        // Already landscape: skip the redundant request (and the
                        // guest pause handshake it triggers).
                    } else if #available(iOS 16.0, *) {
                        if let windowScene = self.view.window?.windowScene {
                            let geometryPreferences = UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: .landscape)
                            windowScene.requestGeometryUpdate(geometryPreferences) { error in
                                NSLog("[KuDroid] requestGeometryUpdate landscape error: %@", error.localizedDescription)
                            }
                        }
                    }
                } else if reqOri == 1 || reqOri == 7 || reqOri == 9 || reqOri == 12 {
                    // Portrait
                    NSLog("[KuDroid] Guest app requested PORTRAIT orientation (%d)", reqOri)
                    // No-op when the guest re-asserts the orientation the app is
                    // already showing (ULTRAKILL calls setRequestedOrientation on
                    // every touch-driven UI transition). A second
                    // requestGeometryUpdate to the current geometry pauses the
                    // Unity engine for the 2s handshake that follows and then
                    // times out — the "Timeout (2000 ms) while trying to pause"
                    // storm at the end of the last run.
                    if let scene = self.view.window?.windowScene,
                       scene.interfaceOrientation.isPortrait {
                        // Already portrait: skip the redundant request (and the
                        // guest pause handshake it triggers).
                    } else if #available(iOS 16.0, *) {
                        if let windowScene = self.view.window?.windowScene {
                            let geometryPreferences = UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: .portrait)
                            windowScene.requestGeometryUpdate(geometryPreferences) { error in
                                NSLog("[KuDroid] requestGeometryUpdate portrait error: %@", error.localizedDescription)
                            }
                        }
                    }
                }
            }
        }

        // Run Android APK with highest Interactive UI priority, on a thread of its own:
        // the guest's main thread runs the interpreter and guest static initialisers,
        // and a GCD worker's 512 KB stack is too small for that.
        let guestMainThread = Thread { [weak self] in
            guard let self = self else { return }
            NSLog("[KuDroid] Starting kudroid_run_apk(%@)...", self.appName)
            let cString = kudroid_run_apk(self.appName)
            var logOutput = ""
            if let cString = cString {
                logOutput = String(cString: cString)
                free(UnsafeMutablePointer(mutating: cString))
            }
            NSLog("[KuDroid] Finished kudroid_run_apk(%@). Log chars: %ld", self.appName, logOutput.count)
            DispatchQueue.main.async {
                guard !self.didCleanup else { return }
                self.crashCheckTimer?.invalidate()
                if kudroid_has_crashed() != 0 {
                    self.handleCrash(fallbackLog: logOutput)
                } else {
                    self.cleanupAndResetOrientation()
                    if let onEnded = self.onEnded {
                        onEnded()
                        return
                    }
                    // App finished without crashing
                    self.statusLabel.alpha = 1.0
                    self.statusLabel.text = "Session ended. Close KuDroid from the app switcher."
                    self.statusLabel.textColor = .yellow
                }
            }
        }
        guestMainThread.name = "KuDroidGuestMain"
        guestMainThread.stackSize = 16 * 1024 * 1024
        guestMainThread.qualityOfService = .userInteractive
        guestMainThread.start()
    }

    private func handleCrash(fallbackLog: String = "") {
        cleanupAndResetOrientation()
        var tailLog = ""
        if let cTail = kudroid_get_last_crash_tail() {
            tailLog = String(cString: cTail)
            free(UnsafeMutablePointer(mutating: cTail))
        }
        kudroid_clear_crash_state()
        let finalLog = tailLog.isEmpty || tailLog == "No recent crash detected." ? fallbackLog : tailLog
        onCrash(appName, finalLog)
    }

    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        cleanupAndResetOrientation()
    }
}

class NativeMetalView: GuestTouchView {
    override class var layerClass: AnyClass {
        return CAMetalLayer.self
    }

    // Keyboard traits the guest's focused field asked for; the UITextInputTraits
    // properties in the UIKeyInput extension read and write these.
    fileprivate var guestKeyboardType: UIKeyboardType = .default
    fileprivate var guestReturnKeyType: UIReturnKeyType = .default
    fileprivate var guestSecureEntry = false
    fileprivate var guestAutocorrection: UITextAutocorrectionType = .no
    fileprivate var guestAutocapitalization: UITextAutocapitalizationType = .none

    // Camera preview shown over the guest's surface; kept at the view's size.
    private var guestPreviewLayer: CALayer?
    private var guestVideoLayers: [Int64: CALayer] = [:]

    fileprivate func setGuestVideoLayer(_ handle: Int64, _ video: CALayer?) {
        if guestVideoLayers[handle] !== video {
            guestVideoLayers[handle]?.removeFromSuperlayer()
            guestVideoLayers[handle] = video
            if let video = video {
                video.zPosition = 2
                video.frame = bounds
                layer.addSublayer(video)
            }
        }
    }

    fileprivate func setGuestPreviewLayer(_ preview: CALayer?) {
        if guestPreviewLayer !== preview {
            guestPreviewLayer?.removeFromSuperlayer()
            guestPreviewLayer = preview
            if let preview = preview {
                preview.zPosition = 1
                layer.addSublayer(preview)
            }
        }
        guestPreviewLayer?.frame = bounds
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        setupLayer()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupLayer()
    }

    private func setupLayer() {
        self.isMultipleTouchEnabled = true
        self.isUserInteractionEnabled = true
        guard let metalLayer = self.layer as? CAMetalLayer else { return }
        if metalLayer.device == nil {
            metalLayer.device = MTLCreateSystemDefaultDevice()
        }
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = false
        metalLayer.allowsNextDrawableTimeout = false
        metalLayer.maximumDrawableCount = 3
        metalLayer.presentsWithTransaction = false
        metalLayer.isOpaque = true
        let scale = guestDisplayScale
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = guestDrawableSize
    }

    // Last drawable size reported to the guest. layoutSubviews fires on rotation;
    // without forwarding, the guest renders forever into the launch geometry.
    private var lastReportedSize: CGSize = .zero
    // Guest orientation at the last forward. Rotation can arrive with identical
    // bounds (scale-only change, or a rotate-then-rotate-back between layouts);
    // the guest still needs the forward because its orientation value changed.
    private var lastReportedOrientation: Int32 = -1
    private var lastReportedScale: CGFloat = 0

    override func layoutSubviews() {
        super.layoutSubviews()
        guard guestGeometryActive else { return }
        guestPreviewLayer?.frame = bounds
        for video in guestVideoLayers.values { video.frame = bounds }
        guard let metalLayer = self.layer as? CAMetalLayer else { return }
        let scale = guestDisplayScale
        metalLayer.contentsScale = scale
        let sz = guestDrawableSize
        if sz.width > 0 && sz.height > 0 && metalLayer.drawableSize != sz {
            metalLayer.drawableSize = sz
        }
        let reqOri = kudroid_get_requested_orientation()
        if sz.width > 0 && sz.height > 0 &&
            (sz != lastReportedSize || reqOri != lastReportedOrientation || scale != lastReportedScale) {
            lastReportedSize = sz
            lastReportedOrientation = reqOri
            lastReportedScale = scale
            let unmanaged = Unmanaged.passUnretained(metalLayer)
            kudroid_set_metal_layer(unmanaged.toOpaque(), Int32(sz.width),
                                    Int32(sz.height), Float(scale))
        }
    }


}

// MARK: - Soft keyboard
//
// UIKeyInput (not UITextInput) bridges the guest IME: iOS reports keystrokes,
// the guest's InputConnection decides what they mean.
extension NativeMetalView: UIKeyInput {
    // The keyboard only appears for a first responder, and a plain UIView refuses.
    override var canBecomeFirstResponder: Bool { true }

    // Always true. Reporting false makes iOS hide the delete key's repeat behaviour
    // and, on some versions, suppress backspace entirely — and the host cannot know
    // whether the guest's buffer is empty without asking across the bridge on every
    // keystroke. Letting the guest ignore a backspace it cannot apply is cheaper.
    var hasText: Bool { true }

    func insertText(_ text: String) {
        // Newline arrives as "\n" from the return key. Passed through unchanged: the
        // guest's InputConnection decides whether that commits a chat line or inserts
        // a break, which is not the host's business.
        text.withCString { kudroid_dispatch_text_input($0) }
    }

    func deleteBackward() {
        kudroid_dispatch_delete_backward()
    }

    // Keyboard traits follow the guest's focused field (see applyGuestInputTraits).
    // Games render their own text, so iOS must not also draw an insertion caret or
    // selection over the Metal surface; the traits only shape the keyboard itself.
    var keyboardType: UIKeyboardType {
        get { guestKeyboardType }
        set { guestKeyboardType = newValue }
    }

    var returnKeyType: UIReturnKeyType {
        get { guestReturnKeyType }
        set { guestReturnKeyType = newValue }
    }

    var isSecureTextEntry: Bool {
        get { guestSecureEntry }
        set { guestSecureEntry = newValue }
    }

    var autocorrectionType: UITextAutocorrectionType {
        get { guestAutocorrection }
        set { guestAutocorrection = newValue }
    }

    var autocapitalizationType: UITextAutocapitalizationType {
        get { guestAutocapitalization }
        set { guestAutocapitalization = newValue }
    }

    var spellCheckingType: UITextSpellCheckingType {
        get { .no }
        set { }
    }

    /// Map the guest field's EditorInfo (inputType / imeOptions) onto UIKit's traits.
    func applyGuestInputTraits(inputType: Int32, imeOptions: Int32) {
        let cls = inputType & 0xf
        let variation = inputType & 0xff0
        let flags = inputType & 0x00fff000

        var keyboard: UIKeyboardType = .default
        var secure = false
        switch cls {
        case 1:  // text
            switch variation {
            case 0x10: keyboard = .URL
            case 0x20, 0xd0: keyboard = .emailAddress
            case 0x80, 0xe0: secure = true
            case 0x90: keyboard = .asciiCapable
            default: break
            }
        case 2:  // number
            keyboard = (flags & 0x2000) != 0 ? .decimalPad : .numberPad
            secure = variation == 0x10
        case 3: keyboard = .phonePad
        case 4: keyboard = .numbersAndPunctuation
        default: break
        }

        var returnKey: UIReturnKeyType = .default
        switch imeOptions & 0xff {
        case 2: returnKey = .go
        case 3: returnKey = .search
        case 4: returnKey = .send
        case 5: returnKey = .next
        case 6: returnKey = .done
        default: break
        }

        var capitalization: UITextAutocapitalizationType = .none
        if cls == 1 {
            if (flags & 0x1000) != 0 { capitalization = .allCharacters }
            else if (flags & 0x2000) != 0 { capitalization = .words }
            else if (flags & 0x4000) != 0 { capitalization = .sentences }
        }

        guestKeyboardType = keyboard
        guestReturnKeyType = returnKey
        guestSecureEntry = secure
        guestAutocapitalization = capitalization
        guestAutocorrection = (cls == 1 && (flags & 0x8000) != 0 && !secure) ? .yes : .no
    }
}


// Present guest termination without exposing runtime logs.
struct CrashAlertView: View {
    let crashInfo: CrashInfo
    let onDismiss: () -> Void

    var body: some View {
        ZStack {
            Color.black.opacity(0.5).ignoresSafeArea()
            VStack(alignment: .leading, spacing: 20) {
                Image(systemName: "app.badge")
                    .font(.system(size: 32, weight: .light))
                Text("App stopped")
                    .font(.title2.bold())
                Text("\(crashInfo.appName) closed unexpectedly.")
                    .font(.subheadline).foregroundColor(.secondary)
                Button(action: onDismiss) {
                    Text("Return to library")
                        .font(.headline).frame(maxWidth: .infinity)
                        .padding(.vertical, 14)
                        .background(ShellStyle.ink).foregroundColor(ShellStyle.inverse)
                        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                }.buttonStyle(.plain)
            }
            .padding(28).frame(maxWidth: 380, alignment: .leading)
            .background(ShellStyle.surface)
            .clipShape(RoundedRectangle(cornerRadius: 24, style: .continuous))
            .padding(24)
        }
    }
}

#Preview {
    ContentView()
}

@_cdecl("kudroid_trigger_haptic")
public func kudroid_trigger_haptic(intensity: Int32) {
    DispatchQueue.main.async {
        if intensity == 1 {
            let generator = UIImpactFeedbackGenerator(style: .light)
            generator.prepare()
            generator.impactOccurred()
        } else if intensity == 2 {
            let generator = UIImpactFeedbackGenerator(style: .medium)
            generator.prepare()
            generator.impactOccurred()
        } else if intensity >= 3 {
            let generator = UIImpactFeedbackGenerator(style: .heavy)
            generator.prepare()
            generator.impactOccurred()
        }
    }
}

@_cdecl("kudroid_notify_orientation_change")
public func kudroid_notify_orientation_change(orientation: Int32) {
    // Only active guests may rotate their surface.
    if kudroid_has_crashed() != 0 { return }
    DispatchQueue.main.async {
        guard AppDelegate.guestOrientationActive, kudroid_has_crashed() == 0,
              kudroid_get_requested_orientation() == orientation else { return }
        if #available(iOS 16.0, *) {
            NativeMetalViewController.sCurrentRunnerVC?.setNeedsUpdateOfSupportedInterfaceOrientations()
        }
        UIViewController.attemptRotationToDeviceOrientation()

        if orientation == 0 || orientation == 6 || orientation == 8 || orientation == 11 {
            // Landscape
            NSLog("[KuDroid] kudroid_notify_orientation_change: LANDSCAPE (%d)", orientation)
            if #available(iOS 16.0, *) {
                for scene in UIApplication.shared.connectedScenes {
                    if let windowScene = scene as? UIWindowScene {
                        let geometryPreferences = UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: .landscape)
                        windowScene.requestGeometryUpdate(geometryPreferences) { error in
                            NSLog("[KuDroid] requestGeometryUpdate landscape error: %@", error.localizedDescription)
                        }
                    }
                }
            } else {
                UIDevice.current.setValue(UIInterfaceOrientation.landscapeRight.rawValue, forKey: "orientation")
                UIViewController.attemptRotationToDeviceOrientation()
            }
        } else if orientation == 1 || orientation == 7 || orientation == 9 || orientation == 12 {
            // Portrait
            NSLog("[KuDroid] kudroid_notify_orientation_change: PORTRAIT (%d)", orientation)
            if #available(iOS 16.0, *) {
                for scene in UIApplication.shared.connectedScenes {
                    if let windowScene = scene as? UIWindowScene {
                        let geometryPreferences = UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: .portrait)
                        windowScene.requestGeometryUpdate(geometryPreferences) { error in
                            NSLog("[KuDroid] requestGeometryUpdate portrait error: %@", error.localizedDescription)
                        }
                    }
                }
            } else {
                UIDevice.current.setValue(UIInterfaceOrientation.portrait.rawValue, forKey: "orientation")
                UIViewController.attemptRotationToDeviceOrientation()
            }
        }
    }
}
