bool Invoke_java_io_File(Interpreter* interp, const char* name, const DexValue* args,
                         size_t num_args, DexValue* result) {
    std::string path = RemapJavaPath(GetFilePath(args[0]));
    if (path.empty()) {
        if (std::strcmp(name, "createNewFile") == 0 || std::strcmp(name, "createNewFile0") == 0) {
            interp->ThrowException("Ljava/io/IOException;", "Empty file path");
            return true;
        }
        // Every native here returns boolean/long/array; zero is the right answer
        // for an unusable path in all of them.
        *result = DexValue::Int(0);
        if (std::strcmp(name, "list") == 0) result->l = nullptr;
        return true;
    }

    struct stat st;
    int res = 0;
    {
        // File syscalls can block (network-backed roots, slow storage); the VM lock must
        // not be held across them or every Java thread, touch dispatch included, convoys.
        VmLockRelease unlocked;
        res = stat(path.c_str(), &st);
    }

    if (std::strcmp(name, "exists") == 0) {
        *result = DexValue::Int(res == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isDirectory") == 0) {
        *result = DexValue::Int(res == 0 && S_ISDIR(st.st_mode) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isFile") == 0) {
        *result = DexValue::Int(res == 0 && S_ISREG(st.st_mode) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "renameTo") == 0) {
        // The destination needs the same remapping as the source, or a rename inside
        // /data/data would move a container file to a path outside it and fail.
        const std::string dest = RemapJavaPath(GetFilePath(num_args > 1 ? args[1] : DexValue()));
        int rc = -1;
        if (!dest.empty()) {
            VmLockRelease unlocked;
            rc = kudroid::vfs_rename_resolved(path.c_str(), dest.c_str());
        }
        *result = DexValue::Int(rc == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "list") == 0) {
        // Collect names with the lock released; the DexObject allocation that follows
        // must happen under it. readdir's buffer is reused, so copy each name out.
        std::vector<std::string> names;
        bool opened = false;
        {
            VmLockRelease unlocked;
            DIR* dir = opendir(path.c_str());
            if (dir != nullptr) {
                opened = true;
                while (dirent* entry = readdir(dir)) {
                    if (std::strcmp(entry->d_name, ".") == 0 ||
                        std::strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }
                    names.emplace_back(entry->d_name);
                }
                closedir(dir);
            }
        }
        if (!opened) {
            result->l = nullptr;
            return true;
        }
        std::vector<DexObject*> objects;
        objects.reserve(names.size());
        for (const std::string& n : names) objects.push_back(interp->linker()->NewString(n.c_str()));
        result->l = NewRefArray(interp->linker(), "[Ljava/lang/String;", objects);
        return true;
    }
    if (std::strcmp(name, "canRead") == 0) {
        int rc = -1;
        {
            VmLockRelease unlocked;
            rc = access(path.c_str(), R_OK);
        }
        *result = DexValue::Int(rc == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "canWrite") == 0) {
        int rc = -1;
        {
            VmLockRelease unlocked;
            rc = access(path.c_str(), W_OK);
        }
        *result = DexValue::Int(rc == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "length") == 0) {
        *result = DexValue::Long(res == 0 ? st.st_size : 0);
        return true;
    }
    if (std::strcmp(name, "lastModified") == 0) {
        *result = DexValue::Long(res == 0 ? static_cast<int64_t>(st.st_mtime) * 1000 : 0);
        return true;
    }
    if (std::strcmp(name, "delete") == 0) {
        int rc = -1;
        {
            VmLockRelease unlocked;
            rc = (unlink(path.c_str()) == 0 || rmdir(path.c_str()) == 0) ? 0 : -1;
        }
        *result = DexValue::Int(rc == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "mkdir") == 0) {
        int rc = -1;
        {
            VmLockRelease unlocked;
            rc = mkdir(path.c_str(), 0755);
        }
        *result = DexValue::Int(rc == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "createNewFile") == 0 || std::strcmp(name, "createNewFile0") == 0) {
        int fd = -1;
        int error = 0;
        {
            VmLockRelease unlocked;
            fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
            if (fd < 0) error = errno;
            else close(fd);
        }
        *result = DexValue::Int(fd >= 0 ? 1 : 0);
        if (fd < 0 && error != EEXIST) {
            interp->ThrowException("Ljava/io/IOException;", std::strerror(error));
        }
        return true;
    }
    return false;
}

// java.io.FileInputStream / FileOutputStream / PrintStream
bool Invoke_java_io_FileInputStream(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                    size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "openNative") == 0) {
        // Static native: args[0] is the path, not a receiver. Resolve the path under
        // the lock; open() itself can block, so it runs with the lock released.
        const std::string path = RemapJavaPath(GetStringUtf8(args[0]));
        int fd = -1;
        if (!path.empty()) {
            VmLockRelease unlocked;
            fd = open(path.c_str(), O_RDONLY);
        }
        *result = DexValue::Int(fd);
        return true;
    }
    if (std::strcmp(name, "readNative") == 0) {
        int fd = args[0].i;
        auto* arr = reinterpret_cast<DexArray*>(args[1].l);
        int32_t off = args[2].i;
        int32_t len = args[3].i;
        if (fd < 0 || arr == nullptr || off < 0 || len < 0 || off + len > arr->length) {
            *result = DexValue::Int(-1);
            return true;
        }
        uint8_t* buf = reinterpret_cast<uint8_t*>(arr + 1) + off;
        ssize_t n;
        {
            // Reading a large asset can take tens of ms; release the lock for it.
            VmLockRelease unlocked;
            n = read(fd, buf, static_cast<size_t>(len));
        }
        *result = DexValue::Int(static_cast<int32_t>(n));
        return true;
    }
    if (std::strcmp(name, "skipNative") == 0) {
        const int fd = args[0].i;
        const int64_t n = args[1].j;
        if (fd < 0 || n <= 0) {
            *result = DexValue::Long(0);
            return true;
        }
        off_t before = -1;
        off_t after = -1;
        {
            VmLockRelease unlocked;
            before = lseek(fd, 0, SEEK_CUR);
            after = lseek(fd, static_cast<off_t>(n), SEEK_CUR);
        }
        *result = DexValue::Long(after < 0 || before < 0 ? 0 : static_cast<int64_t>(after - before));
        return true;
    }
    if (std::strcmp(name, "availableNative") == 0) {
        const int fd = args[0].i;
        struct stat st;
        off_t pos = -1;
        int rc = -1;
        {
            VmLockRelease unlocked;
            pos = fd >= 0 ? lseek(fd, 0, SEEK_CUR) : -1;
            rc = (fd < 0 || pos < 0) ? -1 : fstat(fd, &st);
        }
        if (rc != 0) {
            *result = DexValue::Int(0);
            return true;
        }
        const off_t remaining = st.st_size - pos;
        *result = DexValue::Int(remaining > 0 ? static_cast<int32_t>(remaining) : 0);
        return true;
    }
    if (std::strcmp(name, "closeNative") == 0) {
        int fd = args[0].i;
        if (fd >= 0) close(fd);
        return true;
    }
    return false;
}

