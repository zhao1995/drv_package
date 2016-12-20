#include <ros/ros.h>

#include <tf2/transform_datatypes.h>
#include <tf2_sensor_msgs/tf2_sensor_msgs.h>
#include <tf2_ros/transform_listener.h>

#include <geometry_msgs/TransformStamped.h>

#include <math.h>
#include <string.h>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>

#include <std_msgs/Bool.h>
#include <std_msgs/UInt16MultiArray.h>
#include <visualization_msgs/Marker.h>

#include <geometry_msgs/PoseStamped.h>

//Custom message
#include <drv_msgs/recognized_target.h>

//PCL-ROS
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>
#include <pcl_conversions/pcl_conversions.h>

//PCL
#include <pcl/filters/extract_indices.h>
#include <pcl/point_types.h>

#include "makeplan.h"

using namespace std;

typedef pcl::PointCloud<pcl::PointXYZRGB> PointCloud;

//ros::Publisher graspPubInfo_;
ros::Publisher graspPubMarker_;
ros::Publisher graspPubStatus_;
ros::Publisher graspPubCloud_;
ros::Publisher graspPubPose_;
ros::Publisher graspPubLocation_;

enum ModeType{m_wander, m_search, m_track};
int modeType_ = m_wander;
int modeTypeTemp_ = m_wander;

string param_running_mode = "/status/running_mode";
bool hasGraspPlan_ = false;

bool posePublished_ = false;

// target pointcloud index of image
#ifdef USE_CENTER
int idx_;
#else
pcl::PointIndices::Ptr inliers_(new pcl::PointIndices);
#endif

// marker type
uint32_t shape = visualization_msgs::Marker::ARROW;

// transform frame
string camera_base_frame_ = "camera_yaw_frame";
string camera_optical_frame_ = "openni_rgb_optical_frame";
string target_location_frame_ = "map";
geometry_msgs::TransformStamped trans_c_;
geometry_msgs::TransformStamped trans_m_;


void getCloudByInliers(PointCloud::Ptr cloud_in, PointCloud::Ptr &cloud_out,
                       pcl::PointIndices::Ptr inliers, bool negative, bool organized)
{
    pcl::ExtractIndices<pcl::PointXYZRGB> extract;
    extract.setNegative (negative);
    extract.setInputCloud (cloud_in);
    extract.setIndices (inliers);
    extract.setKeepOrganized(organized);
    extract.filter (*cloud_out);
}

void msgToCloud(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr cloud_in, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud_out)
{
    cloud_out->height = cloud_in->height;
    cloud_out->width  = cloud_in->width;
    cloud_out->is_dense = false;
    cloud_out->resize(cloud_out->height * cloud_out->width);

    for (size_t i = 0; i < cloud_out->size(); ++i)
        {
            cloud_out->points[i].x = cloud_in->points[i].x;
            cloud_out->points[i].y = cloud_in->points[i].y;
            cloud_out->points[i].z = cloud_in->points[i].z;
            cloud_out->points[i].r = cloud_in->points[i].r;
            cloud_out->points[i].g = cloud_in->points[i].g;
            cloud_out->points[i].b = cloud_in->points[i].b;
        }
}

void doTransform(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_in, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud_out, const geometry_msgs::TransformStamped& t_in)
{
    Eigen::Transform<float,3,Eigen::Affine> t = Eigen::Translation3f(t_in.transform.translation.x, t_in.transform.translation.y,t_in.transform.translation.z)
            * Eigen::Quaternion<float>(t_in.transform.rotation.w, t_in.transform.rotation.x, t_in.transform.rotation.y, t_in.transform.rotation.z);

    Eigen::Vector3f point;

    cloud_out->height = cloud_in->height;
    cloud_out->width  = cloud_in->width;
    cloud_out->is_dense = false;
    cloud_out->resize(cloud_out->height * cloud_out->width);

    for (size_t i = 0; i < cloud_in->size(); ++i)
        {
            point = t * Eigen::Vector3f(cloud_in->points[i].x, cloud_in->points[i].y, cloud_in->points[i].z);
            cloud_out->points[i].x = point.x();
            cloud_out->points[i].y = point.y();
            cloud_out->points[i].z = point.z();
            cloud_out->points[i].r = cloud_in->points[i].r;
            cloud_out->points[i].g = cloud_in->points[i].g;
            cloud_out->points[i].b = cloud_in->points[i].b;
        }
}

