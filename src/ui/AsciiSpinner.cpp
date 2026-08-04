#include "ui/AsciiSpinner.hpp"

#include "core/Assets.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cyberdeck {

namespace {

// --- Tunables. Raise kGlyphPixels or kFrameInterval first if a Pi needs headroom. ---
constexpr int kGlyphPixels = 13;                // smaller glyph => finer grid => more fidelity
constexpr float kFrameInterval = 1.0f / 3.0f;   // grid regenerations per second
constexpr float kSpinSpeedY = 0.405f;           // radians/sec, yaw only (1.8x base)
constexpr float kViewTiltX = 0.30f;             // FIXED downward-look tilt; never animates,
                                                // so emblems stay up-side-up while turning
constexpr char kRamp[] = " .:-=+*#%@";
constexpr int kRampTop = static_cast<int>(sizeof(kRamp)) - 2;  // last usable ramp index
constexpr int kFlatChar = 7;                    // ramp index used for flat 2D icons
constexpr int kMaxCols = 72;                    // hard caps bound worst-case draw calls
constexpr int kMaxRows = 44;
constexpr float kAmbient = 0.15f;               // floor so unlit faces still read as solid
constexpr float kFitMargin = 0.42f;             // half-extent of panel the emblem may occupy

struct V3 {
    float x, y, z;
};
struct Tri {
    int a, b, c;
};

struct MeshData {
    std::vector<V3> verts;
    std::vector<Tri> tris;
    bool flat = false;  // flat icons skip rotation entirely and shade uniformly
    // Extents used to auto-fit the emblem to its panel; filled in by finalize().
    float maxR = 0.0f;     // max sweep radius in the XZ plane (worst case under yaw)
    float maxAbsY = 0.0f;  // max vertical extent
};

// Records the yaw-invariant bounds so no emblem can ever clip its panel,
// regardless of how a mesh was authored.
void finalize(MeshData& m) {
    for (const V3& v : m.verts) {
        m.maxR = std::max(m.maxR, std::sqrt(v.x * v.x + v.z * v.z));
        m.maxAbsY = std::max(m.maxAbsY, std::fabs(v.y));
    }
}

// ---- Mesh builders. Procedural so vertex indices are never hand-authored. ----

void addQuad(MeshData& m, int a, int b, int c, int d) {
    m.tris.push_back({a, b, c});
    m.tris.push_back({a, c, d});
}

// Axis-aligned solid box spanning lo..hi.
void addBox(MeshData& m, const V3& lo, const V3& hi) {
    const int base = static_cast<int>(m.verts.size());
    m.verts.push_back({lo.x, lo.y, lo.z});  // 0
    m.verts.push_back({hi.x, lo.y, lo.z});  // 1
    m.verts.push_back({hi.x, hi.y, lo.z});  // 2
    m.verts.push_back({lo.x, hi.y, lo.z});  // 3
    m.verts.push_back({lo.x, lo.y, hi.z});  // 4
    m.verts.push_back({hi.x, lo.y, hi.z});  // 5
    m.verts.push_back({hi.x, hi.y, hi.z});  // 6
    m.verts.push_back({lo.x, hi.y, hi.z});  // 7

    addQuad(m, base + 4, base + 5, base + 6, base + 7);  // front (+z)
    addQuad(m, base + 1, base + 0, base + 3, base + 2);  // back  (-z)
    addQuad(m, base + 5, base + 1, base + 2, base + 6);  // right (+x)
    addQuad(m, base + 0, base + 4, base + 7, base + 3);  // left  (-x)
    addQuad(m, base + 7, base + 6, base + 2, base + 3);  // top   (+y)
    addQuad(m, base + 0, base + 1, base + 5, base + 4);  // bottom(-y)
}

// Solid cylinder/cone along Z with capped front face.
void addCylinderZ(MeshData& m, float cx, float cy, float z0, float z1, float r0, float r1,
                  int segments) {
    const int base = static_cast<int>(m.verts.size());
    for (int i = 0; i < segments; ++i) {
        const float t = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        m.verts.push_back({cx + std::cos(t) * r0, cy + std::sin(t) * r0, z0});
        m.verts.push_back({cx + std::cos(t) * r1, cy + std::sin(t) * r1, z1});
    }
    const int capCenter = static_cast<int>(m.verts.size());
    m.verts.push_back({cx, cy, z1});

    for (int i = 0; i < segments; ++i) {
        const int cur = base + i * 2;
        const int next = base + ((i + 1) % segments) * 2;
        addQuad(m, cur, next, next + 1, cur + 1);          // barrel
        m.tris.push_back({cur + 1, next + 1, capCenter});  // front cap fan
    }
}

// Solid triangular prism extruded along Z (used for the play glyph).
void addPrismZ(MeshData& m, const float (&pts)[3][2], float z0, float z1) {
    const int base = static_cast<int>(m.verts.size());
    for (const auto& p : pts) {
        m.verts.push_back({p[0], p[1], z1});  // front
    }
    for (const auto& p : pts) {
        m.verts.push_back({p[0], p[1], z0});  // back
    }
    m.tris.push_back({base + 0, base + 1, base + 2});
    m.tris.push_back({base + 5, base + 4, base + 3});
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        addQuad(m, base + i, base + 3 + i, base + 3 + j, base + j);
    }
}

