#pragma once

#include <SDL.h>
#include <cstdint>
#include <vector>

// A deliberately tiny flat-shaded polygon renderer used for the turbo
// cinematic. Everything is drawn with SDL_RenderGeometry into the 128x160
// guest-sized target, colours are quantised to the T610's RGB332 palette and
// there is no texturing: the look is meant to match an early phone racer.

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	Vec3() {}
	Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
	Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
	Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
	Vec3 operator*(float scale) const { return {x * scale, y * scale, z * scale}; }
	Vec3 operator-() const { return {-x, -y, -z}; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float length(const Vec3& v);
Vec3 normalize(const Vec3& v);
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Rgb {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	Rgb() {}
	Rgb(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
};

Rgb scaleRgb(const Rgb& color, float factor);
Rgb mixRgb(const Rgb& a, const Rgb& b, float t);
Rgb quantizeRgb332(const Rgb& color);

// Rigid transform: rotation matrix (row major) plus translation.
struct Transform {
	float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	Vec3 t;
	static Transform identity() { return Transform(); }
	static Transform translation(const Vec3& t);
	static Transform rotationX(float radians);
	static Transform rotationY(float radians);
	static Transform rotationZ(float radians);
	static Transform scale(float sx, float sy, float sz);
	Transform operator*(const Transform& other) const; // this after other
	Vec3 apply(const Vec3& p) const;
	Vec3 applyVector(const Vec3& v) const;
};

struct Triangle {
	Vec3 a;
	Vec3 b;
	Vec3 c;
	Rgb color;
	bool unlit = false;
	bool doubleSided = false;
};

class Mesh {
	public:
		std::vector<Triangle> triangles;
		void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Rgb& color,
			bool doubleSided = false, bool unlit = false);
		// Points in order around the quad; the normal follows the right-hand rule.
		void addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Rgb& color,
			bool doubleSided = false, bool unlit = false);
		void addBox(const Vec3& center, const Vec3& size, const Rgb& color);
		void addBox(const Vec3& center, const Vec3& size, const Rgb& color, const Transform& transform);
		// Eight corners: bottom face p[0..3] then top face p[4..7], both listed
		// counter-clockwise when seen from above.
		void addHexahedron(const Vec3 points[8], const Rgb& color);
		void addHexahedron(const Vec3 points[8], const Rgb& top, const Rgb& sides, const Rgb& bottom);
		// Cylinder along the given axis (0 = x, 1 = y, 2 = z).
		void addCylinder(const Vec3& center, int axis, float radius, float length, int segments,
			const Rgb& sideColor, const Rgb& capColor);
		void append(const Mesh& other, const Transform& transform);
		void transform(const Transform& transform);
		void clear() { triangles.clear(); }
};

struct Camera {
	Vec3 position{0, 1, -5};
	Vec3 target{0, 0, 0};
	float verticalFov = 55.0f; // degrees
	float roll = 0.0f;         // radians
};

struct RenderSettings {
	Vec3 lightDirection{0.4f, 1.0f, -0.5f}; // towards the light
	float ambient = 0.55f;
	bool quantize = true;
	bool fog = false;
	Rgb fogColor{160, 190, 235};
	float fogStart = 30.0f;
	float fogEnd = 90.0f;
	float nearPlane = 0.04f;
};

class RetroRenderer {
	public:
		RetroRenderer(SDL_Renderer* renderer, int width, int height);
		void begin(const Camera& camera, const RenderSettings& settings);
		void submit(const Mesh& mesh, const Transform& transform);
		void flush();
		// Screen projection of a world point (returns false when behind the camera).
		bool project(const Vec3& world, float& sx, float& sy, float& depth) const;

	private:
		struct ScreenTriangle {
			SDL_Vertex v[3];
			float depth;
		};
		SDL_Renderer* renderer;
		int width;
		int height;
		Camera camera;
		RenderSettings settings;
		Vec3 right;
		Vec3 up;
		Vec3 forward;
		float focal = 1.0f;
		std::vector<ScreenTriangle> queue;
		Vec3 toView(const Vec3& world) const;
		void pushClipped(const Vec3 view[3], const Rgb& color);
};
