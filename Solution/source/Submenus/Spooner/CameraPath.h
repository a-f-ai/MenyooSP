/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* A camera flythrough: keys in time, and the maths that turns them into a
* position, an orientation and a field of view at any moment.
*
* Deliberately free of natives, Windows and ImGui, so it can be reasoned
* about and tested on its own.
*/
#pragma once

#include "../../Util/GTAmath.h"

#include <string>
#include <vector>

namespace sub::Spooner::CameraPaths
{
	enum class Easing
	{
		Linear,
		InQuad, OutQuad, InOutQuad,
		InCubic, OutCubic, InOutCubic,
		InSine, OutSine, InOutSine,
		InExpo, OutExpo, InOutExpo,
		Hold,          // jump at the end of the segment: a cut, not a move
	};

	const char* EasingName(Easing easing);
	int EasingCount();
	Easing EasingFromIndex(int index);
	Easing EasingFromName(const std::string& name);

	// Shapes the normalised progress through one segment.
	float ApplyEasing(Easing easing, float t);

	// Orientation is carried as a quaternion, because interpolating euler
	// angles sends the camera the long way round whenever yaw crosses 180.
	struct Quat
	{
		float x, y, z, w;

		static Quat FromEuler(const Vector3& degrees);
		Vector3 ToEuler() const;
		static Quat Slerp(const Quat& from, const Quat& to, float t);
		static Quat Normalize(const Quat& q);
		static float Dot(const Quat& a, const Quat& b);
	};

	struct CameraKey
	{
		float time = 0.0f;            // seconds from the start of the path
		Vector3 position;
		Vector3 rotation;             // degrees, pitch/roll/yaw as Menyoo stores them
		float fov = 50.0f;
		Easing easing = Easing::InOutSine;  // shapes the segment that starts here
	};

	struct CameraPose
	{
		Vector3 position;
		Vector3 rotation;
		float fov;
	};

	class CameraPath
	{
	public:
		std::string name = "untitled";
		bool loop = false;
		// Spreads progress by distance travelled rather than by spline
		// parameter, so a long segment does not race a short one.
		bool constantSpeed = true;

		std::vector<CameraKey> keys;

		float Duration() const;
		bool Empty() const { return keys.empty(); }

		// Keeps keys ordered by time and rebuilds the arc-length tables.
		// Call after any edit; evaluation assumes it has been done.
		void Rebuild();

		// Pose at `time` seconds. Clamps outside the path, or wraps when
		// looping. Safe on an empty path.
		CameraPose Evaluate(float time) const;

		// Index of the segment containing `time`, or -1 when there is none.
		int SegmentAt(float time) const;

		// Positions sampled along the whole path, for drawing the trajectory.
		std::vector<Vector3> Polyline(int samplesPerSegment) const;

		void AddKey(const CameraKey& key);
		void RemoveKey(size_t index);

	private:
		// Per segment, cumulative distance at each sample, used to convert a
		// distance fraction back into a spline parameter.
		std::vector<std::vector<float>> m_arcTables;

		Vector3 SplinePosition(int segment, float u) const;
		float ReparameterizeByArcLength(int segment, float t) const;
	};
}