// Flat filled convex polygon in the z-plane (fan from the first vertex).
void addPolygonZ(MeshData& m, const std::vector<std::pair<float, float>>& pts, float z) {
    const int base = static_cast<int>(m.verts.size());
    for (const auto& p : pts) {
        m.verts.push_back({p.first, p.second, z});
    }
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        m.tris.push_back({base, base + static_cast<int>(i), base + static_cast<int>(i) + 1});
    }
}

void addEllipseZ(MeshData& m, float cx, float cy, float rx, float ry, float z, int segments) {
    std::vector<std::pair<float, float>> pts;
    pts.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float t = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        pts.emplace_back(cx + std::cos(t) * rx, cy + std::sin(t) * ry);
    }
    addPolygonZ(m, pts, z);
}

// ---- The emblems themselves ----

MeshData buildLogo() {
    MeshData m;
    addBox(m, {-0.85f, -0.85f, -0.85f}, {0.85f, 0.85f, 0.85f});
    finalize(m);
    return m;
}

MeshData buildCamera() {
    MeshData m;
    addBox(m, {-1.05f, -0.60f, -0.45f}, {1.05f, 0.55f, 0.45f});       // body
    addBox(m, {-0.72f, 0.55f, -0.22f}, {-0.24f, 0.78f, 0.22f});       // flash hump
    addCylinderZ(m, 0.12f, -0.02f, 0.45f, 0.92f, 0.42f, 0.34f, 10);   // lens barrel
    finalize(m);
    return m;
}

MeshData buildVideo() {
    MeshData m;
    addBox(m, {-0.95f, -0.68f, -0.16f}, {0.95f, 0.68f, 0.16f});  // screen slab
    // Play glyph as a FRUSTUM (tapered pyramid): the bright top cap is smaller
    // than the base, and the three sloped side walls are steep enough that they
    // shade much darker than the flat slab face. That dark tapered ring is what
    // silhouettes the triangle against the slab, so the two no longer blend.
    // Base sits just proud of the slab front; cap floats higher.
    const float zb = 0.18f;  // base (just off the slab face)
    const float zc = 0.52f;  // cap apex height
    // Outer (base) triangle — larger.
    const float bx0 = -0.34f, by0 = 0.50f;   // top-left
    const float bx1 = -0.34f, by1 = -0.50f;  // bottom-left
    const float bx2 = 0.58f, by2 = 0.0f;     // right tip
    // Inner (cap) triangle — same centroid, scaled down so it reads as the lit top.
    const float cx = (bx0 + bx1 + bx2) / 3.0f;
    const float cy = (by0 + by1 + by2) / 3.0f;
    const float k = 0.55f;  // cap shrink toward centroid
    const float ix0 = cx + (bx0 - cx) * k, iy0 = cy + (by0 - cy) * k;
    const float ix1 = cx + (bx1 - cx) * k, iy1 = cy + (by1 - cy) * k;
    const float ix2 = cx + (bx2 - cx) * k, iy2 = cy + (by2 - cy) * k;

    const int base = static_cast<int>(m.verts.size());
    m.verts.push_back({bx0, by0, zb});  // 0 base
    m.verts.push_back({bx1, by1, zb});  // 1 base
    m.verts.push_back({bx2, by2, zb});  // 2 base
    m.verts.push_back({ix0, iy0, zc});  // 3 cap
    m.verts.push_back({ix1, iy1, zc});  // 4 cap
    m.verts.push_back({ix2, iy2, zc});  // 5 cap
    // Bright top cap.
    m.tris.push_back({base + 3, base + 4, base + 5});
    // Three sloped side walls (dark tapered ring).
    addQuad(m, base + 0, base + 1, base + 4, base + 3);  // left edge
    addQuad(m, base + 1, base + 2, base + 5, base + 4);  // bottom edge
    addQuad(m, base + 2, base + 0, base + 3, base + 5);  // hypotenuse edge
    finalize(m);
    return m;
}