void doTransform(pcl::PointXYZRGB p_in, pcl::PointXYZRGB &p_out, const geometry_msgs::TransformStamped& t_in)
{
    Eigen::Transform<float,3,Eigen::Affine> t = Eigen::Translation3f(t_in.transform.translation.x, t_in.transform.translation.y,t_in.transform.translation.z)
            * Eigen::Quaternion<float>(t_in.transform.rotation.w, t_in.transform.rotation.x, t_in.transform.rotation.y, t_in.transform.rotation.z);

    Eigen::Vector3f point;

    point = t * Eigen::Vector3f(p_in.x, p_in.y, p_in.z);
    p_out.x = point.x();
    p_out.y = point.y();
    p_out.z = point.z();
    p_out.r = p_in.r;
    p_out.g = p_in.g;
    p_out.b = p_in.b;
}

void trackResultCallback(const drv_msgs::recognized_targetConstPtr & msg)
{
    if (modeType_ != m_track)
        {
            return;
        }

#ifdef USE_CENTER
    // directly use center as tgt location
    idx_ = msg->tgt_bbox_center.data[0] + msg->tgt_bbox_center.data[1] * 640;
#else
    inliers_->indices.clear();
    // use mask as tgt area
    for (size_t i = 0; i < msg->tgt_pixels.data.size(); i++)
        {
            inliers_->indices.push_back(msg->tgt_pixels.data[i]);
        }
#endif
}

