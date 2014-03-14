/**
 *      _____
 *     /  _  \
 *    / _/ \  \
 *   / / \_/   \
 *  /  \_/  _   \  ___  _    ___   ___   ____   ____   ___   _____  _   _
 *  \  / \_/ \  / /  _\| |  | __| / _ \ | ++ \ | ++ \ / _ \ |_   _|| | | |
 *   \ \_/ \_/ /  | |  | |  | ++ | |_| || ++ / | ++_/| |_| |  | |  | +-+ |
 *    \  \_/  /   | |_ | |_ | ++ |  _  || |\ \ | |   |  _  |  | |  | +-+ |
 *     \_____/    \___/|___||___||_| |_||_| \_\|_|   |_| |_|  |_|  |_| |_|
 *             ROBOTICS™ 
 *
 *  File: jaco_comm.cpp
 *  Desc: Class for moving/querying jaco arm.
 *  Auth: Alex Bencz, Jeff Schmidt
 *
 *  Copyright (c) 2013, Clearpath Robotics, Inc. 
 *  All Rights Reserved
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Clearpath Robotics, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CLEARPATH ROBOTICS, INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Please send comments, questions, or patches to skynet@clearpathrobotics.com 
 *
 */

#include <ros/ros.h>
#include "jaco_driver/jaco_comm.h"

#define PI 3.14159265358

namespace jaco
{

JacoComm::JacoComm(JacoAngles home) : software_stop(false), home_position(home)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	/* Connecting to Jaco Arm */
	ROS_INFO("Initiating Library");

	API = new JacoAPI();
	ROS_INFO("Initiating API");

	int api_result = 0; //stores result from the API
	ros::Duration(1.0).sleep();

	api_result = (API->InitAPI());

	/* 
	A common result that may be returned is "1014", which means communications
	could not be established with the arm.  This often means the arm is not turned on, 
	or the InitAPI command was initiated before the arm had fully booted up.
	*/


	// On a cold boot the arm may not respond to commands from the API right away.  
	// This kick-starts the Control API so that it's ready to go.
	API->StartControlAPI();

	ros::Duration(3.0).sleep();
	API->StopControlAPI();

	if (api_result != 1)
	{
		/* Failed to contact arm */
		ROS_FATAL("Could not initialize arm");
		ROS_FATAL("Jaco_InitAPI returned: %d", api_result);
		#ifndef DEBUG_WITHOUT_ARM
		ros::shutdown();
		#endif
	} 
	else
	{
		ROS_INFO("API Initialized Successfully!");
	}

}

JacoComm::~JacoComm()
{
	API->CloseAPI();
}

/*!
 * \brief Determines whether the arm has returned to its "Home" state. 
 * 
 * Checks the current joint angles, then compares them to the known "Home"
 * joint angles.
 */
bool JacoComm::HomeState(void)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);

	QuickStatus currentstat;
	API->GetQuickStatus(currentstat);

	//ROS_INFO("Retract State: %d", currentstat.RetractType);
	
	if (currentstat.RetractType == 1)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

/*!
 * \brief Send the arm to the "home" position.
 * 
 * The code replicates the function of the "home" button on the user controller
 * by "pressing" the home button long enough for the arm to return to the home
 * position.
 *
 * Fingers are homed by manually opening them fully, then returning them to a
 * half-open position.
 */
void JacoComm::HomeArm(void)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	if (Stopped())
	{
		return;
	}

	if (HomeState())
	{
		ROS_INFO("Arm is already in \"home\" position");
		return;
	}

	
	API->StopControlAPI();
	API->StartControlAPI();
	//API->EraseAllTrajectories();

	ROS_INFO("Homing the Arm");
	API->MoveHome();

	API->StopControlAPI(); // test
}

/*!
 * \brief Initialize finger actuators.
 *
 * Move fingers to the full-open position to initialize them for use.
 */
void JacoComm::InitializeFingers(void)
{
// The InitFingers routine requires firmware version 5.05.x.

	FingerAngles fingers_home;

	ROS_INFO("Initializing Fingers");

	API->InitFingers();

	// Set the fingers to "half-open"
	fingers_home.Finger1 = 3000.0;
	fingers_home.Finger2 = 3000.0;
	fingers_home.Finger3 = 0.0;
	SetFingers(fingers_home, 5.0);

	ros::Duration(3.0).sleep();

}

/*!
 * \brief Sends a joint angle command to the Jaco arm.
 * 
 * Waits until the arm has stopped moving before releasing control of the API.
 */
