#include "cinematic.h"
#include "turbo_config.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float Pi = 3.14159265f;
constexpr float CarSpeed = 38.0f;      // metres per second the world scrolls at full speed
constexpr float WheelRadius = 0.32f;
constexpr float WheelX = 0.78f;
constexpr float WheelZ = 1.25f;
const Vec3 DriverHead(-0.40f, 1.02f, -0.12f);
const Vec3 WheelCenter(-0.40f, 0.86f, 0.40f);
constexpr float WheelTilt = -0.42f;    // radians about X; top of the wheel leans toward the driver
constexpr float WheelRingRadius = 0.185f;

const Rgb White(240, 240, 240);
const Rgb Red(214, 30, 34);
const Rgb Glass(46, 64, 118);
const Rgb DarkTrim(38, 38, 44);
const Rgb Tire(28, 28, 32);
const Rgb Rim(196, 196, 206);
const Rgb Skin(224, 182, 142);
const Rgb Hair(58, 40, 30);
const Rgb Denim(50, 86, 150);
const Rgb DenimLight(96, 134, 196);
const Rgb Shirt(236, 236, 236);
const Rgb Seat(52, 52, 64);
const Rgb Dash(40, 40, 48);
const Rgb Metal(120, 122, 130);
const Rgb ButtonRed(236, 40, 40);
const Rgb CoverOrange(244, 172, 44);
const Rgb Headlight(252, 244, 190);
const Rgb Smoke(150, 150, 156);
const Rgb Trunk(96, 64, 30);
const Rgb Canopy(198, 168, 58);
const Rgb CanopyDark(160, 132, 40);

float smoothstep(float t)
{
	t = std::max(0.0f, std::min(1.0f, t));
	return t * t * (3.0f - 2.0f * t);
}

float ramp(float value, float from, float to)
{
	return smoothstep((value - from) / std::max(0.0001f, to - from));
}

Transform wheelSpace()
{
	return Transform::translation(WheelCenter) * Transform::rotationX(WheelTilt);
}

void addOctahedron(Mesh& mesh, const Vec3& center, float radius, const Rgb& color)
{
	const Vec3 top(center.x, center.y + radius, center.z);
	const Vec3 bottom(center.x, center.y - radius, center.z);
	const Vec3 ring[4] = {
		{center.x + radius, center.y, center.z}, {center.x, center.y, center.z + radius},
		{center.x - radius, center.y, center.z}, {center.x, center.y, center.z - radius}};
	for (int i = 0; i < 4; ++i)
	{
		const Vec3& a = ring[i];
		const Vec3& b = ring[(i + 1) % 4];
		mesh.addTriangle(top, b, a, color);
		mesh.addTriangle(bottom, a, b, color);
	}
}

} // namespace

Cinematic::Cinematic(const TurboConfig& config, SDL_Renderer* renderer, int width, int height)
	: config(config), renderer(renderer), width(width), height(height), retro(renderer, width, height)
{
	shots = {
		{"ACTIVATION", 0.0f, 4.0f},
		{"CAR", 4.0f, 10.0f},
		{"WHEEL", 10.0f, 13.0f},
		{"EXHAUST", 13.0f, 16.0f},
		{"DRIVER", 16.0f, 19.0f},
		{"HANDS", 19.0f, 22.0f},
		{"EYES", 22.0f, 23.0f},
		{"COVER", 23.0f, 26.0f},
		{"BUTTON", 26.0f, 28.0f},
		{"INTERIOR", 28.0f, 30.5f},
		{"EXIT", 30.5f, 32.0f}};
	cues = {
		{10.2f, TurboSfx::WheelCreak, 0.5f},
		{13.4f, TurboSfx::ExhaustPulse, 0.25f},
		{14.2f, TurboSfx::ExhaustPulse, 0.4f},
		{14.75f, TurboSfx::ExhaustPulse, 0.55f},
		{15.2f, TurboSfx::ExhaustPulse, 0.7f},
		{15.55f, TurboSfx::ExhaustPulse, 0.85f},
		{15.85f, TurboSfx::ExhaustPulse, 1.0f},
		{20.3f, TurboSfx::WheelCreak, 0.7f},
		{22.0f, TurboSfx::Thump, 0.5f},
		{24.55f, TurboSfx::CoverClick, 1.0f},
		{26.5f, TurboSfx::ButtonClick, 1.0f},
		{26.55f, TurboSfx::Ignition, 1.0f},
		{28.0f, TurboSfx::Whoosh, 0.9f},
		{30.5f, TurboSfx::Whoosh, 1.0f}};
	buildModels();
}

double Cinematic::dropTime() const
{
	return static_cast<double>(config.musicDropOffset - config.musicStartOffset);
}

double Cinematic::beatLength() const
{
	return dropTime() / std::max(1.0f, config.cinematicBeats);
}

int Cinematic::shotCount() const
{
	return static_cast<int>(shots.size());
}

void Cinematic::begin(const CinematicPalette& newPalette, const SDL_Rect& rect)
{
	palette = newPalette;
	carRect = rect;
	lastTime = 0.0;
	worldTime = 0.0;
	lastBeat = -1.0f;
	smokePuffs.clear();
	nextPulse = 0;
	buildModels();
}