void cloudCallback(const PointCloud::ConstPtr& msg)
{
    if (modeType_ != m_track)
        {
            return;
        }

    if (msg->empty())
        {
            ROS_ERROR("Can't get source cloud message.\n");
            return;
        }

#ifndef USE_CENTER
    if (inliers_->indices.empty())
        {
            ROS_WARN_THROTTLE(5, "Object grasp plan has not been found.\n");
            return;
        }
#endif

    MakePlan MP;
    pcl::PointXYZRGB graspPt; // in robot's frame
    pcl::PointXYZRGB locationPt; // in map frame

#ifdef USE_CENTER
    pcl::PointXYZRGB p_in = msg->points[idx_];
    if (isnan(p_in.x) || isnan(p_in.y) || isnan(p_in.z))
        hasGraspPlan_ = false;
    else
        {
            doTransform(p_in, graspPt, trans_c_);
            // doTransform(p_in, locationPt, trans_m_);
            hasGraspPlan_ = MP.smartOffset(graspPt, 0.02);
        }
#else
    PointCloud::Ptr cloud_in (new PointCloud());
    msgToCloud(msg, cloud_in);

    PointCloud::Ptr cloud_fl (new PointCloud ());
    getCloudByInliers(cloud_in, cloud_fl, inliers_, false, false);

    PointCloud::Ptr cloud_fo (new PointCloud ());
    MP.removeNans(cloud_fl, cloud_fo);

    // transform the point cloud
    PointCloud::Ptr cloud_ts (new PointCloud ());
    doTransform(cloud_fo, cloud_ts, transformStamped_);

    cloud_ts->header.stamp = msg->header.stamp;
    cloud_ts->header.frame_id = targetFrame_;
    graspPubCloud_.publish(*cloud_ts);

    hasGraspPlan_ = MP.process(cloud_ts, pa_, pb_, pc_, pd_, graspPt);
#endif

    if (hasGraspPlan_)
        {
            visualization_msgs::Marker marker;
            // Set the frame ID and timestamp.  See the TF tutorials for information on these.
            marker.header.frame_id = camera_base_frame_;
            marker.header.stamp = pcl_conversions::fromPCL(msg->header.stamp);

            // Set the namespace and id for this marker.  This serves to create a unique ID
            // Any marker sent with the same namespace and id will overwrite the old one
            marker.ns = "grasp";
            marker.id = 0;

            marker.type = shape;
            marker.action = visualization_msgs::Marker::ADD;

            // Set the pose of the marker.  This is a full 6DOF pose relative to the frame/time specified in the header
            marker.points.resize(2);
            // The point at index 0 is assumed to be the start point, and the point at index 1 is assumed to be the end.
            marker.points[0].x = graspPt.x;
            marker.points[0].y = graspPt.y;
            marker.points[0].z = graspPt.z;

            marker.points[1].x = graspPt.x;
            marker.points[1].y = graspPt.y;
            marker.points[1].z = graspPt.z + 0.15; // arrow height = 0.15m
            /*
             if we use pose and scale to represent the arrow, remember the original direction is along x axis
             marker.scale.x = 0.1;
             marker.scale.y = 0.01;
             marker.scale.z = 0.01;
             marker.pose = ps.pose;
            */
            // scale.x is the shaft diameter, and scale.y is the head diameter. If scale.z is not zero, it specifies the head length.
            // Set the scale of the marker -- 1x1x1 here means 1m on a side
            marker.scale.x = 0.01;
            marker.scale.y = 0.015;
            marker.scale.z = 0.04;

            // Set the color -- be sure to set alpha to something non-zero!
            marker.color.r = 0.0f;
            marker.color.g = 1.0f;
            marker.color.b = 0.0f;
            marker.color.a = 1.0;

            graspPubMarker_.publish(marker);

            // geometry_msgs::PoseStamped location_ps;
            // location_ps.header.frame_id = "/map";
            // location_ps.header.stamp = marker.header.stamp;
            // location_ps.pose.position.x = locationPt.x;
            // location_ps.pose.position.y = locationPt.y;
            // location_ps.pose.position.z = locationPt.z;
            // location_ps.pose.orientation.w = 1; // to rotate x axis to z axis, so the rotation angle = -90 deg
            // location_ps.pose.orientation.x = 0;
            // location_ps.pose.orientation.y = 0;
            // location_ps.pose.orientation.z = 0;
            // graspPubLocation_.publish(location_ps);

            geometry_msgs::PoseStamped grasp_ps;
            grasp_ps.header = marker.header;
            grasp_ps.pose.position.x = graspPt.x;
            grasp_ps.pose.position.y = graspPt.y;
            grasp_ps.pose.position.z = graspPt.z;
            grasp_ps.pose.orientation.w = sqrt(0.5); // to rotate x axis to z axis, so the rotation angle = -90 deg
            grasp_ps.pose.orientation.x = 0;
            grasp_ps.pose.orientation.y = -sqrt(0.5);
            grasp_ps.pose.orientation.z = 0;
            graspPubPose_.publish(grasp_ps);
        }
    else
        {
            ROS_WARN_THROTTLE(5, "Failed to generate grasp plan.\n");
        }
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "drv_grasp");

    ros::NodeHandle nh;

    graspPubMarker_ = nh.advertise<visualization_msgs::Marker>("grasp/marker", 1);
    graspPubStatus_ = nh.advertise<std_msgs::Bool>("status/grasp/feedback", 1);
    graspPubCloud_ = nh.advertise<sensor_msgs::PointCloud2>("grasp/pointcloud", 1);
    graspPubPose_ = nh.advertise<geometry_msgs::PoseStamped>("grasp/pose", 1);
    graspPubLocation_ = nh.advertise<geometry_msgs::PoseStamped>("grasp/location", 1);

    tf2_ros::Buffer tfBufferC_;
    tf2_ros::TransformListener tfListenerC_(tfBufferC_);
    tf2_ros::Buffer tfBufferM_;
    tf2_ros::TransformListener tfListenerM_(tfBufferM_);

    ros::Subscriber sub_track = nh.subscribe<drv_msgs::recognized_target>("track/recognized_target", 1, trackResultCallback);
    ros::Subscriber sub_clouds = nh.subscribe<PointCloud>("/point_cloud", 1, cloudCallback);

    ROS_INFO("Grasp planning function initialized!\n");

    while (ros::ok())
        {
            if (ros::param::has(param_running_mode))
                {
                    ros::param::get(param_running_mode, modeTypeTemp_);
                    if (modeType_ == m_wander && modeTypeTemp_ != m_wander)
                        posePublished_ = false;
                    modeType_ = modeTypeTemp_;
                }

            std_msgs::Bool flag;
            flag.data = true;

            if (modeType_ != m_track)
                continue;

            try
            {
                // the first frame is the target frame
                trans_c_ = tfBufferC_.lookupTransform(camera_base_frame_, camera_optical_frame_, ros::Time(0));
                // trans_m_ = tfBufferM_.lookupTransform(target_location_frame_, camera_optical_frame_, ros::Time(0));
            }
            catch (tf2::TransformException &ex)
            {
                ROS_WARN("%s",ex.what());
                ros::Duration(1.0).sleep();
                continue;
            }

            ros::spinOnce();

            flag.data = hasGraspPlan_;
            graspPubStatus_.publish(flag);
        }

    return 0;
}
