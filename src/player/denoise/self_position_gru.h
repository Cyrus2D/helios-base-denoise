#ifndef HELIOS_DENOISE_SELF_POSITION_GRU_H
#define HELIOS_DENOISE_SELF_POSITION_GRU_H

#include <array>
#include <filesystem>

namespace denoise {

// Matches Exp2 schema.py and the Step 05 circular position-only checkpoint.
enum Feature {
    DashPower, DashDir, Turn, NeckTurn, Effort, Dash, TurnCommand, Tackle,
    Speed, SpeedDir, Neck, HasSpeed, HasNeck, Collision, See, FixPos, FixFace,
    FixX, FixY, FixAngle, FixErrorX, FixErrorY, FixErrorAngle, PosAge, FaceAge,
    Narrow, Normal, Wide, LandCount, LandMean, LandMin, LandSpread, LandSpan,
    LineCount, FeatureCount
};

struct SelfPositionState {
    float x, y, vx, vy, body, neck;
};

struct SelfPositionPhysics {
    float inertia, dash_rate, decay, speed_max;
    float accel_max, side_dash_rate, back_dash_rate, min_neck, max_neck;
};

class SelfPositionGRU {
public:
    static constexpr int HiddenSize = 48;
    using Features = std::array<float, FeatureCount>;
    using Hidden = std::array<float, HiddenSize>;

    void load(const std::filesystem::path & path);
    SelfPositionState step(const Features & input, const SelfPositionState & state,
                           const SelfPositionPhysics & physics, Hidden & hidden) const;

private:
    static constexpr int InputSize = FeatureCount + 3 + 9 + 16;
    using Gates = std::array<float, 3 * HiddenSize>;

    Features scales_{};
    std::array<std::array<float, InputSize>, 3 * HiddenSize> input_weights_{};
    std::array<Hidden, 3 * HiddenSize> hidden_weights_{};
    Gates input_bias_{};
    Gates hidden_bias_{};
    Hidden output_weights_{};
    float output_bias_ = 0;
};

} // namespace denoise
#endif