bool Invoke_java_io_FileOutputStream(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                     size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "openNative") == 0) {
        const std::string path = RemapJavaPath(GetStringUtf8(args[0]));
        bool append = args[1].i != 0;
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = -1;
        if (!path.empty()) {
            VmLockRelease unlocked;
            fd = open(path.c_str(), flags, 0644);
        }
        *result = DexValue::Int(fd);
        return true;
    }
    if (std::strcmp(name, "writeNative") == 0) {
        int fd = args[0].i;
        auto* arr = reinterpret_cast<DexArray*>(args[1].l);
        int32_t off = args[2].i;
        int32_t len = args[3].i;
        if (fd >= 0 && arr != nullptr && off >= 0 && len >= 0 && off + len <= arr->length) {
            uint8_t* buf = reinterpret_cast<uint8_t*>(arr + 1) + off;
            ssize_t n;
            {
                VmLockRelease unlocked;
                n = write(fd, buf, static_cast<size_t>(len));
            }
            *result = DexValue::Int(static_cast<int32_t>(n));
        } else {
            *result = DexValue::Int(-1);
        }
        return true;
    }
    if (std::strcmp(name, "closeNative") == 0) {
        int fd = args[0].i;
        if (fd >= 0) close(fd);
        return true;
    }
    return false;
}

