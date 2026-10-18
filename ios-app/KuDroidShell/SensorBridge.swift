import CoreMotion
import Foundation

final class KuDroidSensorBridge {
    static let shared = KuDroidSensorBridge()
    private let motion = CMMotionManager()
    private let samples: OperationQueue = {
        let queue = OperationQueue()
        queue.name = "kudroid.sensor.samples"
        queue.maxConcurrentOperationCount = 1
        return queue
    }()
    private var rates: [Int32: Int32] = [:]
    private var active = false

    func register() {
        active = true
        var available: UInt32 = 0
        if motion.isAccelerometerAvailable { available |= 1 << 1 }
        if motion.isMagnetometerAvailable { available |= 1 << 2 }
        if motion.isGyroAvailable { available |= 1 << 4 }
        if motion.isDeviceMotionAvailable { available |= (1 << 9) | (1 << 10) }
        var host = KuDroidSensorHost(configure: { type, period in
            DispatchQueue.main.async { KuDroidSensorBridge.shared.configure(type, period) }
        })
        kudroid_set_sensor_host(&host, available)
    }

    private func publish(_ type: Int32, _ x: Double, _ y: Double, _ z: Double) {
        let values = [Float(x), Float(y), Float(z)]
        values.withUnsafeBufferPointer { kudroid_inject_sensor_sample(type, $0.baseAddress, 3) }
    }

    // Main thread owns stream configuration; samples arrive on a separate queue.
    private func configure(_ type: Int32, _ period: Int32) {
        guard active, rates[type] != period else { return }
        rates[type] = period
        let interval = Double(max(5000, period)) / 1_000_000
        switch type {
        case 1:
            if period == 0 { motion.stopAccelerometerUpdates(); return }
            motion.accelerometerUpdateInterval = interval
            if !motion.isAccelerometerActive {
                motion.startAccelerometerUpdates(to: samples) { [weak self] data, _ in
                    guard let data = data else { return }
                    self?.publish(1, -data.acceleration.x * 9.80665, -data.acceleration.y * 9.80665, -data.acceleration.z * 9.80665)
                }
            }
        case 2:
            if period == 0 { motion.stopMagnetometerUpdates(); return }
            motion.magnetometerUpdateInterval = interval
            if !motion.isMagnetometerActive {
                motion.startMagnetometerUpdates(to: samples) { [weak self] data, _ in
                    guard let data = data else { return }
                    self?.publish(2, data.magneticField.x, data.magneticField.y, data.magneticField.z)
                }
            }
        case 4:
            if period == 0 { motion.stopGyroUpdates(); return }
            motion.gyroUpdateInterval = interval
            if !motion.isGyroActive {
                motion.startGyroUpdates(to: samples) { [weak self] data, _ in
                    guard let data = data else { return }
                    self?.publish(4, data.rotationRate.x, data.rotationRate.y, data.rotationRate.z)
                }
            }
        case 9, 10:
            let periods = [rates[9] ?? 0, rates[10] ?? 0].filter { $0 > 0 }
            guard let fastest = periods.min() else { motion.stopDeviceMotionUpdates(); return }
            motion.deviceMotionUpdateInterval = Double(fastest) / 1_000_000
            if !motion.isDeviceMotionActive {
                motion.startDeviceMotionUpdates(to: samples) { [weak self] data, _ in
                    guard let data = data else { return }
                    self?.publish(9, -data.gravity.x * 9.80665, -data.gravity.y * 9.80665, -data.gravity.z * 9.80665)
                    self?.publish(10, -data.userAcceleration.x * 9.80665, -data.userAcceleration.y * 9.80665, -data.userAcceleration.z * 9.80665)
                }
            }
        default: break
        }
    }

    func stop() {
        active = false
        rates.removeAll()
        kudroid_set_sensor_host(nil, 0)
        motion.stopAccelerometerUpdates()
        motion.stopMagnetometerUpdates()
        motion.stopGyroUpdates()
        motion.stopDeviceMotionUpdates()
    }
}
