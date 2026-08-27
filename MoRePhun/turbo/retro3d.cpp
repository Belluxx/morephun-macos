#include "retro3d.h"

#include <algorithm>
#include <cmath>

float length(const Vec3& v)
{
	return std::sqrt(dot(v, v));
}

Vec3 normalize(const Vec3& v)
{
	const float len = length(v);
	return len > 1e-6f ? v * (1.0f / len) : Vec3(0, 0, 0);
}

Rgb scaleRgb(const Rgb& color, float factor)
{
	auto clamp = [](float value) { return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, value))); };
	return {clamp(color.r * factor), clamp(color.g * factor), clamp(color.b * factor)};
}

Rgb mixRgb(const Rgb& a, const Rgb& b, float t)
{
	t = std::max(0.0f, std::min(1.0f, t));
	return {static_cast<uint8_t>(a.r + (b.r - a.r) * t), static_cast<uint8_t>(a.g + (b.g - a.g) * t),
		static_cast<uint8_t>(a.b + (b.b - a.b) * t)};
}

Rgb quantizeRgb332(const Rgb& color)
{
	const uint8_t r = static_cast<uint8_t>(((color.r + 18) / 36) * 255 / 7);
	const uint8_t g = static_cast<uint8_t>(((color.g + 18) / 36) * 255 / 7);
	const uint8_t b = static_cast<uint8_t>(((color.b + 42) / 85) * 255 / 3);
	return {r, g, b};
}

Transform Transform::translation(const Vec3& t)
{
	Transform result;
	result.t = t;
	return result;
}

Transform Transform::rotationX(float radians)
{
	Transform result;
	const float c = std::cos(radians);
	const float s = std::sin(radians);
	result.m[1][1] = c; result.m[1][2] = -s;
	result.m[2][1] = s; result.m[2][2] = c;
	return result;
}

Transform Transform::rotationY(float radians)
{
	Transform result;
	const float c = std::cos(radians);
	const float s = std::sin(radians);
	result.m[0][0] = c; result.m[0][2] = s;
	result.m[2][0] = -s; result.m[2][2] = c;
	return result;
}

Transform Transform::rotationZ(float radians)
{
	Transform result;
	const float c = std::cos(radians);
	const float s = std::sin(radians);
	result.m[0][0] = c; result.m[0][1] = -s;
	result.m[1][0] = s; result.m[1][1] = c;
	return result;
}

Transform Transform::scale(float sx, float sy, float sz)
{
	Transform result;
	result.m[0][0] = sx;
	result.m[1][1] = sy;
	result.m[2][2] = sz;
	return result;
}

Transform Transform::operator*(const Transform& other) const
{
	Transform result;
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 3; ++column)
			result.m[row][column] = m[row][0] * other.m[0][column] + m[row][1] * other.m[1][column]
				+ m[row][2] * other.m[2][column];
	result.t = applyVector(other.t) + t;
	return result;
}

Vec3 Transform::apply(const Vec3& p) const
{
	return applyVector(p) + t;
}

Vec3 Transform::applyVector(const Vec3& v) const
{
	return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
		m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
		m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

void Mesh::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Rgb& color,
	bool doubleSided, bool unlit)
{
	Triangle triangle;
	triangle.a = a;
	triangle.b = b;
	triangle.c = c;
	triangle.color = color;
	triangle.doubleSided = doubleSided;
	triangle.unlit = unlit;
	triangles.push_back(triangle);
}

void Mesh::addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Rgb& color,
	bool doubleSided, bool unlit)
{
	addTriangle(a, b, c, color, doubleSided, unlit);
	addTriangle(a, c, d, color, doubleSided, unlit);
}

void Mesh::addBox(const Vec3& center, const Vec3& size, const Rgb& color)
{
	addBox(center, size, color, Transform::identity());
}

void Mesh::addBox(const Vec3& center, const Vec3& size, const Rgb& color, const Transform& transform)
{
	const Vec3 h = size * 0.5f;
	Vec3 points[8] = {
		{center.x - h.x, center.y - h.y, center.z - h.z}, {center.x + h.x, center.y - h.y, center.z - h.z},
		{center.x + h.x, center.y - h.y, center.z + h.z}, {center.x - h.x, center.y - h.y, center.z + h.z},
		{center.x - h.x, center.y + h.y, center.z - h.z}, {center.x + h.x, center.y + h.y, center.z - h.z},
		{center.x + h.x, center.y + h.y, center.z + h.z}, {center.x - h.x, center.y + h.y, center.z + h.z}};
	for (Vec3& point : points)
		point = transform.apply(point);
	addHexahedron(points, color);
}

