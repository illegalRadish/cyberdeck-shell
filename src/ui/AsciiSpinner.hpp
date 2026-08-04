#pragma once

#include "render/Font.hpp"
#include "ui/Node.hpp"

#include <string>
#include <vector>

namespace cyberdeck {

// Which emblem the panel shows. Meshes are built procedurally in AsciiSpinner.cpp;
// add a kind here plus a builder and a meshFor() case to introduce a new one.
enum class SpinnerMesh {
    Logo,    // generic cube — browsing / unknown
    Camera,  // photos
    Video,   // videos
    Music,   // songs
    Film,    // movies
    Tv,      // shows
    Cog,     // settings gear
    MediaM,  // big "M" — media category
};

// Solid-shaded 3D emblem rendered as ASCII characters. Cheap by construction:
// a small mesh is rasterized into a character grid on a throttled timer rather
// than every render frame, and drawing emits run-length batched strings whose
// alphabet is bounded, so Font's per-string texture cache cannot grow unbounded.
//
// Rotation is yaw-only with a fixed viewing tilt, so emblems turn about their own
// vertical axis and always stay up-side-up. The grid auto-fits the node's bounds
// using the spinner's own small font, so panel size drives fidelity.
class AsciiSpinner final : public Node {
public:
    // fallbackFont is used only if the dedicated small font fails to load.
    AsciiSpinner(NodeId id, Font* fallbackFont, SpinnerMesh mesh = SpinnerMesh::Logo);

    void setMesh(SpinnerMesh mesh);
    SpinnerMesh mesh() const { return mesh_; }

    // Reset the emblem to face-on (yaw = 0) and force a redraw. Called whenever a
    // new menu item is focused so icons stay legible and forward-facing.
    void resetAngle();

    void setSpinning(bool spinning) { spinning_ = spinning; }
    bool spinning() const { return spinning_; }

    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    struct Vec3f {
        float x, y, z;
    };

    void ensureGrid();
    void rebuildFrame();

    Font ownFont_;
    Font* font_ = nullptr;
    SpinnerMesh mesh_ = SpinnerMesh::Logo;
    float angleY_ = 0.6f;
    float refreshTimer_ = 0.0f;
    float advance_ = 0.0f;
    float lineH_ = 0.0f;
    int cols_ = 0;
    int rows_ = 0;
    bool spinning_ = true;
    bool dirty_ = true;
    bool monospaced_ = false;
    std::vector<std::string> grid_;
    std::vector<float> depth_;
    std::vector<Vec3f> rotated_;
    std::vector<Vec3f> screen_;
    std::string run_;
};

}  // namespace cyberdeck
