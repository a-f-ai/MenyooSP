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

	// Where the easing is applied.
	enum class Smoothing
	{
		// One curve over the whole path: it accelerates once at the start and
		// settles once at the end, passing through the keys in between at
		// speed. This is what a flythrough usually wants.
		WholePath,
		// A curve per segment. Every key becomes a full stop, which is right
		// for deliberate stop-and-go and wrong for everything else.
		PerKey,
	};

	struct CameraKey
	{
		// Stable across edits. Sorting by time reshuffles indices, so anything
		// that remembers a key - a selection, a drag in progress - holds this
		// instead of a position in the vector.
		unsigned id = 0;
		float time = 0.0f;            // seconds from the start of the path
		Vector3 position;
		Vector3 rotation;             // degrees, pitch/roll/yaw as Menyoo stores them
		float fov = 50.0f;
		// Linear by default: a curve that runs 0 to 1 inside one segment brings
		// the camera to a halt at both of its ends, so easing every key turns a
		// flythrough into a series of stops. Ease the ends of the path instead.
		Easing easing = Easing::Linear;     // shapes the segment that starts here
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
		Smoothing smoothing = Smoothing::PerKey;
		Easing pathEasing = Easing::InOutSine;
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

		void AddKey(const CameraKey& key);      // assigns the id
		void RemoveKey(size_t index);
		void RemoveIds(const std::vector<unsigned>& ids);

		int IndexOfId(unsigned id) const;

		// Stretches or squeezes the given keys about `anchorTime`. This is how
		// a move recorded too fast is slowed down without replacing it.
		void ScaleTimes(const std::vector<unsigned>& ids, float factor, float anchorTime);

		// Scales the whole path so it lasts `seconds`.
		void SetTotalDuration(float seconds);

		// Redistributes key times in proportion to how far the camera actually
		// travels between them, keeping the start and the total length. This is
		// what makes speed even without giving up per-key control of it.
		void RetimeByArcLength();

		// Accelerate out of the first key, settle into the last, and hold a
		// steady pace in between.
		void EaseEnds();

		// Moves every key at or after `fromTime` by `delta`, which is how an
		// insert makes room for itself instead of overwriting what follows.
		void ShiftTimesFrom(float fromTime, float delta);

		// A second key with the same pose `seconds` later, everything after it
		// pushed back. Two identical keys evaluate to a still camera, so this
		// is a pause at the key. Returns the new key's id, or 0 on failure.
		unsigned InsertPause(unsigned id, float seconds);

		// Pastes keys whose times are relative to the first one.
		std::vector<unsigned> InsertKeys(const std::vector<CameraKey>& items,
			float atTime, bool shiftLater);

		std::vector<CameraKey> CopyKeys(const std::vector<unsigned>& ids) const;

	private:
		// Per segment, cumulative distance at each sample, used to convert a
		// distance fraction back into a spline parameter.
		std::vector<std::vector<float>> m_arcTables;
		unsigned m_nextId = 1;

		Vector3 SplinePosition(int segment, float u) const;
		float ReparameterizeByArcLength(int segment, float t) const;
	};
}
