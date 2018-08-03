#include "drake/automotive/lane_frame_kinematic_plant.h"

#include <gtest/gtest.h>

namespace drake {

using maliput::api::LaneEnd;

using systems::BasicVector;
using systems::LeafContext;
using systems::Parameters;

namespace api = maliput::api;

namespace automotive {
namespace {


class LaneFrameKinematicPlantTest : public::testing::Test {
 protected:
  void SetUp() override {
    dut_ = std::make_unique<LaneFrameKinematicPlant<double>>();
    context_ = dut_->CreateDefaultContext();
  }

  std::unique_ptr<const api::RoadGeometry> MakeBasicRoad() {

    const double kStraightLength = 100.;
    const double kCurveRadius = 50.;

    multilane::Builder b(kLaneWidth, kElevationBounds,
                         kLinearTolerance, kAngularTolerance, kScaleLength,
                         kComputationPolicy);

    auto straight = b.Connect(
        "straight", kThreeLaneLayout,
        StartLane(0).at(kOrigin, Direction::kForward),
        LineOffset(kStraightLength),
        EndLane(0).z_at(kLowFlatZ, Direction::kForward));
    auto curve = b.Connect(
        "curve", kThreeLaneLayout,
        StartLane(0).at(curve, 1, Which::kFinish, Direction::kForward),
        ArcOffset(50., M_PI),
        EndLane(0).z_at(kLowFlatZ, Direction::kForward));

    return b->Build(api::RoadGeometryId("basic-road"));
  }


  LaneFrameKinematicPlantContinuousState<double>& continuous_state() {
    auto result = dynamic_cast<LaneFrameKinematicPlantContinuousState<double>*>(
        &context_->get_mutable_continuous_state_vector());
    if (result == nullptr) { throw std::bad_cast(); }
    return *result;
  }


  LaneFrameKinematicPlant<double>::AbstractState& abstract_state() {
    // TODO(maddog@tri.global)  Zero index is magic.
    return context_->template get_mutable_abstract_state<
      LaneFrameKinematicPlant<double>::AbstractState>(0);
  }


  const LaneFrameKinematicPlantContinuousState<double>&
  continuous_output(systems::SystemOutput<double>* output) const {
    auto result =
        dynamic_cast<const LaneFrameKinematicPlantContinuousState<double>*>(
            output->get_vector_data(
                dut_->continuous_output_port().get_index()));
    if (result == nullptr) { throw std::bad_cast(); }
    return *result;
  }

  const LaneFrameKinematicPlant<double>::AbstractState&
  abstract_output(systems::SystemOutput<double>* output) const {
    return output->get_data(dut_->abstract_output_port().get_index())->
        GetValueOrThrow<LaneFrameKinematicPlant<double>::AbstractState>();
  }