MeshData buildFilm() {
    MeshData m;
    addBox(m, {-1.0f, -0.80f, -0.18f}, {1.0f, 0.26f, 0.18f});  // slate body
    // Hinged clapper bar, raised and skewed so it reads as open.
    const int base = static_cast<int>(m.verts.size());
    m.verts.push_back({-1.0f, 0.34f, -0.18f});
    m.verts.push_back({0.92f, 0.60f, -0.18f});
    m.verts.push_back({0.86f, 0.90f, -0.18f});
    m.verts.push_back({-1.06f, 0.64f, -0.18f});
    m.verts.push_back({-1.0f, 0.34f, 0.18f});
    m.verts.push_back({0.92f, 0.60f, 0.18f});
    m.verts.push_back({0.86f, 0.90f, 0.18f});
    m.verts.push_back({-1.06f, 0.64f, 0.18f});
    addQuad(m, base + 4, base + 5, base + 6, base + 7);  // front
    addQuad(m, base + 1, base + 0, base + 3, base + 2);  // back
    addQuad(m, base + 7, base + 6, base + 2, base + 3);  // top
    addQuad(m, base + 0, base + 1, base + 5, base + 4);  // bottom
    addQuad(m, base + 5, base + 1, base + 2, base + 6);  // right
    addQuad(m, base + 0, base + 4, base + 7, base + 3);  // left
    finalize(m);
    return m;
}

MeshData buildTv() {
    MeshData m;
    addBox(m, {-1.0f, -0.50f, -0.32f}, {1.0f, 0.55f, 0.32f});    // cabinet
    addBox(m, {-0.12f, -0.78f, -0.12f}, {0.12f, -0.50f, 0.12f}); // stand post
    addBox(m, {-0.46f, -0.92f, -0.20f}, {0.46f, -0.78f, 0.20f}); // foot
    // Antennae are kept short and at least a cell thick; sub-cell rods alias into
    // stray single characters and steal scale from the cabinet.
    addBox(m, {-0.66f, 0.55f, -0.09f}, {-0.44f, 1.02f, 0.09f});  // left antenna
    addBox(m, {0.44f, 0.55f, -0.09f}, {0.66f, 0.99f, 0.09f});    // right antenna
    finalize(m);
    return m;
}

MeshData buildMusic() {
    MeshData m;  // 3D so it spins like the other emblems
    const float d = 0.14f;  // half-depth extrusion along Z
    addCylinderZ(m, -0.30f, -0.52f, -d, d, 0.42f, 0.42f, 12);        // note head
    // Stem sits tangent to the head's right edge and reaches below its centre,
    // so the two never separate into disconnected blobs on a coarse grid.
    addBox(m, {-0.10f, -0.70f, -d}, {0.14f, 0.95f, d});              // stem
    addBox(m, {0.14f, 0.55f, -d}, {0.64f, 0.95f, d});                // flag
    finalize(m);
    return m;
}

