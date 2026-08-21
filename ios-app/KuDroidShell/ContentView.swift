import SwiftUI
import UIKit
import AVFAudio
import Metal
import QuartzCore
import CoreMotion

struct ContentView: View {
    @State private var fullLog = "KuDroid Core Status"
    @State private var jitStatus = "JIT: Unknown"
    @State private var showJitWarning = false
    
    var body: some View {
        TabView {
            AppsView(fullLog: $fullLog)
                .tabItem {
                    Label("Apps", systemImage: "square.grid.2x2.fill")
                }
            
            PermissionsView()
                .tabItem {
                    Label("Permissions", systemImage: "lock.shield.fill")
                }
            
            DebugView(fullLog: $fullLog, jitStatus: $jitStatus)
                .tabItem {
                    Label("Debug", systemImage: "terminal.fill")
                }
        }
        .preferredColorScheme(.dark)
        .onAppear {
            setupLogDir()
            jitStatus = runJitStatus()
            activateAudioSession()
            if kudroid_is_jit_enabled() == 0 {
                showJitWarning = true
            }
        }
        .alert("JIT Required", isPresented: $showJitWarning) {
            Button("OK", role: .cancel) { }
        } message: {
            Text("To use this application please enable JIT because nothing will work without it,sorry about that but we have no choose left")
        }
    }
}

struct AppItem: Identifiable {
    let id: String // folder name (e.g. "rolling-sky-5-5-8")
    let displayName: String // display name (e.g. "Rolling Sky")
    let version: String // version (e.g. "5.5.8")
    let iconImage: UIImage?
}

// mark: - tab ứng dụng
struct AppsView: View {
    @EnvironmentObject private var session: AppSession
    @Binding var fullLog: String
    @State private var installedApps: [AppItem] = []
    @State private var showAPKInstaller = false
    @State private var showRenameAlert = false
    @State private var showJitWarning = false
    @State private var renamingAppId: String = ""
    @State private var newAppName: String = ""
    