void Mesh::addHexahedron(const Vec3 points[8], const Rgb& color)
{
	addHexahedron(points, color, color, color);
}

void Mesh::addHexahedron(const Vec3 p[8], const Rgb& top, const Rgb& sides, const Rgb& bottom)
{
	// Bottom p0 p1 p2 p3 (p0 -> p1 along +X, p1 -> p2 along +Z), top p4 p5 p6 p7.
	// Winding chosen so cross(b - a, c - a) points outward for each face.
	addQuad(p[0], p[1], p[2], p[3], bottom); // bottom, facing down
	addQuad(p[4], p[7], p[6], p[5], top);    // top, facing up
	addQuad(p[0], p[4], p[5], p[1], sides);  // side 0-1 (-Z)
	addQuad(p[1], p[5], p[6], p[2], sides);  // side 1-2 (+X)
	addQuad(p[2], p[6], p[7], p[3], sides);  // side 2-3 (+Z)
	addQuad(p[3], p[7], p[4], p[0], sides);  // side 3-0 (-X)
}

void Mesh::addCylinder(const Vec3& center, int axis, float radius, float halfLengthTimesTwo, int segments,
	const Rgb& sideColor, const Rgb& capColor)
{
	const float half = halfLengthTimesTwo * 0.5f;
	auto point = [&](float angle, float along) {
		const float c = std::cos(angle) * radius;
		const float s = std::sin(angle) * radius;
		switch (axis)
		{
			case 0: return Vec3(center.x + along, center.y + c, center.z + s);
			case 1: return Vec3(center.x + s, center.y + along, center.z + c);
			default: return Vec3(center.x + c, center.y + s, center.z + along);
		}
	};
	Vec3 capA;
	Vec3 capB;
	switch (axis)
	{
		case 0: capA = {center.x - half, center.y, center.z}; capB = {center.x + half, center.y, center.z}; break;
		case 1: capA = {center.x, center.y - half, center.z}; capB = {center.x, center.y + half, center.z}; break;
		default: capA = {center.x, center.y, center.z - half}; capB = {center.x, center.y, center.z + half}; break;
	}
	for (int i = 0; i < segments; ++i)
	{
		const float a0 = static_cast<float>(i) / segments * 6.2831853f;
		const float a1 = static_cast<float>(i + 1) / segments * 6.2831853f;
		const Vec3 p0 = point(a0, -half);
		const Vec3 p1 = point(a1, -half);
		const Vec3 p2 = point(a1, half);
		const Vec3 p3 = point(a0, half);
		addQuad(p0, p3, p2, p1, sideColor, true);
		addTriangle(capA, p0, p1, capColor, true);
		addTriangle(capB, p2, p3, capColor, true);
	}
}

void Mesh::append(const Mesh& other, const Transform& transform)
{
	for (Triangle triangle : other.triangles)
	{
		triangle.a = transform.apply(triangle.a);
		triangle.b = transform.apply(triangle.b);
		triangle.c = transform.apply(triangle.c);
		triangles.push_back(triangle);
	}
}

void Mesh::transform(const Transform& transform)
{
	for (Triangle& triangle : triangles)
	{
		triangle.a = transform.apply(triangle.a);
		triangle.b = transform.apply(triangle.b);
		triangle.c = transform.apply(triangle.c);
	}
}

RetroRenderer::RetroRenderer(SDL_Renderer* renderer, int width, int height)
	: renderer(renderer), width(width), height(height)
{
}

void RetroRenderer::begin(const Camera& newCamera, const RenderSettings& newSettings)
{
	camera = newCamera;
	settings = newSettings;
	forward = normalize(camera.target - camera.position);
	Vec3 worldUp(0, 1, 0);
	if (std::fabs(dot(forward, worldUp)) > 0.999f)
		worldUp = Vec3(0, 0, 1);
	right = normalize(cross(worldUp, forward));
	up = cross(forward, right);
	if (camera.roll != 0.0f)
	{
		const float c = std::cos(camera.roll);
		const float s = std::sin(camera.roll);
		const Vec3 newRight = right * c + up * s;
		const Vec3 newUp = up * c - right * s;
		right = newRight;
		up = newUp;
	}
	focal = (height * 0.5f) / std::tan(camera.verticalFov * 0.5f * 3.14159265f / 180.0f);
	queue.clear();
}

Vec3 RetroRenderer::toView(const Vec3& world) const
{
	const Vec3 d = world - camera.position;
	return {dot(d, right), dot(d, up), dot(d, forward)};
}

