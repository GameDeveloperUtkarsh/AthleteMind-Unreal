"""Run in Blender background mode with -- <Quinn GLB> <output.npz>."""

import math
import sys

import bpy
import numpy as np
from mathutils import Quaternion, Vector


source, output = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=source)
rig = next(obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE")
meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and obj.find_armature() == rig]
if not meshes:
    raise ValueError("The rig has no skinned character meshes")


def joint(name):
    return rig.matrix_world @ rig.pose.bones[name].head


up = Vector((0, 0, 1))
left = joint("thigh_l") - joint("thigh_r")
left.z = 0
left.normalize()
forward = left.cross(up)
toe_direction = joint("ball_l") - joint("foot_l")
if forward.dot(toe_direction) < 0:
    forward.negate()
bend_axis = (-up).cross(forward)
basis = np.array([list(left), list(forward), list(up)])
local_axis = rig.matrix_world.to_quaternion().inverted() @ bend_axis
vertices, references = [], []
for degrees in range(0, 91, 5):
    for side in ("l", "r"):
        for name, fraction in (("thigh", 0.65), ("calf", -1.0), ("foot", 0.35)):
            bone = rig.pose.bones[f"{name}_{side}"]
            axis = bone.bone.matrix_local.to_quaternion().inverted() @ local_axis
            bone.rotation_mode = "QUATERNION"
            bone.rotation_quaternion = Quaternion(axis, math.radians(degrees * fraction))
    bpy.context.view_layer.update()
    hip, knee, ankle = (joint(n) for n in ("thigh_l", "calf_l", "foot_l"))
    references.append(180 - math.degrees((hip - knee).angle(ankle - knee)))
    origin = np.array((ankle + joint("foot_r")) / 2)
    parts, triangles = [], []
    offset = 0
    for obj in meshes:
        evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh = evaluated.data
        coords = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", coords)
        matrix = np.array(evaluated.matrix_world)
        xyz = coords.reshape(-1, 3) @ matrix[:3, :3].T + matrix[:3, 3]
        parts.append(((xyz - origin) @ basis.T).astype(np.float32))
        if degrees == 0:
            mesh.calc_loop_triangles()
            faces = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
            mesh.loop_triangles.foreach_get("vertices", faces)
            triangles.append(faces.reshape(-1, 3) + offset)
        offset += len(mesh.vertices)
    if degrees == 0:
        topology = np.concatenate(triangles)
    vertices.append(np.concatenate(parts))
np.savez_compressed(output, vertices=vertices, triangles=topology, reference=references)
print("Reference flexion range", min(references), max(references))
