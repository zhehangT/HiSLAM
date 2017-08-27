/**
* This file is part of hi_slam.
*
* Author: tzh
* Date: 2017-03-28
*
*/


#include "software_bus_manual.h"
#include <tf2_ros/static_transform_broadcaster.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Vector3.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <stdlib.h>
#include <yaml-cpp/yaml.h>


namespace hi_slam{

  SoftwareBusManual::SoftwareBusManual()
    :private_nh_("~"),
     slam_loader_("hi_slam", "hi_slam::SLAMBase"),
     softbus_frequency_(1){

    ROS_INFO_STREAM("Initialising hi_slam ...");
    ReadConfig();

    //TODO
    //start with "rgdb_module" will die, do not know why
    slam_module_ = slam_loader_.createInstance("laser_module");


    pose.translation.x = 0;
    pose.translation.y = 0;
    pose.translation.z = 0;
    pose.rotation.x = 0;
    pose.rotation.y = 0;
    pose.rotation.z = 0;
    pose.rotation.w = 1;

    last_pose.translation.x = 0;
    last_pose.translation.y = 0;
    last_pose.translation.z = 0;
    last_pose.rotation.x = 0;
    last_pose.rotation.y = 0;
    last_pose.rotation.z = 0;
    last_pose.rotation.w = 1;


    first_setup_ = true;
    if_new_module_ = true;
    map_id = 0;

  }

  SoftwareBusManual::~SoftwareBusManual(){

    slam_module_->Shutdown();

  }


  void SoftwareBusManual::StartSoftwareBus(){

    dsrv_ = new dynamic_reconfigure::Server<hi_slam::SoftwareBusConfig>(
            ros::NodeHandle("hi_slam"));
    dsrv_->getConfigDefault(default_config_);
    ROS_INFO("Loading default scene %d", default_config_.scene);
    dynamic_reconfigure::Server<hi_slam::SoftwareBusConfig>::CallbackType cb = boost::bind(
                   &SoftwareBusManual::reconfigureCB, this, _1, _2);
    dsrv_->setCallback(cb);

    // scene_ = default_config_.scene;

    state_ = START;
    softbusThread_ = new std::thread(&SoftwareBusManual::RunSoftwareBus, this);
  }

  void SoftwareBusManual::RunSoftwareBus(){

    ros::WallRate r(softbus_frequency_);
    ROS_INFO("Running hi_slam state machine in %d Hz", softbus_frequency_);

    while(nh_.ok()){

      switch (state_) {

      case START:
        ROS_INFO("Running hi_slam state machine in START");
        StartSlamBaseScene(scene_);
        state_ = RUNNING;
        break;

      case RUNNING:
        break;

      case RESET:
        ROS_INFO("Running hi_slam state machine in RESET");
        ShutdownSlam();
        state_ = START;
        break;

      case IDLE:
        break;

      default:
        break;
      }


    r.sleep();

    }
  }


  void SoftwareBusManual::StartSlamBaseScene(string scene){
   
   if(scene != ""){

      ROS_INFO("StartSlamBaseScene %s", config_.SceneConfig[scene].c_str());
      StartSlamBaseName(config_.SceneConfig[scene].c_str());
    }



  }

  void SoftwareBusManual::StartSlamBaseName(std::string name){

    try{
      if(if_new_module_){
        slam_module_ = slam_loader_.createInstance(name);
      }
      std::string map_frame = name + "_" + std::to_string(map_id++);
      slam_module_->Activate(pose, map_frame, scene_);
      ROS_INFO("Start slam module %s", name.c_str());
    }catch(const pluginlib::PluginlibException &ex){
      ROS_FATAL(
            "Failed to create the %s plugin, are you sure it is properly registered and that the containing library is built? Exception: %s",
            default_config_.slam_module.c_str(), ex.what());
      exit(1);
    }
  }


  void SoftwareBusManual::ShutdownSlam(){


    geometry_msgs::Transform pose_temp;
    slam_module_->Shutdown(pose_temp);
    slam_module_.reset();

    tf2::Matrix3x3 r1(tf2::Quaternion(pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w));
    tf2::Matrix3x3 r2(tf2::Quaternion(pose_temp.rotation.x, pose_temp.rotation.y, pose_temp.rotation.z, pose_temp.rotation.w));


    tf2::Vector3 t1(pose.translation.x, pose.translation.y, pose.translation.z);
    tf2::Vector3 t2(pose_temp.translation.x, pose_temp.translation.y, pose_temp.translation.z);
    t1 = r1 * t2 + t1;
    r1 = r1 * r2;
    tf2::Quaternion q;
    r1.getRotation(q);

    pose.translation.x = t1.getX();
    pose.translation.y = t1.getY();
    pose.translation.z = t1.getZ();
    pose.rotation.x = q.getX();
    pose.rotation.y = q.getY();
    pose.rotation.z = q.getZ();
    pose.rotation.w = q.getW();

  }

//  void SoftwareBus::publish_transform(){

//    tf2_ros::StaticTransformBroadcaster tf2_br;
//    geometry_msgs::TransformStamped map_rgbd_transform;

//    map_rgbd_transform.transform = last_pose;
//    map_rgbd_transform.header.frame_id = "map";
//    map_rgbd_transform.child_frame_id = "laser_module_0";
//    map_rgbd_transform.header.stamp = ros::Time::now();
//    tf2_br.sendTransform(map_rgbd_transform);

//  }



  void SoftwareBusManual::reconfigureCB(hi_slam::SoftwareBusConfig &config, uint32_t level){

    boost::recursive_mutex::scoped_lock l(configuration_mutex_);
    string scene;

    switch(config.scene){

    case 0:

      scene = "Dark";
      break;

    case 1:

      scene = "Indoor";
      break;

    case 2:

      scene = "Outdoor";
      break;


    }

    if(scene_ != scene && config_.SceneConfig.find(scene) != config_.SceneConfig.end()){
      if(first_setup_){
        scene_ = scene;
        state_ = START;
        first_setup_ = false;

      }
      else if(config_.SceneConfig[scene] == config_.SceneConfig[scene_]){
        if_new_module_ = false;
        scene_ = scene;
        state_ = START;
      }
      else{
        if_new_module_ = true;
        scene_ = scene;
        state_ = RESET;

      }
      ROS_INFO("Change to Scene: %s", scene_.c_str());

    }

  }

  void SoftwareBusManual::ReadConfig(){

    std::string config_path = ros::package::getPath("hi_slam") + "/data/config.yaml";

    YAML::Node config_yaml = YAML::LoadFile(config_path);
    config_.SceneRecognition = config_yaml["SceneRecognition"].as<std::string>();

    YAML::Node sceneConfig_yaml = config_yaml["SceneConfig"];

    for (std::size_t i=0; i<sceneConfig_yaml.size(); i++) {

      string name = sceneConfig_yaml[i]["name"].as<string>();
      string slam = sceneConfig_yaml[i]["slam"].as<string>();
      config_.SceneConfig.insert( pair<string, string>(name, slam) );
    }

  }


}