void JacoComm::SetAngles(JacoAngles &angles, int timeout, bool push)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	if (Stopped())
		return;

	TrajectoryPoint Jaco_Position;
	Jaco_Position.InitStruct();

	memset(&Jaco_Position, 0, sizeof(Jaco_Position)); //zero structure

	if (push == true)
	{
		API->EraseAllTrajectories();
		API->StopControlAPI();
	}
	
	API->StopControlAPI();
	API->StartControlAPI();
	//API->EraseAllTrajectories();
	API->SetAngularControl();
	
	//Jaco_Position.LimitationsActive = false;
	Jaco_Position.Position.Delay = 0.0;
	Jaco_Position.Position.Type = ANGULAR_POSITION;

	Jaco_Position.Position.Actuators = angles; 

	API->SendAdvanceTrajectory(Jaco_Position);

	//API->StopControlAPI(); // test
}

/*!
 * \brief Sends a cartesian coordinate trajectory to the Jaco arm.
 *
 * Waits until the arm has stopped moving before releasing control of the API.
 */
void JacoComm::SetPosition(JacoPose &position, int timeout, bool push)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	if (Stopped())
		return;

	TrajectoryPoint Jaco_Position;
	Jaco_Position.InitStruct();

	memset(&Jaco_Position, 0, sizeof(Jaco_Position)); //zero structure

	if (push == true)
	{
		API->EraseAllTrajectories();
		API->StopControlAPI();
	}

	API->StopControlAPI();
	API->StartControlAPI();
	//API->EraseAllTrajectories();
	API->SetCartesianControl();

	//Jaco_Position.LimitationsActive = false;
	Jaco_Position.Position.Delay = 0.0;
	Jaco_Position.Position.Type = CARTESIAN_POSITION;
	Jaco_Position.Position.HandMode = HAND_NOMOVEMENT;

	// These values will not be used but are initialized anyway.
        Jaco_Position.Position.Actuators.Actuator1 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator2 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator3 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator4 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator5 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator6 = 0.0f;

	Jaco_Position.Position.CartesianPosition = position;
	//Jaco_Position.Position.CartesianPosition.ThetaZ += 0.0001; // A workaround for a bug in the Kinova API

	API->SendBasicTrajectory(Jaco_Position);

	//API->StopControlAPI(); // test
}

/*!
 * \brief Sets the finger positions
 */
void JacoComm::SetFingers(FingerAngles &fingers, int timeout, bool push)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	if (Stopped())
		return;

	TrajectoryPoint Jaco_Position;
	Jaco_Position.InitStruct();

	memset(&Jaco_Position, 0, sizeof(Jaco_Position)); //zero structure

	if (push == true)
	{
		API->EraseAllTrajectories();
		API->StopControlAPI();
	}

	API->StopControlAPI();
	API->StartControlAPI();
	//API->EraseAllTrajectories();

	//ROS_INFO("Got a finger command");

	// Initialize Cartesian control of the fingers
	Jaco_Position.Position.HandMode = POSITION_MODE;
	Jaco_Position.Position.Type = CARTESIAN_POSITION;
	Jaco_Position.Position.Fingers = fingers;
	Jaco_Position.Position.Delay = 0.0;
	Jaco_Position.LimitationsActive = 0;

	// These values will not be used but are initialized anyway.
        Jaco_Position.Position.Actuators.Actuator1 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator2 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator3 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator4 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator5 = 0.0f;
        Jaco_Position.Position.Actuators.Actuator6 = 0.0f;


	// When loading a cartesian position for the fingers, values are required for the arm joints
	// as well or the arm goes nuts.  Grab the current position and feed it back to the arm.
	JacoPose pose;
	GetPosition(pose);

	Jaco_Position.Position.CartesianPosition.X = pose.X;
	Jaco_Position.Position.CartesianPosition.Y = pose.Y;
	Jaco_Position.Position.CartesianPosition.Z = pose.Z;
	Jaco_Position.Position.CartesianPosition.ThetaX = pose.ThetaX;
	Jaco_Position.Position.CartesianPosition.ThetaY =pose.ThetaY;
	Jaco_Position.Position.CartesianPosition.ThetaZ = pose.ThetaZ;

	//ROS_INFO("Finger1: %f", Jaco_Position.Position.Fingers.Finger1);
	//ROS_INFO("Finger2: %f", Jaco_Position.Position.Fingers.Finger2);
	//ROS_INFO("Finger3: %f", Jaco_Position.Position.Fingers.Finger3);


	// Send the position to the arm.
	API->SendAdvanceTrajectory(Jaco_Position);
	ROS_DEBUG("Sending Fingers");

	//API->StopControlAPI(); // test
}

