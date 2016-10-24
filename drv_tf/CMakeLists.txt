cmake_minimum_required(VERSION 2.8.3)
project(drv_tf)

aux_source_directory(./src SRC_LIST)

#Comment this if don't want debug
#set(CMAKE_BUILD_TYPE Debug)

find_package(catkin REQUIRED COMPONENTS
  drv_msgs
  dynamic_reconfigure
  geometry_msgs
  pcl_ros
  roscpp
  rospy
  sensor_msgs
  std_msgs
  tf2
  tf2_ros
)

find_package(PCL 1.7 REQUIRED)

#add dynamic reconfigure api
generate_dynamic_reconfigure_options(cfg/tf.cfg)

catkin_package()

link_directories(${PCL_LIBRARY_DIRS})
add_definitions(${PCL_DEFINITIONS})

include_directories(${catkin_INCLUDE_DIRS} ${PCL_INCLUDE_DIRS})

add_executable(drv_tf ${SRC_LIST})

target_link_libraries(drv_tf ${catkin_LIBRARIES} ${PCL_LIBRARIES})