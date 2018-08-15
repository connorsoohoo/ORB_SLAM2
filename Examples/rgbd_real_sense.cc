/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.

Usage: 

./csi_camera path_to_vocabulary path_to_settings WIDTH HEIGHT FPS TIME 


*/


#include<iostream>
#include <sstream>
#include<algorithm>
#include<fstream>
#include<chrono>
#include <iomanip>

// include OpenCV header file
#include<opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>

#include<opencv2/core/opengl.hpp>
#include<opencv2/cudacodec.hpp>

#include<System.h>
#include <Utils.hpp>

#include <librealsense2/rs.hpp>
#include <cv-helpers.hpp>

using namespace std;

/*
#define SET_CLOCK(t0) \
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();


#define TIME_DIFF(t1, t0) \
        (std::chrono::duration_cast<std::chrono::duration<double>>((t1) - (t0)).count())
*/

float get_depth_scale(rs2::device dev);
rs2_stream find_stream_to_align(const std::vector<rs2::stream_profile>& streams);
bool profile_changed(const std::vector<rs2::stream_profile>& current, const std::vector<rs2::stream_profile>& prev);

int main(int argc, char **argv)
{

    if(argc < 3)
    {
        cerr << endl << "Usage: ./csi_camera path_to_vocabulary path_to_settings" << endl;
        return 1;
    } else if (argc > 7) {
        cerr << endl << "Usage: ./csi_camera path_to_vocabulary path_to_settings" << endl;
        return 1;
    }

    int WIDTH, HEIGHT, FPS;
    double TIME; 
    if (argc > 3) WIDTH = std::atoi(argv[3]); else WIDTH = 640;  //1280
    if (argc > 4) HEIGHT = std::atoi(argv[4]); else HEIGHT = 480; //720 
    if (argc > 5) FPS = std::atoi(argv[5]); else FPS = 30;
    if (argc > 6) TIME = std::atof(argv[6]); else TIME = 30.0;

    //Contruct a pipeline which abstracts the device
    rs2::pipeline pipe;
    //Calling pipeline's start() without any additional parameters will start the first device
    // with its default streams.
    //The start function returns the pipeline profile which the pipeline used to start the device
    //rs2::pipeline_profile profile = pipe.start();

    // Each depth camera might have different units for depth pixels, so we get it here
    // Using the pipeline's profile, we can retrieve the device that the pipeline uses
    //float depth_scale = get_depth_scale(profile.get_device());

    //Pipeline could choose a device that does not have a color stream
    //If there is no color stream, choose to align depth to another stream
    //rs2_stream align_to = find_stream_to_align(profile.get_streams());

    // Create a rs2::align object.
    // rs2::align allows us to perform alignment of depth frames to others frames
    //The "align_to" is the stream type to which we plan to align depth frames.
    //rs2::align align(align_to);

    //Create a configuration for configuring the pipeline with a non default profile
    rs2::config cfg;

    //Add desired streams to configuration
    cfg.enable_stream(RS2_STREAM_INFRARED, 1280, 720, RS2_FORMAT_Y8, 30);
    cfg.enable_stream(RS2_STREAM_DEPTH, 1280, 720, RS2_FORMAT_Z16, 30);

    //cfg.enable_stream(RS2_STREAM_INFRARED, 1, WIDTH, HEIGHT, RS2_FORMAT_Y8, FPS);
    //cfg.enable_stream(RS2_STREAM_INFRARED, 2, WIDTH, HEIGHT, RS2_FORMAT_Y8, FPS);

    //Instruct pipeline to start streaming with the requested configuration
    //Instruct pipeline to start streaming with the requested configuration
    rs2::pipeline_profile selection = pipe.start(cfg);
    auto depth_stream = selection.get_stream(RS2_STREAM_DEPTH)
                             .as<rs2::video_stream_profile>();
    auto resolution = std::make_pair(depth_stream.width(), depth_stream.height());
    auto i = depth_stream.get_intrinsics();
    auto principal_point = std::make_pair(i.ppx, i.ppy);
    auto focal_length = std::make_pair(i.fx, i.fy);

    //std::cout << "Width: " << resolution[0] << "Height: " << resolution[1] << std::endl;
    std::cout << "ppx: " << i.ppx << " ppy: " << i.ppy << std::endl;
    std::cout << "fx: " << i.fx << " fy: " << i.fy << std::endl;
    std::cout << "k1: " << i.coeffs[0] << " k2: " << i.coeffs[1] << " p1: " << i.coeffs[2] << " p2: " << i.coeffs[3] << " k3: " << i.coeffs[4] << std::endl;

    // Camera warmup - dropping several first frames to let auto-exposure stabilize
    rs2::frameset frames;
    for(int i = 0; i < 30; i++)
    {
        //Wait for all configured streams to produce a frame
        frames = pipe.wait_for_frames();
    }

    bool bUseViz = true;

    // Create SLAM system. It initializes all system threads and gets ready to process frames.
    ORB_SLAM2::System SLAM(argv[1],argv[2],ORB_SLAM2::System::RGBD,bUseViz);


    cout << endl << "-------" << endl;
    cout << "Start processing sequence ..." << endl;


    double tsum = 0;
    double tbuf[10] = {0.0};
    int tpos = 0;
    double trackTimeSum = 0.0;
    // Main loop
    cv::Mat im;

    //cv::cuda::GpuMat im2;
    //cv::Ptr<cv::cudacodec::VideoReader> d_reader = cv::cudacodec::createVideoReader(pipeline, cv::CAP_GSTREAMER);
    // If I want to feed a GpuMat directly to the frame 

    SET_CLOCK(t0);
    int frameNumber = 0;
    while (true) {

      //Get each frame
      frames = pipe.wait_for_frames();
      rs2::video_frame ir_frame = frames.first(RS2_STREAM_INFRARED);
      rs2::depth_frame d_frame = frames.get_depth_frame();

      SET_CLOCK(t1);

      cv::Mat infared = frame_to_mat(ir_frame);
      cv::Mat depth = depth_frame_to_meters(pipe, d_frame);
      // get left and right infrared frames from frameset
      //rs2::video_frame ir_frame_left = frames.get_infrared_frame(1);
      //rs2::video_frame ir_frame_right = frames.get_infrared_frame(2);

      //cv::Mat dMat_left = cv::Mat(cv::Size(WIDTH, HEIGHT), CV_8UC1, (void*)ir_frame_left.get_data());
      //cv::Mat dMat_right = cv::Mat(cv::Size(WIDTH, HEIGHT), CV_8UC1, (void*)ir_frame_right.get_data());
 
      //if (im.empty()) continue;

      // rs2::pipeline::wait_for_frames() can replace the device it uses in case of device error or disconnection.
      // Since rs2::align is aligning depth to some other stream, we need to make sure that the stream was not changed
      //  after the call to wait_for_frames();
      /*
      if (profile_changed(pipe.get_active_profile().get_streams(), profile.get_streams()))
      {
          //If the profile was changed, update the align object, and also get the new device's depth scale
          profile = pipe.get_active_profile();
          align_to = find_stream_to_align(profile.get_streams());
          align = rs2::align(align_to);
          depth_scale = get_depth_scale(profile.get_device());
      }
      */

      //Get processed aligned frame
      //auto processed = align.process(frames);

      // Trying to get both other and aligned depth frames
      //rs2::video_frame other_frame = processed.first(align_to);
      //rs2::depth_frame aligned_depth_frame = processed.get_depth_frame();

      //If one of them is unavailable, continue iteration
      //if (!aligned_depth_frame || !other_frame)
      //{
      //    continue;
      //}
      
      double tframe = TIME_DIFF(t1, t0);
      if (tframe > TIME) {
        break;
      }

      //cv::Mat infared = frame_to_mat(other_frame);
      //cv::Mat depth = frame_to_mat(aligned_depth_frame);

      PUSH_RANGE("Track image", 4);
      // Pass the image to the SLAM system
      //SLAM.TrackMonocular(dMat_left,tframe);
      //SLAM.TrackStereo(dMat_left, dMat_right, tframe);
      SLAM.TrackRGBD(infared, depth, tframe);
      POP_RANGE;
      SET_CLOCK(t2);

      double trackTime = TIME_DIFF(t2, t1);
      trackTimeSum += trackTime;
      tsum = tframe - tbuf[tpos];
      tbuf[tpos] = tframe;
      tpos = (tpos + 1) % 10;
      //cerr << "Frame " << frameNumber << " : " << tframe << " " << trackTime << " " << 10 / tsum << "\n";
      ++frameNumber;
    }

    cerr << "Mean track time: " << trackTimeSum / frameNumber << " , mean fps: " << frameNumber / TIME << "\n";

    // Stop all threads
    SLAM.Shutdown();




    // Save camera trajectory
    SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");

    return 0;
}

