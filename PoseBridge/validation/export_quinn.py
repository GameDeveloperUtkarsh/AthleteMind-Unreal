"""Run in the Unreal editor Python environment to export the validation rig."""

from pathlib import Path

import unreal


saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
task = unreal.AssetExportTask()
task.object = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple")
task.filename = str(saved / "ValidationQuinn.glb")
task.automated = True
task.prompt = False
task.replace_identical = True
task.options = unreal.GLTFExportOptions()
task.options.bake_material_inputs = unreal.GLTFMaterialBakeMode.DISABLED
task.options.texture_image_format = unreal.GLTFTextureImageFormat.NONE
if not unreal.Exporter.run_asset_export_task(task):
    raise RuntimeError("Unable to export Quinn for the pose benchmark.")
