#!/usr/bin/env python
import numpy as np
import cv2
import roslib
import rospy
import tf
import struct
import math
import time
from sensor_msgs.msg import Image
from sensor_msgs.msg import CameraInfo
from geometry_msgs.msg import PoseArray, Point
from visualization_msgs.msg import Marker, MarkerArray
from robotx_msgs.msg import PCL_points, ObjectPose, ObjectPoseList
import rospkg
from cv_bridge import CvBridge, CvBridgeError
from PIL import Image


class pcl2img():
	def __init__(self):
		self.my_path = "/home/arg_ws3/david_trainings/images/totem/black/"
		self.node_name = rospy.get_name()
		rospy.loginfo("[%s] Initializing " %(self.node_name))
		rospy.Subscriber('/obj_list/roi', ObjectPoseList, self.call_back)
		#rospy.Subscriber('/pcl_array', PoseArray, self.call_back)
		self.bridge = CvBridge()
		self.boundary = 50
		self.height = 480
		self.width = 480
		self.point_size = 4	# must be integer
		self.image = np.zeros((int(self.height), int(self.width), 3), np.uint8)
		self.index = 0

	def call_back(self, msg):
		obj_list = ObjectPoseList()
		obj_list = msg
		cluster_num = obj_list.size
		#cluster_num = len(tf_points.list)
		#pcl_size = len(msg.poses)
		for i in range(cluster_num):
			self.get_roi_image(obj_list.list[i].img)
			tf_points = PoseArray()
			tf_points = obj_list.list[i].pcl_points
			centroids = Point()
			centroids = obj_list.list[i].centroid
			self.image = np.zeros((int(self.height), int(self.width), 3), np.uint8)
			plane_xy = []
			plane_yz = []
			plane_xz = []
			pcl_size = len(tf_points.poses)

			# ======= Coordinate transform for better project performance ======
			position = [0, 0, 0]
			rad = math.atan2(centroids.y, centroids.x)
			quaternion = tf.transformations.quaternion_from_euler(0., 0., -rad)
			transformer = tf.TransformerROS()
			transpose_matrix = transformer.fromTranslationRotation(position, quaternion)
			for m in range(pcl_size):
				new_x = tf_points.poses[m].position.x
				new_y = tf_points.poses[m].position.y
				new_z = tf_points.poses[m].position.z
				orig_point = np.array([new_x, new_y, new_z, 1])
				new_center = np.dot(transpose_matrix, orig_point)
				tf_points.poses[m].position.x = new_center[0]
				tf_points.poses[m].position.y = new_center[1]
				tf_points.poses[m].position.z = new_center[2]

			# ======= project to XY, YZ, XZ plane =======
			for j in range(pcl_size):
				plane_xy.append([tf_points.poses[j].position.x, tf_points.poses[j].position.y])
				plane_yz.append([tf_points.poses[j].position.y, tf_points.poses[j].position.z])
				plane_xz.append([tf_points.poses[j].position.x, tf_points.poses[j].position.z])
			self.toIMG(pcl_size, plane_xy, 'xy')
			self.toIMG(pcl_size, plane_yz, 'yz')
			self.toIMG(pcl_size, plane_xz, 'xz')
			#cv2.imwrite( "Image.jpg", self.image)
			cv2.imwrite( "pcl/Image" + str(self.index) + ".jpg", self.image)
			self.index = self.index + 1
			print "Save image"
		rospy.sleep(0.6)

	def get_roi_image(self, ros_img):
		if ros_img != None and ros_img.height != 0 and ros_img.width!=0:
			try:
				cv_image = self.bridge.imgmsg_to_cv2(ros_img, "bgr8")
				img = self.resize_keep_ratio(cv_image, self.width, self.height)
				cv2.imwrite( "roi/Image" + str(self.index) + ".jpg", img)
			except CvBridgeError as e:
				print (e)
		else:
			print "No camera image, fill with black image!!!"
			black_img = np.zeros((self.height, self.width, 3), np.uint8)
			cv2.imwrite( "roi/Image" + str(self.index) + ".jpg", black_img)

	def resize_keep_ratio(self, img, width, height, fill_color=(0, 0, 0, 0)):
		#======= Make sure image is smaller than background =======
		h, w, channel = img.shape
		h_ratio = float(float(height)/float(h))
		w_ratio = float(float(width)/float(w))
		if h_ratio <= 1 or w_ratio <= 1:
			ratio = h_ratio
			if h_ratio > w_ratio:
				ratio = w_ratio
			img = cv2.resize(img,(int(ratio*w),int(ratio*h)))
		#======== Paste image to background =======
		im = Image.fromarray(np.uint8(img))
		x, y = im.size
		new_im = Image.new('RGBA', (width, height), fill_color)
		new_im.paste(im, ((width - x) / 2, (height - y) / 2))
		return np.asarray(new_im)

	def toIMG(self, pcl_size, pcl_array, plane):
		min_m = 10e5
		min_n = 10e5
		max_m = -10e5
		max_n = -10e5
		for i in range(pcl_size):
			if min_m > pcl_array[i][0]:
				min_m = pcl_array[i][0]
			if min_n > pcl_array[i][1]:
				min_n = pcl_array[i][1]
			if max_m < pcl_array[i][0]:
				max_m = pcl_array[i][0]
			if max_n < pcl_array[i][1]:
				max_n = pcl_array[i][1]

		m_size = max_m - min_m
		n_size = max_n - min_n
		max_size = None
		min_size = None
		shift_m = False
		shift_n = False
		if m_size > n_size:
			max_size = m_size
			min_size = n_size
			shift_n = True
		else:
			max_size = n_size
			min_size = m_size
			shift_m = True
		scale = float((self.height-self.boundary*2)/max_size)
		shift_size = int(round((self.height - self.boundary*2 - min_size*scale)/2))
		img = np.zeros((int(self.height), int(self.width), 3), np.uint8)
		for i in range(pcl_size):
			if shift_m:
				pcl_array[i][0] = int(round((pcl_array[i][0] - min_m)*scale)) + shift_size + self.boundary
				pcl_array[i][1] = int(round((pcl_array[i][1] - min_n)*scale)) + self.boundary
			elif shift_n:
				pcl_array[i][0] = int(round((pcl_array[i][0] - min_m)*scale)) + self.boundary
				pcl_array[i][1] = int(round((pcl_array[i][1] - min_n)*scale)) + shift_size + self.boundary
			for m in range(-self.point_size, self.point_size + 1):
				for n in range(-self.point_size, self.point_size + 1):
					img[pcl_array[i][0] + m  , pcl_array[i][1] + n] = (0,255,255)
					if plane == 'xz':
						self.image[pcl_array[i][0] + m  , pcl_array[i][1] + n][0] = 255
					elif plane == 'yz':
						self.image[pcl_array[i][0] + m  , pcl_array[i][1] + n][1] = 255
					elif plane == 'xy':
						self.image[pcl_array[i][0] + m  , pcl_array[i][1] + n][2] = 255
		#cv2.imwrite( "Image_" + plane + ".jpg", img )

if __name__ == '__main__':
	rospy.init_node('pcl2img')
	foo = pcl2img()
	rospy.spin()