bool RetroRenderer::project(const Vec3& world, float& sx, float& sy, float& depth) const
{
	const Vec3 v = toView(world);
	if (v.z <= settings.nearPlane)
		return false;
	sx = width * 0.5f + v.x * focal / v.z;
	sy = height * 0.5f - v.y * focal / v.z;
	depth = v.z;
	return true;
}

void RetroRenderer::pushClipped(const Vec3 view[3], const Rgb& color)
{
	// Sutherland-Hodgman against the near plane.
	Vec3 polygon[4];
	int count = 0;
	for (int i = 0; i < 3; ++i)
	{
		const Vec3& current = view[i];
		const Vec3& next = view[(i + 1) % 3];
		const bool currentInside = current.z > settings.nearPlane;
		const bool nextInside = next.z > settings.nearPlane;
		if (currentInside)
			polygon[count++] = current;
		if (currentInside != nextInside)
		{
			const float t = (settings.nearPlane - current.z) / (next.z - current.z);
			polygon[count++] = lerp(current, next, t);
		}
	}
	if (count < 3)
		return;
	const Rgb finalColor = settings.quantize ? quantizeRgb332(color) : color;
	const SDL_Color sdlColor = {finalColor.r, finalColor.g, finalColor.b, 255};
	auto toVertex = [&](const Vec3& v) {
		SDL_Vertex vertex;
		vertex.position.x = width * 0.5f + v.x * focal / v.z;
		vertex.position.y = height * 0.5f - v.y * focal / v.z;
		vertex.color = sdlColor;
		vertex.tex_coord.x = 0;
		vertex.tex_coord.y = 0;
		return vertex;
	};
	for (int i = 1; i + 1 < count; ++i)
	{
		ScreenTriangle triangle;
		triangle.v[0] = toVertex(polygon[0]);
		triangle.v[1] = toVertex(polygon[i]);
		triangle.v[2] = toVertex(polygon[i + 1]);
		triangle.depth = (polygon[0].z + polygon[i].z + polygon[i + 1].z) / 3.0f;
		queue.push_back(triangle);
	}
}

void RetroRenderer::submit(const Mesh& mesh, const Transform& transform)
{
	const Vec3 light = normalize(settings.lightDirection);
	for (const Triangle& triangle : mesh.triangles)
	{
		const Vec3 a = transform.apply(triangle.a);
		const Vec3 b = transform.apply(triangle.b);
		const Vec3 c = transform.apply(triangle.c);
		Vec3 normal = normalize(cross(b - a, c - a));
		const Vec3 va = toView(a);
		const Vec3 vb = toView(b);
		const Vec3 vc = toView(c);
		if (va.z <= settings.nearPlane && vb.z <= settings.nearPlane && vc.z <= settings.nearPlane)
			continue;
		// Back-face test in world space against the eye position.
		const float facing = dot(normal, a - camera.position);
		bool flipped = false;
		if (facing > 0.0f)
		{
			if (!triangle.doubleSided)
				continue;
			normal = -normal;
			flipped = true;
		}
		Rgb color = triangle.color;
		if (!triangle.unlit)
		{
			const float diffuse = std::max(0.0f, dot(normal, light));
			color = scaleRgb(color, settings.ambient + (1.0f - settings.ambient) * diffuse);
		}
		if (settings.fog)
		{
			const float depth = (va.z + vb.z + vc.z) / 3.0f;
			const float t = (depth - settings.fogStart) / std::max(0.01f, settings.fogEnd - settings.fogStart);
			color = mixRgb(color, settings.fogColor, t);
		}
		Vec3 view[3] = {va, flipped ? vc : vb, flipped ? vb : vc};
		pushClipped(view, color);
	}
}

void RetroRenderer::flush()
{
	std::stable_sort(queue.begin(), queue.end(), [](const ScreenTriangle& a, const ScreenTriangle& b) {
		return a.depth > b.depth;
	});
	if (triangleSink)
	{
		for (const ScreenTriangle& triangle : queue)
		{
			RetroScreenTriangle captured;
			for (int vertex = 0; vertex < 3; ++vertex)
			{
				captured.x[vertex] = triangle.v[vertex].position.x;
				captured.y[vertex] = triangle.v[vertex].position.y;
			}
			captured.color = {triangle.v[0].color.r, triangle.v[0].color.g,
				triangle.v[0].color.b};
			triangleSink(captured);
		}
	}
	std::vector<SDL_Vertex> vertices;
	vertices.reserve(queue.size() * 3);
	for (const ScreenTriangle& triangle : queue)
	{
		vertices.push_back(triangle.v[0]);
		vertices.push_back(triangle.v[1]);
		vertices.push_back(triangle.v[2]);
	}
	if (renderer != nullptr && !vertices.empty())
	{
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
		SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
	}
	queue.clear();
}
