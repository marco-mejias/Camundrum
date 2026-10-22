
/* Camundrum - Background Controller */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/camera3d.hpp>
#include <Godot/classes/viewport.hpp>
#include <Godot/classes/array_mesh.hpp>
#include <Godot/classes/multi_mesh.hpp>
#include <Godot/classes/multi_mesh_instance3d.hpp>
#include <Godot/classes/shader.hpp>
#include <Godot/classes/shader_material.hpp>
#include <Godot/classes/mesh.hpp>
#include <Godot/variant/packed_vector3_array.hpp>
#include <Godot/variant/array.hpp>
#include <Godot/variant/variant.hpp>

// C++ Runtime
#include <cmath>
#include <cstdlib>
#include <vector>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// The neon pyramids floating behind every level, kept outside the script block because Jenova cannot read these types inside it
// Each one is placed across the screen (u, v from -1 to 1) and far behind the level, so whatever the camera frames is always filled
struct Pyramid
{
	float u = 0.0f;
	float v = 0.0f;
	float depth = 0.0f;
	float size = 1.0f;
	float speed = 0.0f;
	float angle = 0.0f;
	float spin = 0.0f;
};
static std::vector<Pyramid> pyramids;
static float Random(float low, float high)
{
	// Purpose: a random number between two values, for scattering the pyramids.

	// Plain rand is enough for decoration
	return low + (high - low) * (float)rand() / (float)RAND_MAX;
}
static Ref<ArrayMesh> PyramidLines()
{
	// Purpose: build the edges of a square pyramid as lines, one unit wide and tall, its middle at the origin and its tip pointing up.

	// The four corners of the base and the tip
	float h = 0.5f;
	Vector3 corners[4] = { Vector3(-h, -h, -h), Vector3(h, -h, -h), Vector3(h, -h, h), Vector3(-h, -h, h) };
	Vector3 tip = Vector3(0.0f, h, 0.0f);

	// Each edge is a pair of points: the square of the base, then one edge from each corner up to the tip
	PackedVector3Array points;
	for (int corner = 0; corner < 4; corner++)
	{
		points.push_back(corners[corner]);
		points.push_back(corners[(corner + 1) % 4]);
		points.push_back(corners[corner]);
		points.push_back(tip);
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = points;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, arrays);
	return mesh;
}

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties
JENOVA_PROPERTY(int, pyramid_count, 160)
JENOVA_PROPERTY(double, drift_speed, 0.08)
JENOVA_PROPERTY(double, brightness, 3.0)

// The background reads the view like the rest of the game:
// iso shows the pyramids whole, turning on themselves; side shows them as triangles rolling like wheels; top shows them tip first, turning on the spot.
// It follows the camera, so it is always behind the level and fills the screen, and it changes with the same short blend as the camera.

// State
Node3D* background = nullptr;
MultiMeshInstance3D* drawn = nullptr;
int target_view = VIEW_SIDE;
double iso_amount = 0.0;
double top_amount = 0.0;
double drift_copy = 0.08;

// Blending
double StepAmount(double amount, bool wanted, double delta)
{
	// Purpose: move one of the view blends towards on or off over the length of a camera move.

	// Walk towards 1 or 0
	double goal = wanted ? 1.0 : 0.0;
	double step = delta / 0.35;
	if (amount < goal) return (amount + step > goal) ? goal : amount + step;
	return (amount - step < goal) ? goal : amount - step;
}
double Eased(double amount)
{
	// Purpose: soften a blend so it starts and ends gently, like the camera.

	// Smooth step
	return amount * amount * (3.0 - 2.0 * amount);
}