void Cinematic::buildModels()
{
	carBody.clear();
	rearGlass.clear();
	wheel.clear();
	interior.clear();
	driverBody.clear();
	steeringWheel.clear();
	tree.clear();
	groundBands.clear();

	// --- Car body ---------------------------------------------------------------
	{
		const Vec3 lower[8] = {
			{-0.86f, 0.30f, -1.95f}, {0.86f, 0.30f, -1.95f}, {0.86f, 0.30f, 1.95f}, {-0.86f, 0.30f, 1.95f},
			{-0.88f, 0.78f, -1.90f}, {0.88f, 0.78f, -1.90f}, {0.88f, 0.72f, 1.85f}, {-0.88f, 0.72f, 1.85f}};
		carBody.addHexahedron(lower, White, White, DarkTrim);
		// Hood slopes down toward the nose.
		const Vec3 hood[8] = {
			{-0.84f, 0.70f, 0.45f}, {0.84f, 0.70f, 0.45f}, {0.84f, 0.62f, 1.90f}, {-0.84f, 0.62f, 1.90f},
			{-0.80f, 0.82f, 0.45f}, {0.80f, 0.82f, 0.45f}, {0.82f, 0.70f, 1.92f}, {-0.82f, 0.70f, 1.92f}};
		carBody.addHexahedron(hood, White);
		// Cabin: glass block with a white roof.
		const Vec3 c[8] = {
			{-0.80f, 0.78f, -1.55f}, {0.80f, 0.78f, -1.55f}, {0.80f, 0.78f, 0.55f}, {-0.80f, 0.78f, 0.55f},
			{-0.68f, 1.34f, -1.25f}, {0.68f, 1.34f, -1.25f}, {0.66f, 1.32f, -0.05f}, {-0.66f, 1.32f, -0.05f}};
		carBody.addQuad(c[4], c[7], c[6], c[5], White);   // roof
		carBody.addQuad(c[1], c[5], c[6], c[2], Glass);   // right windows
		carBody.addQuad(c[3], c[7], c[4], c[0], Glass);   // left windows
		carBody.addQuad(c[2], c[6], c[7], c[3], Glass);   // windscreen
		rearGlass.addQuad(c[0], c[4], c[5], c[1], Glass); // rear window (drawn only from outside)
		// Pillars.
		carBody.addBox({-0.72f, 1.05f, 0.25f}, {0.08f, 0.60f, 0.10f}, White,
			Transform::translation({-0.72f, 1.05f, 0.25f}) * Transform::rotationX(-0.45f) * Transform::translation({0.72f, -1.05f, -0.25f}));
		carBody.addBox({0.72f, 1.05f, 0.25f}, {0.08f, 0.60f, 0.10f}, White,
			Transform::translation({0.72f, 1.05f, 0.25f}) * Transform::rotationX(-0.45f) * Transform::translation({-0.72f, -1.05f, -0.25f}));
		carBody.addBox({-0.74f, 1.05f, -0.55f}, {0.08f, 0.56f, 0.10f}, White);
		carBody.addBox({0.74f, 1.05f, -0.55f}, {0.08f, 0.56f, 0.10f}, White);
		carBody.addBox({-0.74f, 1.05f, -1.40f}, {0.08f, 0.56f, 0.12f}, White,
			Transform::translation({-0.74f, 1.05f, -1.40f}) * Transform::rotationX(0.35f) * Transform::translation({0.74f, -1.05f, 1.40f}));
		carBody.addBox({0.74f, 1.05f, -1.40f}, {0.08f, 0.56f, 0.12f}, White,
			Transform::translation({0.74f, 1.05f, -1.40f}) * Transform::rotationX(0.35f) * Transform::translation({-0.74f, -1.05f, 1.40f}));
		// Livery stripes.
		carBody.addBox({-0.885f, 0.52f, 0.0f}, {0.02f, 0.14f, 3.6f}, Red);
		carBody.addBox({0.885f, 0.52f, 0.0f}, {0.02f, 0.14f, 3.6f}, Red);
		carBody.addBox({0.0f, 0.83f, 1.15f}, {0.34f, 0.02f, 1.3f}, Red);
		carBody.addBox({0.0f, 1.35f, -0.65f}, {0.34f, 0.02f, 1.1f}, Red);
		// Bumpers, lights, wing, mirrors, exhaust.
		carBody.addBox({0.0f, 0.36f, 1.92f}, {1.74f, 0.22f, 0.16f}, DarkTrim);
		carBody.addBox({0.0f, 0.36f, -1.97f}, {1.74f, 0.22f, 0.16f}, DarkTrim);
		carBody.addBox({-0.52f, 0.60f, 1.94f}, {0.36f, 0.14f, 0.04f}, Headlight);
		carBody.addBox({0.52f, 0.60f, 1.94f}, {0.36f, 0.14f, 0.04f}, Headlight);
		carBody.addBox({-0.60f, 0.62f, -1.95f}, {0.30f, 0.12f, 0.04f}, Red);
		carBody.addBox({0.60f, 0.62f, -1.95f}, {0.30f, 0.12f, 0.04f}, Red);
		carBody.addBox({0.0f, 1.22f, -1.78f}, {1.50f, 0.05f, 0.34f}, White);
		carBody.addBox({-0.55f, 1.02f, -1.80f}, {0.06f, 0.40f, 0.10f}, DarkTrim);
		carBody.addBox({0.55f, 1.02f, -1.80f}, {0.06f, 0.40f, 0.10f}, DarkTrim);
		carBody.addBox({-0.98f, 0.95f, 0.50f}, {0.20f, 0.12f, 0.10f}, White);
		carBody.addBox({0.98f, 0.95f, 0.50f}, {0.20f, 0.12f, 0.10f}, White);
		carBody.addCylinder({-0.50f, 0.24f, -2.08f}, 2, 0.075f, 0.40f, 8, Metal, DarkTrim);
		carBody.addCylinder({-0.50f, 0.24f, -2.27f}, 2, 0.055f, 0.06f, 8, Rgb(20, 20, 22), Rgb(12, 12, 14));
		carBody.addBox({0.0f, 0.22f, 0.0f}, {1.5f, 0.10f, 3.4f}, DarkTrim);
	}

	// --- Wheel (axis along X, centred at the origin) --------------------------------
	{
		wheel.addCylinder({0, 0, 0}, 0, WheelRadius, 0.24f, 12, Tire, Tire);
		wheel.addCylinder({0, 0, 0}, 0, WheelRadius * 0.62f, 0.26f, 10, Rim, Rim);
		for (int spoke = 0; spoke < 5; ++spoke)
		{
			const float angle = spoke * 2.0f * Pi / 5.0f;
			wheel.addBox({0.0f, 0.0f, 0.0f}, {0.28f, 0.05f, WheelRadius * 1.1f}, DarkTrim,
				Transform::rotationX(angle));
		}
		wheel.addCylinder({0, 0, 0}, 0, 0.05f, 0.30f, 6, DarkTrim, DarkTrim);
	}

	// --- Interior (seen from inside the cabin) ----------------------------------------
	{
		// Roof and floor.
		interior.addQuad({-0.68f, 1.33f, -1.25f}, {-0.66f, 1.31f, -0.05f}, {0.66f, 1.31f, -0.05f},
			{0.68f, 1.33f, -1.25f}, Rgb(70, 70, 80), true);
		interior.addQuad({-0.80f, 0.32f, -1.55f}, {0.80f, 0.32f, -1.55f}, {0.80f, 0.32f, 0.60f},
			{-0.80f, 0.32f, 0.60f}, Rgb(36, 36, 42), true);
		// Door panels (inner sides) below the windows.
		interior.addQuad({-0.80f, 0.32f, -1.55f}, {-0.80f, 0.32f, 0.60f}, {-0.80f, 0.82f, 0.60f},
			{-0.80f, 0.82f, -1.55f}, Rgb(64, 64, 74), true);
		interior.addQuad({0.80f, 0.32f, 0.60f}, {0.80f, 0.32f, -1.55f}, {0.80f, 0.82f, -1.55f},
			{0.80f, 0.82f, 0.60f}, Rgb(64, 64, 74), true);
		// Rear hatch below the window and the rear window frame (window is open).
		interior.addQuad({0.80f, 0.32f, -1.55f}, {-0.80f, 0.32f, -1.55f}, {-0.80f, 0.86f, -1.55f},
			{0.80f, 0.86f, -1.55f}, Rgb(58, 58, 68), true);
		interior.addBox({0.0f, 0.87f, -1.45f}, {1.44f, 0.04f, 0.06f}, Rgb(88, 88, 100));
		interior.addBox({0.0f, 1.31f, -1.28f}, {1.44f, 0.04f, 0.06f}, Rgb(88, 88, 100));
		interior.addBox({-0.72f, 1.09f, -1.38f}, {0.05f, 0.46f, 0.10f}, Rgb(88, 88, 100));
		interior.addBox({0.72f, 1.09f, -1.38f}, {0.05f, 0.46f, 0.10f}, Rgb(88, 88, 100));
		// Roll cage.
		interior.addBox({-0.64f, 1.05f, -1.15f}, {0.06f, 0.56f, 0.06f}, Metal);
		interior.addBox({0.64f, 1.05f, -1.15f}, {0.06f, 0.56f, 0.06f}, Metal);
		interior.addBox({0.0f, 1.27f, -1.15f}, {1.30f, 0.06f, 0.06f}, Metal);
		interior.addBox({-0.62f, 1.25f, -0.55f}, {0.06f, 0.06f, 1.30f}, Metal);
		interior.addBox({0.62f, 1.25f, -0.55f}, {0.06f, 0.06f, 1.30f}, Metal);
		// Dashboard and windscreen frame.
		const Vec3 dash[8] = {
			{-0.80f, 0.72f, 0.30f}, {0.80f, 0.72f, 0.30f}, {0.80f, 0.72f, 0.75f}, {-0.80f, 0.72f, 0.75f},
			{-0.80f, 1.00f, 0.40f}, {0.80f, 1.00f, 0.40f}, {0.80f, 0.92f, 0.78f}, {-0.80f, 0.92f, 0.78f}};
		interior.addHexahedron(dash, Dash);
		interior.addBox({-0.40f, 0.97f, 0.42f}, {0.30f, 0.05f, 0.10f}, Rgb(24, 24, 28));
		interior.addBox({-0.40f, 0.985f, 0.41f}, {0.06f, 0.02f, 0.02f}, Rgb(230, 60, 40));
		interior.addBox({-0.30f, 0.985f, 0.41f}, {0.04f, 0.02f, 0.02f}, Rgb(60, 220, 90));
		interior.addBox({0.0f, 0.33f, -0.15f}, {0.30f, 0.30f, 1.30f}, Rgb(46, 46, 54)); // tunnel
		interior.addBox({0.10f, 0.62f, 0.20f}, {0.05f, 0.30f, 0.05f}, Metal);         // gear lever
		interior.addBox({0.10f, 0.78f, 0.20f}, {0.09f, 0.07f, 0.09f}, DarkTrim);
		// Seats.
		for (float side : {-0.40f, 0.40f})
		{
			interior.addBox({side, 0.50f, -0.20f}, {0.52f, 0.20f, 0.56f}, Seat);
			interior.addBox({side, 0.78f, -0.50f}, {0.52f, 0.70f, 0.14f}, Seat);
			interior.addBox({side - 0.22f, 0.55f, -0.10f}, {0.08f, 0.16f, 0.50f}, Rgb(70, 70, 84));
			interior.addBox({side + 0.22f, 0.55f, -0.10f}, {0.08f, 0.16f, 0.50f}, Rgb(70, 70, 84));
		}
	}

	// --- Driver (body only; head, hands and thumb are animated per frame) --------------
	{
		// Torso in a denim jacket, with lighter lapels and a white t-shirt.
		const Vec3 torso[8] = {
			{-0.62f, 0.62f, -0.34f}, {-0.18f, 0.62f, -0.34f}, {-0.18f, 0.62f, -0.06f}, {-0.62f, 0.62f, -0.06f},
			{-0.66f, 0.98f, -0.40f}, {-0.14f, 0.98f, -0.40f}, {-0.14f, 0.96f, -0.08f}, {-0.66f, 0.96f, -0.08f}};
		driverBody.addHexahedron(torso, Denim, Denim, Denim);
		driverBody.addBox({-0.30f, 0.86f, -0.075f}, {0.05f, 0.22f, 0.02f}, DenimLight);
		driverBody.addBox({-0.50f, 0.86f, -0.075f}, {0.05f, 0.22f, 0.02f}, DenimLight);
		driverBody.addBox({-0.40f, 0.86f, -0.08f}, {0.12f, 0.20f, 0.02f}, Shirt);
		driverBody.addBox({-0.40f, 0.70f, -0.075f}, {0.34f, 0.03f, 0.02f}, DenimLight); // hem seam
		driverBody.addBox({-0.40f, 0.99f, -0.24f}, {0.11f, 0.08f, 0.12f}, Skin);      // neck
		// Upper arms angle forward from the shoulders, forearms reach the wheel.
		for (float side : {-1.0f, 1.0f})
		{
			const Vec3 shoulder(-0.40f + side * 0.27f, 0.92f, -0.22f);
			const Vec3 elbow(-0.40f + side * 0.30f, 0.72f, 0.06f);
			const Vec3 hand = wheelSpace().apply({side * 0.16f, 0.10f, 0.0f});
			auto limb = [&](const Vec3& from, const Vec3& to, float thickness, const Rgb& color) {
				const Vec3 axis = to - from;
				const float len = length(axis);
				const Vec3 mid = (from + to) * 0.5f;
				const Vec3 dir = normalize(axis);
				const float yaw = std::atan2(dir.x, dir.z);
				const float pitch = -std::asin(std::max(-1.0f, std::min(1.0f, dir.y)));
				driverBody.addBox({0, 0, 0}, {thickness, thickness, len}, color,
					Transform::translation(mid) * Transform::rotationY(yaw) * Transform::rotationX(pitch));
			};
			limb(shoulder, elbow, 0.12f, Denim);
			limb(elbow, hand, 0.10f, Denim);
			driverBody.addBox({0, 0, 0}, {0.09f, 0.09f, 0.06f}, DenimLight,
				Transform::translation(lerp(elbow, hand, 0.82f)));
		}
		// Legs disappear under the dashboard.
		driverBody.addBox({-0.52f, 0.52f, 0.10f}, {0.16f, 0.16f, 0.60f}, Rgb(40, 52, 90));
		driverBody.addBox({-0.28f, 0.52f, 0.10f}, {0.16f, 0.16f, 0.60f}, Rgb(40, 52, 90));
	}

	// --- Steering wheel (wheel-local: ring in the XY plane) ------------------------------
	{
		const int segments = 14;
		for (int i = 0; i < segments; ++i)
		{
			const float a0 = static_cast<float>(i) / segments * 2.0f * Pi;
			const float a1 = static_cast<float>(i + 1) / segments * 2.0f * Pi;
			const Vec3 c0(std::cos(a0) * WheelRingRadius, std::sin(a0) * WheelRingRadius, 0.0f);
			const Vec3 c1(std::cos(a1) * WheelRingRadius, std::sin(a1) * WheelRingRadius, 0.0f);
			const Vec3 mid = (c0 + c1) * 0.5f;
			const float len = length(c1 - c0) * 1.05f;
			const float angle = std::atan2(c1.y - c0.y, c1.x - c0.x);
			steeringWheel.addBox({0, 0, 0}, {len, 0.032f, 0.030f}, Rgb(30, 30, 34),
				Transform::translation(mid) * Transform::rotationZ(angle));
		}
		for (float angle : {Pi * 0.0f, Pi * 0.78f, Pi * 1.22f})
		{
			// Spokes at 3 o'clock, ~10 o'clock and ~8 o'clock.
			const Vec3 mid(std::cos(angle) * WheelRingRadius * 0.5f, std::sin(angle) * WheelRingRadius * 0.5f, 0.0f);
			steeringWheel.addBox({0, 0, 0}, {WheelRingRadius * 0.95f, 0.030f, 0.026f}, Rgb(40, 40, 46),
				Transform::translation(mid) * Transform::rotationZ(angle));
		}
		steeringWheel.addCylinder({0, 0, 0.005f}, 2, 0.052f, 0.04f, 10, Rgb(46, 46, 54), Rgb(60, 60, 70));
		// Turbo module sits on the 3 o'clock spoke, inside the ring next to the right hand.
		steeringWheel.addBox({0.105f, 0.0f, -0.012f}, {0.062f, 0.048f, 0.026f}, Rgb(70, 70, 78));
		steeringWheel.addBox({0.105f, 0.0f, -0.026f}, {0.052f, 0.038f, 0.004f}, Rgb(24, 24, 28));
	}

	// --- Roadside tree (origin at the trunk base) -----------------------------------------
	{
		tree.addBox({0.0f, 0.9f, 0.0f}, {0.30f, 1.8f, 0.30f}, Trunk);
		addOctahedron(tree, {0.0f, 2.3f, 0.0f}, 1.6f, Canopy);
		addOctahedron(tree, {0.9f, 2.0f, 0.4f}, 1.0f, CanopyDark);
		addOctahedron(tree, {-0.8f, 2.1f, -0.3f}, 1.1f, Canopy);
	}
}

