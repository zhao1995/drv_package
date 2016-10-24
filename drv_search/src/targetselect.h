#ifndef TARGETSELECT_H
#define TARGETSELECT_H

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <std_msgs/UInt16MultiArray.h>

#include <sensor_msgs/Image.h>
#include <image_transport/image_transport.h>

#include <cv_bridge/cv_bridge.h>
#include <cv.h>

#include <drv_msgs/user_select.h>
#include <drv_msgs/recognize.h>

class TargetSelect
{
public:
    TargetSelect();

    int select(std::string targetLabel, drv_msgs::recognizeResponse response, sensor_msgs::Image img_in, cv_bridge::CvImagePtr &img_out, int &choosed_id);

private:
    ros::NodeHandle nh;
    image_transport::ImageTransport it_;
    image_transport::Publisher searchPubImage_; // publish labeled image for user judgement
    ros::ServiceClient client;

    int callService(int num);
    void paintTarget(cv::Mat &img, int id, int fc, std::vector<std_msgs::UInt16MultiArray> box_array);

    inline std::string intToString(int number)
    {
        std::stringstream ss;
        ss << number;
        return ss.str();
    }
};

#endif // TARGETSELECT_H