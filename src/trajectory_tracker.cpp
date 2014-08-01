#include <ros/ros.h>
#include <math.h>

#include <geometry_msgs/Twist.h>
#include <trajectory_tracker/TrajectoryTrackerStatus.h>
#include <nav_msgs/Path.h>
#include <tf/transform_listener.h>

class tracker
{
public:
	tracker();
	~tracker();
	void spin();
private:
	std::string topicPath;
	std::string topicCmdVel;
	std::string frameRobot;
	double hz;
	double lookForward;
	double curvForward;
	double k[3];
	double d_lim;
	double d_stop;
	double vel[2];
	double acc[2];
	double w;
	double v;
	double dec;
	double rotate_ang;
	double angFactor;
	double swDist;
	double goalToleranceDist;
	double goalToleranceAng;
	int pathStep;
	int pathStepDone;
	bool outOfLineStrip;

	ros::NodeHandle nh;
	ros::Subscriber subPath;
	ros::Publisher pubVel;
	ros::Publisher pubStatus;
	ros::Publisher pubTracking;
	tf::TransformListener tf;

	nav_msgs::Path path;

	void cbPath(const nav_msgs::Path::ConstPtr& msg);
	void control();
};

template<typename T>
class average
{
public:
	average():
		sum()
	{
		num = 0;
	};
	void operator +=(const T &val)
	{
		sum += val;
		num ++;
	};
	operator T()
	{
		if(num == 0) return 0;
		return sum / num;
	};
private:
	T sum;
	int num;
};

tracker::tracker():
	nh("~")
{
	nh.param("frame_robot", frameRobot, std::string("base_link"));
	nh.param("path", topicPath, std::string("path"));
	nh.param("cmd_vel", topicCmdVel, std::string("cmd_vel"));
	nh.param("hz", hz, 50.0);
	nh.param("look_forward", lookForward, 0.5);
	nh.param("curv_forward", curvForward, 0.5);
	nh.param("k_dist", k[0], 1.0);
	nh.param("k_ang", k[1], 1.0);
	nh.param("k_avel", k[2], 1.0);
	nh.param("k_dcel", dec, 0.2);
	nh.param("dist_lim", d_lim, 0.5);
	nh.param("dist_stop", d_stop, 2.0);
	nh.param("rotate_ang", rotate_ang, M_PI / 4);
	nh.param("max_vel", vel[0], 0.5);
	nh.param("max_angvel", vel[1], 1.0);
	nh.param("max_acc", acc[0], 1.0);
	nh.param("max_angacc", acc[1], 2.0);
	nh.param("path_step", pathStep, 1);
	nh.param("distance_angle_factor", angFactor, 0.0);
	nh.param("switchback_dist", swDist, 0.3);
	nh.param("goal_tolerance_dist", goalToleranceDist, 0.2);
	nh.param("goal_tolerance_ang", goalToleranceAng, 0.1);

	subPath = nh.subscribe(topicPath, 200, &tracker::cbPath, this);
	pubVel = nh.advertise<geometry_msgs::Twist>(topicCmdVel, 10);
	pubStatus = nh.advertise<trajectory_tracker::TrajectoryTrackerStatus>("status", 10);
	pubTracking = nh.advertise<geometry_msgs::PoseStamped>("tracking", 10);
}
tracker::~tracker()
{
	geometry_msgs::Twist cmd_vel;
	cmd_vel.linear.x = 0;
	cmd_vel.angular.z = 0;
	pubVel.publish(cmd_vel);
}

