#include "screens/AiAssetsScreen.hpp"

#include "core/Types.hpp"

#include <algorithm>
#include <cstdio>

namespace cyberdeck {

namespace {

// The asset list is sized to fit the band above the action rows rather than
// using a fixed pitch, so adding a tenth asset shrinks the rows instead of
// silently drawing one underneath "DOWNLOAD MISSING".
constexpr float kRowTop = 128.0f;
constexpr float kRowBandBottom = 490.0f;
constexpr float kRowPitchMax = 46.0f;
constexpr float kActionTop = 500.0f;
constexpr float kActionPitch = 56.0f;
constexpr float kActionHeight = 48.0f;

std::string humanBytes(std::int64_t bytes) {
    char buf[48];
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0) {
        std::snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    } else if (mb >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.0f MB", mb);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    }
    return buf;
}

}  // namespace

AiAssetsScreen::AiAssetsScreen(Font* titleFont, Font* bodyFont, AiAssets* ai,
                               ScreenManager* navigator)
    : Screen(910, "AI Assets"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      ai_(ai),
      navigator_(navigator) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);
}

void AiAssetsScreen::onEnter() {
    focusIndex_ = 0;
    if (ai_) {
        ai_->refresh();
        status_ = ai_->status();
    }
    Screen::onEnter();
}

void AiAssetsScreen::handleInput(const Input& input) {
    const int count = static_cast<int>(Action_::Count);
    for (Action action : input.actions()) {
        if (action == Action::Up || action == Action::Left) {
            focusIndex_ = (focusIndex_ == 0) ? count - 1 : focusIndex_ - 1;
        } else if (action == Action::Down || action == Action::Right) {
            focusIndex_ = (focusIndex_ + 1) % count;
        } else if (action == Action::Confirm && ai_) {
            switch (static_cast<Action_>(focusIndex_)) {
                case Action_::Download:
                    if (ai_->isRunning()) {
                        ai_->requestStop();
                        flashMessage_ = "Stopping — partial files are kept";
                    } else if (status_.totalMissingBytes <= 0 && status_.ready) {
                        flashMessage_ = "Everything is already installed";
                    } else {
                        ai_->startDownload();
                        flashMessage_ = "Download started";
                    }
                    flashTimer_ = 2.5f;
                    break;
                case Action_::Verify:
                    if (ai_->isRunning()) {
                        flashMessage_ = "Busy — cancel the download first";
                    } else {
                        ai_->startVerify();
                        flashMessage_ = "Verifying assets";
                    }
                    flashTimer_ = 2.5f;
                    break;
                default:
                    break;
            }
        }
    }
}

void AiAssetsScreen::update(float dt) {
    if (flashTimer_ > 0.0f) {
        flashTimer_ -= dt;
        if (flashTimer_ <= 0.0f) {
            flashMessage_.clear();
        }
    }
    refreshTimer_ += dt;
    if (refreshTimer_ >= 0.5f) {
        refreshTimer_ = 0.0f;
        if (ai_) {
            if (!ai_->isRunning()) {
                ai_->refresh();
            }
            status_ = ai_->status();
        }
    }
    Screen::update(dt);
}

