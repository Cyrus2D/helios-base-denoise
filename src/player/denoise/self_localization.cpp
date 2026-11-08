#include "self_localization.h"

#include <rcsc/common/player_type.h>
#include <rcsc/common/server_param.h>
#include <rcsc/player/body_sensor.h>
#include <rcsc/player/player_agent.h>
#include <rcsc/player/visual_sensor.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace denoise {
namespace {
constexpr double DegreesToRadians = 3.14159265358979323846 / 180;
constexpr int BurnIn = 5;
constexpr double MaxCorrection = 0.5; // meters, fallback to librcsc for unexpected jumps

bool valid(const rcsc::Vector2D & point)
{
    return point.isValid() && std::isfinite(point.x) && std::isfinite(point.y);
}

double side_sign(const rcsc::WorldModel & world)
{
    // Training logs use the stadium frame, including for right-side players.
    return world.ourSide() == rcsc::RIGHT ? -1 : 1;
}

double stadium_angle(double degrees, double sign)
{
    return rcsc::AngleDeg::normalize_angle(degrees + (sign < 0 ? 180 : 0));
}

float number_after(const std::string & batch, const std::string & command)
{
    const auto start = batch.find(command);
    const auto say = batch.find("(say ");
    if (start == std::string::npos || start >= say) return 0;
    std::istringstream args(batch.substr(start + command.size()));
    args.imbue(std::locale::classic());
    float value = 0;
    args >> value;
    return value;
}
} // namespace

SelfLocalization::SelfLocalization(const std::string & mode, const std::string & model_path)
    : apply_(mode == "apply")
{
    if (!apply_ && mode != "shadow")
        throw std::runtime_error("expected self localization mode off, shadow or apply");
    model_.load(model_path);
}

void SelfLocalization::reset()
{
    seeded_ = false;
    hidden_.fill(0);
    warmup_steps_ = 0;
}

