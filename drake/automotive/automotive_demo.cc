#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <gflags/gflags.h>

#include "drake/automotive/automotive_simulator.h"
#include "drake/automotive/create_trajectory_params.h"
#include "drake/automotive/maliput/api/lane_data.h"
#include "drake/automotive/maliput/monolane/loader.h"
#include "drake/automotive/maliput/utility/generate_obj.h"
#include "drake/common/drake_path.h"
#include "drake/common/text_logging_gflags.h"

// "Ego car" in this instance means "controlled by something smarter than
// this demo code".
DEFINE_bool(use_ego_car, true,
            "Provide one or more user-controlled vehicles.  To get more than "
            "one, see ego_car_names parameter.");
DEFINE_string(ego_car_names, "",
              "A comma-separated list (e.g., 'Abel,Bacon,Cara' would provide "
              "3 ego-cars subscribed to DRIVING_COMMAND_Abel, "
              "DRIVING_COMMAND_Bacon, and DRIVING_COMMAND_Cara.  A non-empty "
              "value implies use_ego_car=true.");
DEFINE_int32(num_ado_car, 1,
             "Number of vehicles controlled by a "
             "(possibly trivial) traffic model");

DEFINE_double(target_realtime_rate, 1.0,
              "Playback speed.  See documentation for "
              "Simulator::set_target_realtime_rate() for details.");
DEFINE_double(simulation_sec, std::numeric_limits<double>::infinity(),
              "Number of seconds to simulate.");

DEFINE_string(road_file, "",
              "yaml file defining a maliput monolane road geometry");
DEFINE_string(road_path, "",
              "A string defining a circuit through the road geometry, "
              "consisting of lane id's separated by commas.  The first "
              "lane id must be prefixed by either 'start:' or 'end:' "
              "indicating at which end of the first lane to begin the "
              "circuit.  If the string is empty, a default path will "
              "be selected.");
DEFINE_bool(use_idm, false, "Use IDM to control ado cars on roads.");

namespace drake {
namespace automotive {
namespace {

const maliput::api::Lane* FindLaneByIdOrDie(
    const std::string& id, const maliput::api::RoadGeometry* road) {
  for (int ji = 0; ji < road->num_junctions(); ++ji) {
    const maliput::api::Junction* jnx = road->junction(ji);
    for (int si = 0; si < jnx->num_segments(); ++si) {
      const maliput::api::Segment* seg = jnx->segment(si);
      for (int li = 0; li < seg->num_lanes(); ++li) {
        const maliput::api::Lane* lane = seg->lane(li);
        if (lane->id().id == id) {
          return lane;
        }
      }
    }
  }
  std::cerr << "ERROR:  No lane named '" << id << "'." << std::endl;
  std::exit(1);
}


std::string MakeChannelName(const std::string& name) {
  static const std::string kDrivingCommandChannelName {"DRIVING_COMMAND"};
  if (name.empty()) {
    return kDrivingCommandChannelName;
  }
  return kDrivingCommandChannelName + "_" + name;
}


int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  logging::HandleSpdlogGflags();

  // TODO(liang.fok): Generalize this demo to allow arbitrary models to be
  // specified via command line parameters. This will involve removing some
  // hard-coded assumptions about the model's geometry. For exeample, the call
  // to CreateTrajectoryParams() below expects a "car" to have a particular
  // length and width.
  const std::string kSdfFile =
      GetDrakePath() + "/automotive/models/prius/prius_with_lidar.sdf";
  auto simulator = std::make_unique<AutomotiveSimulator<double>>();

  // Parse FLAGS_use_ego_car and FLAGS_ego_car_names into a vector of
  // name-strings.  One ego car will be provisioned for each name, and
  // the names will be appended to driving-command subscription name for
  // each car.  An empty name-string results in the default subscription
  // name.  An empty vector results in no ego cars.
  std::vector<std::string> ego_car_names;
  if (FLAGS_use_ego_car && FLAGS_ego_car_names.empty()) {
    ego_car_names.push_back("");
  } else if (!FLAGS_ego_car_names.empty()) {
    std::istringstream name_stream(FLAGS_ego_car_names);
    std::string name;
    while (getline(name_stream, name, ',')) {
      if (name.empty()) {
        ego_car_names.push_back("");
      } else {
        ego_car_names.push_back(name);
      }
    }
  }