MeshData buildCog() {
    MeshData m;
    addCylinderZ(m, 0.0f, 0.0f, -0.18f, 0.18f, 0.50f, 0.50f, 14);    // ring body
    addCylinderZ(m, 0.0f, 0.0f, -0.20f, 0.20f, 0.20f, 0.20f, 10);    // hub
    // Teeth around the rim. Sized to survive the coarse ASCII grid.
    for (int i = 0; i < 8; ++i) {
        const float t = 6.2831853f * static_cast<float>(i) / 8.0f;
        const float cx = std::cos(t) * 0.66f;
        const float cy = std::sin(t) * 0.66f;
        addBox(m, {cx - 0.16f, cy - 0.16f, -0.16f}, {cx + 0.16f, cy + 0.16f, 0.16f});
    }
    finalize(m);
    return m;
}

MeshData buildMediaM() {
    MeshData m;
    const float d = 0.16f;    // half-depth extrusion along Z
    const float top = 0.85f;
    const float bot = -0.85f;
    const float w = 0.20f;    // stroke thickness
    // Two full-height outer legs.
    addBox(m, {-0.90f, bot, -d}, {-0.90f + w, top, d});            // left leg
    addBox(m, {0.90f - w, bot, -d}, {0.90f, top, d});              // right leg
    // Centre V as a continuous stepped diagonal from each leg down to a shared
    // apex block, so the middle reads as one connected stroke.
    addBox(m, {-0.70f, 0.45f, -d}, {-0.70f + w, top, d});          // left diag step 1
    addBox(m, {-0.50f, 0.05f, -d}, {-0.50f + w, 0.65f, d});        // left diag step 2
    addBox(m, {0.70f - w, 0.45f, -d}, {0.70f, top, d});            // right diag step 1
    addBox(m, {0.50f - w, 0.05f, -d}, {0.50f, 0.65f, d});          // right diag step 2
    // Shared apex block that both diagonals merge into — connects the middle.
    addBox(m, {-0.30f, -0.35f, -d}, {0.30f, 0.25f, d});            // apex / join
    finalize(m);
    return m;
}

const MeshData& meshFor(SpinnerMesh kind) {
    static const MeshData logo = buildLogo();
    static const MeshData camera = buildCamera();
    static const MeshData video = buildVideo();
    static const MeshData music = buildMusic();
    static const MeshData film = buildFilm();
    static const MeshData tv = buildTv();
    static const MeshData cog = buildCog();
    static const MeshData mediaM = buildMediaM();
    switch (kind) {
        case SpinnerMesh::Camera: return camera;
        case SpinnerMesh::Video:  return video;
        case SpinnerMesh::Music:  return music;
        case SpinnerMesh::Film:   return film;
        case SpinnerMesh::Tv:     return tv;
        case SpinnerMesh::Cog:    return cog;
        case SpinnerMesh::MediaM: return mediaM;
        case SpinnerMesh::Logo:
        default:                  return logo;
    }
}

}  // namespace

AsciiSpinner::AsciiSpinner(NodeId id, Font* fallbackFont, SpinnerMesh mesh)
    : Node(id, "AsciiSpinner"), mesh_(mesh) {
    setFocusable(false);
    setFill(Color{0, 0, 0, 0});

    const std::string path = assets::findFont();
    if (!path.empty() && ownFont_.load(path, kGlyphPixels)) {
        font_ = &ownFont_;
    } else {
        font_ = fallbackFont;
    }

    if (font_) {
        // Run-length batching below assumes fixed advance; verify before trusting it.
        const Vec2 one = font_->measure("#");
        const Vec2 two = font_->measure("##");
        monospaced_ = one.x > 0.0f && std::fabs(two.x - one.x * 2.0f) < 0.5f;
    }
}

void AsciiSpinner::setMesh(SpinnerMesh mesh) {
    if (mesh_ == mesh) {
        return;
    }
    mesh_ = mesh;
    dirty_ = true;
}

void AsciiSpinner::resetAngle() {
    angleY_ = 0.0f;
    refreshTimer_ = 0.0f;
    dirty_ = true;
}