    private var androidRootAppsURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("android_root/data/app", isDirectory: true)
    }
    
    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                
                VStack(spacing: 0) {
                    // tiêu đề
                    HStack {
                        Image(systemName: "cpu")
                            .font(.title)
                            .foregroundColor(.green)
                        Text("KuDroid")
                            .font(.title2.bold())
                        // Hiển thị version ngay trên màn hình — phân biệt bản cũ/mới
                        // khi nghi ngờ iPhone đang chạy IPA lỗi thời.
                        Text("v" + appVersion())
                            .font(.caption.weight(.semibold))
                            .foregroundColor(.green.opacity(0.8))
                        Spacer()
                        Button(action: { showAPKInstaller = true }) {
                            Image(systemName: "plus.circle.fill")
                                .font(.title2)
                            .foregroundColor(.green)
                        }
                    }
                    .padding()
                    
                    if installedApps.isEmpty {
                        VStack(spacing: 16) {
                            Spacer()
                            Image(systemName: "cube.box.fill")
                                .font(.system(size: 64))
                                .foregroundColor(.secondary)
                            Text("No Android Apps Installed")
                                .font(.headline)
                            Text("Tap the + button to install an APK")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                            Spacer()
                        }
                    } else {
                        List(installedApps) { app in
                            HStack(spacing: 12) {
                                if let icon = app.iconImage {
                                    Image(uiImage: icon)
                                        .resizable()
                                        .aspectRatio(contentMode: .fill)
                                        .frame(width: 48, height: 48)
                                        .clipShape(RoundedRectangle(cornerRadius: 11, style: .continuous))
                                        .overlay(
                                            RoundedRectangle(cornerRadius: 11, style: .continuous)
                                                .stroke(Color.white.opacity(0.15), lineWidth: 0.5)
                                        )
                                        .shadow(color: .black.opacity(0.3), radius: 4, x: 0, y: 2)
                                } else {
                                    ZStack {
                                        RoundedRectangle(cornerRadius: 11, style: .continuous)
                                            .fill(LinearGradient(colors: [Color.green.opacity(0.3), Color.teal.opacity(0.2)], startPoint: .topLeading, endPoint: .bottomTrailing))
                                            .frame(width: 48, height: 48)
                                            .overlay(
                                                RoundedRectangle(cornerRadius: 11, style: .continuous)
                                                    .stroke(Color.green.opacity(0.3), lineWidth: 1)
                                            )
                                        Image(systemName: "cube.fill")
                                            .font(.system(size: 22))
                                            .foregroundColor(.green)
                                    }
                                }
                                
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(app.displayName)
                                        .font(.headline)
                                        .foregroundColor(.white)
                                        .lineLimit(1)
                                    
                                    HStack(spacing: 6) {
                                        Text("v\(app.version)")
                                            .font(.caption2.bold())
                                            .padding(.horizontal, 6)
                                            .padding(.vertical, 2)
                                            .background(Color.green.opacity(0.2))
                                            .foregroundColor(.green)
                                            .cornerRadius(6)
                                        
                                        Text("ARM64 Native")
                                            .font(.caption)
                                            .foregroundColor(.secondary)
                                    }
                                }
                                
                                Spacer()
                                
                                Button(action: {
                                    if kudroid_is_jit_enabled() == 0 {
                                        showJitWarning = true
                                    } else {
                                        session.activeGuestApp = app.id
                                    }
                                }) {
                                    Text("RUN")
                                        .font(.caption.bold())
                                        .padding(.horizontal, 16)
                                        .padding(.vertical, 8)
                                        .background(Color.green.opacity(0.25))
                                        .foregroundColor(.green)
                                        .cornerRadius(16)
                                }
                            }
                            .padding(.vertical, 4)
                            .listRowBackground(Color(.systemGray6))
                            .contextMenu {
                                Button {
                                    renamingAppId = app.id
                                    newAppName = app.displayName
                                    showRenameAlert = true
                                } label: {
                                    Label("Rename", systemImage: "pencil")
                                }
                                Button(role: .destructive) {
                                    clearAppCache(name: app.id)
                                } label: {
                                    Label("Clear Cache", systemImage: "trash")
                                }
                                Button(role: .destructive) {
                                    deleteApp(name: app.id)
                                } label: {
                                    Label("Delete App", systemImage: "xmark.bin")
                                }
                            }
                        }
                        .onAppear {
                            // cách khắc phục tạm thời cho các phiên bản trước ios 16
                            UITableView.appearance().backgroundColor = .clear
                        }
                    }
                }
            }
            .navigationBarHidden(true)
            .onAppear(perform: loadInstalledApps)
            .sheet(isPresented: $showAPKInstaller, onDismiss: loadInstalledApps) {
                APKInstallerView { log in
                    fullLog = log
                    showAPKInstaller = false
                }
            }
            .alert("Rename App", isPresented: $showRenameAlert) {
                TextField("App Name", text: $newAppName)
                Button("Cancel", role: .cancel) {}
                Button("Save") {
                    renameApp(id: renamingAppId, newName: newAppName)
                }
            } message: {
                Text("Enter a new display name for this app.")
            }
            .alert("JIT Required", isPresented: $showJitWarning) {
                Button("OK", role: .cancel) { }
            } message: {
                Text("To use this application please enable JIT because nothing will work without it,sorry about that but we have no choose left")
            }
        }
    }

    private func loadInstalledApps() {
        guard let url = androidRootAppsURL else { return }
        do {
            let contents = try FileManager.default.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isDirectoryKey])
            var items: [AppItem] = []
            for folder in contents.filter({ $0.hasDirectoryPath }) {
                var folderName = folder.lastPathComponent
                var currentFolderURL = folder
                
                // 1. Tự động chuẩn hóa ngay lập tức nếu tên thư mục có dấu gạch dưới version (ví dụ ru.zdevs.zarchiver_1.0.10)
                if folderName.contains("_") {
                    let cleanPkg = String(folderName.split(separator: "_")[0])
                    if cleanPkg.contains(".") {
                        let targetURL = url.appendingPathComponent(cleanPkg, isDirectory: true)
                        if !FileManager.default.fileExists(atPath: targetURL.path) {
                            try? FileManager.default.moveItem(at: currentFolderURL, to: targetURL)
                            folderName = cleanPkg
                            currentFolderURL = targetURL
                        } else {
                            try? FileManager.default.removeItem(at: currentFolderURL)
                            folderName = cleanPkg
                            currentFolderURL = targetURL
                        }
                    }
                }

                // Đọc app_info.json nếu có
                var infoURL = currentFolderURL.appendingPathComponent("app_info.json")
                var displayName = prettifyAppName(folderName)
                var version = extractVersionFromFolderName(folderName) ?? "1.0.0"
                
                if let infoData = try? Data(contentsOf: infoURL),
                   let rawObj = try? JSONSerialization.jsonObject(with: infoData),
                   let json = rawObj as? [String: Any] {
                    if let realPkg = json["package"] as? String, !realPkg.isEmpty && realPkg != folderName {
                        let targetURL = url.appendingPathComponent(realPkg, isDirectory: true)
                        if !FileManager.default.fileExists(atPath: targetURL.path) {
                            try? FileManager.default.moveItem(at: currentFolderURL, to: targetURL)
                            folderName = realPkg
                            currentFolderURL = targetURL
                            infoURL = currentFolderURL.appendingPathComponent("app_info.json")
                        }
                    }
                    if let label = json["label"] as? String, !label.isEmpty {
                        displayName = prettifyAppName(label)
                    }
                    if let ver = json["version"] as? String, !ver.isEmpty && ver != "1.0.0" {
                        version = ver
                    }
                }
                
                // Đọc app_icon.png nếu có
                var iconImg: UIImage? = nil
                let iconURL = currentFolderURL.appendingPathComponent("app_icon.png")
                if FileManager.default.fileExists(atPath: iconURL.path) {
                    iconImg = UIImage(contentsOfFile: iconURL.path)
                }
                
                items.append(AppItem(id: folderName, displayName: displayName, version: version, iconImage: iconImg))
            }
            installedApps = items.sorted { $0.displayName.localizedCaseInsensitiveCompare($1.displayName) == .orderedAscending }
        } catch {
            print("Failed to load apps: \(error)")
            installedApps = []
        }
    }
    
    private func prettifyAppName(_ raw: String) -> String {
        if raw.isEmpty { return "Android App" }
        var s = raw

        // 1. Tách package name nếu có (ví dụ "com.discord" -> "discord")
        if s.contains(".") {
            if let last = s.split(separator: ".").last {
                s = String(last)
            }
        }

        // 2. Bỏ đuôi .apk
        if s.lowercased().hasSuffix(".apk") {
            s = String(s.dropLast(4))
        }

        let lower = s.lowercased()
        if lower.contains("minecraft") || lower.contains("mojang") { return "Minecraft" }
        if lower.contains("ultrakill") { return "ULTRAKILL" }
        if lower.contains("discord") { return "Discord" }
        if lower.contains("rolling") && lower.contains("sky") { return "Rolling Sky" }
        if lower.contains("triangle") { return "Triangle Test" }

        // 3. Tách các tiền tố/hậu tố rác thường gặp trong tên file APK mod/port
        let junkWords: Set<String> = [
            "apk", "arm64", "arm64v8a", "v8a", "vulkan", "gles", "mod",
            "signed", "release", "debug", "beta", "alpha", "jakitomzed",
            "bandishare", "apkpure", "moddroid", "an1", "apkmirror"
        ]
        
        let components = s.split(whereSeparator: { $0 == "-" || $0 == "_" || $0 == " " })
        var validWords: [String] = []
        for comp in components {
            let str = String(comp)
            let wLower = str.lowercased()
            if junkWords.contains(wLower) { continue }
            // Bỏ qua version thuần số ví dụ "5.5.8" hoặc "v202"
            if str.allSatisfy({ $0.isNumber || $0 == "." }) || (str.hasPrefix("v") && str.dropFirst().allSatisfy({ $0.isNumber || $0 == "." })) {
                continue
            }
            validWords.append(str)
        }
        
        if !validWords.isEmpty {
            s = validWords.joined(separator: " ")
        } else {
            s = s.replacingOccurrences(of: "-", with: " ").replacingOccurrences(of: "_", with: " ")
        }

        // 4. Viết hoa chữ cái đầu và tách CamelCase (ví dụ: "rollingsky" -> "Rolling Sky", "Discord" -> "Discord")
        var result = ""
        for (i, char) in s.enumerated() {
            if i > 0 && char.isUppercase && s[s.index(s.startIndex, offsetBy: i - 1)].isLowercase && !result.hasSuffix(" ") {
                result += " "
            }
            result.append(char)
        }

        return result.capitalized
    }
    
    private func runApp(name: String) {
        guard let cString = kudroid_run_apk(name) else {
            fullLog = "[kudroid_core] ERROR: null result from kudroid_run_apk"
            return
        }
        fullLog = String(cString: cString)
        free(UnsafeMutablePointer(mutating: cString))
    }
    
    private func clearAppCache(name: String) {
        let success = kudroid_clear_app_cache(name)
        fullLog = success == 1 ? "Cleared cache for \(name)" : "Failed to clear cache for \(name)"
    }
    
    private func deleteApp(name: String) {
        let success = kudroid_delete_app(name)
        fullLog = success == 1 ? "Deleted app \(name)" : "Failed to delete app \(name)"
        loadInstalledApps()
    }
    
    private func renameApp(id: String, newName: String) {
        let trimmed = newName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, let root = androidRootAppsURL else { return }
        let infoURL = root.appendingPathComponent(id).appendingPathComponent("app_info.json")
        var json: [String: Any] = [:]
        if let data = try? Data(contentsOf: infoURL),
           let rawObj = try? JSONSerialization.jsonObject(with: data),
           let existing = rawObj as? [String: Any] {
            json = existing
        }
        json["label"] = trimmed
        if let outData = try? JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted]) {
            try? outData.write(to: infoURL)
        }
        loadInstalledApps()
    }
}