  std::unique_ptr<LaneFrameKinematicPlant<double>> dut_;
  std::unique_ptr<systems::Context<double>> context_;
};


TEST_F(LaneFrameKinematicPlantTest, SystemTopology) {
  // Check composition of input ports.
  EXPECT_EQ(dut_->get_num_input_ports(), 2);

  const auto& abstract_input = dut_->abstract_input_port();
  EXPECT_EQ(abstract_input.get_data_type(), systems::kAbstractValued);

  const auto& continuous_input = dut_->continuous_input_port();
  EXPECT_EQ(continuous_input.get_data_type(), systems::kVectorValued);
  EXPECT_EQ(continuous_input.size(),
            LaneFrameKinematicPlantContinuousInputIndices::kNumCoordinates);

  // Check composition of output ports.
  EXPECT_EQ(dut_->get_num_output_ports(), 2);

  const auto& abstract_output = dut_->abstract_output_port();
  EXPECT_EQ(abstract_output.get_data_type(), systems::kAbstractValued);

  const auto& continuous_output = dut_->continuous_output_port();
  EXPECT_EQ(continuous_output.get_data_type(), systems::kVectorValued);
  EXPECT_EQ(continuous_output.size(),
            LaneFrameKinematicPlantContinuousStateIndices::kNumCoordinates);

  // Check composition of context's state.
  EXPECT_EQ(context_->get_num_abstract_states(), 1);
  EXPECT_EQ(context_->get_continuous_state().size(),
            LaneFrameKinematicPlantContinuousStateIndices::kNumCoordinates);
  EXPECT_EQ(context_->get_continuous_state().num_q(), 2);
  EXPECT_EQ(context_->get_continuous_state().num_v(), 2);

  // TODO(maddog@tri.global)  Check composition of context's parameters.
}


TEST_F(LaneFrameKinematicPlantTest, OutputCopiesState) {
  // Set up state in context_.
  api::Lane* const kExpectedLane = reinterpret_cast<api::Lane*>(0xDeadBeef);
  constexpr double kExpectedS = 99.9;
  constexpr double kExpectedR = -2.3;
  constexpr double kExpectedHeading = 0.32;
  constexpr double kExpectedSpeed = 500.7;
  abstract_state().lane = kExpectedLane;
  continuous_state().set_s(kExpectedS);
  continuous_state().set_r(kExpectedR);
  continuous_state().set_heading(kExpectedHeading);
  continuous_state().set_speed(kExpectedSpeed);

  // Run dut_.
  std::unique_ptr<systems::SystemOutput<double>> output =
      dut_->AllocateOutput();
  dut_->CalcOutput(*context_, output.get());

  // Verify results.
  EXPECT_EQ(abstract_output(output.get()).lane, kExpectedLane);
  EXPECT_EQ(continuous_output(output.get()).s(), kExpectedS);
  EXPECT_EQ(continuous_output(output.get()).r(), kExpectedR);
  EXPECT_EQ(continuous_output(output.get()).heading(), kExpectedHeading);
  EXPECT_EQ(continuous_output(output.get()).speed(), kExpectedSpeed);
}


TEST_F(LaneFrameKinematicPlantTest, Derivatives) {

  // Set up plumbing for derivatives results.
  std::unique_ptr<systems::ContinuousState<double>> derivatives_state =
      dut_->AllocateTimeDerivatives();
  LaneFrameKinematicPlantContinuousState<double>& derivatives =
      dynamic_cast<LaneFrameKinematicPlantContinuousState<double>>(
          derivatives_state->get_mutable_vector());

  // Set up state in context_...
  abstract_state().lane = ...;
  continuous_state().set_s(...);
  continuous_state().set_r(...);
  continuous_state().set_heading(...);
  continuous_state().set_speed(...);

  // Set up input...
  abstract_input().ongoing_lane = ...;
  continuous_input().forward_acceleration = ...;
  continuous_input().curvature = ...;

  // Run dut_.
  dut_->CalcTimeDerivatives(*context_, derivatives_state.get());


  // Verify results.
  EXPECT_EQ(derivatives.s(), kExpectedDs);
  EXPECT_EQ(derivatives.r(), kExpectedDr);
  EXPECT_EQ(derivatives.heading(), kExpectedDheading);
  EXPECT_EQ(derivatives.speed(), kExpectedDspeed);





  // Set up state in context_.
  api::Lane* const kExpectedLane = reinterpret_cast<api::Lane*>(0xDeadBeef);
  constexpr double kExpectedS = 99.9;
  constexpr double kExpectedR = -2.3;
  constexpr double kExpectedHeading = 0.32;
  constexpr double kExpectedSpeed = 500.7;
  abstract_state().lane = kExpectedLane;
  continuous_state().set_s(kExpectedS);
  continuous_state().set_r(kExpectedR);
  continuous_state().set_heading(kExpectedHeading);
  continuous_state().set_speed(kExpectedSpeed);

  // Run dut_.
  std::unique_ptr<systems::SystemOutput<double>> output =
      dut_->AllocateOutput();
  dut_->CalcOutput(*context_, output.get());

  // Verify results.
  EXPECT_EQ(abstract_output(output.get()).lane, kExpectedLane);
  EXPECT_EQ(continuous_output(output.get()).s(), kExpectedS);
  EXPECT_EQ(continuous_output(output.get()).r(), kExpectedR);
  EXPECT_EQ(continuous_output(output.get()).heading(), kExpectedHeading);
  EXPECT_EQ(continuous_output(output.get()).speed(), kExpectedSpeed);
}


}  // namespace
}  // namespace automotive
}  // namespace drake
