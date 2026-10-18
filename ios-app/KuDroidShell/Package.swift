// swift-tools-version: 6.0
import PackageDescription
import Foundation

let packageDir = URL(fileURLWithPath: #filePath).deletingLastPathComponent().path

let package = Package(
    name: "KuDroidShell",
    platforms: [
        .iOS(.v15),
    ],
    products: [
        .library(
            name: "KuDroidShell",
            targets: ["KuDroidShell"]
        ),
    ],
    targets: [
        .target(
            name: "KuDroidShell",
            dependencies: [
                "MoltenVK",
                "libEGL",
                "libGLESv2",
            ],
            path: ".",
            sources: [
                "KuDroidApp.swift",
                "ContentView.swift",
                "RemoteDebugClient.swift",
                "IOSDiagnostics.swift",
                "CameraBridge.swift",
                "MediaPlayerBridge.swift",
                "SensorBridge.swift",
                "NetworkBridge.swift",
            ],
            resources: [.copy("THIRD_PARTY_NOTICES.txt")],
            swiftSettings: [
                .unsafeFlags([
                    "-swift-version", "5",
                    "-import-objc-header", "\(packageDir)/kudroid_bridge.h",
                    "-I", "\(packageDir)"
                ]),
            ],
            linkerSettings: [
                .linkedLibrary("kudroid_core_combined"),
                .linkedLibrary("z"),
                .linkedLibrary("resolv"),
                .linkedLibrary("c++"),
                .linkedFramework("Metal"),
                .linkedFramework("QuartzCore"),
                .linkedFramework("Foundation"),
                .linkedFramework("UIKit"),
                .linkedFramework("CoreGraphics"),
                .linkedFramework("IOSurface"),
                .linkedFramework("AudioToolbox"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("CoreMedia"),
                .linkedFramework("CoreVideo"),
                .linkedFramework("Network"),
                .linkedFramework("Security"),
                .unsafeFlags(["-L", "\(packageDir)/libs", "-Xlinker", "-export_dynamic"])
            ]
        ),
        .binaryTarget(
            name: "MoltenVK",
            path: "../../third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework"
        ),
        .binaryTarget(
            name: "libEGL",
            path: "../../third_party/ANGLE/lib/libEGL.xcframework"
        ),
        .binaryTarget(
            name: "libGLESv2",
            path: "../../third_party/ANGLE/lib/libGLESv2.xcframework"
        ),
    ]
)