// mark: - tab quản lý quyền ứng dụng (PermissionsView)
struct PermissionGroupItem: Identifiable {
    var id: String { key }
    let key: String
    let displayName: String
    let description: String
    var granted: Bool
}

struct PermissionsView: View {
    @State private var installedApps: [AppItem] = []
    @State private var appPermissions: [String: [PermissionGroupItem]] = [:]
    @State private var expandedAppIds: Set<String> = []
    @State private var searchText: String = ""
    
    private var androidRootAppsURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("android_root/data/app", isDirectory: true)
    }

    private var filteredApps: [AppItem] {
        if searchText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return installedApps
        }
        return installedApps.filter {
            $0.displayName.localizedCaseInsensitiveContains(searchText) ||
            $0.id.localizedCaseInsensitiveContains(searchText)
        }
    }

    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                
                VStack(spacing: 0) {
                    // Tiêu đề
                    HStack {
                        Image(systemName: "lock.shield.fill")
                            .font(.title)
                            .foregroundColor(.green)
                        Text("App Permissions")
                            .font(.title2.bold())
                        Spacer()
                    }
                    .padding(.horizontal)
                    .padding(.top, 12)
                    .padding(.bottom, 8)
                    
                    // Thanh tìm kiếm
                    HStack(spacing: 8) {
                        Image(systemName: "magnifyingglass")
                            .foregroundColor(.secondary)
                        TextField("Search installed apps...", text: $searchText)
                            .font(.subheadline)
                            .foregroundColor(.white)
                            .autocapitalization(.none)
                            .disableAutocorrection(true)
                        
                        if !searchText.isEmpty {
                            Button(action: { searchText = "" }) {
                                Image(systemName: "xmark.circle.fill")
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                    .padding(10)
                    .background(Color(.systemGray6))
                    .cornerRadius(10)
                    .padding(.horizontal)
                    .padding(.bottom, 8)
                    
                    if installedApps.isEmpty {
                        VStack(spacing: 16) {
                            Spacer()
                            Image(systemName: "shield.slash")
                                .font(.system(size: 64))
                                .foregroundColor(.secondary)
                            Text("No Installed Apps Found")
                                .font(.headline)
                            Text("Install apps from the Apps tab to configure permissions.")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                                .multilineTextAlignment(.center)
                                .padding(.horizontal)
                            Spacer()
                        }
                    } else if filteredApps.isEmpty {
                        VStack(spacing: 12) {
                            Spacer()
                            Image(systemName: "magnifyingglass")
                                .font(.system(size: 48))
                                .foregroundColor(.secondary)
                            Text("No matching apps found")
                                .font(.headline)
                                .foregroundColor(.secondary)
                            Spacer()
                        }
                    } else {
                        ScrollView {
                            LazyVStack(spacing: 10) {
                                ForEach(filteredApps) { app in
                                    appPermissionCard(app: app)
                                }
                            }
                            .padding(.horizontal)
                            .padding(.bottom, 20)
                        }
                    }
                }
            }
            .navigationBarHidden(true)
            .onAppear(perform: loadData)
        }
    }
    
    private func appPermissionCard(app: AppItem) -> some View {
        let isExpanded = expandedAppIds.contains(app.id)
        let groups = appPermissions[app.id] ?? []
        let grantedCount = groups.filter({ $0.granted }).count
        
        return VStack(spacing: 0) {
            // Header bấm để mở/đóng
            Button(action: {
                withAnimation(.easeInOut(duration: 0.22)) {
                    if isExpanded {
                        expandedAppIds.remove(app.id)
                    } else {
                        expandedAppIds.insert(app.id)
                    }
                }
            }) {
                HStack(spacing: 12) {
                    if let icon = app.iconImage {
                        Image(uiImage: icon)
                            .resizable()
                            .aspectRatio(contentMode: .fill)
                            .frame(width: 40, height: 40)
                            .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
                    } else {
                        ZStack {
                            RoundedRectangle(cornerRadius: 9, style: .continuous)
                                .fill(Color.green.opacity(0.2))
                                .frame(width: 40, height: 40)
                            Image(systemName: "cube.fill")
                                .font(.system(size: 18))
                                .foregroundColor(.green)
                        }
                    }
                    
                    VStack(alignment: .leading, spacing: 2) {
                        Text(app.displayName)
                            .font(.headline)
                            .foregroundColor(.white)
                            .lineLimit(1)
                        
                        Text("\(grantedCount)/\(groups.count) permissions granted")
                            .font(.caption2)
                            .foregroundColor(grantedCount > 0 ? .green : .secondary)
                    }
                    
                    Spacer()
                    
                    Image(systemName: isExpanded ? "chevron.down" : "chevron.right")
                        .font(.caption.bold())
                        .foregroundColor(.secondary)
                        .padding(6)
                }
                .padding(12)
                .background(Color(.systemGray6))
            }
            .buttonStyle(PlainButtonStyle())
            
            // Danh sách permissions (hiện ra khi expanded)
            if isExpanded {
                VStack(spacing: 0) {
                    Divider().background(Color.white.opacity(0.1))
                    
                    ForEach(groups) { group in
                        VStack(spacing: 0) {
                            HStack {
                                HStack(spacing: 8) {
                                    Image(systemName: iconForGroup(group.key))
                                        .foregroundColor(.green)
                                        .frame(width: 22)
                                    VStack(alignment: .leading, spacing: 2) {
                                        Text(group.displayName)
                                            .font(.subheadline.bold())
                                            .foregroundColor(.white)
                                        Text(group.description)
                                            .font(.caption2)
                                            .foregroundColor(.secondary)
                                    }
                                }
                                
                                Spacer()
                                
                                Toggle("", isOn: Binding(
                                    get: { group.granted },
                                    set: { newValue in
                                        togglePermission(appId: app.id, groupKey: group.key, granted: newValue)
                                    }
                                ))
                                .labelsHidden()
                                .toggleStyle(SwitchToggleStyle(tint: .green))
                            }
                            .padding(.horizontal, 14)
                            .padding(.vertical, 10)
                            
                            if group.id != groups.last?.id {
                                Divider()
                                    .background(Color.white.opacity(0.05))
                                    .padding(.leading, 44)
                            }
                        }
                    }
                    
                    // Nút thao tác nhanh Grant All / Revoke All ở chân Card
                    HStack {
                        Button(action: {
                            revokeAll(appId: app.id)
                        }) {
                            Text("Revoke All")
                                .font(.caption.bold())
                                .foregroundColor(.red.opacity(0.8))
                                .padding(.horizontal, 12)
                                .padding(.vertical, 6)
                                .background(Color.red.opacity(0.15))
                                .cornerRadius(8)
                        }
                        
                        Spacer()
                        
                        Button(action: {
                            grantAll(appId: app.id)
                        }) {
                            Text("Grant All")
                                .font(.caption.bold())
                                .foregroundColor(.green)
                                .padding(.horizontal, 12)
                                .padding(.vertical, 6)
                                .background(Color.green.opacity(0.2))
                                .cornerRadius(8)
                        }
                    }
                    .padding(.horizontal, 14)
                    .padding(.vertical, 10)
                    .background(Color.white.opacity(0.02))
                }
                .background(Color(.systemGray6).opacity(0.6))
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .stroke(isExpanded ? Color.green.opacity(0.3) : Color.white.opacity(0.08), lineWidth: 1)
        )
    }
    
    private func iconForGroup(_ key: String) -> String {
        switch key {
        case "storage": return "internaldrive.fill"
        case "network": return "network"
        case "camera": return "camera.fill"
        case "microphone": return "mic.fill"
        case "location": return "location.fill"
        case "bluetooth": return "gamecontroller.fill"
        default: return "checkmark.shield.fill"
        }
    }
    
    private func loadData() {
        loadInstalledApps()
        for app in installedApps {
            loadPermissionsForApp(appId: app.id)
        }
    }
    
    private func loadInstalledApps() {
        guard let url = androidRootAppsURL else { return }
        do {
            let contents = try FileManager.default.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isDirectoryKey])
            var items: [AppItem] = []
            for folder in contents.filter({ $0.hasDirectoryPath }) {
                let folderName = folder.lastPathComponent
                var displayName = folderName
                var version = "1.0.0"
                let infoURL = folder.appendingPathComponent("app_info.json")
                if let infoData = try? Data(contentsOf: infoURL),
                   let rawObj = try? JSONSerialization.jsonObject(with: infoData),
                   let json = rawObj as? [String: Any] {
                    if let label = json["label"] as? String, !label.isEmpty {
                        displayName = label
                    }
                    if let ver = json["version"] as? String, !ver.isEmpty {
                        version = ver
                    }
                }
                var iconImg: UIImage? = nil
                let iconURL = folder.appendingPathComponent("app_icon.png")
                if FileManager.default.fileExists(atPath: iconURL.path) {
                    iconImg = UIImage(contentsOfFile: iconURL.path)
                }
                items.append(AppItem(id: folderName, displayName: displayName, version: version, iconImage: iconImg))
            }
            installedApps = items.sorted { $0.displayName.localizedCaseInsensitiveCompare($1.displayName) == .orderedAscending }
        } catch {
            installedApps = []
        }
    }
    
    private func loadPermissionsForApp(appId: String) {
        let defaultGroups = [
            PermissionGroupItem(key: "storage", displayName: "Storage Access", description: "Read & write files, SD card (/sdcard/)", granted: kudroid_is_group_granted(appId, "storage") == 1),
            PermissionGroupItem(key: "network", displayName: "Internet & Network", description: "Access Wi-Fi and mobile networks", granted: kudroid_is_group_granted(appId, "network") == 1),
            PermissionGroupItem(key: "camera", displayName: "Camera", description: "Take pictures and record video", granted: kudroid_is_group_granted(appId, "camera") == 1),
            PermissionGroupItem(key: "microphone", displayName: "Microphone & Audio", description: "Record voice and audio", granted: kudroid_is_group_granted(appId, "microphone") == 1),
            PermissionGroupItem(key: "location", displayName: "Location", description: "Access GPS and location", granted: kudroid_is_group_granted(appId, "location") == 1),
            PermissionGroupItem(key: "bluetooth", displayName: "Bluetooth & Gamepads", description: "Connect controllers and gamepads", granted: kudroid_is_group_granted(appId, "bluetooth") == 1)
        ]
        appPermissions[appId] = defaultGroups
    }
    
    private func togglePermission(appId: String, groupKey: String, granted: Bool) {
        kudroid_set_group_permission(appId, groupKey, granted ? 1 : 0)
        if var list = appPermissions[appId] {
            for i in 0..<list.count {
                if list[i].key == groupKey {
                    list[i].granted = granted
                }
            }
            appPermissions[appId] = list
        }
    }
    
    private func grantAll(appId: String) {
        kudroid_grant_all_permissions(appId)
        loadPermissionsForApp(appId: appId)
    }

    private func revokeAll(appId: String) {
        let groups = appPermissions[appId] ?? []
        for group in groups {
            kudroid_set_group_permission(appId, group.key, 0)
        }
        loadPermissionsForApp(appId: appId)
    }
}

