
/****************************************************************************
 *
 *   Copyright (C) 2017 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file ControlMath.cpp
 *
 * Simple functions for vector manipulation that do not fit into matrix lib.
 * These functions are specific for controls.
 */


#include "ControlMath.hpp"
#include <platforms/px4_defines.h>
#include <float.h>
#include <mathlib/mathlib.h>

namespace ControlMath
{
vehicle_attitude_setpoint_s thrustToAttitude(const matrix::Vector3f &thr_sp, const float yaw_sp)
{

	vehicle_attitude_setpoint_s att_sp;
	att_sp.yaw_body = yaw_sp;

	/* desired body_z axis = -normalize(thrust_vector) */
	matrix::Vector3f body_x, body_y, body_z;

	if (thr_sp.length() > 0.00001f) {
		body_z = -thr_sp.normalized();

	} else {
		/* no thrust, set Z axis to safe value */
		body_z.zero();
		body_z(2) = 1.0f;
	}

	/* vector of desired yaw direction in XY plane, rotated by PI/2 */
	matrix::Vector3f y_C(-sinf(att_sp.yaw_body), cosf(att_sp.yaw_body), 0.0f);

	if (fabsf(body_z(2)) > 0.000001f) {
		/* desired body_x axis, orthogonal to body_z */
		body_x = y_C % body_z;

		/* keep nose to front while inverted upside down */
		if (body_z(2) < 0.0f) {
			body_x = -body_x;
		}

		body_x.normalize();

	} else {
		/* desired thrust is in XY plane, set X downside to construct correct matrix,
		 * but yaw component will not be used actually */
		body_x.zero();
		body_x(2) = 1.0f;
	}

	/* desired body_y axis */
	body_y = body_z % body_x;

	matrix::Dcmf R_sp;

	/* fill rotation matrix */
	for (int i = 0; i < 3; i++) {
		R_sp(i, 0) = body_x(i);
		R_sp(i, 1) = body_y(i);
		R_sp(i, 2) = body_z(i);
	}

	/* copy quaternion setpoint to attitude setpoint topic */
	matrix::Quatf q_sp = R_sp;
	q_sp.copyTo(att_sp.q_d);
	att_sp.q_d_valid = true;

	/* calculate euler angles, for logging only, must not be used for control */
	matrix::Eulerf euler = R_sp;
	att_sp.roll_body = euler(0);
	att_sp.pitch_body = euler(1);

	/* fill and publish att_sp message */
	att_sp.thrust = thr_sp.length();

	return att_sp;
}

/* The sum of two vectors are constraint such that v0 has priority over v1.
 * This means that if the length of v0+v1 exceeds max, then it is constraint such
 * that v0 has priority.
 * Inputs:
 * @max => maximum magnitude of vector (v0 + v1)
 * @v0 => vector that is prioritized
 * @v1 => vector that is scaled such that max is not exceeded
 *
 * Output:
 * vector that is the sum of v1 and v0 with v0 prioritized.
 *
 * vf = final vector with ||vf|| <= max
 * s = scaling factor
 * u1 = unit of v1
 * vf = v0 + v1 = v0 + s * u1
 * constraint: ||vf|| <= max
 * solve for s: ||vf|| = ||v0 + s * u1|| <= max
 */
matrix::Vector2f constrainXY(const matrix::Vector2f &v0, const matrix::Vector2f &v1, const float max)
{
	if (matrix::Vector2f(v0 + v1).norm() <= max) {
		/* Vector does not exceed maximum magnitude */
		return v0 + v1;

	} else if (v0.length() >= max) {
		/* The magnitude along v0, which has priority, already exceeds maximum.*/
		return v0;

	} else if (fabsf(matrix::Vector2f(v1 - v0).norm()) < 0.001f) {
		/* The two vectors are equal. */
		return v0.normalized() * max;

	} else if (v1.length() < 0.001f) {
		/* The second vector is 0. */
		return v0.normalized() * max;

	} else {
		/* Constrain but prioritize v0. */
		float s = 0.0f;
		matrix::Vector2f u1 = v1.normalized();
		float det = sqrtf(max * max * u1.length() * u1.length() - (v0(0) * u1(1) - v0(1) * u1(0)) * (v0(0) * u1(1) - v0(1) * u1(
					  0)));
		float b = v0(0) * u1(0) + v0(1) * u1(1);

		if (det >= b) {
			s = (det - b) / (u1.length() * u1.length());
		}

		return v0 + u1 * s;
	}
}
}