SelfPositionGRU::Features
SelfLocalization::features(const rcsc::PlayerAgent & agent, const rcsc::WorldModel & world,
                           const std::string & previous_command) const
{
    SelfPositionGRU::Features x{};
    x[Effort] = effort_;
    if (previous_command.rfind("(dash ", 0) == 0)
    {
        x[Dash] = 1;
        std::istringstream dash(previous_command.substr(6));
        dash.imbue(std::locale::classic());
        float direction = 0;
        dash >> x[DashPower] >> direction;
        x[DashDir] = direction * DegreesToRadians;
    }
    x[TurnCommand] = previous_command.rfind("(turn ", 0) == 0;
    if (x[TurnCommand]) x[Turn] = number_after(previous_command, "(turn ") * DegreesToRadians;
    x[Tackle] = previous_command.rfind("(tackle ", 0) == 0;
    x[NeckTurn] = number_after(previous_command, "(turn_neck ") * DegreesToRadians;

    const auto & body = agent.bodySensor();
    const auto & time = world.time();
    if (body.time() == time)
    {
        x[HasSpeed] = x[HasNeck] = 1;
        x[Speed] = body.speedMag();
        x[SpeedDir] = body.speedDir() * DegreesToRadians;
        x[Neck] = body.neckDir() * DegreesToRadians;
        x[Collision] = body.ballCollided() || body.playerCollided() || body.postCollided();
    }

    const auto & self = world.self();
    x[PosAge] = self.posValid() ? self.posCount() : 1000;
    x[FaceAge] = self.faceValid() ? self.faceCount() : 1000;
    x[Narrow] = self.viewWidth().type() == rcsc::ViewWidth::NARROW;
    x[Normal] = self.viewWidth().type() == rcsc::ViewWidth::NORMAL;
    x[Wide] = self.viewWidth().type() == rcsc::ViewWidth::WIDE;

    x[See] = world.seeTime() == time;
    if (!x[See]) return x;

    const double sign = side_sign(world);
    if (world.selfRawTime() == time)
    {
        x[FixPos] = valid(world.selfRawPos());
        if (x[FixPos])
        {
            x[FixX] = sign * world.selfRawPos().x;
            x[FixY] = sign * world.selfRawPos().y;
            x[FixErrorX] = std::abs(world.selfRawPosError().x);
            x[FixErrorY] = std::abs(world.selfRawPosError().y);
        }
        x[FixFace] = world.selfRawFaceValid();
        if (x[FixFace])
        {
            x[FixAngle] = stadium_angle(world.selfRawFace(), sign) * DegreesToRadians;
            x[FixErrorAngle] = world.selfRawFaceError() * DegreesToRadians;
        }
    }

    const auto & visual = agent.visualSensor();
    if (visual.time() != time) return x;
    x[LineCount] = visual.lines().size();
    double distance_sum = 0, sin_sum = 0, cos_sum = 0;
    const double infinity = std::numeric_limits<double>::infinity();
    double min_distance = infinity;
    double min_x = infinity, max_x = -infinity;
    double min_y = infinity, max_y = -infinity;
    const auto & field = landmarks_.landmarkMap();
    for (const auto & marker : visual.markers())
    {
        const auto point = field.find(marker.id_);
        if (point == field.end()) continue;
        ++x[LandCount];
        distance_sum += marker.dist_;
        min_distance = std::min(min_distance, marker.dist_);
        const double bearing = marker.dir_ * DegreesToRadians;
        sin_sum += std::sin(bearing);
        cos_sum += std::cos(bearing);
        min_x = std::min(min_x, point->second.x);
        max_x = std::max(max_x, point->second.x);
        min_y = std::min(min_y, point->second.y);
        max_y = std::max(max_y, point->second.y);
    }
    if (x[LandCount] > 0)
    {
        x[LandMean] = distance_sum / x[LandCount];
        x[LandMin] = min_distance;
        x[LandSpread] = std::clamp(1 - std::hypot(sin_sum, cos_sum) / x[LandCount], 0.0, 1.0);
        x[LandSpan] = std::hypot(max_x - min_x, max_y - min_y);
    }
    return x;
}

SelfLocalization::Result
SelfLocalization::update(const rcsc::PlayerAgent & agent, const rcsc::WorldModel & world,
                         const std::string & previous_command)
{
    const auto & server = rcsc::ServerParam::i();
    const auto & time = world.time();
    const auto & game = world.gameMode();
    const auto & body = agent.bodySensor();
    const bool fresh_body = body.time() == time;
    const bool moved = previous_command.rfind("(move ", 0) == 0
                       || (fresh_body && move_count_ >= 0 && body.moveCount() != move_count_);
    if (fresh_body) move_count_ = body.moveCount();

    if (last_time_.cycle() + 1 != time.cycle() || play_start_ != game.time() || moved)
        reset();
    last_time_ = time;
    play_start_ = game.time();

    if (server.seeNoiseFree() || game.type() != rcsc::GameMode::PlayOn || time.stopped() != 0)
    {
        reset();
        return {server.seeNoiseFree() ? "noise_free" : "outside_play_on"};
    }

    const auto & self = world.self();
    if (!self.posValid() || !valid(self.pos()) || !self.velValid() || !valid(self.vel()))
    {
        reset();
        return {"invalid_state"};
    }

    const double sign = side_sign(world);
    const SelfPositionState reference{
        float(sign * self.pos().x), float(sign * self.pos().y),
        float(sign * self.vel().x), float(sign * self.vel().y),
        float(stadium_angle(self.body().degree(), sign) * DegreesToRadians),
        float(self.neck().degree() * DegreesToRadians)
    };

    const auto x = features(agent, world, previous_command);
    effort_ = fresh_body ? body.effort() : 1;
    if (!seeded_)
    {
        if (!x[FixPos] || self.posCount() != 0) return {"waiting_for_fix"};
        state_ = reference;
        seeded_ = true;
        return {"seed"};
    }

    const auto & type = self.playerType();
    const bool unexpected_jump = x[FixPos]
                                 && std::hypot(x[FixX] - state_.x, x[FixY] - state_.y)
                                    > type.playerSpeedMax() + MaxCorrection;
    const bool two_leg_dash = previous_command.rfind("(dash (", 0) == 0;
    if (unexpected_jump || two_leg_dash)
    {
        reset();
        return {unexpected_jump ? "position_jump" : "unsupported_dash"};
    }

    const SelfPositionPhysics physics{
        float(type.inertiaMoment()), float(type.dashPowerRate()),
        float(type.playerDecay()), float(type.playerSpeedMax()),
        float(server.playerAccelMax()), float(server.sideDashRate()), float(server.backDashRate()),
        float(server.minNeckAngle() * DegreesToRadians), float(server.maxNeckAngle() * DegreesToRadians)
    };
    const auto start = std::chrono::steady_clock::now();
    const auto prediction = model_.step(x, state_, physics, hidden_);
    const double micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    const rcsc::Vector2D candidate(sign * prediction.x, sign * prediction.y);

    // Also catch unannounced repositioning or unsupported motion before applying a correction.
    if (!valid(candidate) || candidate.dist(self.pos()) > MaxCorrection)
    {
        reset();
        return {"rejected", candidate, micros};
    }
    state_ = reference;
    state_.x = prediction.x;
    state_.y = prediction.y;
    if (warmup_steps_ < BurnIn)
    {
        ++warmup_steps_;
        return {"warmup", candidate, micros};
    }
    return {"ready", candidate, micros};
}

