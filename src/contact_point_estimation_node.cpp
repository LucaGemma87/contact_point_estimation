/*
 *  contact_point_estimation_node.cpp
 *
 *
 *  Created on: Jan 14, 2014
 *  Authors:   Francisco Viña
 *            fevb <at> kth.se
 */

/* Copyright (c) 2014, Francisco Viña, CVAP, KTH
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
      * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * Neither the name of KTH nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL KTH BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <ros/ros.h>
#include <contact_point_estimation/ContactPointEstimator.h>
#include <contact_point_estimation/ContactPointEstimatorParams.h>
#include <contact_point_estimation/SurfaceNormalEstimator.h>
#include <contact_point_estimation/SurfaceNormalEstimatorParams.h>

#include <geometry_msgs/WrenchStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <geometry_msgs/PointStamped.h>
#include <geometry_msgs/Vector3Stamped.h>
#include <std_srvs/Empty.h>

#include <boost/thread.hpp>
#include <boost/bind.hpp>



class ContactPointEstimationNode
{
public:
    ros::NodeHandle n_;

    /// declaration of topics to publish
    ros::Publisher topicPub_ContactPointEstimate_;
    ros::Publisher topicPub_SurfaceNormalEstimate_;

    /// declaration of topics to subscribe, callback is called for new messages arriving
    ros::Subscriber topicSub_FT_compensated_;
    ros::Subscriber topicSub_Twist_FT_Sensor_;

    /// declaration of service servers
    ros::ServiceServer srvServer_Start_;
    ros::ServiceServer srvServer_Stop_;

	ros::Time last_publish_time;
	ContactPointEstimatorParams *cpe_params;
	ContactPointEstimator *cpe;

	SurfaceNormalEstimatorParams *sne_params;
	SurfaceNormalEstimator *sne;


	ContactPointEstimationNode()
	{
		n_ = ros::NodeHandle("~");
        m_received_ft = false;
        m_run_estimator = false;

        cpe_params = NULL;
        cpe = NULL;

        sne_params = NULL;
        sne = NULL;

		topicPub_ContactPointEstimate_ = n_.advertise<geometry_msgs::PointStamped>("contact_point_estimate", 1);
		topicPub_SurfaceNormalEstimate_ = n_.advertise<geometry_msgs::Vector3Stamped>("surface_normal_estimate", 1);

        topicSub_FT_compensated_ = n_.subscribe("/ft_sensor/ft_compensated", 1, &ContactPointEstimationNode::topicCallback_FT_compensated, this);
          
//            topicSub_FT_compensated_ = n_.subscribe("/left_ft_sensor/left/force_torque_sensor_filtered", 1, &ContactPointEstimationNode::topicCallback_FT_compensated, this);
//         topicSub_Twist_FT_Sensor_ = n_.subscribe("/left_arm_twist_estimation/ft_sensor_twist", 1, &ContactPointEstimationNode::topicCallback_Twist_FT_Sensor, this);

        srvServer_Start_ = n_.advertiseService("start", &ContactPointEstimationNode::srvCallback_Start,
				this);
        srvServer_Stop_ = n_.advertiseService("stop", &ContactPointEstimationNode::srvCallback_Stop, this);
	}

	~ContactPointEstimationNode()
	{
		delete cpe_params;
		delete cpe;
		delete sne_params;
		delete sne;
	}

    bool getEstimatorParameters()
	{
		double gamma_r;
		if (n_.hasParam("gamma_r"))
		{
			n_.getParam("gamma_r", gamma_r);
		}

		else
		{
			ROS_ERROR("Parameter gamma_r not set, shutting down node...");
			n_.shutdown();
			return false;
		}

		double kappa_r;
		if (n_.hasParam("kappa_r"))
		{
			n_.getParam("kappa_r", kappa_r);
		}

		else
		{
			ROS_ERROR("Parameter kappa_r not set, shutting down node...");
			n_.shutdown();
			return false;
		}


		double beta_r;
		if (n_.hasParam("beta_r"))
		{
			n_.getParam("beta_r", beta_r);
		}

		else
		{
			ROS_ERROR("Parameter beta_r not set, shutting down node...");
			n_.shutdown();
			return false;
		}


        /// Get initial estimate of the contact point
		XmlRpc::XmlRpcValue initial_r_XmlRpc;
		Vector3d initial_r;
		if (n_.hasParam("initial_r"))
		{
			n_.getParam("initial_r", initial_r_XmlRpc);
		}

		else
		{
			ROS_ERROR("Parameter initial_r not set, shutting down node...");
			n_.shutdown();
			return false;
		}

		if(initial_r_XmlRpc.size()!=3)
		{
			ROS_ERROR("Wrong initial_r size.");
			n_.shutdown();
			return false;
		}

		/// Resize and assign of values to the initial_r
		for (int i = 0; i < initial_r_XmlRpc.size(); i++)
		{
			initial_r(i) = (double)initial_r_XmlRpc[i];
		}

		double cpe_update_frequency;
		if (n_.hasParam("cpe_update_frequency"))
		{
			n_.getParam("cpe_update_frequency", cpe_update_frequency);
		}

		else
		{
			ROS_ERROR("Parameter cpe_update_frequency not set, shutting down node...");
			n_.shutdown();
			return false;
		}

		double gamma_n;
		if (n_.hasParam("gamma_n"))
		{
			n_.getParam("gamma_n", gamma_n);
		}

		else
		{
			ROS_ERROR("Parameter gamma_n not set, shutting down node...");
			n_.shutdown();
			return false;
		}

		double beta_n;
		if (n_.hasParam("beta_n"))
		{
			n_.getParam("beta_n", beta_n);
		}

		else
		{
			ROS_ERROR("Parameter beta_n not set, shutting down node...");
			n_.shutdown();
			return false;
		}

        /// Get initial estimate of the surface normal
		XmlRpc::XmlRpcValue initial_n_XmlRpc;
		Vector3d initial_n;
		if (n_.hasParam("initial_n"))
		{
			n_.getParam("initial_n", initial_n_XmlRpc);
		}

		else
		{
			ROS_ERROR("Parameter initial_n not set, shutting down node...");
			n_.shutdown();
			return false;
		}

		if(initial_n_XmlRpc.size()!=3)
		{
			ROS_ERROR("Wrong initial_n size.");
			n_.shutdown();
			return false;
		}

		/// Resize and assign of values to the initial_n
		for (int i = 0; i < initial_n_XmlRpc.size(); i++)
		{
			initial_n(i) = (double)initial_n_XmlRpc[i];
		}


        double sne_update_frequency;
        if (n_.hasParam("sne_update_frequency"))
        {
            n_.getParam("sne_update_frequency", sne_update_frequency);
        }

        else
        {
            ROS_ERROR("Parameter sne_update_frequency not set, shutting down node...");
            n_.shutdown();
            return false;
        }

		bool ret = true;
        cpe_params = new ContactPointEstimatorParams();
        sne_params = new SurfaceNormalEstimatorParams();

        cpe_params->setGammaR(gamma_r);
        cpe_params->setKappaR(kappa_r);
        cpe_params->setBetaR(beta_r);
        cpe_params->setInitialR(initial_r);

        sne_params->setGammaN(gamma_n);
        sne_params->setBetaN(beta_n);
        sne_params->setInitialN(initial_n);

        cpe_params->setUpdateFrequency(cpe_update_frequency);
        sne_params->setUpdateFrequency(sne_update_frequency);

		return ret;

	}

	void topicCallback_FT_compensated(const geometry_msgs::WrenchStampedPtr &msg)
	{
		m_ft_mutex.lock();
		m_ft_compensated = *msg;
// 		m_ft_compensated.wrench.force.x=-m_ft_compensated.wrench.force.x;
// 		m_ft_compensated.wrench.force.y=-m_ft_compensated.wrench.force.y;
// 		m_ft_compensated.wrench.force.z=-m_ft_compensated.wrench.force.z;
// 		
// 		m_ft_compensated.wrench.torque.x=-m_ft_compensated.wrench.torque.x;
// 		m_ft_compensated.wrench.torque.y=-m_ft_compensated.wrench.torque.y;
// 		m_ft_compensated.wrench.torque.z=-m_ft_compensated.wrench.torque.z;
		
		
		m_ft_mutex.unlock();

		m_received_ft = true;
	}

    void topicCallback_Twist_FT_Sensor(const geometry_msgs::TwistStampedPtr &msg)
    {
    	m_twist_mutex.lock();
    	m_twist_ft_sensor = *msg;
    	m_twist_mutex.unlock();

        m_received_twist = true;
    }

    bool srvCallback_Start(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res)
	{
    	ROS_INFO("Starting cpe + sne node");
    	cpe->reset();
    	sne->reset();

    	getEstimatorParameters();
    	m_run_estimator = true;
    	m_cpe_thread = boost::thread(boost::bind(&ContactPointEstimationNode::CPEThreadFunction, this));
    	//m_sne_thread = boost::thread(boost::bind(&ContactPointEstimationNode::SNEThreadFunction, this));

		return true;
	}

    bool srvCallback_Stop(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res)
    {
    	ROS_INFO("Stopping cpe + sne node");

        m_run_estimator = false;

        m_received_ft = false;
        m_received_twist = false;

        return true;
    }


    bool estimatorRunning()
	{
        return m_run_estimator;
	}

    void CPEThreadFunction()
    {
    	static ros::Rate loop_rate(cpe_params->getUpdateFrequency());
    	for(;;)
    	{
    		if(!m_run_estimator)
    		{
    			return;
    		}

    		else if(!m_received_ft)
    		{
    			static ros::Time t = ros::Time::now();
    			if((ros::Time::now()-t).toSec()>1.0)
    			{
    				ROS_ERROR("Haven't received FT sensor measurements");
    				t = ros::Time::now();
    			}

    		}

    		else if(!n_.ok())
    		{
    			return;
    		}

    		else
    		{
    			m_ft_mutex.lock();
    			cpe->update(m_ft_compensated);
    			m_ft_mutex.unlock();

    			topicPub_ContactPointEstimate_.publish(cpe->getEstimate());
    			loop_rate.sleep();
    		}
    	}
    }

    void SNEThreadFunction()
    {
    	static ros::Rate loop_rate(sne_params->getUpdateFrequency());
    	for(;;)
    	{
    		if(!m_run_estimator)
    		{
    			return;
    		}

    		else if(!m_received_twist)
    		{
    			static ros::Time t = ros::Time::now();
    			if((ros::Time::now()-t).toSec()>1.0)
    			{
    				ROS_ERROR("Haven't received FT sensor twist");
    				t = ros::Time::now();
    			}
    		}

    		else if(!n_.ok())
    		{
    			return;
    		}

    		else
    		{
    			m_twist_mutex.lock();
    			sne->update(m_twist_ft_sensor);
    			m_twist_mutex.unlock();

    			topicPub_SurfaceNormalEstimate_.publish(sne->getEstimate());
    			loop_rate.sleep();
    		}
    	}
    }




private:

    geometry_msgs::WrenchStamped m_ft_compensated;
    geometry_msgs::TwistStamped m_twist_ft_sensor;

    boost::mutex m_ft_mutex;
    boost::mutex m_twist_mutex;

    boost::thread m_cpe_thread;
    boost::thread m_sne_thread;

	bool m_received_ft;
    bool m_received_twist;

    bool m_run_estimator;
};


int main(int argc, char **argv)
{
	ros::init(argc, argv, "contact_point_estimation_node");

	ContactPointEstimationNode cpe_node;

	if(!cpe_node.getEstimatorParameters())
	{
		cpe_node.n_.shutdown();
		return 0;
	}


	cpe_node.cpe = new ContactPointEstimator(cpe_node.cpe_params);
	cpe_node.sne = new SurfaceNormalEstimator(cpe_node.sne_params);

    ros::AsyncSpinner s(4);
    s.start();

    ros::waitForShutdown();

	return 0;
}


