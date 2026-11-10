import SwiftUI
import UIKit
import AVFAudio
import AVFoundation
import Metal
import QuartzCore
import CoreMotion
import Combine
import UniformTypeIdentifiers

func runJniJvmTest() -> String {
    // KuART embeds framework.dex in the binary so no jar path is needed.
    guard let cString = kudroid_test_jvm("") else { return "Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runGpuTest() -> String {
    // note: make sure kudroid_test_gpu is declared in the connection header
    guard let cString = kudroid_test_gpu() else { return "Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runVFSSelfTest() -> String {
    guard let cString = kudroid_vfs_self_test_log() else { return "Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runVFSExtendedTest() -> String {
    guard let cString = kudroid_vfs_extended_test_log() else { return "Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func installAPK(at apkURL: URL) -> String {
    guard let cString = kudroid_install_apk(apkURL.path) else { return "[kudroid_apk] Native installer returned no log" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func extractVersionFromFolderName(_ name: String) -> String? {
    let pattern = #"(\d+[\.\-_]\d+([\.\-_]\d+)*)"#
    if let regex = try? NSRegularExpression(pattern: pattern, options: []) {
        let nsString = name as NSString
        let results = regex.matches(in: name, options: [], range: NSRange(location: 0, length: nsString.length))
        if let lastMatch = results.last {
            let matchString = nsString.substring(with: lastMatch.range)
            let cleaned = matchString.replacingOccurrences(of: "_", with: ".").replacingOccurrences(of: "-", with: ".")
            return cleaned
        }
    }
    return nil
}

/// Read versionName from binary AndroidManifest.xml (AXML).
/// Standard source for display version — app_info.json may be stale.
/// Minimalist parser: string pool UTF-16/UTF-8 + find attr "versionName" in
///first START_ELEMENT tag named "manifest".
func extractVersionNameFromAxml(_ data: Data) -> String? {
    let bytes = [UInt8](data)
    guard bytes.count > 40 else { return nil }

    func u16(_ o: Int) -> Int { Int(bytes[o]) | (Int(bytes[o+1]) << 8) }
    func u32(_ o: Int) -> Int {
        u16(o) | (u16(o+2) << 16)
    }

    // File header: type=0x0003 | (headerSize<<16), fileSize.
    guard u32(0) == 0x00080003 else { return nil }

    // String pool at offset 8: type=0x0001, headerSize=28.
    let poolOff = 8
    guard u16(poolOff) == 0x0001 else { return nil }
    let stringCount = u32(poolOff + 8)
    let flags = u32(poolOff + 16)
    let stringsStart = poolOff + u32(poolOff + 20)
    let isUtf8 = (flags & (1 << 8)) != 0

    var strings: [String] = []
    strings.reserveCapacity(stringCount)
    for i in 0..<stringCount {
        let offEntry = poolOff + 28 + i * 4
        guard offEntry + 4 <= bytes.count else { break }
        let strOff = stringsStart + u32(offEntry)
        guard strOff < bytes.count else { strings.append(""); continue }

        if isUtf8 {
            var cur = strOff
            // u8len: 1-2 byte length prefix (number of characters), then byte-length prefix.
            cur += (bytes[cur] & 0x80) != 0 ? 2 : 1
            var byteLen = 0
            if cur < bytes.count {
                if (bytes[cur] & 0x80) != 0, cur + 1 < bytes.count {
                    byteLen = (Int(bytes[cur] & 0x7F) << 8) | Int(bytes[cur + 1])
                    cur += 2
                } else {
                    byteLen = Int(bytes[cur])
                    cur += 1
                }
            }
            guard cur + byteLen <= bytes.count else { strings.append(""); continue }
            strings.append(String(decoding: bytes[cur..<(cur+byteLen)], as: UTF8.self))
        } else {
            var cur = strOff
            guard cur + 2 <= bytes.count else { strings.append(""); continue }
            var charLen = u16(cur)
            cur += 2
            if (charLen & 0x8000) != 0 {
                guard cur + 2 <= bytes.count else { strings.append(""); continue }
                charLen = ((charLen & 0x7FFF) << 16) | u16(cur)
                cur += 2
            }
            var s = ""
            for _ in 0..<charLen where cur + 2 <= bytes.count {
                let ch = u16(cur)
                cur += 2
                if let scalar = Unicode.Scalar(ch) { s.unicodeScalars.append(scalar) }
            }
            strings.append(s)
        }
    }

    // Browse chunks to find START_ELEMENT of tag "manifest", read attr versionName.
    var cur = poolOff + u32(poolOff + 4)
    // Ignore resource map if present (type 0x0180).
    if cur + 8 <= bytes.count && u32(cur) == 0x00080180 {
        cur += u32(cur + 4)
    }
    while cur + 8 <= bytes.count {
        let chunkType = u32(cur)
        let chunkSize = u32(cur + 4)
        guard chunkSize >= 8, cur + chunkSize <= bytes.count else { break }

        if chunkType == 0x00100102 { // START_ELEMENT
            let nameIdx = u32(cur + 20)
            let tagName = nameIdx < strings.count ? strings[nameIdx] : ""
            if tagName == "manifest" {
                let attrStart = u16(cur + 24)
                let attrSize = u16(cur + 26)
                let attrCount = u16(cur + 28)
                // attributeStart counts from the beginning of struct attrExt (= chunk+16).
                var a = cur + 16 + attrStart
                for _ in 0..<attrCount where a + attrSize <= cur + chunkSize {
                    let attrNameIdx = u32(a + 4)
                    let rawValIdx = u32(a + 8)
                    let attrName = attrNameIdx < strings.count ? strings[attrNameIdx] : ""
                    if attrName == "versionName" {
                        if rawValIdx != 0xFFFFFFFF && rawValIdx < strings.count {
                            return strings[rawValIdx]
                        }
                        // typed value: dataType in a+15, data in a+16.
                        let dataType = bytes[a + 15]
                        if dataType == 0x03 { // TYPE_STRING
                            let dataVal = u32(a + 16)
                            if dataVal < strings.count { return strings[dataVal] }
                        }
                    }
                    a += attrSize
                }
                return nil // manifest is the first tag — if you don't see it, stop
            }
        }
        cur += chunkSize
    }
    return nil
}

// Copy provider files before handing them to the native installer.
final class APKInstallModel: ObservableObject {
    static let shared = APKInstallModel()
    @Published var busy = false
    @Published var stage = ""
    @Published var result: String?
    @Published var showError = false
    @Published var error = ""
    private let worker = DispatchQueue(label: "kudroid.install", qos: .userInitiated)

    func report(_ message: String) {
        error = message
        showError = true
    }

    func install(_ url: URL, completion: (() -> Void)? = nil) {
        guard !busy else { completion?(); return }
        busy = true
        result = nil
        stage = "Installing " + url.lastPathComponent + "…"
        let access = url.startAccessingSecurityScopedResource()
        worker.async {
            defer { if access { url.stopAccessingSecurityScopedResource() } }
            let fm = FileManager.default
            let work = fm.temporaryDirectory.appendingPathComponent("Install-" + UUID().uuidString)
            defer { try? fm.removeItem(at: work) }
            do {
                try fm.createDirectory(at: work, withIntermediateDirectories: true)
                let local = work.appendingPathComponent(url.lastPathComponent)
                try fm.copyItem(at: url, to: local)
                let log = installAPK(at: local)
                DispatchQueue.main.async {
                    self.busy = false
                    self.result = log
                    if log.contains("ERROR") { self.report(log) }
                    completion?()
                }
            } catch {
                DispatchQueue.main.async { self.busy = false; self.report(error.localizedDescription); completion?() }
            }
        }
    }
}

// mark: - other native support functions
func runJitStatus() -> String {
    guard let cString = kudroid_jit_status() else { return "JIT: Unknown" }
    let status = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return status
}

func runNativeTest(soName: String, testFunction: (UnsafePointer<CChar>?) -> UnsafePointer<CChar>?) -> String {
    guard let bundledURL = Bundle.main.url(forResource: soName, withExtension: "so") else {
        return "❌ \(soName).so not found in bundle"
    }
    let tmpURL = FileManager.default.temporaryDirectory.appendingPathComponent("\(soName).so")
    do {
        if FileManager.default.fileExists(atPath: tmpURL.path) { try FileManager.default.removeItem(at: tmpURL) }
        try FileManager.default.copyItem(at: bundledURL, to: tmpURL)
    } catch {
        return "❌ failed to copy \(soName).so: \(error.localizedDescription)"
    }
    guard let cString = tmpURL.path.withCString({ testFunction($0) }) else { return "❌ error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runBionicExecutionTest() -> String { return runNativeTest(soName: "test_bionic_lib", testFunction: kudroid_bionic_execution_test) }
func runGpuVulkanSoTest() -> String { return runNativeTest(soName: "test_gpu_vulkan", testFunction: kudroid_gpu_vulkan_so_test) }
func runGpuOpenglSoTest() -> String { return runNativeTest(soName: "test_gpu_opengl", testFunction: kudroid_gpu_opengl_so_test) }
func runSyscallSoTest() -> String { return runNativeTest(soName: "test_syscalls", testFunction: kudroid_syscall_so_test) }
func runJniMassiveTest() -> String { return runNativeTest(soName: "test_jni_massive", testFunction: kudroid_jni_massive_so_test) }

func runMultiElfTest() -> String {
    guard let consumer = Bundle.main.url(forResource: "libkudroid_consumer", withExtension: "so"),
          let provider = Bundle.main.url(forResource: "libkudroid_provider", withExtension: "so") else {
        return "❌ multi-elf provider/consumer libraries are missing from bundle"
    }
    let directory = FileManager.default.temporaryDirectory
    let consumerURL = directory.appendingPathComponent("libkudroid_consumer.so")
    let providerURL = directory.appendingPathComponent("libkudroid_provider.so")
    do {
        for url in [consumerURL, providerURL] {
            if FileManager.default.fileExists(atPath: url.path) { try FileManager.default.removeItem(at: url) }
        }
        try FileManager.default.copyItem(at: consumer, to: consumerURL)
        try FileManager.default.copyItem(at: provider, to: providerURL)
    } catch {
        return "❌ failed to prepare multi-elf test: \(error.localizedDescription)"
    }
    guard let cString = kudroid_multi_elf_test(consumerURL.path, providerURL.path) else { return "❌ error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func saveTestLog(filename: String, content: String) {
    if let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
        let fileURL = docs.appendingPathComponent(filename)
        try? content.write(to: fileURL, atomically: true, encoding: .utf8)
    }
}

func executeKuDroidTest(name: String) -> (success: Bool, log: String, logFilename: String) {
    let lower = name.lowercased().trimmingCharacters(in: .whitespacesAndNewlines)
    var log = ""
    var logFile = "test_\(lower).log"

    switch lower {
    case "gpu", "gpu_native", "shader":
        log = runGpuTest()
        logFile = "test_gpu.log"

    case "bionic":
        log = runBionicExecutionTest()
        logFile = "test_bionic.log"

    case "jni", "jni_massive":
        log = runJniMassiveTest()
        logFile = "test_jni.log"

    case "jvm", "jni_jvm", "kuart":
        log = runJniJvmTest()
        logFile = "test_kuart.log"

    case "syscall", "syscalls":
        log = runSyscallSoTest()
        logFile = "test_syscall.log"

    case "multi_elf", "linker":
        log = runMultiElfTest()
        logFile = "test_multi_elf.log"

    case "opengl_so", "opengl":
        log = runGpuOpenglSoTest()
        logFile = "test_opengl_so.log"

    case "vulkan_so", "vulkan":
        log = runGpuVulkanSoTest()
        logFile = "test_vulkan_so.log"

    case "vfs":
        log = runVFSExtendedTest()
        logFile = "test_vfs.log"

    case "audio", "sound":
        guard let cString = kudroid_test_audio() else { return (false, "❌ error: null result", "test_audio.log") }
        log = String(cString: cString)
        free(UnsafeMutablePointer(mutating: cString))
        logFile = "test_audio.log"

    case "all":
        var combined = "=== RUNNING ALL KUDROID ISOLATED SUBSYSTEM TESTS ===\n\n"
        let tests = ["gpu", "audio", "bionic", "syscall", "vfs", "multi_elf", "jvm", "jni"]
        for t in tests {
            combined += "--- TEST: \(t.uppercased()) ---\n"
            let res = executeKuDroidTest(name: t)
            combined += res.log + "\n\n"
        }
        log = combined
        logFile = "test_all.log"

    default:
        log = "❌ Unknown test: '\(name)'. Available: gpu, bionic, jni, kuart, syscall, multi_elf, opengl_so, vulkan_so, vfs, all"
        logFile = "test_error.log"
    }

    saveTestLog(filename: logFile, content: log)
    let isSuccess = !log.contains("❌") && !log.contains("ERROR") && !log.contains("Failed")
    return (isSuccess, log, logFile)
}