bool SelfLocalization::openLog(const rcsc::PlayerAgent & agent, const rcsc::WorldModel & world)
{
    if (log_failed_) return false;
    if (log_.is_open()) return true;
    const char side = world.ourSide() == rcsc::LEFT ? 'l' : 'r';
    std::string game_id = agent.config().denoiseGameId();
    for (char & c : game_id)
        if (std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-").find(c)
            == std::string::npos) c = '_';
    const auto path = std::filesystem::path(agent.config().denoiseLogDir())
                      / game_id / (std::string(1, side) + "_" + std::to_string(world.self().unum()));
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (!error) log_.open(path / "self_localization.csv");
    if (!log_)
    {
        std::cerr << "Self localization disabled: cannot open log in " << path << '\n';
        log_failed_ = true;
        return false;
    }
    log_.imbue(std::locale::classic());
    log_ << "cycle,stopped,mode,status,applied,pos_age,baseline_x,baseline_y,model_x,model_y,inference_us\n";
    log_ << std::setprecision(9);
    return true;
}

rcsc::Vector2D SelfLocalization::estimate(const rcsc::PlayerAgent & agent, const rcsc::WorldModel & world,
                                          const std::string & previous_command)
{
    if (!openLog(agent, world)) return rcsc::Vector2D::INVALIDATED;
    const Result result = update(agent, world, previous_command);
    const bool applied = apply_ && std::string_view(result.status) == "ready";
    const double sign = side_sign(world);
    const auto write_position = [this, sign](const rcsc::Vector2D & point) {
        log_ << ',';
        if (valid(point)) log_ << sign * point.x << ',' << sign * point.y;
        else log_ << ',';
    };
    log_ << world.time().cycle() << ',' << world.time().stopped() << ','
         << (apply_ ? "apply" : "shadow") << ',' << result.status << ',' << applied << ','
         << world.self().posCount();
    write_position(world.self().posValid() ? world.self().pos() : rcsc::Vector2D::INVALIDATED);
    write_position(result.candidate);
    log_ << ',' << result.inference_us << '\n';
    if (!log_)
    {
        std::cerr << "Self localization disabled: could not write its log\n";
        log_failed_ = true;
        return rcsc::Vector2D::INVALIDATED;
    }
    return applied ? result.candidate : rcsc::Vector2D::INVALIDATED;
}

} // namespace denoise
