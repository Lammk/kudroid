import AVFoundation
import UIKit

/// Host side of android.hardware.Camera: one AVCaptureSession whose frames and photos
/// are pushed into the core, and whose preview can be shown over the guest's surface.
final class KuDroidCameraBridge: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate,
                                 AVCapturePhotoCaptureDelegate {
    static let shared = KuDroidCameraBridge()

    // Session control and frame delivery each get a serial queue; neither touches the main thread.
    private let sessionQueue = DispatchQueue(label: "kudroid.camera.session")
    private let frameQueue = DispatchQueue(label: "kudroid.camera.frames")

    private var session: AVCaptureSession? {
        get { sessionLock.lock(); defer { sessionLock.unlock() }; return sessionStorage }
        set { sessionLock.lock(); sessionStorage = newValue; sessionLock.unlock() }
    }
    private var sessionStorage: AVCaptureSession?
    private let sessionLock = NSLock()
    private var videoOutput: AVCaptureVideoDataOutput?
    private var photoOutput: AVCapturePhotoOutput?
    // Front sensors deliver frames a half turn away from the orientation reported to the
    // guest (270), so the core rotates them before the guest sees them.
    private var rotateFrames = false
    private var previewLayer: AVCaptureVideoPreviewLayer?
    private var errorObserver: NSObjectProtocol?

    private func device(for id: Int32) -> AVCaptureDevice? {
        let position: AVCaptureDevice.Position = id == 0 ? .back : .front
        return AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: position)
    }

    // Camera ids: 0 is the back camera, 1 the front, each only when the device has it.
    func cameraCount() -> Int32 {
        if device(for: 0) == nil { return device(for: 1) == nil ? 0 : 1 }
        return device(for: 1) == nil ? 1 : 2
    }

    private func physicalID(_ id: Int32) -> Int32 {
        // With no back camera the front one is the only camera, id 0.
        return device(for: 0) == nil ? 1 : id
    }

    func info(_ id: Int32, _ facing: UnsafeMutablePointer<Int32>?,
              _ orientation: UnsafeMutablePointer<Int32>?) -> Int32 {
        let physical = physicalID(id)
        guard device(for: physical) != nil else { return -1 }
        facing?.pointee = physical == 0 ? 0 : 1
        orientation?.pointee = physical == 0 ? 90 : 270
        return 0
    }

    func open(_ id: Int32) -> Int32 {
        guard AVCaptureDevice.authorizationStatus(for: .video) == .authorized else { return -1 }
        let physical = physicalID(id)
        guard let device = device(for: physical) else { return -1 }
        return sessionQueue.sync { () -> Int32 in
            closeLocked()
            guard let input = try? AVCaptureDeviceInput(device: device) else { return -1 }
            let session = AVCaptureSession()
            // Capture must not retarget the audio session the guest's audio runs on.
            session.automaticallyConfiguresApplicationAudioSession = false
            guard session.canAddInput(input) else { return -1 }
            session.addInput(input)
            self.session = session
            self.rotateFrames = physical == 1
            errorObserver = NotificationCenter.default.addObserver(
                forName: .AVCaptureSessionRuntimeError, object: session, queue: nil
            ) { _ in
                kudroid_camera_report_error(1)
            }
            return 0
        }
    }

    func close() {
        setPreview(visible: false, rotation: 0)
        sessionQueue.sync { closeLocked() }
    }

    // sessionQueue only.
    private func closeLocked() {
        if let observer = errorObserver {
            NotificationCenter.default.removeObserver(observer)
            errorObserver = nil
        }
        session?.stopRunning()
        session = nil
        videoOutput = nil
        photoOutput = nil
    }

    private func preset(_ width: Int32, _ height: Int32) -> AVCaptureSession.Preset {
        switch (width, height) {
        case (1920, 1080): return .hd1920x1080
        case (1280, 720): return .hd1280x720
        case (352, 288): return .cif352x288
        default: return .vga640x480
        }
    }

    func start(_ width: Int32, _ height: Int32) -> Int32 {
        return sessionQueue.sync { () -> Int32 in
            guard let session = session else { return -1 }
            session.beginConfiguration()
            let wanted = preset(width, height)
            if session.canSetSessionPreset(wanted) { session.sessionPreset = wanted }
            if videoOutput == nil {
                let output = AVCaptureVideoDataOutput()
                output.videoSettings = [
                    kCVPixelBufferPixelFormatTypeKey as String:
                        kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                ]
                output.alwaysDiscardsLateVideoFrames = true
                output.setSampleBufferDelegate(self, queue: frameQueue)
                if session.canAddOutput(output) {
                    session.addOutput(output)
                    videoOutput = output
                }
            }
            if photoOutput == nil {
                let output = AVCapturePhotoOutput()
                if session.canAddOutput(output) {
                    session.addOutput(output)
                    photoOutput = output
                }
            }
            session.commitConfiguration()
            guard videoOutput != nil else { return -1 }
            if !session.isRunning { session.startRunning() }
            return session.isRunning ? 0 : -1
        }
    }

    func stop() {
        sessionQueue.sync { session?.stopRunning() }
    }

    func takePicture() -> Int32 {
        return sessionQueue.sync { () -> Int32 in
            guard let output = photoOutput, session?.isRunning == true,
                  output.availablePhotoCodecTypes.contains(.jpeg) else { return -1 }
            let settings = AVCapturePhotoSettings(format: [AVVideoCodecKey: AVVideoCodecType.jpeg])
            output.capturePhoto(with: settings, delegate: self)
            return 0
        }
    }

    // Show the live preview over the guest's surface, or remove it.
    func setPreview(visible: Bool, rotation: Int32) {
        DispatchQueue.main.async {
            guard visible else {
                self.previewLayer = nil
                kudroidSetGuestPreviewLayer(nil)
                return
            }
            guard let session = self.session else { return }
            let layer = self.previewLayer ?? AVCaptureVideoPreviewLayer(session: session)
            layer.videoGravity = .resizeAspectFill
            if let connection = layer.connection, connection.isVideoOrientationSupported {
                switch rotation {
                case 90: connection.videoOrientation = .portrait
                case 180: connection.videoOrientation = .landscapeLeft
                case 270: connection.videoOrientation = .portraitUpsideDown
                default: connection.videoOrientation = .landscapeRight
                }
            }
            self.previewLayer = layer
            kudroidSetGuestPreviewLayer(layer)
        }
    }

    // MARK: AVCaptureVideoDataOutputSampleBufferDelegate

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer,
                       from connection: AVCaptureConnection) {
        guard let pixels = CMSampleBufferGetImageBuffer(sampleBuffer),
              CVPixelBufferGetPlaneCount(pixels) == 2 else { return }
        CVPixelBufferLockBaseAddress(pixels, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(pixels, .readOnly) }
        guard let luma = CVPixelBufferGetBaseAddressOfPlane(pixels, 0),
              let chroma = CVPixelBufferGetBaseAddressOfPlane(pixels, 1) else { return }
        kudroid_camera_push_frame(
            luma.assumingMemoryBound(to: UInt8.self),
            Int32(CVPixelBufferGetBytesPerRowOfPlane(pixels, 0)),
            chroma.assumingMemoryBound(to: UInt8.self),
            Int32(CVPixelBufferGetBytesPerRowOfPlane(pixels, 1)),
            Int32(CVPixelBufferGetWidthOfPlane(pixels, 0)),
            Int32(CVPixelBufferGetHeightOfPlane(pixels, 0)),
            rotateFrames ? 1 : 0)
    }

    // MARK: AVCapturePhotoCaptureDelegate

    func photoOutput(_ output: AVCapturePhotoOutput, didFinishProcessingPhoto photo: AVCapturePhoto,
                     error: Error?) {
        guard error == nil, let data = photo.fileDataRepresentation() else {
            kudroid_camera_push_photo(nil, 0)
            return
        }
        data.withUnsafeBytes { raw in
            kudroid_camera_push_photo(raw.bindMemory(to: UInt8.self).baseAddress, Int32(data.count))
        }
    }
}

private func makeCameraHost() -> KuDroidCameraHost {
    return KuDroidCameraHost(
        count: { KuDroidCameraBridge.shared.cameraCount() },
        info: { id, facing, orientation in KuDroidCameraBridge.shared.info(id, facing, orientation) },
        open: { KuDroidCameraBridge.shared.open($0) },
        close: { KuDroidCameraBridge.shared.close() },
        start: { KuDroidCameraBridge.shared.start($0, $1) },
        stop: { KuDroidCameraBridge.shared.stop() },
        take_picture: { KuDroidCameraBridge.shared.takePicture() },
        set_preview: { KuDroidCameraBridge.shared.setPreview(visible: $0 != 0, rotation: $1) }
    )
}

/// Register the capture session with the core so the guest can open a camera.
func registerKuDroidCameraHost() {
    var host = makeCameraHost()
    kudroid_set_camera_host(&host)
}