// mark: - tab gỡ lỗi (DebugView rút gọn chỉ giữ KDB & Live Terminal Log)
struct DebugView: View {
    @Binding var fullLog: String
    @Binding var jitStatus: String
    @State private var showCopyAlert = false
    @State private var kdbServerIP: String = UserDefaults.standard.string(forKey: "kdb_server_ip") ?? ""
    @State private var isKdbConnected: Bool = false
    
    private var previewLog: String {
        let lines = fullLog.components(separatedBy: "\n")
        if lines.count <= 35 { return fullLog }
        return lines.suffix(35).joined(separator: "\n")
    }
    
    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                
                VStack(spacing: 12) {
                    // Thanh kết nối KDB Bridge từ xa
                    HStack(spacing: 8) {
                        Image(systemName: isKdbConnected ? "antenna.radiowaves.left.and.right" : "antenna.radiowaves.left.and.right.slash")
                            .foregroundColor(isKdbConnected ? .green : .gray)
                        TextField("KDB Server IP (e.g. 192.168.1.5:8080)", text: $kdbServerIP)
                            .font(.caption.monospaced())
                            .textFieldStyle(.roundedBorder)
                            .autocapitalization(.none)
                            .disableAutocorrection(true)
                        
                        Button(isKdbConnected ? "Disconnect" : "Connect") {
                            UserDefaults.standard.set(kdbServerIP, forKey: "kdb_server_ip")
                            if isKdbConnected {
                                RemoteDebugClient.shared.disconnect()
                            } else {
                                RemoteDebugClient.shared.connect(host: kdbServerIP)
                            }
                        }
                        .font(.caption.bold())
                        .padding(.horizontal, 10)
                        .padding(.vertical, 6)
                        .background(isKdbConnected ? Color.red.opacity(0.25) : Color.green.opacity(0.25))
                        .foregroundColor(isKdbConnected ? .red : .green)
                        .cornerRadius(8)
                    }
                    .padding(.horizontal)
                    .onAppear {
                        RemoteDebugClient.shared.onConnectionStatusChanged = { connected in
                            isKdbConnected = connected
                        }
                    }

                    HStack {
                        Text("Live Terminal Log")
                            .font(.headline)
                            .foregroundColor(.white)
                        Spacer()
                        Label(jitStatus, systemImage: jitStatus.contains("Enabled") ? "bolt.fill" : "bolt.slash.fill")
                            .font(.caption)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(jitStatus.contains("Enabled") ? Color.green.opacity(0.2) : Color.red.opacity(0.2))
                            .foregroundColor(jitStatus.contains("Enabled") ? .green : .red)
                            .cornerRadius(8)
                    }
                    .padding(.horizontal)
                    
                    ScrollView {
                        Text(previewLog)
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundColor(.green)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(12)
                    }
                    .frame(maxHeight: .infinity)
                    .background(Color(.systemGray6))
                    .cornerRadius(12)
                    .padding(.horizontal)
                    
                    // Nút thao tác Log (Copy Log & Clear Log)
                    HStack(spacing: 12) {
                        Button(action: {
                            UIPasteboard.general.string = fullLog
                            showCopyAlert = true
                        }) {
                            HStack {
                                Image(systemName: "doc.on.doc.fill")
                                Text("Copy Log")
                            }
                            .font(.subheadline.bold())
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 10)
                            .background(Color.green.opacity(0.2))
                            .foregroundColor(.green)
                            .cornerRadius(10)
                        }
                        
                        Button(action: {
                            fullLog = "[kudroid_core] Log cleared."
                        }) {
                            HStack {
                                Image(systemName: "trash.fill")
                                Text("Clear Log")
                            }
                            .font(.subheadline.bold())
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 10)
                            .background(Color.red.opacity(0.2))
                            .foregroundColor(.red)
                            .cornerRadius(10)
                        }
                    }
                    .padding(.horizontal)
                    .padding(.bottom, 8)
                }
            }
            .navigationBarHidden(true)
            .alert("Copied!", isPresented: $showCopyAlert) {
                Button("OK", role: .cancel) {}
            } message: {
                Text("Full log (\(fullLog.count) chars) copied to clipboard.")
            }
        }
    }
}

