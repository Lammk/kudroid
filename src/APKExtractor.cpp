#include "kudroid/APKExtractor.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/ResourceTable.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <set>
#include <unordered_set>
#include <sys/stat.h>
#include <sstream>
#include <vector>
#include <zlib.h>

namespace kudroid {
#include "apk/ArchiveReader.inc"
#include "apk/BinaryManifest.inc"
#include "apk/Extract.inc"
#include "apk/ManifestAndPreview.inc"
