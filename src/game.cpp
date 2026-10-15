// ===========================================================================
//  ascent-ios / game.cpp
//  The cheat core: find player transforms by the 22-byte "playerpattern",
//  find the camera view-projection by mtx_checker, pick the right camera by
//  projecting players, publish a snapshot for the overlay.
// ===========================================================================
#include "game.hpp"
#include "memory.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

// --- the player pattern (wonderful/cheat.cpp:17) --------------------------
static const unsigned char kPlayerPattern[] = {
    0x00, 0x00, 0x80, 0x3F,   // scale.x = 1.0f
    0x00, 0x00, 0x80, 0x3F,   // scale.y = 1.0f
    0x00, 0x00, 0x80, 0x3F,   // scale.z = 1.0f
    0x80, 0x96, 0x18, 0x4B,   // scale.w = 1e7
    0x00, 0x00, 0x00, 0x00,
    0xCD, 0xCC
};
static constexpr size_t kPatternLen = sizeof(kPlayerPattern);   // 22
static constexpr uintptr_t kBlockDelta = 0x20;                  // pattern is at block + 0x20
static constexpr uintptr_t kMatrixDelta = 0x178 - 0x100;        // marker - matrix = 0x78

static constexpr size_t  kChunk = 8u * 1024u * 1024u;           // 8 MB
static constexpr size_t  kOverlap = 64;                         // for patterns across chunks
static constexpr float   kMaxPlayerDist = 400.f;                // meters
static constexpr float   kMinPlayerDist = 1.5f;                 // skip local player
static constexpr int     kFullRescanMs = 4000;
static constexpr int     kFastTickMs = 33;

// Screen size (points), set by the overlay. Used only for camera scoring.
std::atomic<float> g_screenW{ 0.f };
std::atomic<float> g_screenH{ 0.f };

namespace {

std::mutex g_mtx;
std::shared_ptr<const Snapshot> g_snapshot;

std::atomic<bool> g_running{ false };
std::thread       g_thread;

std::mutex         g_addrMtx;
std::vector<uintptr_t> g_blocks;      // TMatrix block addresses
uintptr_t          g_camAddr = 0;     // chosen camera matrix address

std::atomic<int>   g_playerCount{ 0 };
std::atomic<int>   g_scanCount{ 0 };
std::atomic<bool>  g_haveCamera{ false };
std::chrono::steady_clock::time_point g_start;

inline void publish(std::shared_ptr<const Snapshot> snap) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_snapshot = std::move(snap);
}

inline Vec3 blockPos(const TMatrix& t) { return { t.position[0], t.position[1], t.position[2] }; }

bool readValidatedBlock(uintptr_t addr, TMatrix& out) {
    if (!safeReadT(addr, out)) return false;
    if (out.rotation[0] != 0.f) return false;          // wonderful: rotation.x == 0
    if (out.scale[0] != 1.f || out.scale[1] != 1.f || out.scale[2] != 1.f) return false;
    const Vec3 p = blockPos(out);
    if (!finite3(p)) return false;
    if (std::fabs(p.x) > 100000.f || std::fabs(p.y) > 100000.f || std::fabs(p.z) > 100000.f) return false;
    return true;
}

int scoreCamera(const Mat4& m, const std::vector<uintptr_t>& blocks, float sw, float sh) {
    if (!m.looksLikeCamera()) return -1;
    int score = 0;
    for (uintptr_t b : blocks) {
        TMatrix t;
        if (!readValidatedBlock(b, t)) continue;
        const Vec3 p = blockPos(t);
        Vec2 feet;
        if (!m.w2s(p, feet, sw, sh)) continue;
        if (feet.x < -sw || feet.x > 2.f * sw || feet.y < -sh || feet.y > 2.f * sh) continue;
        const Vec3 head{ p.x, p.y + 1.8f, p.z };
        Vec2 hs;
        if (!m.w2s(head, hs, sw, sh)) continue;
        const float hpx = std::fabs(hs.y - feet.y);
        if (hpx < 3.f || hpx > sh * 1.5f) continue;
        ++score;
    }
    return score;
}

// One pass over every writable region: collect TMatrix blocks, camera
// candidates and raw mtx_checker markers.
void fullScan(std::vector<uintptr_t>& blocks,
              std::vector<uintptr_t>& camCands,
              std::vector<uintptr_t>& markers) {
    const auto regions = enumWritableRegions();
    std::vector<unsigned char> buf;
    buf.reserve(kChunk + kOverlap);

    for (const auto& r : regions) {
        // Never scan our own dylib data: it contains kPlayerPattern itself.
        const uintptr_t patAddr = (uintptr_t)kPlayerPattern;
        if (patAddr >= r.base && patAddr < r.base + r.size) continue;

        size_t done = 0;
        while (done < r.size) {
            const size_t want = std::min(kChunk, r.size - done);
            const size_t readLen = std::min(want + kOverlap, r.size - done);
            buf.resize(readLen);
            if (!safeRead(r.base + done, buf.data(), readLen)) { done += want; continue; }

            // ---- playerpattern ----
            const unsigned char* cur = buf.data();
            size_t remaining = readLen;
            while (remaining >= kPatternLen) {
                const void* hit = memmem(cur, remaining, kPlayerPattern, kPatternLen);
                if (!hit) break;
                const size_t off = (const unsigned char*)hit - buf.data();
                const uintptr_t matchAddr = r.base + done + off;
                if (matchAddr > kBlockDelta) {
                    const uintptr_t blockAddr = matchAddr - kBlockDelta;
                    TMatrix t;
                    if (readValidatedBlock(blockAddr, t))
                        blocks.push_back(blockAddr);
                }
                cur = (const unsigned char*)hit + 1;
                remaining = readLen - (size_t)(cur - buf.data());
            }

            // ---- camera markers (mtx_checker) ----
            for (size_t off = 0; off + 24 + 64 <= readLen; off += 4) {
                float a2;
                std::memcpy(&a2, buf.data() + off + 8, 4);
                if (!(a2 >= 10.f && a2 <= 170.f)) continue;
                MtxChecker mc;
                std::memcpy(&mc, buf.data() + off, sizeof(mc));
                if (!mc.valid()) continue;
                const uintptr_t markerAddr = r.base + done + off;
                markers.push_back(markerAddr);
                if (markerAddr <= kMatrixDelta) continue;
                const uintptr_t cand = markerAddr - kMatrixDelta;
                Mat4 m;
                if (safeReadT(cand, m) && m.looksLikeCamera())
                    camCands.push_back(cand);
            }

            done += want;
        }
    }
}

