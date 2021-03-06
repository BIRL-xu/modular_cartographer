/*
 * Copyright 2016 The Cartographer Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CARTOGRAPHER_ROS_CARTOGRAPHER_ROS_NODE_H
#define CARTOGRAPHER_ROS_CARTOGRAPHER_ROS_NODE_H

#include <std_msgs/UInt8MultiArray.h>
#include <std_srvs/Trigger.h>

#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "cartographer/common/fixed_ratio_sampler.h"
#include "cartographer/mapping/map_builder_interface.h"
#include "cartographer_ros/map_builder_bridge.h"
#include "cartographer_ros/metrics/family_factory.h"
#include "cartographer_ros/node_constants.h"
#include "cartographer_ros/node_options.h"
#include "cartographer_ros/trajectory_options.h"
#include "cartographer_ros_msgs/FinishTrajectory.h"
#include "cartographer_ros_msgs/GetTrajectoryStates.h"
#include "cartographer_ros_msgs/LoadState.h"
#include "cartographer_ros_msgs/ReadMetrics.h"
#include "cartographer_ros_msgs/StartLocalisation.h"
#include "cartographer_ros_msgs/StartMapping.h"
#include "cartographer_ros_msgs/StatusResponse.h"
#include "cartographer_ros_msgs/SubmapEntry.h"
#include "cartographer_ros_msgs/SubmapList.h"
#include "cartographer_ros_msgs/SubmapQuery.h"
#include "cartographer_ros_msgs/SystemState.h"
#include "cartographer_ros_msgs/WriteState.h"
#include "nav_msgs/OccupancyGrid.h"
#include "nav_msgs/Odometry.h"
#include "ros/ros.h"
#include "sensor_msgs/LaserScan.h"
#include "sensor_msgs/MultiEchoLaserScan.h"
#include "sensor_msgs/NavSatFix.h"
#include "sensor_msgs/PointCloud2.h"
#include "tf2_ros/transform_broadcaster.h"

namespace cartographer_ros
{

// Wires up ROS topics to SLAM.
class Node
{
  public:
    Node(const NodeOptions& node_options, const TrajectoryOptions& trajectory_options,
         const std::shared_ptr<tf2_ros::Buffer>& tf_buffer, const bool collect_metrics);

    ~Node();

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    void Reset() EXCLUSIVE_LOCKS_REQUIRED(mutex_);
    void StartTimerCallbacks();

    void FinishAllTrajectories() LOCKS_EXCLUDED(mutex_);
    bool FinishTrajectory(int trajectory_id);
    void RunFinalOptimization() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

    void HandleOdometryMessage(int trajectory_id, const std::string& sensor_id,
                               const nav_msgs::Odometry::ConstPtr& msg);
    void HandleNavSatFixMessage(int trajectory_id, const std::string& sensor_id,
                                const sensor_msgs::NavSatFix::ConstPtr& msg);
    void HandleLandmarkMessage(int trajectory_id, const std::string& sensor_id,
                               const cartographer_ros_msgs::LandmarkList::ConstPtr& msg);
    void HandleLaserScanMessage(int trajectory_id, const std::string& sensor_id,
                                const sensor_msgs::LaserScan::ConstPtr& msg);
    void HandleMultiEchoLaserScanMessage(int trajectory_id, const std::string& sensor_id,
                                         const sensor_msgs::MultiEchoLaserScan::ConstPtr& msg);
    void HandlePointCloud2Message(int trajectory_id, const std::string& sensor_id,
                                  const sensor_msgs::PointCloud2::ConstPtr& msg);

    void SerializeState(const std::string& filename, const bool include_unfinished_submaps);
    void LoadState(const std::string& state_filename, bool load_frozen_state);

    ::ros::NodeHandle* node_handle();

    int AddOfflineTrajectory(
        const std::set<cartographer::mapping::TrajectoryBuilderInterface::SensorId>& expected_sensor_ids,
        const TrajectoryOptions& options);

  private:
    struct Subscriber
    {
        ::ros::Subscriber subscriber;

        // ::ros::Subscriber::getTopic() does not necessarily return the same
        // std::string
        // it was given in its constructor. Since we rely on the topic name as the
        // unique identifier of a subscriber, we remember it ourselves.
        std::string topic;
    };

    bool HandleSubmapQuery(cartographer_ros_msgs::SubmapQuery::Request& request,
                           cartographer_ros_msgs::SubmapQuery::Response& response);
    bool HandleTrajectoryQuery(cartographer_ros_msgs::TrajectoryQuery::Request& request,
                               cartographer_ros_msgs::TrajectoryQuery::Response& response);
    bool HandleLoadState(cartographer_ros_msgs::LoadState::Request& request,
                         cartographer_ros_msgs::LoadState::Response& response);
    bool HandleWriteState(cartographer_ros_msgs::WriteState::Request& request,
                          cartographer_ros_msgs::WriteState::Response& response);
    bool HandleGetTrajectoryStates(::cartographer_ros_msgs::GetTrajectoryStates::Request& request,
                                   ::cartographer_ros_msgs::GetTrajectoryStates::Response& response);
    bool HandleReadMetrics(cartographer_ros_msgs::ReadMetrics::Request& request,
                           cartographer_ros_msgs::ReadMetrics::Response& response);

    bool HandleStartLocalisation(cartographer_ros_msgs::StartLocalisation::Request& request,
                                 cartographer_ros_msgs::StartLocalisation::Response& response) LOCKS_EXCLUDED(mutex_);
    bool HandleStopLocalisation(std_srvs::TriggerRequest& req, std_srvs::TriggerResponse& res) LOCKS_EXCLUDED(mutex_);

    bool HandlePauseLocalisation(std_srvs::TriggerRequest& req, std_srvs::TriggerResponse& res) LOCKS_EXCLUDED(mutex_);
    bool HandleResumeLocalisation(std_srvs::TriggerRequest& req, std_srvs::TriggerResponse& res) LOCKS_EXCLUDED(mutex_);

    bool HandleStartMapping(cartographer_ros_msgs::StartMapping::Request& request,
                            cartographer_ros_msgs::StartMapping::Response& response) LOCKS_EXCLUDED(mutex_);
    bool HandleStopMapping(std_srvs::TriggerRequest& req, std_srvs::TriggerResponse& res) LOCKS_EXCLUDED(mutex_);

    void HandleMapData(const std_msgs::UInt8MultiArray::ConstPtr& msg) LOCKS_EXCLUDED(mutex_);

    int AddTrajectory(const TrajectoryOptions& options);

    void PublishSubmapList(const ::ros::WallTimerEvent& timer_event);
    void PublishLocalTrajectoryData(const ::ros::WallTimerEvent& timer_event) LOCKS_EXCLUDED(mutex_);
    void PublishTrajectoryNodeList(const ::ros::WallTimerEvent& timer_event);
    void PublishLandmarkPosesList(const ::ros::WallTimerEvent& timer_event);
    void PublishConstraintList(const ::ros::WallTimerEvent& timer_event);

    void PausedTimer(const ::ros::WallTimerEvent&);

    cartographer_ros_msgs::StatusResponse FinishTrajectoryUnderLock(int trajectory_id) EXCLUSIVE_LOCKS_REQUIRED(mutex_);

    void MaybeWarnAboutTopicMismatch(const ::ros::WallTimerEvent&);

    // Helper function for service handlers that need to check trajectory states.
    cartographer_ros_msgs::StatusResponse TrajectoryStateToStatus(
        int trajectory_id, const std::set<cartographer::mapping::PoseGraphInterface::TrajectoryState>& valid_states);

    const NodeOptions node_options_;
    const TrajectoryOptions trajectory_options_;

    tf2_ros::TransformBroadcaster tf_broadcaster_;

    absl::Mutex mutex_;
    std::unique_ptr<cartographer_ros::metrics::FamilyFactory> metrics_registry_;
    const std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
    std::unique_ptr<MapBuilderBridge> map_builder_bridge_ GUARDED_BY(mutex_);

    std::string map_data_ GUARDED_BY(mutex_);
    cartographer_ros_msgs::SystemState system_state_ GUARDED_BY(mutex_);

    ::cartographer::transform::Rigid3d paused_tracking_in_global_ GUARDED_BY(mutex_);
    ::cartographer::transform::Rigid3d paused_global_to_odom_ GUARDED_BY(mutex_);
    ::ros::WallTimer paused_timer_;

    ::ros::NodeHandle nh_;
    ::ros::NodeHandle p_nh_;

    ::ros::Publisher submap_list_publisher_;
    ::ros::Publisher trajectory_node_list_publisher_;
    ::ros::Publisher landmark_poses_list_publisher_;
    ::ros::Publisher constraint_list_publisher_;
    ::ros::Publisher occupancy_grid_publisher_;

    std::vector<::ros::ServiceServer> service_servers_;
    ::ros::Publisher scan_matched_point_cloud_publisher_;
    ::ros::Publisher scan_features_publisher_;
    ::ros::Publisher submap_features_publisher_;
    ::ros::Publisher system_state_publisher_;
    ::ros::Subscriber map_data_subscriber_;

    struct TrajectorySensorSamplers
    {
        TrajectorySensorSamplers(const double rangefinder_sampling_ratio, const double odometry_sampling_ratio,
                                 const double fixed_frame_pose_sampling_ratio, const double landmark_sampling_ratio)
            : rangefinder_sampler(rangefinder_sampling_ratio), odometry_sampler(odometry_sampling_ratio),
              fixed_frame_pose_sampler(fixed_frame_pose_sampling_ratio), landmark_sampler(landmark_sampling_ratio)
        {
        }

        ::cartographer::common::FixedRatioSampler rangefinder_sampler;
        ::cartographer::common::FixedRatioSampler odometry_sampler;
        ::cartographer::common::FixedRatioSampler fixed_frame_pose_sampler;
        ::cartographer::common::FixedRatioSampler landmark_sampler;
    };

    // These are keyed with 'trajectory_id'.
    std::unordered_map<int, TrajectorySensorSamplers> sensor_samplers_;
    std::unordered_map<int, std::vector<Subscriber>> subscribers_;
    std::unordered_set<int> trajectories_scheduled_for_finish_;

    // We have to keep the timer handles of ::ros::WallTimers around, otherwise they do not fire
    std::vector<::ros::WallTimer> wall_timers_;
};

}  // namespace cartographer_ros

#endif  // CARTOGRAPHER_ROS_CARTOGRAPHER_ROS_NODE_H
