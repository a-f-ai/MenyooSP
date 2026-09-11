/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CameraPath.h"

#include <algorithm>
#include <cmath>

namespace sub::Spooner::CameraPaths
{
	namespace
	{
		constexpr float kPi = 3.14159265358979323846f;
		constexpr int kArcSamplesPerSegment = 24;

		float Clamp01(float value)
		{
			if (value < 0.0f) return 0.0f;
			if (value > 1.0f) return 1.0f;
			return value;
		}

		float Radians(float degrees) { return degrees * kPi / 180.0f; }
		float Degrees(float radians) { return radians * 180.0f / kPi; }

		const char* const kEasingNames[] = {
			"Linear",
			"In Quad", "Out Quad", "In-Out Quad",
			"In Cubic", "Out Cubic", "In-Out Cubic",
			"In Sine", "Out Sine", "In-Out Sine",
			"In Expo", "Out Expo", "In-Out Expo",
			"Hold",
		};
	}

	const char* EasingName(Easing easing)
	{
		const int index = static_cast<int>(easing);
		if (index < 0 || index >= EasingCount())
			return "Linear";
		return kEasingNames[index];
	}

	int EasingCount()
	{
		return static_cast<int>(sizeof(kEasingNames) / sizeof(kEasingNames[0]));
	}

	Easing EasingFromIndex(int index)
	{
		if (index < 0 || index >= EasingCount())
			return Easing::Linear;
		return static_cast<Easing>(index);
	}

	Easing EasingFromName(const std::string& name)
	{
		for (int i = 0; i < EasingCount(); ++i)
		{
			if (name == kEasingNames[i])
				return static_cast<Easing>(i);
		}
		return Easing::Linear;
	}

