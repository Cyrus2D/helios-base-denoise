#ifndef HELIOS_DENOISE_SELF_LOCALIZATION_H
#define HELIOS_DENOISE_SELF_LOCALIZATION_H

#include "self_position_gru.h"

#include <rcsc/game_time.h>
#include <rcsc/geom/vector_2d.h>
#include <rcsc/player/object_table.h>

#include <fstream>
#include <string>

namespace rcsc {
class PlayerAgent;
class WorldModel;
}

namespace denoise {

class SelfLocalization {
public:
    SelfLocalization(const std::string & mode, const std::string & model_path);
    rcsc::Vector2D estimate(const rcsc::PlayerAgent & agent, const rcsc::WorldModel & world,
                            const std::string & previous_command);

private:
    struct Result {
        const char * status;
        rcsc::Vector2D candidate = rcsc::Vector2D::INVALIDATED;
        double inference_us = 0;
    };

    void reset();
    Result update(const rcsc::PlayerAgent & agent, const rcsc::WorldModel & world,
                  const std::string & previous_command);
    bool openLog(const rcsc::PlayerAgent & agent, const rcsc::WorldModel & world);
    SelfPositionGRU::Features features(const rcsc::PlayerAgent & agent, const rcsc::WorldModel & world,
                                       const std::string & previous_command) const;

    bool apply_;
    SelfPositionGRU model_;
    SelfPositionGRU::Hidden hidden_{};
    SelfPositionState state_{};
    rcsc::ObjectTable landmarks_;
    rcsc::GameTime last_time_{-1, 0};
    rcsc::GameTime play_start_{-1, 0};
    int move_count_ = -1;
    int warmup_steps_ = 0;
    float effort_ = 1;
    bool seeded_ = false;
    bool log_failed_ = false;
    std::ofstream log_;
};

} // namespace denoise
#endif