void AsciiSpinner::ensureGrid() {
    if (!font_) {
        return;
    }
    if (advance_ <= 0.0f || lineH_ <= 0.0f) {
        const Vec2 g = font_->measure("#");
        if (g.x <= 0.0f || g.y <= 0.0f) {
            return;
        }
        advance_ = g.x;
        lineH_ = g.y;
    }

    const Rect r = bounds();
    const int cols = std::clamp(static_cast<int>(r.w / advance_), 4, kMaxCols);
    const int rows = std::clamp(static_cast<int>(r.h / lineH_), 4, kMaxRows);
    if (cols != cols_ || rows != rows_) {
        cols_ = cols;
        rows_ = rows;
        grid_.assign(static_cast<std::size_t>(rows_),
                     std::string(static_cast<std::size_t>(cols_), ' '));
        depth_.assign(static_cast<std::size_t>(rows_ * cols_), 0.0f);
        dirty_ = true;
    }
}

void AsciiSpinner::rebuildFrame() {
    if (cols_ <= 0 || rows_ <= 0) {
        return;
    }
    for (std::string& row : grid_) {
        row.assign(static_cast<std::size_t>(cols_), ' ');
    }
    std::fill(depth_.begin(), depth_.end(), -1e9f);

    const MeshData& mesh = meshFor(mesh_);
    if (mesh.verts.empty() || mesh.tris.empty()) {
        return;
    }

    // Yaw only, then a constant viewing tilt — the emblem turns about its own
    // vertical axis and never tumbles.
    const bool flat = mesh.flat;
    const float sy = flat ? 0.0f : std::sin(angleY_);
    const float cy = flat ? 1.0f : std::cos(angleY_);
    const float st = flat ? 0.0f : std::sin(kViewTiltX);
    const float ct = flat ? 1.0f : std::cos(kViewTiltX);

    // Fit exactly: scale so the mesh's worst-case yaw extent lands inside the panel
    // on both axes. Emblems can be authored at any size without clipping.
    const Rect r = bounds();
    const float extentH = std::max(mesh.maxR, 1e-3f);
    const float extentV = std::max(mesh.maxAbsY * ct + mesh.maxR * st, 1e-3f);
    const float scalePx = std::min(r.w * kFitMargin / extentH, r.h * kFitMargin / extentV);
    const float halfC = cols_ * 0.5f;
    const float halfR = rows_ * 0.5f;

    rotated_.resize(mesh.verts.size());
    screen_.resize(mesh.verts.size());
    for (std::size_t i = 0; i < mesh.verts.size(); ++i) {
        const V3& v = mesh.verts[i];
        const float x1 = v.x * cy + v.z * sy;
        const float z1 = -v.x * sy + v.z * cy;
        const float y2 = v.y * ct - z1 * st;
        const float z2 = v.y * st + z1 * ct;
        rotated_[i] = {x1, y2, z2};
        // Orthographic projection straight into character-cell space, corrected
        // for glyph aspect so the emblem stays square in any panel.
        screen_[i] = {halfC + x1 * scalePx / advance_, halfR - y2 * scalePx / lineH_, z2};
    }

    // Light from upper-front-right.
    constexpr float lx = 0.32f, ly = 0.48f, lz = 0.82f;

    for (const Tri& tri : mesh.tris) {
        const Vec3f& s0 = screen_[static_cast<std::size_t>(tri.a)];
        const Vec3f& s1 = screen_[static_cast<std::size_t>(tri.b)];
        const Vec3f& s2 = screen_[static_cast<std::size_t>(tri.c)];

        const float area = (s1.x - s0.x) * (s2.y - s0.y) - (s2.x - s0.x) * (s1.y - s0.y);
        if (std::fabs(area) < 1e-5f) {
            continue;  // degenerate / edge-on
        }

        char ch = kRamp[kFlatChar];
        if (!flat) {
            const Vec3f& r0 = rotated_[static_cast<std::size_t>(tri.a)];
            const Vec3f& r1 = rotated_[static_cast<std::size_t>(tri.b)];
            const Vec3f& r2 = rotated_[static_cast<std::size_t>(tri.c)];
            const float ux = r1.x - r0.x, uy = r1.y - r0.y, uz = r1.z - r0.z;
            const float vx = r2.x - r0.x, vy = r2.y - r0.y, vz = r2.z - r0.z;
            float nx = uy * vz - uz * vy;
            float ny = uz * vx - ux * vz;
            float nz = ux * vy - uy * vx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            // Two-sided lighting: robust to whichever way a face happens to wind.
            const float lum = std::fabs(nx * lx + ny * ly + nz * lz);
            const float shade = kAmbient + (1.0f - kAmbient) * std::clamp(lum, 0.0f, 1.0f);
            ch = kRamp[std::clamp(1 + static_cast<int>(shade * (kRampTop - 1) + 0.5f), 1,
                                  kRampTop)];
        }

        const int minC = std::max(0, static_cast<int>(std::floor(std::min({s0.x, s1.x, s2.x}))));
        const int maxC = std::min(cols_ - 1,
                                  static_cast<int>(std::ceil(std::max({s0.x, s1.x, s2.x}))));
        const int minR = std::max(0, static_cast<int>(std::floor(std::min({s0.y, s1.y, s2.y}))));
        const int maxR = std::min(rows_ - 1,
                                  static_cast<int>(std::ceil(std::max({s0.y, s1.y, s2.y}))));

        for (int row = minR; row <= maxR; ++row) {
            for (int col = minC; col <= maxC; ++col) {
                const float px = col + 0.5f;
                const float py = row + 0.5f;
                // Barycentric weights; dividing by signed area keeps this
                // winding-agnostic (numerator and denominator flip together).
                const float w0 = ((s1.x - px) * (s2.y - py) - (s2.x - px) * (s1.y - py)) / area;
                const float w1 = ((s2.x - px) * (s0.y - py) - (s0.x - px) * (s2.y - py)) / area;
                const float w2 = 1.0f - w0 - w1;
                constexpr float kEdgeBias = -0.0005f;
                if (w0 < kEdgeBias || w1 < kEdgeBias || w2 < kEdgeBias) {
                    continue;
                }
                const float z = w0 * s0.z + w1 * s1.z + w2 * s2.z;
                const std::size_t idx = static_cast<std::size_t>(row * cols_ + col);
                if (z > depth_[idx]) {
                    depth_[idx] = z;
                    grid_[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] = ch;
                }
            }
        }
    }
}