	float ApplyEasing(Easing easing, float t)
	{
		t = Clamp01(t);
		switch (easing)
		{
		case Easing::Linear:     return t;
		case Easing::InQuad:     return t * t;
		case Easing::OutQuad:    return 1.0f - (1.0f - t) * (1.0f - t);
		case Easing::InOutQuad:  return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
		case Easing::InCubic:    return t * t * t;
		case Easing::OutCubic:   return 1.0f - std::pow(1.0f - t, 3.0f);
		case Easing::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
		case Easing::InSine:     return 1.0f - std::cos(t * kPi / 2.0f);
		case Easing::OutSine:    return std::sin(t * kPi / 2.0f);
		case Easing::InOutSine:  return -(std::cos(kPi * t) - 1.0f) / 2.0f;
		case Easing::InExpo:     return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
		case Easing::OutExpo:    return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
		case Easing::InOutExpo:
			if (t <= 0.0f) return 0.0f;
			if (t >= 1.0f) return 1.0f;
			return t < 0.5f
				? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
				: (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
		case Easing::Hold:       return t >= 1.0f ? 1.0f : 0.0f;
		}
		return t;
	}

	// ---- Quat -------------------------------------------------------------

	float Quat::Dot(const Quat& a, const Quat& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	}

	Quat Quat::Normalize(const Quat& q)
	{
		const float length = std::sqrt(Dot(q, q));
		if (length <= 1e-8f)
			return Quat{ 0.0f, 0.0f, 0.0f, 1.0f };
		return Quat{ q.x / length, q.y / length, q.z / length, q.w / length };
	}

	// Menyoo hands out rotations as (pitch, roll, yaw) in degrees, which is the
	// game's ZXY order once converted.
	Quat Quat::FromEuler(const Vector3& degrees)
	{
		const float pitch = Radians(degrees.x) * 0.5f;
		const float roll = Radians(degrees.y) * 0.5f;
		const float yaw = Radians(degrees.z) * 0.5f;

		const float sp = std::sin(pitch), cp = std::cos(pitch);
		const float sr = std::sin(roll), cr = std::cos(roll);
		const float sy = std::sin(yaw), cy = std::cos(yaw);

		Quat q;
		q.w = cy * cp * cr - sy * sp * sr;
		q.x = cy * sp * cr - sy * cp * sr;
		q.y = cy * cp * sr + sy * sp * cr;
		q.z = sy * cp * cr + cy * sp * sr;
		return Normalize(q);
	}

	// Inverse of FromEuler for the same ZXY order. Read off the rotation
	// matrix: sin(pitch) = R21, roll = atan2(-R20, R22), yaw = atan2(-R01, R11).
	Vector3 Quat::ToEuler() const
	{
		const Quat q = Normalize(*this);

		const float sinPitch = 2.0f * (q.w * q.x + q.y * q.z);
		float pitch;
		if (sinPitch >= 1.0f) pitch = kPi / 2.0f;
		else if (sinPitch <= -1.0f) pitch = -kPi / 2.0f;
		else pitch = std::asin(sinPitch);

		const float roll = std::atan2(2.0f * (q.w * q.y - q.x * q.z),
			1.0f - 2.0f * (q.x * q.x + q.y * q.y));
		const float yaw = std::atan2(2.0f * (q.w * q.z - q.x * q.y),
			1.0f - 2.0f * (q.x * q.x + q.z * q.z));

		return Vector3(Degrees(pitch), Degrees(roll), Degrees(yaw));
	}

	Quat Quat::Slerp(const Quat& from, const Quat& to, float t)
	{
		Quat a = Normalize(from);
		Quat b = Normalize(to);

		float dot = Dot(a, b);
		// q and -q are the same orientation; flipping picks the short way round,
		// which is the whole reason rotations are not interpolated as euler.
		if (dot < 0.0f)
		{
			b = Quat{ -b.x, -b.y, -b.z, -b.w };
			dot = -dot;
		}

		if (dot > 0.9995f)
		{
			// Nearly identical: lerp, because sin(theta) underflows.
			Quat result{
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t,
				a.w + (b.w - a.w) * t,
			};
			return Normalize(result);
		}

		const float theta = std::acos(dot);
		const float sinTheta = std::sin(theta);
		const float weightA = std::sin((1.0f - t) * theta) / sinTheta;
		const float weightB = std::sin(t * theta) / sinTheta;

		return Normalize(Quat{
			a.x * weightA + b.x * weightB,
			a.y * weightA + b.y * weightB,
			a.z * weightA + b.z * weightB,
			a.w * weightA + b.w * weightB,
		});
	}

	// ---- CameraPath -------------------------------------------------------

	float CameraPath::Duration() const
	{
		return keys.empty() ? 0.0f : keys.back().time;
	}

	void CameraPath::Rebuild()
	{
		std::stable_sort(keys.begin(), keys.end(),
			[](const CameraKey& a, const CameraKey& b) { return a.time < b.time; });

		m_arcTables.clear();
		if (keys.size() < 2)
			return;

		m_arcTables.resize(keys.size() - 1);
		for (size_t segment = 0; segment + 1 < keys.size(); ++segment)
		{
			std::vector<float>& table = m_arcTables[segment];
			table.assign(kArcSamplesPerSegment + 1, 0.0f);

			Vector3 previous = SplinePosition(static_cast<int>(segment), 0.0f);
			float travelled = 0.0f;
			for (int sample = 1; sample <= kArcSamplesPerSegment; ++sample)
			{
				const float u = static_cast<float>(sample) / kArcSamplesPerSegment;
				const Vector3 current = SplinePosition(static_cast<int>(segment), u);
				travelled += (current - previous).Length();
				table[sample] = travelled;
				previous = current;
			}
		}
	}

	int CameraPath::SegmentAt(float time) const
	{
		if (keys.size() < 2)
			return -1;
		for (size_t i = 0; i + 1 < keys.size(); ++i)
		{
			if (time >= keys[i].time && time <= keys[i + 1].time)
				return static_cast<int>(i);
		}
		return time < keys.front().time ? 0 : static_cast<int>(keys.size()) - 2;
	}

	// Catmull-Rom through the key positions. The ends are duplicated so the
	// first and last segments curve like the rest instead of going straight.
	Vector3 CameraPath::SplinePosition(int segment, float u) const
	{
		const int last = static_cast<int>(keys.size()) - 1;
		const int i1 = segment;
		const int i2 = segment + 1;
		const int i0 = segment > 0 ? segment - 1 : 0;
		const int i3 = i2 < last ? i2 + 1 : last;

		const Vector3& p0 = keys[i0].position;
		const Vector3& p1 = keys[i1].position;
		const Vector3& p2 = keys[i2].position;
		const Vector3& p3 = keys[i3].position;

		const float u2 = u * u;
		const float u3 = u2 * u;

		return (p1 * 2.0f + (p2 - p0) * u +
			(p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * u2 +
			(p1 * 3.0f - p0 - p2 * 3.0f + p3) * u3) * 0.5f;
	}

	float CameraPath::ReparameterizeByArcLength(int segment, float t) const
	{
		if (segment < 0 || segment >= static_cast<int>(m_arcTables.size()))
			return t;

		const std::vector<float>& table = m_arcTables[segment];
		const float total = table.back();
		if (total <= 1e-5f)
			return t;

		const float wanted = Clamp01(t) * total;
		for (size_t sample = 1; sample < table.size(); ++sample)
		{
			if (table[sample] < wanted)
				continue;
			const float spanStart = table[sample - 1];
			const float span = table[sample] - spanStart;
			const float within = span <= 1e-6f ? 0.0f : (wanted - spanStart) / span;
			return (static_cast<float>(sample - 1) + within) / static_cast<float>(table.size() - 1);
		}
		return 1.0f;
	}

	CameraPose CameraPath::Evaluate(float time) const
	{
		if (keys.empty())
			return CameraPose{ Vector3(), Vector3(), 50.0f };
		if (keys.size() == 1)
			return CameraPose{ keys[0].position, keys[0].rotation, keys[0].fov };

		const float duration = Duration();
		if (loop && duration > 0.0f)
		{
			time = std::fmod(time, duration);
			if (time < 0.0f)
				time += duration;
		}
		else
		{
			time = std::max(keys.front().time, std::min(time, keys.back().time));
		}

		const int segment = SegmentAt(time);
		const CameraKey& from = keys[segment];
		const CameraKey& to = keys[segment + 1];

		const float span = to.time - from.time;
		const float raw = span <= 1e-6f ? 1.0f : (time - from.time) / span;
		const float eased = ApplyEasing(from.easing, raw);
		const float u = constantSpeed ? ReparameterizeByArcLength(segment, eased) : eased;

		const Quat orientation = Quat::Slerp(
			Quat::FromEuler(from.rotation), Quat::FromEuler(to.rotation), eased);

		return CameraPose{
			SplinePosition(segment, u),
			orientation.ToEuler(),
			from.fov + (to.fov - from.fov) * eased,
		};
	}

	std::vector<Vector3> CameraPath::Polyline(int samplesPerSegment) const
	{
		std::vector<Vector3> points;
		if (keys.size() < 2)
		{
			if (keys.size() == 1)
				points.push_back(keys[0].position);
			return points;
		}

		const int samples = samplesPerSegment < 2 ? 2 : samplesPerSegment;
		for (size_t segment = 0; segment + 1 < keys.size(); ++segment)
		{
			for (int sample = 0; sample < samples; ++sample)
			{
				const float u = static_cast<float>(sample) / static_cast<float>(samples);
				points.push_back(SplinePosition(static_cast<int>(segment), u));
			}
		}
		points.push_back(keys.back().position);
		return points;
	}

	void CameraPath::AddKey(const CameraKey& key)
	{
		keys.push_back(key);
		Rebuild();
	}

	void CameraPath::RemoveKey(size_t index)
	{
		if (index >= keys.size())
			return;
		keys.erase(keys.begin() + static_cast<long>(index));
		Rebuild();
	}
}
