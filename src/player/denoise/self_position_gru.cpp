#include "self_position_gru.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <locale>
#include <stdexcept>
#include <string>

namespace denoise {
namespace {
constexpr float Pi = 3.14159265358979323846f;

float wrap(float radians)
{
    float positive = std::fmod(radians + Pi, 2 * Pi);
    if (positive < 0) positive += 2 * Pi;
    return positive - Pi;
}

float sigmoid(float value)
{
    return 1 / (1 + std::exp(-value));
}

void limit_norm(float & x, float & y, float limit)
{
    const float scale = std::min(1.0f, limit / std::max(1e-6f, std::hypot(x, y)));
    x *= scale;
    y *= scale;
}

template <std::size_t N>
float dot(const std::array<float, N> & a, const std::array<float, N> & b)
{
    float value = 0;
    for (std::size_t i = 0; i < N; ++i) value += a[i] * b[i];
    return value;
}

void read(std::istream & input, float & value)
{
    if (!(input >> value) || !std::isfinite(value))
        throw std::runtime_error("invalid or incomplete weights");
}

template <typename T, std::size_t N>
void read(std::istream & input, std::array<T, N> & values)
{
    for (auto & value : values) read(input, value);
}
} // namespace

void SelfPositionGRU::load(const std::filesystem::path & path)
{
    std::ifstream input(path);
    input.imbue(std::locale::classic());
    std::string format;
    if (!(input >> format) || format != "exp2-self-position-gru-circular-v1")
        throw std::runtime_error("cannot read Step 05 model format from " + path.string());

    read(input, scales_);
    for (float scale : scales_)
        if (scale <= 0) throw std::runtime_error("invalid feature scale");
    read(input, input_weights_);
    read(input, hidden_weights_);
    read(input, input_bias_);
    read(input, hidden_bias_);
    read(input, output_weights_);
    read(input, output_bias_);
    std::string extra;
    if (input >> extra) throw std::runtime_error("unexpected data after model weights");
}

SelfPositionState SelfPositionGRU::step(const Features & input, const SelfPositionState & old,
                                       const SelfPositionPhysics & physics, Hidden & hidden) const
{
    const float body = wrap(old.body + input[Turn] / (1 + physics.inertia * std::hypot(old.vx, old.vy)));
    const float neck = input[HasNeck] ? input[Neck]
                       : std::clamp(old.neck + input[NeckTurn], physics.min_neck, physics.max_neck);
    const float face = wrap(body + neck);

    const float dash_direction = input[DashDir];
    const float fraction = std::min(Pi, std::abs(dash_direction)) / (Pi / 2);
    const float rate = fraction <= 1 ? 1 + (physics.side_dash_rate - 1) * fraction
                       : physics.side_dash_rate + (physics.back_dash_rate - physics.side_dash_rate) * (fraction - 1);
    const float dash = input[DashPower] * input[Effort] * physics.dash_rate * rate;
    float move_x = dash * std::cos(old.body + dash_direction);
    float move_y = dash * std::sin(old.body + dash_direction);
    limit_norm(move_x, move_y, physics.accel_max);
    move_x += old.vx;
    move_y += old.vy;
    limit_norm(move_x, move_y, physics.speed_max);

    const float x = old.x + move_x;
    const float y = old.y + move_y;
    const float vx = move_x * physics.decay;
    const float vy = move_y * physics.decay;
    const float dx = input[FixPos] ? input[FixX] - x : 0;
    const float dy = input[FixPos] ? input[FixY] - y : 0;
    const float face_delta = input[FixFace] ? wrap(input[FixAngle] - face) : 0;

    std::array<float, InputSize> features{}; // summary run uses an empty 16-float landmark embedding
    for (int i = 0; i < FeatureCount; ++i) features[i] = input[i] / scales_[i];
    const Feature angles[] = {DashDir, SpeedDir, FixAngle};
    const Feature masks[] = {Dash, HasSpeed, FixFace};
    for (int i = 0; i < 3; ++i)
    {
        features[angles[i]] = std::sin(input[angles[i]]) * input[masks[i]];
        features[FeatureCount + i] = std::cos(input[angles[i]]) * input[masks[i]];
    }
    const float prior_and_innovations[] = {
        x / 55, y / 55, vx / 1.2f, vy / 1.2f, std::cos(face), std::sin(face),
        dx / 0.10f, dy / 0.10f, face_delta / 0.02f
    };
    std::copy(std::begin(prior_and_innovations), std::end(prior_and_innovations),
              features.begin() + FeatureCount + 3);
    for (float & value : features) value = std::clamp(value, -12.0f, 12.0f);

    // PyTorch GRUCell stores the reset, update and new gates in that order.
    Gates from_input, from_hidden;
    for (int i = 0; i < 3 * HiddenSize; ++i)
    {
        from_input[i] = dot(input_weights_[i], features) + input_bias_[i];
        from_hidden[i] = dot(hidden_weights_[i], hidden) + hidden_bias_[i];
    }
    for (int i = 0; i < HiddenSize; ++i)
    {
        const float reset = sigmoid(from_input[i] + from_hidden[i]);
        const float update = sigmoid(from_input[HiddenSize + i] + from_hidden[HiddenSize + i]);
        const float candidate = std::tanh(from_input[2 * HiddenSize + i] + reset * from_hidden[2 * HiddenSize + i]);
        hidden[i] = candidate + update * (hidden[i] - candidate);
    }

    const float gain = sigmoid(dot(output_weights_, hidden) + output_bias_);
    return {x + gain * dx, y + gain * dy, vx, vy, body, neck};
}

} // namespace denoise