float Cinematic::worldScale(int shot, float u) const
{
	const float slow = config.cinematicTimeScale;
	if (shot <= 8)
		return slow;
	if (shot == 9)
		return slow + (0.35f - slow) * smoothstep(u);
	return 0.35f + (1.0f - 0.35f) * smoothstep(u);
}

int Cinematic::shotAt(float beat, float& u) const
{
	for (size_t i = 0; i < shots.size(); ++i)
	{
		if (beat < shots[i].endBeat || i + 1 == shots.size())
		{
			u = (beat - shots[i].startBeat) / std::max(0.001f, shots[i].endBeat - shots[i].startBeat);
			u = std::max(0.0f, std::min(1.0f, u));
			return static_cast<int>(i);
		}
	}
	u = 1.0f;
	return static_cast<int>(shots.size()) - 1;
}

void Cinematic::fireCues(float previousBeat, float beat)
{
	for (const SoundCue& cue : cues)
	{
		if (cue.beat > previousBeat && cue.beat <= beat)
		{
			if (sfxHandler)
				sfxHandler(cue.effect, cue.gain);
			if (cue.effect == TurboSfx::ExhaustPulse)
				smokePuffs.push_back(worldTime);
		}
	}
	if (loopHandler)
	{
		// Engine rumble rises with the build-up; jumps after the button press.
		const float base = 0.18f + 0.45f * std::min(1.0f, beat / 32.0f);
		loopHandler(TurboLoop::EngineRumble, beat >= 26.5f ? base + 0.35f : base);
	}
}

