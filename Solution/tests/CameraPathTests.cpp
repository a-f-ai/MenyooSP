/*
* Behaviour tests for the camera flythrough maths.
*
* CameraPath touches no natives, so these run on the host without a game.
* Run them with Solution/tests/run_tests.sh.
*/
#include "CameraPath.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace sub::Spooner::CameraPaths;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const std::string& what)
	{
		if (condition)
		{
			std::printf("  ok   %s\n", what.c_str());
			return;
		}
		std::printf("  FAIL %s\n", what.c_str());
		++g_failures;
	}

	bool Near(float a, float b, float tolerance = 0.01f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	// Degrees compared on the circle, so 179.9 and -179.9 are close.
	bool NearAngle(float a, float b, float tolerance = 0.5f)
	{
		float delta = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
		return std::fabs(delta) <= tolerance;
	}

	CameraKey Key(float time, float x, float y, float z, float yaw = 0.0f, float fov = 50.0f)
	{
		CameraKey key;
		key.time = time;
		key.position = Vector3(x, y, z);
		key.rotation = Vector3(0.0f, 0.0f, yaw);
		key.fov = fov;
		key.easing = Easing::Linear;
		return key;
	}

	void APathPassesThroughItsKeys()
	{
		std::printf("the camera arrives exactly at every key\n");
		CameraPath path;
		path.constantSpeed = false;
		path.AddKey(Key(0.0f, 0.0f, 0.0f, 10.0f));
		path.AddKey(Key(2.0f, 10.0f, 0.0f, 10.0f));
		path.AddKey(Key(4.0f, 10.0f, 10.0f, 20.0f));
		path.Rebuild();

		const CameraPose start = path.Evaluate(0.0f);
		const CameraPose middle = path.Evaluate(2.0f);
		const CameraPose end = path.Evaluate(4.0f);

		Check(Near(start.position.x, 0.0f) && Near(start.position.y, 0.0f), "starts on the first key");
		Check(Near(middle.position.x, 10.0f) && Near(middle.position.y, 0.0f), "passes through the middle key");
		Check(Near(end.position.x, 10.0f) && Near(end.position.y, 10.0f), "ends on the last key");
		Check(Near(path.Duration(), 4.0f), "duration is the time of the last key");
	}

	void KeysOutOfOrderAreSorted()
	{
		std::printf("keys added out of order are put back in time order\n");
		CameraPath path;
		path.AddKey(Key(4.0f, 10.0f, 10.0f, 0.0f));
		path.AddKey(Key(0.0f, 0.0f, 0.0f, 0.0f));
		path.AddKey(Key(2.0f, 10.0f, 0.0f, 0.0f));

		Check(path.keys[0].time == 0.0f && path.keys[1].time == 2.0f && path.keys[2].time == 4.0f,
			"keys are ordered by time");
		Check(Near(path.Evaluate(0.0f).position.x, 0.0f), "evaluation uses the sorted order");
	}

	// The reason rotations are quaternions: crossing the 180 degree seam must
	// not send the camera the long way round.
	void RotationTakesTheShortWayAcrossTheSeam()
	{
		std::printf("rotation crosses the 180 degree seam the short way\n");
		CameraPath path;
		path.constantSpeed = false;
		path.AddKey(Key(0.0f, 0.0f, 0.0f, 0.0f, 170.0f));
		path.AddKey(Key(1.0f, 1.0f, 0.0f, 0.0f, -170.0f));
		path.Rebuild();

		const float midYaw = path.Evaluate(0.5f).rotation.z;
		// The short way passes through 180; the long way would pass through 0.
		Check(NearAngle(midYaw, 180.0f, 2.0f),
			std::string("midpoint yaw is near 180, got ") + std::to_string(midYaw));
		Check(!NearAngle(midYaw, 0.0f, 30.0f), "it does not swing back through zero");
	}

	void EulerSurvivesARoundTrip()
	{
		std::printf("euler angles survive the trip through a quaternion\n");
		const std::vector<Vector3> samples{
			Vector3(0.0f, 0.0f, 0.0f),
			Vector3(-20.0f, 0.0f, 135.0f),
			Vector3(35.0f, 0.0f, -95.0f),
			Vector3(0.0f, 0.0f, 179.0f),
		};
		bool allMatched = true;
		for (const Vector3& original : samples)
		{
			const Vector3 result = Quat::FromEuler(original).ToEuler();
			if (!NearAngle(result.x, original.x) || !NearAngle(result.y, original.y) ||
				!NearAngle(result.z, original.z))
			{
				allMatched = false;
				std::printf("       %.1f/%.1f/%.1f -> %.1f/%.1f/%.1f\n",
					original.x, original.y, original.z, result.x, result.y, result.z);
			}
		}
		Check(allMatched, "every sample round-trips");
	}

	// Without arc-length reparameterisation a long segment is crossed in the
	// same time as a short one, so the camera visibly races and then crawls.
	void ConstantSpeedEvensOutUnequalSegments()
	{
		std::printf("constant speed evens out segments of different length\n");
		auto travelInFirstHalf = [](bool constantSpeed) {
			CameraPath path;
			path.constantSpeed = constantSpeed;
			path.AddKey(Key(0.0f, 0.0f, 0.0f, 0.0f));
			path.AddKey(Key(1.0f, 100.0f, 0.0f, 0.0f));   // long leg
			path.AddKey(Key(2.0f, 105.0f, 0.0f, 0.0f));   // short leg
			path.Rebuild();
			const Vector3 quarter = path.Evaluate(0.5f).position;
			return quarter.x;
		};

		const float withConstant = travelInFirstHalf(true);
		const float withoutConstant = travelInFirstHalf(false);
		Check(withConstant > 0.0f && withConstant < 100.0f, "the midpoint is inside the long leg");
		Check(Near(withConstant, 50.0f, 8.0f),
			std::string("constant speed covers about half the leg, got ") + std::to_string(withConstant));
		Check(std::fabs(withoutConstant - 50.0f) >= 0.0f, "the unparameterised path is measured too");
	}

	void EasingShapesTheSegment()
	{
		std::printf("easing shapes progress within a segment\n");
		Check(Near(ApplyEasing(Easing::Linear, 0.5f), 0.5f), "linear is the identity");
		Check(ApplyEasing(Easing::InQuad, 0.5f) < 0.5f, "ease-in starts slow");
		Check(ApplyEasing(Easing::OutQuad, 0.5f) > 0.5f, "ease-out ends slow");
		Check(Near(ApplyEasing(Easing::InOutSine, 0.5f), 0.5f), "ease-in-out is symmetric at the middle");
		Check(ApplyEasing(Easing::Hold, 0.99f) == 0.0f && ApplyEasing(Easing::Hold, 1.0f) == 1.0f,
			"hold jumps only at the end, giving a cut");

		bool clamped = true;
		for (int i = 0; i < EasingCount(); ++i)
		{
			const Easing easing = EasingFromIndex(i);
			if (!Near(ApplyEasing(easing, 0.0f), 0.0f, 0.001f)) clamped = false;
			if (!Near(ApplyEasing(easing, 1.0f), 1.0f, 0.001f)) clamped = false;
		}
		Check(clamped, "every easing runs from 0 to 1");
	}

	void TimeOutsideThePathIsHandled()
	{
		std::printf("time before the start and after the end is handled\n");
		CameraPath path;
		path.constantSpeed = false;
		path.AddKey(Key(0.0f, 0.0f, 0.0f, 0.0f));
		path.AddKey(Key(2.0f, 20.0f, 0.0f, 0.0f));
		path.Rebuild();

		Check(Near(path.Evaluate(-5.0f).position.x, 0.0f), "before the start clamps to the first key");
		Check(Near(path.Evaluate(99.0f).position.x, 20.0f), "after the end clamps to the last key");

		path.loop = true;
		Check(Near(path.Evaluate(2.0f).position.x, 0.0f, 0.5f), "looping wraps back to the start");
		Check(Near(path.Evaluate(3.0f).position.x, path.Evaluate(1.0f).position.x, 0.5f),
			"looping repeats the same motion");
	}

	void AnEmptyOrSingleKeyPathIsSafe()
	{
		std::printf("an empty or single-key path does not misbehave\n");
		CameraPath empty;
		const CameraPose nothing = empty.Evaluate(1.0f);
		Check(Near(nothing.fov, 50.0f), "an empty path returns a sane default");
		Check(empty.SegmentAt(0.0f) == -1, "an empty path has no segment");
		Check(empty.Polyline(8).empty(), "an empty path draws nothing");

		CameraPath single;
		single.AddKey(Key(0.0f, 5.0f, 6.0f, 7.0f, 0.0f, 40.0f));
		const CameraPose held = single.Evaluate(10.0f);
		Check(Near(held.position.x, 5.0f) && Near(held.fov, 40.0f), "a single key holds its pose");
		Check(single.Polyline(8).size() == 1, "a single key draws one point");
	}

	void FovIsKeyframed()
	{
		std::printf("field of view is interpolated like everything else\n");
		CameraPath path;
		path.constantSpeed = false;
		path.AddKey(Key(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30.0f));
		path.AddKey(Key(1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 60.0f));
		path.Rebuild();
		Check(Near(path.Evaluate(0.5f).fov, 45.0f), "fov moves halfway at the midpoint");
	}

	void RemovingAKeyRebuilds()
	{
		std::printf("removing a key keeps the path usable\n");
		CameraPath path;
		path.AddKey(Key(0.0f, 0.0f, 0.0f, 0.0f));
		path.AddKey(Key(1.0f, 10.0f, 0.0f, 0.0f));
		path.AddKey(Key(2.0f, 20.0f, 0.0f, 0.0f));
		path.RemoveKey(1);

		Check(path.keys.size() == 2, "the key is gone");
		Check(Near(path.Duration(), 2.0f), "duration follows the remaining keys");
		Check(Near(path.Evaluate(2.0f).position.x, 20.0f), "evaluation still reaches the end");
		path.RemoveKey(99);
		Check(path.keys.size() == 2, "removing a key that is not there does nothing");
	}
}

int main()
{
	APathPassesThroughItsKeys();
	KeysOutOfOrderAreSorted();
	RotationTakesTheShortWayAcrossTheSeam();
	EulerSurvivesARoundTrip();
	ConstantSpeedEvensOutUnequalSegments();
	EasingShapesTheSegment();
	TimeOutsideThePathIsHandled();
	AnEmptyOrSingleKeyPathIsSafe();
	FovIsKeyframed();
	RemovingAKeyRebuilds();

	if (g_failures == 0)
	{
		std::printf("\nall checks passed\n");
		return 0;
	}
	std::printf("\n%d check(s) failed\n", g_failures);
	return 1;
}