float dist2d(geometry_msgs::Point &a, geometry_msgs::Point &b)
{
	return sqrtf(powf(a.x-b.x,2) + powf(a.y-b.y,2));
}
float len2d(geometry_msgs::Point &a)
{
	return sqrtf(powf(a.x,2) + powf(a.y,2));
}
float len2d(geometry_msgs::Point a)
{
	return sqrtf(powf(a.x,2) + powf(a.y,2));
}
float curv3p(geometry_msgs::Point &a, geometry_msgs::Point &b, geometry_msgs::Point &c)
{
	float ret;
	ret = 2 * (a.x*b.y + b.x*c.y + c.x*a.y - a.x*c.y - b.x*a.y - c.x*b.y);
	ret /= sqrtf( (powf(b.x-a.x, 2) + powf(b.y-a.y, 2)) * (powf(b.x-c.x, 2) + powf(b.y-c.y, 2)) * (powf(c.x-a.x, 2) + powf(c.y-a.y, 2)) );

	return ret;
}
float cross2d(geometry_msgs::Point &a, geometry_msgs::Point &b) 
{
	return a.x*b.y - a.y*b.x;
}
float cross2d(geometry_msgs::Point a, geometry_msgs::Point b) 
{
	return a.x*b.y - a.y*b.x;
}
float dot2d(geometry_msgs::Point &a, geometry_msgs::Point &b) 
{
	return a.x*b.x + a.y*b.y;
}
float dot2d(geometry_msgs::Point a, geometry_msgs::Point b) 
{
	return a.x*b.x + a.y*b.y;
}
geometry_msgs::Point point2d(float x, float y)
{
	geometry_msgs::Point ret;
	ret.x = x;
	ret.y = y;
	return ret;
}
geometry_msgs::Point sub2d(geometry_msgs::Point &a, geometry_msgs::Point &b)
{
	geometry_msgs::Point ret;
	ret.x = a.x - b.x;
	ret.y = a.y - b.y;
	return ret;
}
float sign(float a)
{
	if(a < 0) return -1;
	return 1;
}
float dist2d_line(geometry_msgs::Point &a, geometry_msgs::Point &b, geometry_msgs::Point &c)
{
	return (cross2d(sub2d(b, a), sub2d(c, a)) / dist2d(b, a));
}
float dist2d_linestrip(geometry_msgs::Point &a, geometry_msgs::Point &b, geometry_msgs::Point &c)
{
	if(dot2d(sub2d(b, a), sub2d(c, a) ) <= 0) return dist2d(c, a) + 0.001;
	if(dot2d(sub2d(a, b), sub2d(c, b) ) <= 0) return -dist2d(c, b) - 0.001;
	return fabs( dist2d_line(a, b, c) );
}
geometry_msgs::Point projection2d(geometry_msgs::Point &a, geometry_msgs::Point &b, geometry_msgs::Point &c)
{
	float r = dot2d(sub2d(b, a), sub2d(c, a)) / pow(len2d(sub2d(b, a)), 2);
	geometry_msgs::Point ret;
	ret.x = b.x*r + a.x*(1-r);
	ret.y = b.y*r + a.y*(1-r);
	return ret;
}


void tracker::cbPath(const nav_msgs::Path::ConstPtr& msg)
{
	path = *msg;
	pathStepDone = 0;
}

void tracker::spin()
{
	ros::Rate loop_rate(hz);

	while(ros::ok())
	{
		control();
		ros::spinOnce();
		loop_rate.sleep();
	}
}