void Cinematic::animationParameters(float beat, float& thumb, float& cover, float& press, float& smirk,
	float& headYaw, float& brow) const
{
	thumb = ramp(beat, 20.2f, 21.9f);
	cover = ramp(beat, 23.5f, 24.6f);
	press = ramp(beat, 26.28f, 26.5f);
	smirk = ramp(beat, 17.2f, 18.3f);
	headYaw = 0.12f * ramp(beat, 16.6f, 18.8f);
	brow = ramp(beat, 22.2f, 22.7f) * (1.0f - 0.4f * ramp(beat, 22.8f, 23.0f));
}

Camera Cinematic::cameraFor(int shot, float u, float beat) const
{
	Camera camera;
	const float wobble = static_cast<float>(worldTime);
	switch (shot)
	{
		case 1: // Three-quarter orbit along the rear-left -> front-right diagonal.
		{
			// Start behind the rear-left corner, drift toward the left flank.
			const float angle = -2.45f + 0.50f * smoothstep(u);
			const float radius = 6.8f - 0.8f * smoothstep(u);
			camera.position = {std::sin(angle) * radius, 1.35f + 0.10f * u, std::cos(angle) * radius};
			camera.target = {0.2f, 0.55f, 0.3f};
			camera.verticalFov = 50.0f;
			break;
		}
		case 2: // Rear-left wheel.
			camera.position = {-2.05f + 0.25f * u, 0.42f + 0.10f * u, -2.55f + 0.45f * u};
			camera.target = {-WheelX + 0.1f, WheelRadius + 0.08f, -WheelZ + 0.1f};
			camera.verticalFov = 44.0f;
			break;
		case 3: // Exhaust.
		{
			const float tremor = 0.004f + 0.012f * u;
			camera.position = {-1.35f + 0.25f * u, 0.34f + 0.04f * u, -3.6f + 0.3f * u};
			camera.target = Vec3(-0.40f, 0.30f, -2.1f)
				+ Vec3(std::sin(wobble * 140.0f) * tremor, std::cos(wobble * 97.0f) * tremor, 0.0f);
			camera.verticalFov = 36.0f;
		}
			break;
		case 4: // Driver profile from the passenger seat, framed below the eyes.
			// Three-quarter view from the passenger side of the dashboard so the
			// smirk reads; framed from just below the eyes to the chest.
			camera.position = {0.34f - 0.03f * u, 0.99f, 0.24f};
			camera.target = {DriverHead.x, 0.885f + 0.012f * u, DriverHead.z + 0.02f};
			camera.verticalFov = 33.0f;
			break;
		case 5: // Hands on the wheel, over the right shoulder.
			camera.position = {-0.08f - 0.03f * u, 1.24f, -0.04f};
			camera.target = wheelSpace().apply({0.05f, 0.02f, 0.0f});
			camera.verticalFov = 44.0f;
			break;
		case 6: // Eyes.
			camera.position = {DriverHead.x + 0.04f, DriverHead.y + 0.05f, DriverHead.z + 0.56f};
			camera.target = {DriverHead.x, DriverHead.y + 0.04f, DriverHead.z};
			camera.verticalFov = 21.0f;
			break;
		case 7: // Safety cover, extreme close-up.
		{
			const Vec3 module = wheelSpace().apply({0.105f, 0.0f, -0.02f});
			camera.position = wheelSpace().apply({0.02f + 0.02f * u, 0.11f, -0.20f});
			camera.target = module;
			camera.verticalFov = 30.0f - 4.0f * u;
			break;
		}
		case 8: // Button press with a little shake.
		{
			const Vec3 module = wheelSpace().apply({0.105f, 0.0f, -0.02f});
			const float shake = std::max(0.0f, 1.0f - (beat - 26.5f) / 1.2f) * (beat >= 26.5f ? 1.0f : 0.0f);
			camera.position = wheelSpace().apply({0.0f, 0.13f, -0.24f})
				+ Vec3(std::sin(wobble * 91.0f) * 0.006f * shake, std::cos(wobble * 73.0f) * 0.006f * shake, 0.0f);
			camera.target = module + Vec3(0.0f, 0.0f, 0.0f);
			camera.verticalFov = 30.0f;
			break;
		}
		case 9: // Rapid travel backward through the cabin toward the rear window.
		{
			const float e = u * u;
			camera.position = lerp({-0.18f, 1.02f, 0.28f}, {0.0f, 1.08f, -1.35f}, e);
			camera.target = camera.position + Vec3(0.0f, 0.02f, -1.0f);
			camera.verticalFov = 60.0f + 20.0f * e;
			break;
		}
		case 10: // Out through the rear window, whip around into the chase camera.
		{
			const float e = smoothstep(u);
			const float yaw = Pi * e; // 0 = looking backward, Pi = looking forward
			camera.position = lerp({0.0f, 1.06f, -1.35f}, {0.0f, 2.2f, -7.2f}, e);
			const Vec3 dir(std::sin(yaw) * 0.0f, 0.0f, -std::cos(yaw));
			camera.target = camera.position + dir * 8.0f + Vec3(0.0f, -0.25f - 0.9f * e, 0.0f) + Vec3(0.0f, 0.0f, 0.0f);
			// Look slightly toward the car once we face forward.
			camera.target = lerp(camera.target, Vec3(0.0f, 0.55f, 3.0f), e * e);
			camera.verticalFov = 80.0f - 24.0f * e;
			break;
		}
		default:
			camera.position = {0.0f, 2.2f, -7.2f};
			camera.target = {0.0f, 0.55f, 3.0f};
			camera.verticalFov = 56.0f;
			break;
	}
	// Subtle handheld drift keeps every shot alive.
	camera.position = camera.position + Vec3(std::sin(wobble * 0.9f) * 0.004f, std::sin(wobble * 1.3f) * 0.003f, 0.0f);
	return camera;
}

