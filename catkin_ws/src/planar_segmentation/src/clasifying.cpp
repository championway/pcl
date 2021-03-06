#include <ros/ros.h>
#include <cmath>        // std::abs
#include <sensor_msgs/PointCloud2.h>
#include "pcl_ros/point_cloud.h"
#include <pcl/io/io.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/filters/filter.h>
#include <pcl/ModelCoefficients.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/features/normal_3d.h>
#include <pcl/kdtree/kdtree.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>

typedef pcl::PointCloud<pcl::PointXYZ> PointCloudXYZ;
typedef pcl::PointCloud<pcl::PointXYZRGB> PointCloudXYZRGB;
PointCloudXYZ::Ptr cloud_inXYZ (new PointCloudXYZ);
PointCloudXYZRGB::Ptr cloud_in (new PointCloudXYZRGB); 
PointCloudXYZRGB::Ptr cloud_filtered (new PointCloudXYZRGB);
PointCloudXYZRGB::Ptr cloud_f (new PointCloudXYZRGB);
PointCloudXYZRGB::Ptr cloud_plane (new PointCloudXYZRGB);
PointCloudXYZRGB::Ptr result (new PointCloudXYZRGB);
sensor_msgs::PointCloud2 ros_out;

ros::Publisher pub_XYZRGB;
ros::Publisher pointcloud2_publisher;
bool lock = false;
void cluster_pointcloud(void);

void cloud_cb(const sensor_msgs::PointCloud2ConstPtr& input)
{
  if (!lock){
    lock = true;
    // covert from ros type to pcl type
    pcl::fromROSMsg (*input, *cloud_inXYZ);
    copyPointCloud(*cloud_inXYZ, *cloud_in);
    for (size_t i = 0; i < cloud_in->points.size(); i++){
      cloud_in->points[i].r = 0;
      cloud_in->points[i].g = 0;
      cloud_in->points[i].b = 0;
    }
    cluster_pointcloud();
  }
  else{
    std::cout << "lock" << std::endl;
  }
}

int point_cloud_color(int input){
  if(input < 0){
    return 0;
  }
  else if(input > 255){
    return 255;
    std::cout << "NO" << std::endl;
  }
  else{
    return input;
  }
}

void max_min(float &i, float &max, float &min)
{
  if(i > max){max = i;}
  if(i < min){min = i;}
}
float min_point(float i, float min)
{
  if(i < min){return i;}
  else{return min;}
}

//void cloud_cb(const sensor_msgs::PointCloud2ConstPtr& input)
void cluster_pointcloud()
{
  // Remove NaN point
  std::vector<int> indices;
  pcl::removeNaNFromPointCloud(*cloud_in, *cloud_in, indices);

  // Create the filtering object: downsample the dataset using a leaf size of 1cm
  pcl::VoxelGrid<pcl::PointXYZRGB> vg;
  vg.setInputCloud (cloud_in);
  vg.setLeafSize (0.04f, 0.04f, 0.04f);
  vg.filter (*cloud_filtered);
  std::cout << "Filtering successfully" << std::endl;

  // Create the segmentation object for the planar model and set all the parameters
  pcl::SACSegmentation<pcl::PointXYZRGB> seg;
  pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
  pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
  pcl::PCDWriter writer;
  seg.setOptimizeCoefficients (true);
  seg.setModelType (pcl::SACMODEL_PLANE);
  seg.setMethodType (pcl::SAC_RANSAC);
  seg.setMaxIterations (100);
  seg.setDistanceThreshold (0.12);

  int i=0, nr_points = (int) cloud_filtered->points.size ();
  while (cloud_filtered->points.size () > 0.2 * nr_points)
  {
    // Segment the largest planar component from the remaining cloud
    seg.setInputCloud (cloud_filtered);
    seg.segment (*inliers, *coefficients);
    if (inliers->indices.size () == 0)
    {
      std::cout << "Could not estimate a planar model for the given dataset." << std::endl;
      //break;
    }

    // Extract the planar inliers from the input cloud
    pcl::ExtractIndices<pcl::PointXYZRGB> extract;
    extract.setInputCloud (cloud_filtered);
    extract.setIndices (inliers);
    extract.setNegative (false);

    // Get the points associated with the planar surface
    extract.filter (*cloud_plane);

    // Remove the planar inliers, extract the rest
    extract.setNegative (true);
    extract.filter (*cloud_f);
    *cloud_filtered = *cloud_f;
  }

  // Creating the KdTree object for the search method of the extraction
  pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZRGB>);
  tree->setInputCloud (cloud_filtered);

  std::vector<pcl::PointIndices> cluster_indices;
  pcl::EuclideanClusterExtraction<pcl::PointXYZRGB> ec;
  ec.setClusterTolerance (0.2); // 2cm
  ec.setMinClusterSize (100);
  ec.setMaxClusterSize (55000);
  ec.setSearchMethod (tree);
  ec.setInputCloud (cloud_filtered);
  ec.extract (cluster_indices);

  int j = 0;
  int a = 0;
  int set_r=0, set_g=0, set_b=0;
  int start_index = 0;
  for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin (); it != cluster_indices.end (); ++it)
  {
    float max_x, max_y, max_z;
    float min_x, min_y, min_z;
    Eigen::Vector4f centroid;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cluster (new pcl::PointCloud<pcl::PointXYZRGB>);
    for (std::vector<int>::const_iterator pit = it->indices.begin (); pit != it->indices.end (); ++pit)
    {
      cloud_cluster->points.push_back (cloud_filtered->points[*pit]); //*
      result->points.push_back(cloud_filtered->points[*pit]);
      max_min(cloud_filtered->points[*pit].x, max_x, min_x);
      max_min(cloud_filtered->points[*pit].y, max_y, min_y);
      max_min(cloud_filtered->points[*pit].z, max_z, min_z);
    }
    pcl::compute3DCentroid(*cloud_cluster, centroid);
    std::cout << centroid << std::endl;
    //cloud_cluster->clear();

    set_r = point_cloud_color(int(255 - std::abs(30*centroid[1])));
    set_g = point_cloud_color(int(std::abs(30*centroid[1])));
    set_b = 0;
    for (int i = start_index; i < result->points.size(); ++i)
    {
      result->points[i].r = set_r;
      result->points[i].g = set_g;
      result->points[i].b = set_b;
    }
    start_index = result->points.size();
    a+=30;
    j++;
  }
  result->header.frame_id = cloud_in->header.frame_id;
  //writer.write<pcl::PointXYZRGB> ("result.pcd", *cloud_filtered, false);
  std::cout << j << std::endl << "Finish" << std::endl << std::endl;
  pub_XYZRGB.publish(*result);
  pcl::toROSMsg(*result, ros_out);
  pointcloud2_publisher.publish(ros_out);
  lock = false;
  result->clear();
}

int main (int argc, char** argv)
{
     // Initialize ROS
     ros::init (argc, argv, "cluster_extraction");
     ros::NodeHandle nh;
     //cluster();
     // Create a ROS subscriber for the input point cloud
     ros::Subscriber sub = nh.subscribe<sensor_msgs::PointCloud2> ("/velodyne_points", 1, cloud_cb);
     // Create a ROS publisher for the output point cloud
     //pub = nh.advertise<sensor_msgs::PointCloud2> ("output", 1); 
     pub_XYZRGB = nh.advertise<PointCloudXYZRGB> ("/ros_pointcloudxyz", 1);
     pointcloud2_publisher = nh.advertise<sensor_msgs::PointCloud2> ("/pcl_ros", 1);
     // Spin
     ros::spin ();
}