// mark: - các hàm hỗ trợ bộ nối gốc

/// Bật audio session playback để CoreAudio (AudioQueue trong AudioShim) phát
/// được âm thanh — không có session hoạt động thì iOS im lặng.
func activateAudioSession() {
    let session = AVAudioSession.sharedInstance()
    // Build chạy với -swift-version 4 (mặc định của CMake khi enable_language(Swift)).
    // Ở mode này NS_TYPED_EXTENSIBLE_ENUM của ObjC (AVAudioSession.Category / Mode)
    // bị import thành raw NSString typealias → không có member .playback/.default,
    // param chỉ nhận String. Dùng raw constant string (đúng giá trị ObjC) —
    // compile được trong Swift 4 mode; nếu sau này nâng lên Swift 5 thì đổi về
    // AVAudioSession.Category.playback / AVAudioSession.Mode.default.
    try? session.setCategory("AVAudioSessionCategoryPlayback",
                             mode: "AVAudioSessionModeDefault",
                             options: [])
    try? session.setActive(true)
}

/// Đọc CFBundleShortVersionString từ Info.plist (0.1.5).
func appVersion() -> String {
    if let v = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String {
        return v
    }
    return "0.0.0"
}

func setupLogDir() {
    if let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
        kudroid_set_log_dir(docs.path)
        kudroid_set_documents_dir(docs.path)
        let apkInbox = docs.appendingPathComponent("put_apk_here", isDirectory: true)
        try? FileManager.default.createDirectory(at: apkInbox, withIntermediateDirectories: true)
    }
}

func runJniJvmTest() -> String {
    let rtJarPath = Bundle.main.path(forResource: "minijvm_rt", ofType: "jar") ?? ""
    guard let cString = kudroid_test_jvm(rtJarPath) else { return "Error: null result" }
    let log = String(cString: cString)
    free(UnsafeMutablePointer(mutating: cString))
    return log
}