void Cinematic::drawWorld(float beat, bool interiorView)
{
	(void)beat;
	const float scroll = std::fmod(static_cast<float>(worldTime) * CarSpeed, 8.0f);
	// Distant haze band and mountains (unlit, far away).
	Mesh backdrop;
	backdrop.addQuad({-600.0f, -2.0f, 320.0f}, {600.0f, -2.0f, 320.0f}, {600.0f, 14.0f, 320.0f},
		{-600.0f, 14.0f, 320.0f}, palette.horizon, true, true);
	backdrop.addQuad({-600.0f, -2.0f, -330.0f}, {600.0f, -2.0f, -330.0f}, {600.0f, 14.0f, -330.0f},
		{-600.0f, 14.0f, -330.0f}, palette.horizon, true, true);
	const float peaks[8][3] = {{-210, 46, 340}, {-120, 62, 360}, {-30, 40, 350}, {60, 70, 365},
		{150, 50, 345}, {240, 58, 360}, {-260, 34, 335}, {330, 44, 350}};
	for (const float* peak : peaks)
	{
		backdrop.addTriangle({peak[0] - 90.0f, 0.0f, peak[2]}, {peak[0] + 90.0f, 0.0f, peak[2]},
			{peak[0], peak[1], peak[2]}, palette.mountain, true, true);
		backdrop.addTriangle({peak[0] - 90.0f, 0.0f, -peak[2]}, {peak[0] + 90.0f, 0.0f, -peak[2]},
			{peak[0], peak[1] * 0.8f, -peak[2]}, palette.mountain, true, true);
	}
	retro.submit(backdrop, Transform::identity());

	// Ground plane, road strip and scrolling bands.
	Mesh ground;
	ground.addQuad({-400.0f, 0.0f, -400.0f}, {-3.4f, 0.0f, -400.0f}, {-3.4f, 0.0f, 400.0f},
		{-400.0f, 0.0f, 400.0f}, palette.ground, true, true);
	ground.addQuad({3.4f, 0.0f, -400.0f}, {400.0f, 0.0f, -400.0f}, {400.0f, 0.0f, 400.0f},
		{3.4f, 0.0f, 400.0f}, palette.ground, true, true);
	for (int band = -12; band < 12; ++band)
	{
		const float z0 = band * 8.0f - scroll;
		const float z1 = z0 + 4.0f;
		ground.addQuad({-3.4f, 0.003f, z0}, {3.4f, 0.003f, z0}, {3.4f, 0.003f, z1}, {-3.4f, 0.003f, z1},
			palette.roadAlt, true, true);
		ground.addQuad({-3.4f, 0.003f, z1}, {3.4f, 0.003f, z1}, {3.4f, 0.003f, z1 + 4.0f},
			{-3.4f, 0.003f, z1 + 4.0f}, palette.road, true, true);
		ground.addQuad({-40.0f, 0.002f, z0}, {-3.4f, 0.002f, z0}, {-3.4f, 0.002f, z1}, {-40.0f, 0.002f, z1},
			palette.groundAlt, true, true);
		ground.addQuad({3.4f, 0.002f, z0}, {40.0f, 0.002f, z0}, {40.0f, 0.002f, z1}, {3.4f, 0.002f, z1},
			palette.groundAlt, true, true);
		// Pale road edge lines.
		ground.addQuad({-3.5f, 0.004f, z0}, {-3.3f, 0.004f, z0}, {-3.3f, 0.004f, z1 + 4.0f}, {-3.5f, 0.004f, z1 + 4.0f},
			scaleRgb(palette.road, 1.25f), true, true);
		ground.addQuad({3.3f, 0.004f, z0}, {3.5f, 0.004f, z0}, {3.5f, 0.004f, z1 + 4.0f}, {3.3f, 0.004f, z1 + 4.0f},
			scaleRgb(palette.road, 1.25f), true, true);
	}
	retro.submit(ground, Transform::identity());

	if (interiorView)
		return;

	// Trees and grass tufts on both sides, wrapping as the world scrolls.
	const float treeScroll = std::fmod(static_cast<float>(worldTime) * CarSpeed, 60.0f);
	for (int i = 0; i < 8; ++i)
	{
		const float side = (i % 2 == 0) ? -1.0f : 1.0f;
		float z = i * 15.0f - treeScroll - 20.0f;
		while (z < -30.0f)
			z += 60.0f;
		const float x = side * (7.0f + (i * 37 % 5));
		retro.submit(tree, Transform::translation({x, 0.0f, z}) * Transform::scale(0.9f + 0.1f * (i % 3), 0.85f + 0.12f * (i % 2), 1.0f));
	}
	Mesh grass;
	const float sway = std::sin(static_cast<float>(worldTime) * 2.0f) * 0.08f;
	for (int i = 0; i < 26; ++i)
	{
		const float side = (i % 2 == 0) ? -1.0f : 1.0f;
		float z = (i * 13 % 40) - scroll * 1.0f - 10.0f;
		const float x = side * (3.6f + (i * 7 % 9) * 0.35f);
		const float h = 0.25f + (i % 3) * 0.08f;
		grass.addTriangle({x - 0.12f, 0.0f, z}, {x + 0.12f, 0.0f, z}, {x + sway, h, z + 0.04f}, Rgb(150, 150, 60), true);
		grass.addTriangle({x, 0.0f, z - 0.12f}, {x, 0.0f, z + 0.12f}, {x + 0.03f, h * 0.8f, z + sway}, Rgb(120, 130, 50), true);
	}
	retro.submit(grass, Transform::identity());

	// Slow dust behind the rear wheels and a few kicked-up stones.
	Mesh dust;
	const float dustColorMix = 0.35f;
	const Rgb dustColor = mixRgb(palette.ground, Rgb(240, 230, 200), dustColorMix);
	for (int i = 0; i < 14; ++i)
	{
		const float side = (i % 2 == 0) ? -1.0f : 1.0f;
		const float life = std::fmod(static_cast<float>(worldTime) * 0.9f + i * 0.37f, 1.6f);
		const float x = side * WheelX + side * life * 0.4f + std::sin(i * 2.1f) * 0.15f;
		const float y = 0.05f + life * 0.55f;
		const float z = -WheelZ - 0.3f - life * 2.2f;
		const float size = 0.05f + life * 0.16f;
		addOctahedron(dust, {x, y, z}, size, dustColor);
	}
	retro.submit(dust, Transform::identity());
}