  if (FLAGS_road_file.empty()) {
    // No road description has been specified.  So, we will run in
    // "free-for-all on the xy-plane" mode.

    // User-controlled vehicles are SimpleCars.
    for (const std::string& name : ego_car_names) {
      const std::string& channel_name = MakeChannelName(name);
      std::cout << "Adding ego car subscribed to "
                << channel_name << "." << std::endl;
      simulator->AddSimpleCarFromSdf(kSdfFile, name, channel_name);
    }

    // "Traffic model" is "drive in a figure-8".
    for (int i = 0; i < FLAGS_num_ado_car; ++i) {
      const auto& params = CreateTrajectoryParams(i);
      simulator->AddTrajectoryCarFromSdf(kSdfFile,
                                         std::get<0>(params),
                                         std::get<1>(params),
                                         std::get<2>(params));
    }

  } else {
    // A road description has been specified.  All vehicles will be constrained
    // to drive on the specified road surface.
    std::cerr << "building road from " << FLAGS_road_file << std::endl;
    auto base_road = maliput::monolane::LoadFile(FLAGS_road_file);

    maliput::api::LaneEnd start(
        base_road->junction(0)->segment(0)->lane(0),
        maliput::api::LaneEnd::kStart);
    std::vector<const maliput::api::Lane*> path;

    if (!FLAGS_road_path.empty()) {
      std::string end;
      std::string lane_id;
      std::stringstream ss(FLAGS_road_path);

      std::getline(ss, end, ':');
      std::getline(ss, lane_id, ',');
      if ((end != "start") && (end != "end")) {
        std::cerr << "ERROR:  road_path must start with 'start:' or 'end:'."
                  << std::endl;
        return 1;
      }
      start = maliput::api::LaneEnd(
          FindLaneByIdOrDie(lane_id, base_road.get()),
          (end == "start") ? maliput::api::LaneEnd::kStart :
          maliput::api::LaneEnd::kFinish);

      while (std::getline(ss, lane_id, ',')) {
        path.push_back(FindLaneByIdOrDie(lane_id, base_road.get()));
      }
    }

    const maliput::utility::InfiniteCircuitRoad* const endless_road =
        simulator->SetRoadGeometry(&base_road, start, path);

    // User-controlled vehicles are EndlessRoadCars with DrivingCommand input.
    for (size_t i = 0; i < ego_car_names.size(); ++i) {
      const double kConstantSpeed = 10.0;
      const double kLateralOffsetUnit = -2.0;

      const double longitudinal_start =
          endless_road->lane()->cycle_length() *
          ((1.0 * (i / 2) / ego_car_names.size()) + 0.5);
      const double lateral_offset =
          (((i % 2) * 2.0) - 1.0) * kLateralOffsetUnit;
      const std::string& given_name = ego_car_names[i];
      const std::string& model_name =
          given_name.empty() ? ("User-" + std::to_string(i)) : given_name;
      const std::string& channel_name = MakeChannelName(given_name);
      std::cout << "Adding ego car '" << model_name << "' subscribed to "
                << channel_name << "." << std::endl;
      simulator->AddEndlessRoadCar(
          model_name,
          kSdfFile,
          longitudinal_start, lateral_offset, kConstantSpeed,
          EndlessRoadCar<double>::kUser, channel_name);
    }

    // "Traffic model" is either clever (car-following, oracular awareness
    // of merging/intersecting vehicles) or dumb ("drive at a constant
    // LANE-space velocity").
    if (FLAGS_use_idm) {
      const double kInitialSpeed = 30.0;
      const double kLateralOffsetUnit = 0.0;
      for (int i = 0; i < FLAGS_num_ado_car; ++i) {
        const double longitudinal_start =
            endless_road->lane()->cycle_length() * i / FLAGS_num_ado_car / 2.;
        const double lateral_offset = kLateralOffsetUnit;
        simulator->AddEndlessRoadCar(
            "IDM-" + std::to_string(i),
            kSdfFile,
            longitudinal_start, lateral_offset, kInitialSpeed,
            EndlessRoadCar<double>::kIdm, "");
      }
    } else {
      const double kConstantSpeed = 10.0;
      const double kLateralOffsetUnit = -2.0;
      for (int i = 0; i < FLAGS_num_ado_car; ++i) {
        const double longitudinal_start =
            endless_road->lane()->cycle_length() * (i / 2) / FLAGS_num_ado_car;
        const double lateral_offset =
            (((i % 2) * 2) - 1) * kLateralOffsetUnit;
        simulator->AddEndlessRoadCar(
            "CV-" + std::to_string(i),
            kSdfFile,
            longitudinal_start, lateral_offset, kConstantSpeed,
            EndlessRoadCar<double>::kNone, "");
      }
    }
  }

  simulator->Start(FLAGS_target_realtime_rate);
  simulator->StepBy(FLAGS_simulation_sec);

  return 0;
}

}  // namespace
}  // namespace automotive
}  // namespace drake

int main(int argc, char* argv[]) { return drake::automotive::main(argc, argv); }