// Camera canonical offset (marker - matrix) can drift between game builds.
// When the direct candidate at marker-0x78 is not a camera, search a small
// window around each marker for a view-projection (wonderful kMtxWin*).
void resolveCamerasFromMarkers(const std::vector<uintptr_t>& markers,
                               std::vector<uintptr_t>& camCands) {
    constexpr int kWinBase = -0x200;               // float window, same as wonderful
    constexpr int kWinEnd = 0x100;
    constexpr int kWinSize = kWinEnd - kWinBase;   // 0x300 bytes

    unsigned char win[kWinSize];
    for (uintptr_t mk : markers) {
        const uintptr_t wbase = (uintptr_t)((intptr_t)mk + kWinBase);
        if (!safeRead(wbase, win, sizeof(win))) continue;
        for (int d = 0; d + (int)sizeof(Mat4) <= kWinSize; d += 4) {
            Mat4 m;
            std::memcpy(&m, win + d, sizeof(Mat4));
            if (m.looksLikeCamera()) {
                camCands.push_back((uintptr_t)((intptr_t)wbase + d));
                break;   // one camera per marker is enough
            }
        }
    }
}

uintptr_t pickCamera(const std::vector<uintptr_t>& cands, const std::vector<uintptr_t>& blocks) {
    const float sw = g_screenW.load();
    const float sh = g_screenH.load();

    uintptr_t best = 0;
    int bestScore = -1;

    // Dedup candidates.
    std::vector<uintptr_t> uniq = cands;
    std::sort(uniq.begin(), uniq.end());
    uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());

    for (uintptr_t c : uniq) {
        Mat4 m;
        if (!safeReadT(c, m)) continue;
        const int s = scoreCamera(m, blocks, sw, sh);
        if (s > bestScore) { bestScore = s; best = c; }
    }

    // Fallback: project by sanity only if no marker candidate scored.
    if (best == 0 && !cands.empty()) best = cands.front();
    return best;
}

void buildSnapshot() {
    auto snap = std::make_shared<Snapshot>();

    std::vector<uintptr_t> blocks;
    uintptr_t camAddr;
    {
        std::lock_guard<std::mutex> lk(g_addrMtx);
        blocks = g_blocks;
        camAddr = g_camAddr;
    }

    if (camAddr) {
        Mat4 m;
        if (safeReadT(camAddr, m) && m.looksLikeCamera()) {
            snap->camera = m;
            snap->hasCamera = true;
            snap->cameraPos = m.eye();
        }
    }

    const Vec3 eye = snap->cameraPos;
    for (uintptr_t b : blocks) {
        TMatrix t;
        if (!readValidatedBlock(b, t)) continue;
        const Vec3 p = blockPos(t);
        const float d = std::sqrt((p.x - eye.x) * (p.x - eye.x) +
                                  (p.y - eye.y) * (p.y - eye.y) +
                                  (p.z - eye.z) * (p.z - eye.z));
        if (d < kMinPlayerDist || d > kMaxPlayerDist) continue;
        snap->players.push_back({ p, d, true });
    }

    snap->scannedAt = (int)std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::steady_clock::now() - g_start).count();

    g_playerCount.store((int)snap->players.size());
    g_haveCamera.store(snap->hasCamera);
    publish(std::move(snap));
}

void scannerLoop() {
    while (g_running.load()) {
        std::vector<uintptr_t> blocks, cams, markers;
        fullScan(blocks, cams, markers);

        std::sort(blocks.begin(), blocks.end());
        blocks.erase(std::unique(blocks.begin(), blocks.end()), blocks.end());

        if (cams.empty() && !markers.empty())
            resolveCamerasFromMarkers(markers, cams);

        const uintptr_t cam = pickCamera(cams, blocks);

        {
            std::lock_guard<std::mutex> lk(g_addrMtx);
            g_blocks.swap(blocks);
            g_camAddr = cam;
        }
        g_scanCount.fetch_add(1);

        // Fast ticks: re-read cached addresses until the next full scan.
        const auto until = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(kFullRescanMs);
        while (g_running.load() && std::chrono::steady_clock::now() < until) {
            buildSnapshot();
            std::this_thread::sleep_for(std::chrono::milliseconds(kFastTickMs));
        }
    }
}

}  // namespace

std::shared_ptr<const Snapshot> gameSnapshot() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_snapshot;
}

void gameStartScanner() {
    if (g_running.exchange(true)) return;
    g_start = std::chrono::steady_clock::now();
    g_thread = std::thread(scannerLoop);
    g_thread.detach();
}

void gameStopScanner() {
    g_running.store(false);
}

const char* gameStatusLine() {
    static char line[128];
    std::snprintf(line, sizeof(line), "players=%d camera=%s scans=%d",
                  g_playerCount.load(), g_haveCamera.load() ? "yes" : "no",
                  g_scanCount.load());
    return line;
}