void Cinematic::drawCar(float beat, bool withRearGlass)
{
	(void)beat;
	const float bob = std::sin(static_cast<float>(worldTime) * 3.1f) * 0.012f;
	const float bodyRoll = std::sin(static_cast<float>(worldTime) * 2.3f) * 0.006f;
	retro.submit(carBody, Transform::translation({0.0f, bob, 0.0f}) * Transform::rotationZ(bodyRoll));
	if (withRearGlass)
		retro.submit(rearGlass, Transform::translation({0.0f, bob, 0.0f}) * Transform::rotationZ(bodyRoll));
	const float wheelAngle = static_cast<float>(worldTime) * 2.4f;
	for (int i = 0; i < 4; ++i)
	{
		const float x = (i % 2 == 0) ? -WheelX : WheelX;
		const float z = (i < 2) ? -WheelZ : WheelZ;
		const float bounce = std::sin(static_cast<float>(worldTime) * 3.1f + i * 1.7f) * 0.008f;
		retro.submit(wheel, Transform::translation({x, WheelRadius + bounce, z}) * Transform::rotationX(-wheelAngle + i * 0.9f));
	}
}

void Cinematic::drawSmoke()
{
	Mesh smoke;
	for (double birth : smokePuffs)
	{
		const float age = static_cast<float>(worldTime - birth);
		if (age < 0.0f || age > 1.8f)
			continue;
		const float radius = 0.05f + age * 0.22f;
		const Vec3 center(-0.50f - age * 0.15f, 0.22f + age * 0.30f, -2.20f - age * 0.9f);
		addOctahedron(smoke, center, radius, mixRgb(Smoke, palette.horizon, age / 1.8f));
		addOctahedron(smoke, center + Vec3(0.06f, 0.04f * age, -0.12f * age), radius * 0.7f, Smoke);
	}
	retro.submit(smoke, Transform::identity());
}