void AsciiSpinner::update(float dt) {
    ensureGrid();
    if (spinning_ && !meshFor(mesh_).flat) {
        refreshTimer_ += dt;
        if (refreshTimer_ >= kFrameInterval) {
            angleY_ += kSpinSpeedY * refreshTimer_;
            refreshTimer_ = 0.0f;
            dirty_ = true;
        }
    }
    if (dirty_) {
        rebuildFrame();
        dirty_ = false;
    }
    Node::update(dt);
}

void AsciiSpinner::draw(IRenderer& renderer) {
    if (!visible_ || drawOpacity() <= 0.0f || !font_ || cols_ <= 0) {
        return;
    }

    const Rect r = scaledBounds();
    const Color ink = modulate(kAccent);

    // Solid fills produce long runs of one character. Emitting a run as a single
    // string cuts draw calls several-fold, and the set of possible run strings is
    // bounded (ramp chars x max columns), so Font's texture cache stays bounded too.
    for (int row = 0; row < rows_; ++row) {
        const std::string& line = grid_[static_cast<std::size_t>(row)];
        const float y = r.y + row * lineH_;
        int col = 0;
        while (col < cols_) {
            const char c = line[static_cast<std::size_t>(col)];
            if (c == ' ') {
                ++col;
                continue;
            }
            int end = col;
            while (end < cols_ && line[static_cast<std::size_t>(end)] == c) {
                ++end;
            }
            if (monospaced_) {
                run_.assign(static_cast<std::size_t>(end - col), c);
                font_->draw(renderer, run_, {r.x + col * advance_, y}, ink);
            } else {
                run_.assign(1, c);
                for (int i = col; i < end; ++i) {
                    font_->draw(renderer, run_, {r.x + i * advance_, y}, ink);
                }
            }
            col = end;
        }
    }
}

}  // namespace cyberdeck