/*!
 * \brief Set the velocity of the angles using angular input.
 */
void JacoComm::SetVelocities(AngularInfo joint_vel)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	if (Stopped())
		return;

	TrajectoryPoint Jaco_Velocity;
	Jaco_Velocity.InitStruct();

	memset(&Jaco_Velocity, 0, sizeof(Jaco_Velocity)); //zero structure

	API->StopControlAPI();
	API->StartControlAPI();
	//API->EraseAllTrajectories();
	Jaco_Velocity.Position.Type = ANGULAR_VELOCITY;

	// confusingly, velocity is passed in the position struct
	Jaco_Velocity.Position.Actuators = joint_vel;

	API->SendAdvanceTrajectory(Jaco_Velocity);

	//API->StopControlAPI(); // test
}

/*!
 * \brief Set the velocity of the angles using cartesian input.
 */
void JacoComm::SetCartesianVelocities(CartesianInfo velocities)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	if (Stopped())
	{
		API->EraseAllTrajectories();
		return;
	}

	TrajectoryPoint Jaco_Velocity;
	Jaco_Velocity.InitStruct();

	memset(&Jaco_Velocity, 0, sizeof(Jaco_Velocity)); //zero structure

	API->StopControlAPI();
	API->StartControlAPI();
	//API->EraseAllTrajectories();
	Jaco_Velocity.Position.Type = CARTESIAN_VELOCITY;

	// confusingly, velocity is passed in the position struct
	Jaco_Velocity.Position.CartesianPosition = velocities;

	API->SendAdvanceTrajectory(Jaco_Velocity);

	//API->StopControlAPI(); // test
}

/*!
 * \brief Obtains the current arm configuration.
 *
 * This is the configuration which are stored on the arm itself. Many of these
 * configurations may be set using the Windows interface.
 */
void JacoComm::SetConfig(ClientConfigurations config)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	
	API->SetClientConfigurations(config);

	//API->StopControlAPI(); // test
}

/*!
 * \brief API call to obtain the current angular position of all the joints.
 */
void JacoComm::GetAngles(JacoAngles &angles)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	

	AngularPosition Jaco_Position;
	API->GetAngularPosition(Jaco_Position);

	angles = Jaco_Position.Actuators;
}

/*!
 * \brief API call to obtain the current cartesian position of the arm.
 */
void JacoComm::GetPosition(JacoPose &position)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	

	CartesianPosition Jaco_Position;

	memset(&Jaco_Position, 0, sizeof(Jaco_Position)); //zero structure

	API->GetCartesianPosition(Jaco_Position);

	position = JacoPose(Jaco_Position.Coordinates);
}

/*!
 * \brief API call to obtain the current finger positions.
 */
void JacoComm::GetFingers(FingerAngles &fingers)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	

	CartesianPosition Jaco_Position;

	API->GetCartesianPosition(Jaco_Position);

	fingers = Jaco_Position.Fingers;
}

/*!
 * \brief API call to obtain the current actuator forces.
 */

/*
void JacoComm::GetForcesInfo(ForcesInfo &forces)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	

	memset(&forces, 0, sizeof(forces)); //zero structure

	API->GetForcesInfo(forces);
}
*/

/*!
 * \brief API call to obtain the current client configuration.
 */
void JacoComm::GetConfig(ClientConfigurations &config)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	

	memset(&config, 0, sizeof(config)); //zero structure
	API->GetClientConfigurations(config);
}

/*!
 * \brief API call to obtain the current "quick status".
 */
void JacoComm::GetQuickStatus(QuickStatus &quickstat)
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	

	memset(&quickstat, 0, sizeof(quickstat)); //zero structure

	API->GetQuickStatus(quickstat);

	//ROS_INFO("Retract State: %d", quickstat.RetractType);
}


/*!
 * \brief Dumps the current joint angles onto the screen.  
 */
void JacoComm::PrintAngles(JacoAngles &angles)
{
	ROS_INFO("Jaco Arm Angles (Degrees)");
	ROS_INFO("Joint 1 = %f", angles.Actuator1);
	ROS_INFO("Joint 2 = %f", angles.Actuator2);
	ROS_INFO("Joint 3 = %f", angles.Actuator3);

	ROS_INFO("Joint 4 = %f", angles.Actuator4);
	ROS_INFO("Joint 5 = %f", angles.Actuator5);
	ROS_INFO("Joint 6 = %f", angles.Actuator6);
}

/*!
 * \brief Dumps the current cartesian positions onto the screen.  
 */