void Cinematic::drawInterior(float beat)
{
	(void)beat;
	retro.submit(interior, Transform::identity());
}

void Cinematic::drawDriver(float beat)
{
	float thumb, cover, press, smirk, headYaw, brow;
	animationParameters(beat, thumb, cover, press, smirk, headYaw, brow);
	const float breathe = std::sin(static_cast<float>(worldTime) * 1.7f) * 0.004f;

	retro.submit(driverBody, Transform::translation({0.0f, breathe, 0.0f}));

	// Head (built around the neck pivot so it can turn).
	Mesh head;
	head.addBox({0.0f, 0.12f, 0.0f}, {0.19f, 0.24f, 0.22f}, Skin);
	head.addBox({0.0f, 0.215f, -0.03f}, {0.20f, 0.07f, 0.20f}, Hair);
	head.addBox({0.0f, 0.13f, -0.115f}, {0.20f, 0.20f, 0.03f}, Hair);
	head.addBox({0.0f, 0.10f, 0.125f}, {0.04f, 0.06f, 0.04f}, scaleRgb(Skin, 0.94f)); // nose
	head.addBox({0.0f, 0.18f, 0.115f}, {0.13f, 0.04f, 0.006f}, scaleRgb(Skin, 1.05f)); // brow ridge
	for (float side : {-1.0f, 1.0f})
	{
		head.addBox({side * 0.045f, 0.155f, 0.112f}, {0.04f, 0.018f, 0.006f}, Rgb(250, 250, 250));
		head.addBox({side * 0.045f, 0.155f, 0.115f}, {0.016f, 0.016f, 0.006f}, Rgb(30, 26, 26));
		head.addBox({side * 0.048f, 0.178f + 0.008f * brow, 0.114f}, {0.05f, 0.010f, 0.006f}, Hair,
			Transform::translation({side * 0.048f, 0.178f + 0.008f * brow, 0.114f})
			* Transform::rotationZ(side * -0.12f * brow) * Transform::translation({-side * 0.048f, -(0.178f + 0.008f * brow), -0.114f}));
		head.addBox({side * 0.10f, 0.12f, -0.01f}, {0.02f, 0.05f, 0.03f}, scaleRgb(Skin, 0.9f)); // ears
	}
	// Mouth: a straight line that lifts on the right side into a smirk.
	{
		const Vec3 mouthCenter(0.012f * smirk, 0.055f + 0.004f * smirk, 0.112f);
		head.addBox({0, 0, 0}, {0.064f, 0.010f, 0.006f}, Rgb(120, 60, 56),
			Transform::translation(mouthCenter) * Transform::rotationZ(0.35f * smirk));
		head.addBox({0.0f, 0.044f - 0.002f * smirk, 0.106f}, {0.05f, 0.012f, 0.012f}, scaleRgb(Skin, 0.96f)); // chin
		// Mouth corners wrap onto the cheeks so the smirk reads in profile; the
		// right corner lifts and the cheek creases.
		head.addBox({0.088f, 0.055f + 0.018f * smirk, 0.096f}, {0.02f, 0.010f, 0.026f}, Rgb(120, 60, 56));
		head.addBox({-0.088f, 0.055f, 0.096f}, {0.02f, 0.010f, 0.022f}, Rgb(120, 60, 56));
		if (smirk > 0.2f)
		{
			head.addBox({0.092f, 0.085f, 0.085f}, {0.012f, 0.045f * smirk, 0.010f}, scaleRgb(Skin, 0.84f));
			head.addBox({0.096f, 0.075f + 0.02f * smirk, 0.10f}, {0.012f, 0.012f, 0.012f}, scaleRgb(Skin, 1.08f));
		}
	}
	const Transform headPose = Transform::translation({DriverHead.x, DriverHead.y - 0.12f + breathe, DriverHead.z})
		* Transform::rotationY(-headYaw + std::sin(static_cast<float>(worldTime) * 0.8f) * 0.01f);
	retro.submit(head, headPose);

	// Steering wheel with hands; the right thumb animates toward the module.
	const float vibration = std::sin(static_cast<float>(worldTime) * 31.0f) * 0.004f
		* (0.4f + 0.6f * std::min(1.0f, beat / 26.0f));
	const Transform wheelPose = wheelSpace() * Transform::rotationZ(vibration);
	retro.submit(steeringWheel, wheelPose);

	Mesh hands;
	for (float side : {-1.0f, 1.0f})
	{
		const Vec3 palm(side * 0.165f, 0.10f, 0.0f);
		hands.addBox(palm + Vec3(0.0f, 0.0f, -0.02f), {0.075f, 0.062f, 0.065f}, Skin);
		hands.addBox(palm + Vec3(side * 0.005f, -0.018f, 0.022f), {0.07f, 0.03f, 0.03f}, scaleRgb(Skin, 0.92f)); // fingers wrap the rim
		hands.addBox(palm + Vec3(side * 0.005f, 0.028f, 0.018f), {0.07f, 0.024f, 0.03f}, scaleRgb(Skin, 0.96f));
		if (side < 0.0f)
			hands.addBox(palm + Vec3(0.045f, 0.012f, -0.03f), {0.022f, 0.045f, 0.022f}, Skin); // left thumb at rest
	}
	// Right thumb: rests along the rim, then swings inward over the module and presses.
	{
		const Vec3 rest(0.118f, 0.128f, -0.034f);
		const Vec3 hover(0.108f, 0.036f, -0.062f);
		const Vec3 pressed(0.106f, 0.010f, -0.040f);
		Vec3 tip = lerp(rest, hover, thumb);
		tip = lerp(tip, pressed, press);
		const Vec3 base(0.135f, 0.11f, -0.03f);
		const Vec3 axis = tip - base;
		const Vec3 mid = (base + tip) * 0.5f;
		const float len = length(axis) + 0.02f;
		const float angle = std::atan2(axis.y, axis.x);
		const float lift = std::atan2(axis.z, std::sqrt(axis.x * axis.x + axis.y * axis.y));
		hands.addBox({0, 0, 0}, {len, 0.024f, 0.024f}, Skin,
			Transform::translation(mid) * Transform::rotationZ(angle) * Transform::rotationY(lift));
		hands.addBox({0, 0, 0}, {0.026f, 0.022f, 0.022f}, scaleRgb(Skin, 1.04f), Transform::translation(tip));
	}
	// Turbo module: button and hinged safety cover.
	{
		const float depress = 0.010f * press;
		hands.addCylinder({0.105f, 0.0f, -0.030f + depress}, 2, 0.016f, 0.014f, 8,
			press > 0.99f ? Rgb(255, 120, 90) : ButtonRed, press > 0.99f ? Rgb(255, 200, 160) : Rgb(255, 90, 90));
		// Cover hinged along its top edge (y = +0.019), opening away from the thumb.
		const float openAngle = -1.95f * cover;
		const Vec3 hinge(0.105f, 0.021f, -0.028f);
		hands.addBox({0.0f, -0.019f, 0.0f}, {0.052f, 0.040f, 0.005f}, CoverOrange,
			Transform::translation(hinge) * Transform::rotationX(openAngle));
		hands.addBox({0.0f, -0.019f, -0.004f}, {0.040f, 0.012f, 0.004f}, scaleRgb(CoverOrange, 0.8f),
			Transform::translation(hinge) * Transform::rotationX(openAngle));
	}
	retro.submit(hands, wheelPose);
}