void tracker::control()
{
	trajectory_tracker::TrajectoryTrackerStatus status;
	status.header.stamp = ros::Time::now();
	status.header.seq = path.header.seq;
	status.distance_remains = 0.0;
	status.angle_remains = 0.0;

	if(path.header.frame_id.size() == 0 ||
			path.poses.size() < 3)
	{
		geometry_msgs::Twist cmd_vel;
		cmd_vel.linear.x = 0;
		cmd_vel.angular.z = 0;
		pubVel.publish(cmd_vel);
		status.status = trajectory_tracker::TrajectoryTrackerStatus::NO_PATH;
		pubStatus.publish(status);
		return;
	}
	// Transform
	nav_msgs::Path lpath;
	lpath.header = path.header;
	try
	{
		tf::StampedTransform transform;
		ros::Time now = ros::Time(0);
		tf.waitForTransform(frameRobot, path.header.frame_id, now, ros::Duration(0.1));
		
		for(int i = 0; i < (int)path.poses.size(); i += pathStep)
		{
			geometry_msgs::PoseStamped pose;
			tf.transformPose(frameRobot, now, path.poses[i], path.header.frame_id, pose);
			lpath.poses.push_back(pose);
		}
	}
	catch (tf::TransformException &e)
	{
		ROS_WARN("TF exception: %s", e.what());
		status.status = trajectory_tracker::TrajectoryTrackerStatus::NO_PATH;
		pubStatus.publish(status);
		return;
	}
	float minDist = FLT_MAX;
	int iclose = 0;
	geometry_msgs::Point origin;
	origin.x = cos(w * lookForward / 2.0) * v * lookForward;
	origin.y = sin(w * lookForward / 2.0) * v * lookForward;
	// Find nearest line strip
	outOfLineStrip = false;
	for(int i = pathStepDone; i < (int)lpath.poses.size(); i ++)
	{
		if(i <= 1) continue;
		float d = dist2d_linestrip(lpath.poses[i-1].pose.position, lpath.poses[i].pose.position, origin);
		float anglePose = tf::getYaw(lpath.poses[i-1].pose.orientation);
		float dplus = (1 - cosf(anglePose)) * 0.5 * angFactor * sign(v);
		if(fabs(v) < 0.001) dplus = 0;
		if(fabs(d) + dplus < fabs(minDist))
		{
			minDist = d;
			iclose = i;
		}
	}
	if(minDist < 0) outOfLineStrip = true;
	if(iclose < 1)
	{
		status.status = trajectory_tracker::TrajectoryTrackerStatus::NO_PATH;
		pubStatus.publish(status);
		return;
	}
	// Signed distance error
	float dist = dist2d_line(lpath.poses[iclose-1].pose.position, lpath.poses[iclose].pose.position, origin);
	float _dist = dist;
	if(iclose == 0)
	{
		_dist = -dist2d(lpath.poses[iclose].pose.position, origin);
	}
	if(iclose + 1 >= (int)path.poses.size())
	{
		_dist = -dist2d(lpath.poses[iclose].pose.position, origin);
	}
	
	// Angular error
	geometry_msgs::Point vec = sub2d(lpath.poses[iclose].pose.position, lpath.poses[iclose-1].pose.position);
	float angle = -atan2(vec.y, vec.x);
	float anglePose = tf::getYaw(lpath.poses[iclose].pose.orientation);
	float signVel = 1.0;
	if(cos(-angle) * cos(anglePose) + sin(-angle) * sin(anglePose) < 0)
	{
		signVel = -1.0;
		angle = angle + M_PI;
		if(angle > M_PI) angle -= 2.0 * M_PI;
	}
	// Curvature
	average<float> curv;
	geometry_msgs::Point posLine = projection2d(lpath.poses[iclose-1].pose.position, lpath.poses[iclose].pose.position, origin);
	for(int i = iclose - 1; i < (int)lpath.poses.size() - 1; i ++)
	{
		if(dist2d(lpath.poses[i].pose.position, posLine) > curvForward) break;
		if(i > 2)
		{
			geometry_msgs::Point vec = sub2d(lpath.poses[i-1].pose.position, 
					lpath.poses[i-2].pose.position);
			float angle = -atan2(vec.y, vec.x);
			float anglePose = tf::getYaw(lpath.poses[i-1].pose.orientation);
			float signVelPath = 1.0;
			if(cos(-angle) * cos(anglePose) + sin(-angle) * sin(anglePose) < 0)
				signVelPath = -1.0;
			if(signVel * signVelPath < 0)
			{
				// Stop read forward if the path switched back
				curv += 0.0;
				break;
			}
			curv += curv3p(lpath.poses[i-2].pose.position, lpath.poses[i-1].pose.position, lpath.poses[i].pose.position);
		}
		else
			curv += 0.0;
	}
	float remain = dist2d(origin, lpath.poses[iclose+1].pose.position);
	for(int i = iclose+1; i < (int)lpath.poses.size() - 1; i ++)
	{
		remain += dist2d(lpath.poses[i].pose.position, lpath.poses[i+1].pose.position);
	}
	float remainLocal = remain;
	if(outOfLineStrip)
	{
		remainLocal = -dist2d(origin, lpath.poses[iclose+1].pose.position);
		if(iclose + 1 >= (int)path.poses.size())
		{
			remain = remainLocal;
		}
	}
	//printf("d=%.2f, th=%.2f, curv=%.2f\n", dist, angle, (float)curv);

	// Control
	if(dist < -d_lim) dist = -d_lim;
	else if(dist > d_lim) dist = d_lim;

	float dt = 1/hz;
	float _v = v;
	
	v = sign(remainLocal) * signVel * sqrtf(2 * fabs(remainLocal) * acc[0] * 0.95);
	if(fabs(remainLocal) < goalToleranceDist) v = 0;
	
	if(v > vel[0]) v = vel[0];
	else if(v < -vel[0]) v = -vel[0];
	if(v > _v + dt*acc[0]) v = _v + dt*acc[0];
	else if(v < _v - dt*acc[0]) v = _v - dt*acc[0];

	float wref = v * signVel * curv;
	float _w = w;
	
	w += dt * (-dist*k[0] -angle*k[1] -(w - wref)*k[2]);

	if(w > vel[1]) w = vel[1];
	else if(w < -vel[1]) w = -vel[1];
	if(w > _w + dt*acc[1]) w = _w + dt*acc[1];
	else if(w < _w - dt*acc[1]) w = _w - dt*acc[1];

	geometry_msgs::Twist cmd_vel;
	if(!std::isfinite(v)) v = 0;
	if(!std::isfinite(w)) w = 0;

	status.distance_remains = remain;
	status.angle_remains = angle;

	// Too far from given path
	if(fabs(_dist) > d_stop)
	{
		geometry_msgs::Twist cmd_vel;
		cmd_vel.linear.x = 0;
		cmd_vel.angular.z = 0;
		pubVel.publish(cmd_vel);
		//ROS_WARN("Far from given path");
		status.status = trajectory_tracker::TrajectoryTrackerStatus::FAR_FROM_PATH;
		pubStatus.publish(status);
		return;
	}
	// Stop and rotate
	if(fabs(rotate_ang) < M_PI && cos(rotate_ang) > cos(angle))
	{
		w = -sign(angle) * sqrtf(2 * fabs(angle) * acc[1] * 0.95);
		v = 0;
		if(v > vel[0]) v = vel[0];
		else if(v < -vel[0]) v = -vel[0];
		if(v > _v + dt*acc[0]) v = _v + dt*acc[0];
		else if(v < _v - dt*acc[0]) v = _v - dt*acc[0];
		if(w > vel[1]) w = vel[1];
		else if(w < -vel[1]) w = -vel[1];
		if(w > _w + dt*acc[1]) w = _w + dt*acc[1];
		else if(w < _w - dt*acc[1]) w = _w - dt*acc[1];
		//ROS_WARN("Rotate");
	}
	cmd_vel.linear.x = v;
	cmd_vel.angular.z = w;
	pubVel.publish(cmd_vel);
	status.status = trajectory_tracker::TrajectoryTrackerStatus::FOLLOWING;
	if(fabs(status.distance_remains) < goalToleranceDist &&
			fabs(status.angle_remains) < goalToleranceAng)
	{
		status.status = trajectory_tracker::TrajectoryTrackerStatus::GOAL;
	}
	pubStatus.publish(status);
	geometry_msgs::PoseStamped tracking;
	tracking.header = status.header;
	tracking.header.frame_id = frameRobot;
	tracking.pose.position = posLine;
	tracking.pose.orientation = lpath.poses[iclose].pose.orientation;
	pubTracking.publish(tracking);

	pathStepDone = iclose;
	if(pathStepDone < 0) pathStepDone = 0;
}


int main(int argc, char **argv)
{
	ros::init(argc, argv, "trajectory_tracker");

	tracker track;
	track.spin();

	return 0;
}