float get_depth_scale(rs2::device dev)
{
    // Go over the device's sensors
    for (rs2::sensor& sensor : dev.query_sensors())
    {
        // Check if the sensor if a depth sensor
        if (rs2::depth_sensor dpt = sensor.as<rs2::depth_sensor>())
        {
            return dpt.get_depth_scale();
        }
    }
    throw std::runtime_error("Device does not have a depth sensor");
}

rs2_stream find_stream_to_align(const std::vector<rs2::stream_profile>& streams)
{
    //Given a vector of streams, we try to find a depth stream and another stream to align depth with.
    //We prioritize color streams to make the view look better.
    //If color is not available, we take another stream that (other than depth)
    rs2_stream align_to = RS2_STREAM_ANY;
    bool depth_stream_found = false;
    bool color_stream_found = false;
    for (rs2::stream_profile sp : streams)
    {
        rs2_stream profile_stream = sp.stream_type();
        if (profile_stream != RS2_STREAM_DEPTH)
        {
            if (!color_stream_found)         //Prefer color
                align_to = profile_stream;

            if (profile_stream == RS2_STREAM_COLOR)
            {
                color_stream_found = true;
            }
        }
        else
        {
            depth_stream_found = true;
        }
    }

    if(!depth_stream_found)
        throw std::runtime_error("No Depth stream available");

    if (align_to == RS2_STREAM_ANY)
        throw std::runtime_error("No stream found to align with Depth");

    return align_to;
}

bool profile_changed(const std::vector<rs2::stream_profile>& current, const std::vector<rs2::stream_profile>& prev)
{
    for (auto&& sp : prev)
    {
        //If previous profile is in current (maybe just added another)
        auto itr = std::find_if(std::begin(current), std::end(current), [&sp](const rs2::stream_profile& current_sp) { return sp.unique_id() == current_sp.unique_id(); });
        if (itr == std::end(current)) //If it previous stream wasn't found in current
        {
            return true;
        }
    }
    return false;
}