func runGpuTest() -> String {
    // lưu ý: đảm bảo kudroid_test_gpu được khai báo trong tiêu đề kết nối
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

// mark: - giao diện cài đặt apk
struct APKInstallerView: View {
    let onInstall: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var apkFiles: [URL] = []
    @State private var selectedAPK: URL?
    @State private var status = ""
    @State private var isInstalling = false
    @State private var installStep = ""
    @State private var installedPackages: Set<String> = []

    private var inboxURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("put_apk_here", isDirectory: true)
    }

    private var androidRootAppsURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent("android_root/data/app", isDirectory: true)
    }

    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                
                VStack(spacing: 0) {
                    if let inboxURL {
                        Text(inboxURL.path)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundColor(.secondary)
                            .textSelection(.enabled)
                            .padding()
                    }

                    List(apkFiles, id: \.path) { apk in
                        Button {
                            if !isInstalling { selectedAPK = apk }
                        } label: {
                            HStack {
                                Image(systemName: "shippingbox.fill")
                                    .font(.title2)
                                    .foregroundColor(.green)
                                    .padding(.trailing, 4)
                                
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(apk.lastPathComponent)
                                        .foregroundColor(.white)
                                        .lineLimit(1)
                                    
                                    HStack(spacing: 6) {
                                        Text(fileSize(apk))
                                            .font(.caption)
                                            .foregroundColor(.secondary)
                                        
                                        if let badge = apkStatusBadge(for: apk) {
                                            Text(badge.text)
                                                .font(.system(size: 9, weight: .black))
                                                .padding(.horizontal, 6)
                                                .padding(.vertical, 2)
                                                .background(badge.isUpdate ? Color.orange.opacity(0.25) : Color.green.opacity(0.25))
                                                .foregroundColor(badge.isUpdate ? .orange : .green)
                                                .cornerRadius(4)
                                        }
                                    }
                                }
                                Spacer()
                                if selectedAPK == apk {
                                    Image(systemName: "checkmark.circle.fill")
                                        .foregroundColor(.green)
                                }
                            }
                        }
                        .listRowBackground(Color(.systemGray6))
                        .disabled(isInstalling)
                    }
                    .onAppear {
                        UITableView.appearance().backgroundColor = .clear
                    }
                    .overlay {
                        if apkFiles.isEmpty {
                            VStack(spacing: 8) {
                                Image(systemName: "folder.badge.plus").font(.largeTitle).foregroundColor(.secondary)
                                Text("Put .apk files in Documents/put_apk_here").multilineTextAlignment(.center).foregroundColor(.white)
                                Text("Then tap Refresh").font(.caption).foregroundColor(.secondary)
                            }
                            .padding()
                        }
                    }

                    // Khung animation trạng thái cài đặt
                    if isInstalling {
                        VStack(spacing: 12) {
                            ProgressView()
                                .progressViewStyle(CircularProgressViewStyle(tint: .green))
                                .scaleEffect(1.3)
                            
                            Text(installStep)
                                .font(.subheadline.weight(.semibold))
                                .foregroundColor(.green)
                            
                            HStack(spacing: 6) {
                                Image(systemName: "cpu")
                                    .font(.caption)
                                    .foregroundColor(.green)
                                Text("Compiling DEX file (DEX→JAR AOT)")
                                    .font(.caption.weight(.medium))
                                    .foregroundColor(.white)
                            }
                            .padding(.horizontal, 10)
                            .padding(.vertical, 4)
                            .background(Color.green.opacity(0.15))
                            .cornerRadius(6)
                            
                            Text("Extracting assets, translating DEX bytecode & linking native ARM64 libraries...")
                                .font(.caption2)
                                .foregroundColor(.secondary)
                                .multilineTextAlignment(.center)
                        }
                        .padding()
                        .frame(maxWidth: .infinity)
                        .background(Color(.systemGray6).opacity(0.8))
                        .cornerRadius(12)
                        .padding(.horizontal)
                        .transition(.opacity.combined(with: .scale))
                    }

                    if !status.isEmpty && !isInstalling {
                        Text(status).font(.caption).foregroundColor(.red).padding(.horizontal)
                    }

                    HStack(spacing: 16) {
                        Button("Refresh") { refresh() }
                            .buttonStyle(.bordered)
                            .disabled(isInstalling)
                        
                        Button(action: startInstall) {
                            HStack {
                                if isInstalling {
                                    ProgressView()
                                        .progressViewStyle(CircularProgressViewStyle(tint: .black))
                                        .padding(.trailing, 4)
                                    Text("Installing...")
                                } else if let selectedAPK, let badge = apkStatusBadge(for: selectedAPK) {
                                    Image(systemName: badge.isUpdate ? "arrow.triangle.2.circlepath" : "arrow.clockwise.circle.fill")
                                    Text(badge.isUpdate ? "Update App" : "Reinstall App")
                                } else {
                                    Image(systemName: "arrow.down.circle.fill")
                                    Text("Install Selected")
                                }
                            }
                            .font(.body.bold())
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(selectedAPK != nil && (apkStatusBadge(for: selectedAPK!)?.isUpdate ?? false) ? .orange : .green)
                        .disabled(selectedAPK == nil || isInstalling)
                    }
                    .padding()
                }
            }
            .navigationTitle("Install APK")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") { dismiss() }
                        .foregroundColor(.green)
                        .disabled(isInstalling)
                }
            }
            .onAppear { refresh() }
        }
    }

    private func getInstalledPackage(for url: URL) -> String? {
        let name = url.deletingPathExtension().lastPathComponent.lowercased()
        for installed in installedPackages {
            let lower = installed.lowercased()
            if lower == name || name.contains(lower) || lower.contains(name) {
                return installed
            }
        }
        return nil
    }

    private func apkStatusBadge(for url: URL) -> (text: String, isUpdate: Bool)? {
        guard let installed = getInstalledPackage(for: url) else { return nil }
        
        let apkVer = extractVersionFromFolderName(url.lastPathComponent)
        let installedVer = extractVersionFromFolderName(installed)
        
        if let av = apkVer, let iv = installedVer, av != iv {
            return ("UPDATE v\(av)", true)
        }
        return ("INSTALLED", false)
    }

    private func startInstall() {
        guard let selectedAPK else { return }
        withAnimation {
            isInstalling = true
            installStep = "Compiling DEX file & installing \(selectedAPK.lastPathComponent)..."
        }
        
        DispatchQueue.global(qos: .userInitiated).async {
            let log = installAPK(at: selectedAPK)
            DispatchQueue.main.async {
                withAnimation {
                    self.isInstalling = false
                    self.onInstall(log)
                }
            }
        }
    }

    private func refresh() {
        guard let inboxURL else {
            status = "Documents directory unavailable"
            return
        }
        do {
            try FileManager.default.createDirectory(at: inboxURL, withIntermediateDirectories: true)
            let supportedExtensions = ["apk", "apkm", "xapk", "apks"]
            apkFiles = try FileManager.default.contentsOfDirectory(
                at: inboxURL, includingPropertiesForKeys: [.fileSizeKey], options: [.skipsHiddenFiles]
            ).filter { supportedExtensions.contains($0.pathExtension.lowercased()) }
             .sorted { $0.lastPathComponent.localizedCaseInsensitiveCompare($1.lastPathComponent) == .orderedAscending }
            if let selectedAPK, !apkFiles.contains(selectedAPK) { self.selectedAPK = nil }
            
            // Tải danh sách app đã cài để check update
            if let root = androidRootAppsURL,
               let folders = try? FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: nil) {
                installedPackages = Set(folders.map { $0.lastPathComponent })
            }
            
            status = ""
        } catch {
            status = "Cannot scan put_apk_here: \(error.localizedDescription)"
        }
    }

    private func fileSize(_ url: URL) -> String {
        let values = try? url.resourceValues(forKeys: [.fileSizeKey])
        return ByteCountFormatter.string(fromByteCount: Int64(values?.fileSize ?? 0), countStyle: .file)
    }
}

// mark: - các hàm hỗ trợ gốc khác
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

    case "jvm", "jni_jvm":
        log = runJniJvmTest()
        logFile = "test_jvm.log"

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
        log = "❌ Unknown test: '\(name)'. Available: gpu, bionic, jni, jvm, syscall, multi_elf, opengl_so, vulkan_so, vfs, all"
        logFile = "test_error.log"
    }

    saveTestLog(filename: logFile, content: log)
    let isSuccess = !log.contains("❌") && !log.contains("ERROR") && !log.contains("Failed")
    return (isSuccess, log, logFile)
}

