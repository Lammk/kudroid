import SwiftUI
import UIKit
import AVFAudio
import AVFoundation
import Metal
import QuartzCore
import CoreMotion
import Combine
import UniformTypeIdentifiers

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
                ShellStyle.background.ignoresSafeArea()
                
                VStack(spacing: 0) {
                    // Title
                    HStack {
                        Image(systemName: "lock.shield.fill")
                            .font(.title)
                            .foregroundColor(.primary)
                        Text("App Permissions")
                            .font(.title2.bold())
                        Spacer()
                    }
                    .padding(.horizontal)
                    .padding(.top, 12)
                    .padding(.bottom, 8)
                    
                    // Search bar
                    HStack(spacing: 8) {
                        Image(systemName: "magnifyingglass")
                            .foregroundColor(.secondary)
                        TextField("Search installed apps...", text: $searchText)
                            .font(.subheadline)
                            .foregroundColor(.primary)
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
                    .background(ShellStyle.surface)
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
            // Header click to open/close
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
                                .fill(ShellStyle.ink.opacity(0.2))
                                .frame(width: 40, height: 40)
                            Image(systemName: "cube.fill")
                                .font(.system(size: 18))
                                .foregroundColor(.primary)
                        }
                    }
                    
                    VStack(alignment: .leading, spacing: 2) {
                        Text(app.displayName)
                            .font(.headline)
                            .foregroundColor(.primary)
                            .lineLimit(1)
                        
                        Text("\(grantedCount)/\(groups.count) permissions granted")
                            .font(.caption2)
                            .foregroundColor(grantedCount > 0 ? .primary : .secondary)
                    }
                    
                    Spacer()
                    
                    Image(systemName: isExpanded ? "chevron.down" : "chevron.right")
                        .font(.caption.bold())
                        .foregroundColor(.secondary)
                        .padding(6)
                }
                .padding(12)
                .background(ShellStyle.surface)
            }
            .buttonStyle(PlainButtonStyle())
            
            // List of permissions (appears when expanded)
            if isExpanded {
                VStack(spacing: 0) {
                    Divider().background(ShellStyle.ink.opacity(0.1))
                    
                    ForEach(groups) { group in
                        VStack(spacing: 0) {
                            HStack {
                                HStack(spacing: 8) {
                                    Image(systemName: iconForGroup(group.key))
                                        .foregroundColor(.primary)
                                        .frame(width: 22)
                                    VStack(alignment: .leading, spacing: 2) {
                                        Text(group.displayName)
                                            .font(.subheadline.bold())
                                            .foregroundColor(.primary)
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
                                .toggleStyle(SwitchToggleStyle(tint: ShellStyle.ink))
                            }
                            .padding(.horizontal, 14)
                            .padding(.vertical, 10)
                            
                            if group.id != groups.last?.id {
                                Divider()
                                    .background(ShellStyle.ink.opacity(0.05))
                                    .padding(.leading, 44)
                            }
                        }
                    }
                    
                    // Quick action button Grant All / Revoke All at the foot of the Card
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
                                .foregroundColor(.primary)
                                .padding(.horizontal, 12)
                                .padding(.vertical, 6)
                                .background(ShellStyle.ink.opacity(0.2))
                                .cornerRadius(8)
                        }
                    }
                    .padding(.horizontal, 14)
                    .padding(.vertical, 10)
                    .background(ShellStyle.ink.opacity(0.02))
                }
                .background(ShellStyle.surface.opacity(0.6))
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .stroke(ShellStyle.line, lineWidth: 1)
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

// mark: - native connector support functions