// java.io.RandomAccessFile: every native is static, args[0] is the path or descriptor.
bool Invoke_java_io_RandomAccessFile(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                     size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "openNative") == 0) {
        // Failure codes are coarse and platform independent: -1 other, -2 missing,
        // -3 access denied, -4 directory.
        const std::string path = RemapJavaPath(GetStringUtf8(args[0]));
        const bool write = args[1].i != 0;
        int fd = -1;
        int err = 0;
        if (!path.empty()) {
            VmLockRelease unlocked;
            fd = open(path.c_str(), write ? (O_RDWR | O_CREAT) : O_RDONLY, 0644);
            err = errno;
            if (fd >= 0) {
                struct stat st;
                if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
                    close(fd);
                    fd = -1;
                    err = EISDIR;
                }
            }
        } else {
            err = ENOENT;
        }
        if (fd < 0) {
            fd = err == ENOENT ? -2 : (err == EACCES || err == EPERM) ? -3 : err == EISDIR ? -4 : -1;
        }
        *result = DexValue::Int(fd);
        return true;
    }
    const int fd = args[0].i;
    if (std::strcmp(name, "readNative") == 0 || std::strcmp(name, "writeNative") == 0) {
        auto* arr = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t off = args[2].i;
        const int32_t len = args[3].i;
        if (fd < 0 || arr == nullptr || off < 0 || len < 0 || off > arr->length ||
            len > arr->length - off) {
            *result = DexValue::Int(-1);
            return true;
        }
        uint8_t* buf = reinterpret_cast<uint8_t*>(arr + 1) + off;
        ssize_t n;
        {
            VmLockRelease unlocked;
            n = name[0] == 'r' ? read(fd, buf, static_cast<size_t>(len))
                               : write(fd, buf, static_cast<size_t>(len));
        }
        *result = DexValue::Int(static_cast<int32_t>(n));
        return true;
    }
    if (std::strcmp(name, "seekNative") == 0) {
        off_t pos = -1;
        if (fd >= 0) {
            VmLockRelease unlocked;
            pos = lseek(fd, static_cast<off_t>(args[1].j), SEEK_SET);
        }
        *result = DexValue::Long(pos);
        return true;
    }
    if (std::strcmp(name, "positionNative") == 0) {
        off_t pos = -1;
        if (fd >= 0) {
            VmLockRelease unlocked;
            pos = lseek(fd, 0, SEEK_CUR);
        }
        *result = DexValue::Long(pos);
        return true;
    }
    if (std::strcmp(name, "lengthNative") == 0) {
        struct stat st;
        int rc = -1;
        if (fd >= 0) {
            VmLockRelease unlocked;
            rc = fstat(fd, &st);
        }
        *result = DexValue::Long(rc == 0 ? static_cast<int64_t>(st.st_size) : -1);
        return true;
    }
    if (std::strcmp(name, "setLengthNative") == 0) {
        int rc = -1;
        if (fd >= 0) {
            VmLockRelease unlocked;
            rc = ftruncate(fd, static_cast<off_t>(args[1].j));
        }
        *result = DexValue::Int(rc == 0 ? 0 : -1);
        return true;
    }
    if (std::strcmp(name, "syncNative") == 0) {
        if (fd >= 0) {
            VmLockRelease unlocked;
            fsync(fd);
        }
        return true;
    }
    if (std::strcmp(name, "closeNative") == 0) {
        if (fd >= 0) close(fd);
        return true;
    }
    return false;
}