// mark: - Bộ thực thi độc quyền toàn màn hình (Dedicated Clean Container)
struct DedicatedAppRunnerView: UIViewControllerRepresentable {
    let appName: String
    let onExit: () -> Void
    let onCrash: (String, String) -> Void

    func makeUIViewController(context: Context) -> NativeMetalViewController {
        let vc = NativeMetalViewController(appName: appName, onExit: onExit, onCrash: onCrash)
        return vc
    }

    func updateUIViewController(_ uiViewController: NativeMetalViewController, context: Context) {
        DispatchQueue.main.async {
            uiViewController.startAppIfNeeded()
        }
    }
}

class NativeMetalViewController: UIViewController {
    let appName: String
    let onExit: () -> Void
    let onCrash: (String, String) -> Void
    private var isStarted = false
    private var metalView: NativeMetalView!
    private var crashCheckTimer: Timer?
    private var statusLabel: UILabel!
    private let motionManager = CMMotionManager()
    private var lastRequestedOrientation: Int32 = -1

    init(appName: String, onExit: @escaping () -> Void, onCrash: @escaping (String, String) -> Void) {
        self.appName = appName
        self.onExit = onExit
        self.onCrash = onCrash
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        metalView = NativeMetalView(frame: UIScreen.main.bounds)
        self.view = metalView
    }

    override var supportedInterfaceOrientations: UIInterfaceOrientationMask {
        let req = kudroid_get_requested_orientation()
        if req == 0 || req == 6 || req == 8 {
            return .landscape
        } else if req == 1 || req == 7 || req == 9 {
            return .portrait
        }
        return .allButUpsideDown
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        self.view.backgroundColor = .black
        metalView.backgroundColor = .black

        // Nhãn chẩn đoán trạng thái tức thời trên màn hình
        statusLabel = UILabel()
        statusLabel.text = "KuDroid: Initializing \(appName)..."
        statusLabel.textColor = UIColor.green.withAlphaComponent(0.8)
        statusLabel.font = UIFont.monospacedSystemFont(ofSize: 12, weight: .semibold)
        statusLabel.backgroundColor = UIColor.black.withAlphaComponent(0.6)
        statusLabel.layer.cornerRadius = 8
        statusLabel.layer.masksToBounds = true
        statusLabel.textAlignment = .center
        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(statusLabel)

        // Nút đóng nổi (Floating Exit Button) để người dùng quay về Launcher bất cứ lúc nào
        let closeButton = UIButton(type: .system)
        let config = UIImage.SymbolConfiguration(pointSize: 22, weight: .bold)
        closeButton.setImage(UIImage(systemName: "xmark.circle.fill", withConfiguration: config), for: .normal)
        closeButton.tintColor = UIColor.white.withAlphaComponent(0.7)
        closeButton.translatesAutoresizingMaskIntoConstraints = false
        closeButton.addTarget(self, action: #selector(handleExitButton), for: .touchUpInside)
        view.addSubview(closeButton)

        NSLayoutConstraint.activate([
            statusLabel.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 10),
            statusLabel.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 16),
            statusLabel.heightAnchor.constraint(equalToConstant: 28),
            statusLabel.widthAnchor.constraint(greaterThanOrEqualToConstant: 180),

            closeButton.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 10),
            closeButton.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),
            closeButton.widthAnchor.constraint(equalToConstant: 40),
            closeButton.heightAnchor.constraint(equalToConstant: 40)
        ])

        // Tự động làm mờ nhãn trạng thái sau 3 giây
        DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) { [weak self] in
            UIView.animate(withDuration: 0.5) {
                self?.statusLabel.alpha = 0.0
            }
        }

        // Khởi động CoreMotion để truyền dữ liệu cảm biến vào KuDroid bridge
        if motionManager.isAccelerometerAvailable {
            motionManager.accelerometerUpdateInterval = 0.02 // 50 Hz
            motionManager.startAccelerometerUpdates(to: .main) { data, _ in
                guard let data = data else { return }
                kudroid_inject_sensor_event(1, Float(data.acceleration.x * 9.81), Float(data.acceleration.y * 9.81), Float(data.acceleration.z * 9.81))
            }
        }
        if motionManager.isGyroAvailable {
            motionManager.gyroUpdateInterval = 0.02
            motionManager.startGyroUpdates(to: .main) { data, _ in
                guard let data = data else { return }
                kudroid_inject_sensor_event(4, Float(data.rotationRate.x), Float(data.rotationRate.y), Float(data.rotationRate.z))
            }
        }

        startAppIfNeeded()
    }

    @objc private func handleExitButton() {
        motionManager.stopAccelerometerUpdates()
        motionManager.stopGyroUpdates()
        crashCheckTimer?.invalidate()
        kudroid_unbind_metal_layer()
        onExit()
    }

    deinit {
        motionManager.stopAccelerometerUpdates()
        motionManager.stopGyroUpdates()
        crashCheckTimer?.invalidate()
        kudroid_unbind_metal_layer()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        if !isStarted, let metalLayer = view.layer as? CAMetalLayer {
            let scale = UIScreen.main.scale
            let bounds = view.bounds.size.width > 0 ? view.bounds : UIScreen.main.bounds
            let targetSize = CGSize(width: bounds.width * scale, height: bounds.height * scale)
            if metalLayer.drawableSize != targetSize {
                metalLayer.drawableSize = targetSize
            }
        }
        startAppIfNeeded()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        startAppIfNeeded()
    }

    func startAppIfNeeded() {
        guard !isStarted else { return }
        isStarted = true

        NSLog("[KuDroid] >>> Launching guest app: %@ <<<", appName)

        let scale = UIScreen.main.scale
        let bounds = view.bounds.size.width > 0 ? view.bounds : UIScreen.main.bounds
        let width = Int32(bounds.width * scale)
        let height = Int32(bounds.height * scale)

        if let metalLayer = view.layer as? CAMetalLayer {
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
        }

        let unmanaged = Unmanaged.passUnretained(view.layer)
        kudroid_set_metal_layer(unmanaged.toOpaque(), width, height, Float(scale))

        // Timer quét trạng thái crash và hướng màn hình định kỳ từ C++ bridge
        crashCheckTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] timer in
            guard let self = self else { return }
            if kudroid_has_crashed() != 0 {
                timer.invalidate()
                self.handleCrash()
                return
            }

            // Quét hướng màn hình yêu cầu từ Android guest app
            let reqOri = kudroid_get_requested_orientation()
            if reqOri != self.lastRequestedOrientation {
                self.lastRequestedOrientation = reqOri
                if reqOri == 0 || reqOri == 6 || reqOri == 8 {
                    // Landscape
                    NSLog("[KuDroid] Guest app requested LANDSCAPE orientation (%d)", reqOri)
                    if #available(iOS 16.0, *) {
                        if let windowScene = self.view.window?.windowScene {
                            let geometryPreferences = UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: .landscape)
                            windowScene.requestGeometryUpdate(geometryPreferences) { error in
                                NSLog("[KuDroid] requestGeometryUpdate landscape error: %@", error.localizedDescription)
                            }
                        }
                    }
                } else if reqOri == 1 || reqOri == 7 || reqOri == 9 {
                    // Portrait
                    NSLog("[KuDroid] Guest app requested PORTRAIT orientation (%d)", reqOri)
                    if #available(iOS 16.0, *) {
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

        // Chạy APK Android với độ ưu tiên cao nhất của Interactive UI
        DispatchQueue.global(qos: .userInteractive).async { [weak self] in
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
                self.crashCheckTimer?.invalidate()
                if kudroid_has_crashed() != 0 {
                    self.handleCrash(fallbackLog: logOutput)
                } else {
                    // App đã chạy xong mà không crash (hoặc trả về log kết thúc)
                    self.statusLabel.alpha = 1.0
                    self.statusLabel.text = "Session ended. Tap ✕ to close."
                    self.statusLabel.textColor = .yellow
                }
            }
        }
    }

    private func handleCrash(fallbackLog: String = "") {
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
        crashCheckTimer?.invalidate()
        kudroid_clear_crash_state()
    }
}