void Cinematic::drawGameShot(SDL_Texture* snapshot, float u, float beat)
{
	(void)beat;
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (snapshot == nullptr)
		return;
	// Slow push-in on the car with a hint of tilt: the camera "begins moving".
	const float scale = 1.0f + 0.30f * smoothstep(u);
	const float anchorX = carRect.w > 0 ? carRect.x + carRect.w * 0.5f : width * 0.5f;
	const float anchorY = carRect.h > 0 ? carRect.y + carRect.h * 0.45f : height * 0.8f;
	SDL_Rect destination;
	destination.w = static_cast<int>(std::lround(width * scale));
	destination.h = static_cast<int>(std::lround(height * scale));
	destination.x = static_cast<int>(std::lround(anchorX - anchorX * scale));
	destination.y = static_cast<int>(std::lround(anchorY - anchorY * scale + 4.0f * smoothstep(u)));
	SDL_SetTextureBlendMode(snapshot, SDL_BLENDMODE_NONE);
	SDL_RenderCopyEx(renderer, snapshot, nullptr, &destination, -1.6 * smoothstep(u), nullptr, SDL_FLIP_NONE);
	// Letterbox bars grow in.
	const int bars = static_cast<int>(std::lround(12.0f * smoothstep(u * 1.5f)));
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_Rect top = {0, 0, width, bars};
	SDL_Rect bottom = {0, height - bars, width, bars};
	SDL_RenderFillRect(renderer, &top);
	SDL_RenderFillRect(renderer, &bottom);
	// Drifting dust motes.
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	for (int i = 0; i < 18; ++i)
	{
		const float phase = static_cast<float>(worldTime) * 0.5f + i * 0.61f;
		const float x = std::fmod(i * 37.0f + std::sin(phase) * 6.0f + static_cast<float>(worldTime) * 3.0f, static_cast<float>(width));
		const float y = std::fmod(i * 53.0f + static_cast<float>(worldTime) * (4.0f + i % 3), static_cast<float>(height));
		SDL_SetRenderDrawColor(renderer, 250, 236, 200, static_cast<Uint8>(90 + (i % 4) * 30));
		SDL_RenderDrawPoint(renderer, static_cast<int>(x), static_cast<int>(y));
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

CinematicFrame Cinematic::render(double t, SDL_Texture* gameSnapshot)
{
	CinematicFrame frame;
	const float beat = static_cast<float>(t / beatLength());
	float u = 0.0f;
	const int shot = shotAt(beat, u);
	const double dt = std::max(0.0, t - lastTime);
	worldTime += dt * worldScale(shot, u);
	lastTime = t;
	fireCues(lastBeat, beat);
	lastBeat = beat;

	frame.shotIndex = shot;
	frame.shotName = shots[shot].name;
	frame.finished = t >= dropTime();
	frame.gameTimeScale = 0.0f;
	frame.gameBlend = 0.0f;

	if (shot == 0)
	{
		frame.gameTimeScale = config.activationTimeScale * (1.0f - 0.7f * smoothstep(u));
		drawGameShot(gameSnapshot, u, beat);
		return frame;
	}

	RenderSettings settings;
	settings.lightDirection = {0.35f, 1.0f, -0.45f};
	settings.fog = true;
	settings.fogColor = palette.horizon;
	settings.fogStart = 40.0f;
	settings.fogEnd = 260.0f;
	const bool interiorShot = shot >= 4 && shot <= 9;
	if (interiorShot)
		settings.ambient = 0.62f;

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(renderer, palette.sky.r, palette.sky.g, palette.sky.b, 255);
	SDL_RenderClear(renderer);

	const Camera camera = cameraFor(shot, u, beat);
	retro.begin(camera, settings);
	drawWorld(beat, interiorShot && shot != 9);
	if (shot == 9 || shot == 10)
	{
		// Interior plus exterior so the exit through the rear window is continuous.
		drawInterior(beat);
		drawDriver(beat);
		drawCar(beat, shot == 10 && u > 0.2f);
	}
	else if (interiorShot)
	{
		drawInterior(beat);
		drawDriver(beat);
	}
	else
	{
		drawCar(beat, true);
		if (shot == 3)
			drawSmoke();
	}
	retro.flush();

	// Button-press flash.
	if (shot == 8 && beat >= 26.5f && beat < 26.72f)
	{
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 255, 240, 200, static_cast<Uint8>(200 * (1.0f - (beat - 26.5f) / 0.22f)));
		SDL_RenderFillRect(renderer, nullptr);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	}

	if (shot == 10)
	{
		frame.gameTimeScale = 0.25f + 0.75f * smoothstep(u);
		const float blend = ramp(u, 0.62f, 1.0f);
		frame.gameBlend = blend;
		if (gameSnapshot != nullptr && blend > 0.0f)
		{
			SDL_SetTextureBlendMode(gameSnapshot, SDL_BLENDMODE_BLEND);
			SDL_SetTextureAlphaMod(gameSnapshot, static_cast<Uint8>(255 * blend));
			SDL_RenderCopy(renderer, gameSnapshot, nullptr, nullptr);
			SDL_SetTextureAlphaMod(gameSnapshot, 255);
			SDL_SetTextureBlendMode(gameSnapshot, SDL_BLENDMODE_NONE);
		}
	}
	return frame;
}