void AiAssetsScreen::draw(IRenderer& renderer) {
    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    if (titleFont_) {
        titleFont_->draw(renderer, "AI ASSETS", {64, 40}, kAccent);
    }

    if (bodyFont_) {
        std::string summary = std::to_string(status_.presentCount) + " OF " +
                              std::to_string(status_.totalCount) + " PRESENT";
        if (status_.totalMissingBytes > 0) {
            summary += "  ·  " + humanBytes(status_.totalMissingBytes) + " TO DOWNLOAD";
        }
        bodyFont_->draw(renderer, summary, {64, 96}, kTextDim);
    }

    const int assetCount = static_cast<int>(status_.assets.size());
    const float pitch =
        assetCount > 0
            ? std::min(kRowPitchMax, (kRowBandBottom - kRowTop) / static_cast<float>(assetCount))
            : kRowPitchMax;
    const float rowHeight = std::max(24.0f, pitch - 6.0f);

    float y = kRowTop;
    for (const AiAssetState& asset : status_.assets) {
        const Rect row{64, y, 1152, rowHeight};
        renderer.drawRect(row, kCard);

        // Keep the 26px body text centred as the pitch shrinks.
        const float textY = row.y + std::max(2.0f, (rowHeight - 26.0f) * 0.5f);

        if (bodyFont_) {
            bodyFont_->draw(renderer, asset.label, {row.x + 20, textY},
                            asset.present ? kTextBright : kTextDim);

            // State text is deliberately low-cardinality: whole percent only, so
            // the font's per-string texture cache cannot grow without bound.
            std::string state;
            Color stateColor = kTextDim;
            if (asset.present) {
                state = "OK";
                stateColor = kAccent;
            } else if (asset.downloading) {
                state = std::to_string(asset.expectedBytes > 0
                                           ? static_cast<int>(asset.haveBytes * 100 /
                                                              std::max<std::int64_t>(
                                                                  1, asset.expectedBytes))
                                           : 0) +
                        "%";
                stateColor = kTextBright;
            } else if (asset.kind == AiAssetKind::External) {
                state = "INSTALL ON DEVICE";
            } else if (!asset.required) {
                state = "OPTIONAL";
            } else {
                state = "MISSING";
            }
            bodyFont_->draw(renderer, state, {row.x + 700, textY}, stateColor);

            if (!asset.note.empty()) {
                // Truncate from the right: notes lead with the useful part
                // (an install command, a reason), and keeping the tail instead
                // turned "curl -fsSL https://…" into "…ttps://…".
                std::string note = asset.note;
                if (note.size() > 40) {
                    note = note.substr(0, 39) + "…";
                }
                bodyFont_->draw(renderer, note, {row.x + 250, textY}, kTextFaint);
            }
        }

        // Determinate bar drawn as rects — the established progress idiom.
        if (asset.kind != AiAssetKind::External) {
            const Rect track{row.x + 940, row.y + (rowHeight - 12.0f) * 0.5f, 190, 12};
            renderer.drawRect(track, kBgPanel);
            float frac = 0.0f;
            if (asset.present) {
                frac = 1.0f;
            } else if (asset.expectedBytes > 0) {
                frac = static_cast<float>(asset.haveBytes) /
                       static_cast<float>(asset.expectedBytes);
            }
            frac = std::clamp(frac, 0.0f, 1.0f);
            renderer.drawRect(Rect{track.x, track.y, track.w * frac, track.h}, kAccent);
        }

        y += pitch;
    }

    const char* actionLabels[2] = {"DOWNLOAD MISSING", "VERIFY ASSETS"};
    for (int i = 0; i < static_cast<int>(Action_::Count); ++i) {
        const Rect row{64.0f, kActionTop + kActionPitch * static_cast<float>(i), 1152.0f,
                       kActionHeight};
        const bool focused = i == focusIndex_;
        renderer.drawRect(row, focused ? kCardFocused : kCard);
        if (focused) {
            renderer.drawRect(Rect{row.x, row.y, 6, row.h}, kAccent);
        }
        if (bodyFont_) {
            std::string label = actionLabels[i];
            if (i == static_cast<int>(Action_::Download)) {
                if (status_.running && !status_.verifying) {
                    label = "CANCEL DOWNLOAD";
                } else if (status_.totalMissingBytes > 0) {
                    label += " (" + humanBytes(status_.totalMissingBytes) + ")";
                }
            }
            bodyFont_->draw(renderer, label, {row.x + 24, row.y + 10},
                            focused ? kTextBright : kTextDim);
        }
    }

    if (bodyFont_) {
        if (!flashMessage_.empty()) {
            bodyFont_->draw(renderer, flashMessage_, {64, 626}, kAccent);
        } else if (!status_.message.empty()) {
            bodyFont_->draw(renderer, status_.message, {64, 626}, kTextDim);
        }
        bodyFont_->draw(renderer, "ENTER SELECT  ·  ESC BACK", {64, 670}, kTextDim);
    }
}

}  // namespace cyberdeck