class NativeMetalView: UIView {
    override class var layerClass: AnyClass {
        return CAMetalLayer.self
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
        let scale = UIScreen.main.scale
        metalLayer.contentsScale = scale
        let bounds = self.bounds.size.width > 0 ? self.bounds : UIScreen.main.bounds
        metalLayer.drawableSize = CGSize(width: bounds.width * scale, height: bounds.height * scale)
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        guard let metalLayer = self.layer as? CAMetalLayer else { return }
        let scale = UIScreen.main.scale
        let sz = CGSize(width: bounds.width * scale, height: bounds.height * scale)
        if sz.width > 0 && sz.height > 0 && metalLayer.drawableSize != sz {
            metalLayer.drawableSize = sz
        }
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

// Hàm nhận buffer 2D vẽ bằng CPU từ C++ và blit thẳng lên màn hình iOS (CALayer)
@_cdecl("kudroid_blit_canvas_to_layer")
public func kudroid_blit_canvas_to_layer(layerPtr: UnsafeMutableRawPointer?, bits: UnsafeRawPointer?, width: Int32, height: Int32) {
    guard let layerPtr = layerPtr, let bits = bits, width > 0, height > 0 else { return }
    let layer = Unmanaged<CALayer>.fromOpaque(layerPtr).takeUnretainedValue()
    let w = Int(width)
    let h = Int(height)
    let bytesPerRow = w * 4
    let colorSpace = CGColorSpaceCreateDeviceRGB()
    let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)
    guard let dataProvider = CGDataProvider(dataInfo: nil, data: bits, size: w * h * 4, releaseData: { _, _, _ in }) else { return }
    guard let cgImage = CGImage(
        width: w,
        height: h,
        bitsPerComponent: 8,
        bitsPerPixel: 32,
        bytesPerRow: bytesPerRow,
        space: colorSpace,
        bitmapInfo: bitmapInfo,
        provider: dataProvider,
        decode: nil,
        shouldInterpolate: false,
        intent: .defaultIntent
    ) else { return }

    DispatchQueue.main.async {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        layer.contents = cgImage
        CATransaction.commit()
    }
}

// mark: - Modal thông báo Gentle Crash
struct CrashAlertView: View {
    let crashInfo: CrashInfo
    let onDismiss: () -> Void
    @State private var copied = false

    var body: some View {
        ZStack {
            Color.black.opacity(0.85).ignoresSafeArea()
            
            VStack(spacing: 16) {
                // Header Icon & Title
                VStack(spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .font(.system(size: 44))
                        .foregroundColor(.yellow)
                    
                    Text("Whoops, the app crashed")
                        .font(.title2.bold())
                        .foregroundColor(.white)
                    
                    Text("App '\(crashInfo.appName)' encountered an unexpected error.")
                        .font(.subheadline)
                        .foregroundColor(.gray)
                        .multilineTextAlignment(.center)
                }
                .padding(.top, 8)

                // Log Container (Tail 30 lines)
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Label("Recent Crash Logs (Tail 30 Lines)", systemImage: "terminal")
                            .font(.caption.bold())
                            .foregroundColor(.green)
                        Spacer()
                        Button(action: {
                            UIPasteboard.general.string = crashInfo.tailLog
                            copied = true
                            DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                                copied = false
                            }
                        }) {
                            HStack(spacing: 4) {
                                Image(systemName: copied ? "checkmark" : "doc.on.doc")
                                Text(copied ? "Copied" : "Copy")
                            }
                            .font(.caption.weight(.semibold))
                            .foregroundColor(.white)
                            .padding(.horizontal, 10)
                            .padding(.vertical, 4)
                            .background(Color.white.opacity(0.15))
                            .cornerRadius(6)
                        }
                    }

                    ScrollView(.vertical, showsIndicators: true) {
                        Text(crashInfo.tailLog.isEmpty ? "No detailed logs available." : crashInfo.tailLog)
                            .font(.system(size: 11, weight: .regular, design: .monospaced))
                            .foregroundColor(.green.opacity(0.9))
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(10)
                    }
                    .frame(maxHeight: 280)
                    .background(Color.black.opacity(0.7))
                    .cornerRadius(8)
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(Color.white.opacity(0.1), lineWidth: 1)
                    )
                }
                .padding(.horizontal)

                // Close Button
                Button(action: onDismiss) {
                    Text("Dismiss")
                        .font(.headline)
                        .foregroundColor(.black)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 12)
                        .background(Color.green)
                        .cornerRadius(10)
                }
                .padding(.horizontal)
                .padding(.bottom, 8)
            }
            .padding(.vertical, 16)
            .background(Color(UIColor.secondarySystemBackground).opacity(0.3))
            .background(.ultraThinMaterial)
            .cornerRadius(20)
            .overlay(
                RoundedRectangle(cornerRadius: 20)
                    .stroke(Color.white.opacity(0.2), lineWidth: 1)
            )
            .padding(.horizontal, 20)
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