void JacoComm::PrintPosition(JacoPose &position)
{
	ROS_INFO("Jaco Arm Position (Meters)");
	ROS_INFO("X = %f", position.X);
	ROS_INFO("Y = %f", position.Y);
	ROS_INFO("Z = %f", position.Z);

	ROS_INFO("Jaco Arm Rotations (Radians)");
	ROS_INFO("Theta X = %f", position.ThetaX);
	ROS_INFO("Theta Y = %f", position.ThetaY);
	ROS_INFO("Theta Z = %f", position.ThetaZ);
}

/*! 
 * \brief Dumps the current finger positions onto the screen.  
 */
void JacoComm::PrintFingers(FingersPosition fingers)
{
	ROS_INFO("Jaco Arm Finger Positions");
	ROS_INFO("Finger 1 = %f", fingers.Finger1);
	ROS_INFO("Finger 2 = %f", fingers.Finger2);
	ROS_INFO("Finger 3 = %f", fingers.Finger3);
}

/*!
 * \brief Dumps the client configuration onto the screen.  
 */
void JacoComm::PrintConfig(ClientConfigurations config)
{
	ROS_INFO("Jaco Config");
	ROS_INFO("ClientID = %s", config.ClientID);
	ROS_INFO("ClientName = %s", config.ClientName);
	ROS_INFO("Organization = %s", config.Organization);
	ROS_INFO("Serial = %s", config.Serial);
	ROS_INFO("Model = %s", config.Model);
	//ROS_INFO("MaxLinearSpeed = %f", config.MaxLinearSpeed);
	//ROS_INFO("MaxAngularSpeed = %f", config.MaxAngularSpeed);
	//ROS_INFO("MaxLinearAcceleration = %f", config.MaxLinearAcceleration);
	ROS_INFO("MaxForce = %f", config.MaxForce);
	ROS_INFO("Sensibility = %f", config.Sensibility);
	ROS_INFO("DrinkingHeight = %f", config.DrinkingHeight);
	ROS_INFO("ComplexRetractActive = %d", config.ComplexRetractActive);
	ROS_INFO("RetractedPositionAngle = %f", config.RetractedPositionAngle);
	ROS_INFO("RetractedPositionCount = %d", config.RetractedPositionCount);
	ROS_INFO("DrinkingDistance = %f", config.DrinkingDistance);
	ROS_INFO("Fingers2and3Inverted = %d", config.Fingers2and3Inverted);
	ROS_INFO("DrinkingLength = %f", config.DrinkingLenght);
	ROS_INFO("DeletePreProgrammedPositionsAtRetract = %d", config.DeletePreProgrammedPositionsAtRetract);
	ROS_INFO("EnableFlashErrorLog = %d", config.EnableFlashErrorLog);
	ROS_INFO("EnableFlashPositionLog = %d", config.EnableFlashPositionLog);
}

void JacoComm::Stop()
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	software_stop = true;

	API->StopControlAPI();
	API->StartControlAPI();
	API->EraseAllTrajectories();

	JoystickCommand home_command;
	memset(&home_command, 0, sizeof(home_command)); //zero structure

	home_command.ButtonValue[2] = 1;
	API->SendJoystickCommand(home_command);
	API->EraseAllTrajectories();
	ros::Duration(0.05).sleep();

	home_command.ButtonValue[2] = 0;
	API->SendJoystickCommand(home_command);
}

void JacoComm::Start()
{
	boost::recursive_mutex::scoped_lock lock(api_mutex);
	software_stop = false;

	API->StopControlAPI();
	API->StartControlAPI();

}

bool JacoComm::Stopped()
{
	return software_stop;
}

/*!
 * \brief Wait for the arm to reach the "home" position.
 *
 * \param timeout Timeout after which to give up waiting for arm to finish "homing".
 */
void JacoComm::WaitForHome(int timeout)
{
	double start_secs;
	double current_sec;

	//If ros is still running use rostime, else use system time
	if (ros::ok())
	{
		start_secs = ros::Time::now().toSec();
		current_sec = ros::Time::now().toSec();
	} 
	else
	{
		start_secs = (double) time(NULL);
		current_sec = (double) time(NULL);
	}

	//while we have not timed out
	while ((current_sec - start_secs) < timeout)
	{
		ros::Duration(0.5).sleep();
		
		//If ros is still running use rostime, else use system time
		if (ros::ok())
		{
			current_sec = ros::Time::now().toSec();
            ros::spinOnce();
		} 
		else
		{
			current_sec = (double) time(NULL);
		}

		if (HomeState())
		{
			ros::Duration(1.0).sleep();  // Grants a bit more time for the arm to "settle"
			return;
		}
	}

	ROS_WARN("Timed out waiting for arm to return \"home\"");
}

} // namespace jaco