// Commands
void ShowView(int view)
{
	// Purpose: let the GameManager tell the background which view is coming, so the pyramids change with the camera.

	// The blends walk there frame by frame
	target_view = view;
}
void SnapView(int view)
{
	// Purpose: let the GameManager put the background straight into a view, when a level starts or restarts.

	// No blend
	target_view = view;
	iso_amount = view == VIEW_ISO ? 1.0 : 0.0;
	top_amount = view == VIEW_TOP ? 1.0 : 0.0;
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, scatters the pyramids and builds the one drawing that holds them all.

	// Keep this node, it is placed at the world's origin and draws in world space
	background = GetSelf<Node3D>(instance);
	drift_copy = drift_speed;

	// White neon lines added over the background, brighter than anything in the level so only they glow
	Ref<ShaderMaterial> material;
	material.instantiate();
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code("shader_type spatial;\nrender_mode blend_add, depth_draw_never, cull_disabled, fog_disabled, shadows_disabled, ambient_light_disabled;\nuniform float brightness = 3.0;\nvoid fragment() { ALBEDO = vec3(0.0); EMISSION = vec3(brightness); }\n");
	material->set_shader(shader);
	material->set_shader_parameter("brightness", brightness);

	// One instance per pyramid
	Ref<ArrayMesh> mesh = PyramidLines();
	mesh->surface_set_material(0, material);
	Ref<MultiMesh> many;
	many.instantiate();
	many->set_transform_format(MultiMesh::TRANSFORM_3D);
	many->set_mesh(mesh);
	many->set_instance_count(pyramid_count > 0 ? pyramid_count : 0);
	drawn = memnew(MultiMeshInstance3D);
	drawn->set_name("Pyramids");
	drawn->set_multimesh(many);
	drawn->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	background->add_child(drawn);

	// Scatter them: small ones, a few a bit bigger, all slowly drifting right and turning at their own pace
	pyramids.clear();
	for (int index = 0; index < pyramid_count; index++)
	{
		Pyramid pyramid;
		pyramid.u = Random(-1.1f, 1.1f);
		pyramid.v = Random(-1.0f, 1.0f);
		pyramid.depth = Random(70.0f, 120.0f);
		pyramid.size = Random(0.2f, 0.6f);
		pyramid.speed = Random(0.5f, 1.5f) * pyramid.size * 2.0f;
		pyramid.angle = Random(0.0f, 6.2831853f);
		pyramid.spin = Random(0.4f, 1.2f) * (rand() % 2 == 0 ? 1.0f : -1.0f);
		pyramids.push_back(pyramid);
	}
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, drops every node reference.

	// Forget the nodes and the pyramids
	background = nullptr;
	drawn = nullptr;
	pyramids.clear();
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, drifts and turns the pyramids and places them behind whatever the camera shows.

	// Nothing to place without the drawing or a camera
	if (!drawn || !background) return;
	Camera3D* camera = background->get_viewport()->get_camera_3d();
	Ref<MultiMesh> many = drawn->get_multimesh();
	if (!camera || many.is_null()) return;

	// Blend towards the view on its way in
	iso_amount = StepAmount(iso_amount, target_view == VIEW_ISO, _delta);
	top_amount = StepAmount(top_amount, target_view == VIEW_TOP, _delta);
	float iso = (float)Eased(iso_amount);
	float top = (float)Eased(top_amount);
	float side = 1.0f - iso - top;
	if (side < 0.0f) side = 0.0f;

	// The side camera looks slightly down, so in side the pyramids are pressed flat in depth to read as clean triangles
	Basis press = Basis::from_scale(Vector3(1.0f, 1.0f, 1.0f - 0.97f * side));

	// The screen as the camera sees it: its size across and up, its axes in the world
	Transform3D view = camera->get_global_transform();
	Vector3 right = view.basis.get_column(0).normalized();
	Vector3 up = view.basis.get_column(1).normalized();
	Vector3 forward = -view.basis.get_column(2).normalized();
	Vector2 screen = background->get_viewport()->get_visible_rect().size;
	float half_up = (float)camera->get_size() * 0.5f;
	float half_across = half_up * (screen.y > 0.0f ? screen.x / screen.y : 1.7777f);

	// Each pyramid drifts right across the screen and comes back in from the left
	for (int index = 0; index < (int)pyramids.size() && index < many->get_instance_count(); index++)
	{
		Pyramid& pyramid = pyramids[index];
		pyramid.u += pyramid.speed * (float)drift_copy * (float)_delta;
		if (pyramid.u > 1.15f)
		{
			pyramid.u -= 2.3f;
			pyramid.v = Random(-1.0f, 1.0f);
		}
		pyramid.angle += pyramid.spin * (float)_delta;
		if (pyramid.angle > 6.2831853f) pyramid.angle -= 6.2831853f;
		if (pyramid.angle < 0.0f) pyramid.angle += 6.2831853f;

		// Turned for the view: in iso it turns on itself with its tip up; in side it rolls like a wheel; in top its tip faces the camera and it turns on the spot
		Basis turn = Basis(Vector3(0.0f, 0.0f, 1.0f), pyramid.angle * (1.0f - iso))
			* Basis(Vector3(1.0f, 0.0f, 0.0f), top * 1.5707963f)
			* Basis(Vector3(0.0f, 1.0f, 0.0f), pyramid.angle * iso);

		// Placed far behind the level, wherever the camera looks
		Vector3 where = view.origin + right * (pyramid.u * half_across) + up * (pyramid.v * half_up) + forward * pyramid.depth;
		many->set_instance_transform(index, Transform3D(press * turn.scaled(Vector3(pyramid.size, pyramid.size, pyramid.size)), where));
	}
}

// Jenova Script Block End
JENOVA_SCRIPT_END