#if defined(KUDROID_HAVE_SQLITE3)
// android.database.sqlite.SQLiteNative: a thin, static binding to the host's SQLite. Handles
// are the sqlite3 / sqlite3_stmt pointers carried as Java longs; the Java side owns their
// lifetime. Anything that can block runs with the VM lock released.
bool Invoke_android_database_sqlite_SQLiteNative(Interpreter* interp, const char* name,
                                                 const DexValue* args, size_t /*num_args*/,
                                                 DexValue* result) {
    auto asDb = [&](size_t i) { return reinterpret_cast<sqlite3*>(static_cast<intptr_t>(args[i].j)); };
    auto asStmt = [&](size_t i) { return reinterpret_cast<sqlite3_stmt*>(static_cast<intptr_t>(args[i].j)); };
    auto retString = [&](const char* text) {
        result->l = text != nullptr
            ? reinterpret_cast<DexObject*>(interp->linker()->NewString(text)) : nullptr;
    };
    static std::mutex s_open_error_mtx;
    static std::string s_open_error;

    if (std::strcmp(name, "open") == 0) {
        // ":memory:" and the empty name are SQLite's own temporary databases, not files.
        const std::string requested = GetStringUtf8(args[0]);
        const std::string path = (requested.empty() || requested == ":memory:")
                                     ? requested : RemapJavaPath(requested);
        const int flags = args[1].i;
        sqlite3* handle = nullptr;
        int rc;
        {
            VmLockRelease unlocked;
            rc = sqlite3_open_v2(path.c_str(), &handle, flags, nullptr);
        }
        if (rc != SQLITE_OK) {
            std::lock_guard<std::mutex> lock(s_open_error_mtx);
            s_open_error = handle != nullptr ? sqlite3_errmsg(handle) : sqlite3_errstr(rc);
            if (handle != nullptr) sqlite3_close_v2(handle);
            handle = nullptr;
        }
        *result = DexValue::Long(static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)));
        return true;
    }
    if (std::strcmp(name, "openError") == 0) {
        std::lock_guard<std::mutex> lock(s_open_error_mtx);
        retString(s_open_error.c_str());
        return true;
    }
    if (std::strcmp(name, "close") == 0) {
        sqlite3* db = asDb(0);
        if (db != nullptr) {
            VmLockRelease unlocked;
            sqlite3_close_v2(db);
        }
        return true;
    }
    if (std::strcmp(name, "errorMessage") == 0) {
        sqlite3* db = asDb(0);
        retString(db != nullptr ? sqlite3_errmsg(db) : "no database");
        return true;
    }
    if (std::strcmp(name, "errorCode") == 0) {
        sqlite3* db = asDb(0);
        *result = DexValue::Int(db != nullptr ? sqlite3_extended_errcode(db) : SQLITE_MISUSE);
        return true;
    }
    if (std::strcmp(name, "exec") == 0) {
        sqlite3* db = asDb(0);
        const std::string sql = GetStringUtf8(args[1]);
        int rc = SQLITE_MISUSE;
        if (db != nullptr) {
            VmLockRelease unlocked;
            rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        }
        *result = DexValue::Int(rc);
        return true;
    }
    if (std::strcmp(name, "changes") == 0) {
        sqlite3* db = asDb(0);
        *result = DexValue::Int(db != nullptr ? sqlite3_changes(db) : 0);
        return true;
    }
    if (std::strcmp(name, "lastInsertRowId") == 0) {
        sqlite3* db = asDb(0);
        *result = DexValue::Long(db != nullptr ? sqlite3_last_insert_rowid(db) : -1);
        return true;
    }
    if (std::strcmp(name, "prepare") == 0) {
        sqlite3* db = asDb(0);
        const std::string sql = GetStringUtf8(args[1]);
        sqlite3_stmt* stmt = nullptr;
        if (db != nullptr) {
            VmLockRelease unlocked;
            if (sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr) != SQLITE_OK) {
                stmt = nullptr;
            }
        }
        *result = DexValue::Long(static_cast<int64_t>(reinterpret_cast<intptr_t>(stmt)));
        return true;
    }
    if (std::strcmp(name, "finalizeStmt") == 0) {
        if (sqlite3_stmt* stmt = asStmt(0)) sqlite3_finalize(stmt);
        return true;
    }
    if (std::strcmp(name, "reset") == 0) {
        if (sqlite3_stmt* stmt = asStmt(0)) sqlite3_reset(stmt);
        return true;
    }
    if (std::strcmp(name, "clearBindings") == 0) {
        if (sqlite3_stmt* stmt = asStmt(0)) sqlite3_clear_bindings(stmt);
        return true;
    }
    if (std::strcmp(name, "bindParameterCount") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        *result = DexValue::Int(stmt != nullptr ? sqlite3_bind_parameter_count(stmt) : 0);
        return true;
    }
    if (std::strcmp(name, "bindNull") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        *result = DexValue::Int(stmt != nullptr ? sqlite3_bind_null(stmt, args[1].i) : SQLITE_MISUSE);
        return true;
    }
    if (std::strcmp(name, "bindLong") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        *result = DexValue::Int(stmt != nullptr ? sqlite3_bind_int64(stmt, args[1].i, args[2].j) : SQLITE_MISUSE);
        return true;
    }
    if (std::strcmp(name, "bindDouble") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        *result = DexValue::Int(stmt != nullptr ? sqlite3_bind_double(stmt, args[1].i, args[2].d) : SQLITE_MISUSE);
        return true;
    }
    if (std::strcmp(name, "bindString") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        const char* text = GetStringUtf8(args[2]);
        *result = DexValue::Int(stmt != nullptr
            ? sqlite3_bind_text(stmt, args[1].i, text, -1, SQLITE_TRANSIENT) : SQLITE_MISUSE);
        return true;
    }
    if (std::strcmp(name, "bindBlob") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        auto* arr = reinterpret_cast<DexArray*>(args[2].l);
        int rc = SQLITE_MISUSE;
        if (stmt != nullptr) {
            rc = arr != nullptr
                ? sqlite3_bind_blob(stmt, args[1].i, reinterpret_cast<const uint8_t*>(arr + 1),
                                    arr->length, SQLITE_TRANSIENT)
                : sqlite3_bind_null(stmt, args[1].i);
        }
        *result = DexValue::Int(rc);
        return true;
    }
    if (std::strcmp(name, "step") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        int rc = SQLITE_MISUSE;
        if (stmt != nullptr) {
            VmLockRelease unlocked;
            rc = sqlite3_step(stmt);
        }
        *result = DexValue::Int(rc);
        return true;
    }
    if (std::strcmp(name, "columnCount") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        *result = DexValue::Int(stmt != nullptr ? sqlite3_column_count(stmt) : 0);
        return true;
    }
    if (std::strcmp(name, "columnName") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        retString(stmt != nullptr ? sqlite3_column_name(stmt, args[1].i) : nullptr);
        return true;
    }
    if (std::strcmp(name, "columnType") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        *result = DexValue::Int(stmt != nullptr ? sqlite3_column_type(stmt, args[1].i) : SQLITE_NULL);
        return true;
    }
    if (std::strcmp(name, "columnLong") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        *result = DexValue::Long(stmt != nullptr ? sqlite3_column_int64(stmt, args[1].i) : 0);
        return true;
    }
    if (std::strcmp(name, "columnDouble") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        *result = DexValue::Double(stmt != nullptr ? sqlite3_column_double(stmt, args[1].i) : 0.0);
        return true;
    }
    if (std::strcmp(name, "columnString") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        const unsigned char* text = stmt != nullptr ? sqlite3_column_text(stmt, args[1].i) : nullptr;
        retString(text != nullptr ? reinterpret_cast<const char*>(text) : nullptr);
        return true;
    }
    if (std::strcmp(name, "columnBlob") == 0) {
        sqlite3_stmt* stmt = asStmt(0);
        result->l = nullptr;
        if (stmt != nullptr) {
            const void* data = sqlite3_column_blob(stmt, args[1].i);
            const int size = sqlite3_column_bytes(stmt, args[1].i);
            if (data != nullptr || size == 0) {
                result->l = reinterpret_cast<DexObject*>(NewByteArray(
                    interp->linker(), static_cast<const char*>(data), static_cast<size_t>(size)));
            }
        }
        return true;
    }
    if (std::strcmp(name, "isAvailable") == 0) {
        *result = DexValue::Int(1);
        return true;
    }
    return false;
}
#else
bool Invoke_android_database_sqlite_SQLiteNative(Interpreter* /*interp*/, const char* name,
                                                 const DexValue* /*args*/, size_t /*num_args*/,
                                                 DexValue* result) {
    // Built without SQLite: every call reports failure (open yields no handle).
    if (std::strcmp(name, "isAvailable") == 0) { *result = DexValue::Int(0); return true; }
    if (std::strcmp(name, "openError") == 0) { result->l = nullptr; return true; }
    *result = DexValue::Long(0);
    return true;
}
#endif

bool Invoke_java_io_PrintStream(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                size_t /*num_args*/, DexValue* /*result*/) {
    if (std::strcmp(name, "writeNative") == 0) {
        int fd = args[0].i;
        auto* arr = reinterpret_cast<DexArray*>(args[1].l);
        if (arr != nullptr) {
            uint8_t* buf = reinterpret_cast<uint8_t*>(arr + 1);
            VmLockRelease unlocked;
            // Console output must never block the thread that produced it.
            kudroid_console_init_nonblocking();
            write(fd == 2 ? STDERR_FILENO : STDOUT_FILENO, buf, static_cast<size_t>(arr->length));
        }
        return true;
    }
    return false;
}

// android.* native methods
bool Invoke_android_util_Log(Interpreter* /*interp*/, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "println_native") == 0) {
        int priority = args[0].i;
        const char* tag = GetStringUtf8(args[1]);
        const char* msg = GetStringUtf8(args[2]);
        const char* prio_char = "V";
        if (priority == 3) prio_char = "D";
        else if (priority == 4) prio_char = "I";
        else if (priority == 5) prio_char = "W";
        else if (priority == 6) prio_char = "E";
        else if (priority == 7) prio_char = "A";

        std::fprintf(stderr, "[%s/%s] %s\n", prio_char, tag ? tag : "", msg ? msg : "");
        *result = DexValue::Int(0);
        return true;
    }
    return false;
}

