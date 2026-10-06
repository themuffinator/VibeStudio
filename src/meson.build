conf = configuration_data()
conf.set('VIBESTUDIO_VERSION', meson.project_version())
conf.set('VIBESTUDIO_GITHUB_REPO', get_option('github_repo'))
conf.set('VIBESTUDIO_UPDATE_CHANNEL', get_option('update_channel'))
conf.set10('VIBESTUDIO_HAVE_AUDIO_PLAYBACK', qt6_multimedia_dep.found())
conf.set10('VIBESTUDIO_HAVE_AUDIO_DUPLEX', pa_enabled)

configure_file(
  input: 'vibestudio_config.h.in',
  output: 'vibestudio_config.h',
  configuration: conf,
)

vibestudio_includes = include_directories('.')

install_headers('core/texture_document.h', subdir: 'vibestudio/core')
install_headers('core/texture_paint.h', subdir: 'vibestudio/core')
install_headers('core/texture_output.h', subdir: 'vibestudio/core')
install_headers('core/texture_transform.h', subdir: 'vibestudio/core')
install_headers('core/texture_export.h', subdir: 'vibestudio/core')
install_headers('core/texture_handoff.h', subdir: 'vibestudio/core')
install_headers('core/texture_project.h', subdir: 'vibestudio/core')
install_headers('core/texture_recovery.h', subdir: 'vibestudio/core')
install_headers('core/audio_clip.h', subdir: 'vibestudio/core')
install_headers('core/audio_decode.h', subdir: 'vibestudio/core')
install_headers('core/audio_markers.h', subdir: 'vibestudio/core')
install_headers('core/audio_project.h', subdir: 'vibestudio/core')
install_headers('core/audio_session.h', 'core/audio_session_io.h', 'core/audio_transport.h', 'core/audio_take.h', 'core/audio_routing.h', subdir: 'vibestudio/core')
install_headers('core/audio_effects.h', subdir: 'vibestudio/core')
install_headers('core/audio_latency.h', 'core/audio_lookahead.h', subdir: 'vibestudio/core')
install_headers('core/audio_tempo.h', 'core/audio_arrangement.h', 'core/audio_range.h', subdir: 'vibestudio/core')
install_headers('core/audio_media.h', subdir: 'vibestudio/core')
install_headers('core/audio_meter.h', subdir: 'vibestudio/core')
install_headers('core/audio_duplex.h', 'core/audio_duplex_queue.h', subdir: 'vibestudio/core')
install_headers('core/audio_recording.h', subdir: 'vibestudio/core')
install_headers('core/audio_recording_import.h', subdir: 'vibestudio/core')
install_headers('core/audio_recording_review.h', subdir: 'vibestudio/core')
install_headers('core/audio_take_lanes.h', subdir: 'vibestudio/core')
install_headers('core/audio_stems.h', subdir: 'vibestudio/core')
install_headers('core/audio_automation.h', subdir: 'vibestudio/core')
install_headers('core/audio_reverb.h', subdir: 'vibestudio/core')
install_headers('core/audio_publication.h', subdir: 'vibestudio/core')
install_headers('core/audio_effect_preset.h', subdir: 'vibestudio/core')
install_headers('core/audio_recovery_store.h', subdir: 'vibestudio/core')
install_headers('core/audio_waveform.h', subdir: 'vibestudio/core')
install_headers('core/audio_resample.h', subdir: 'vibestudio/core')
install_headers('core/audio_export.h', subdir: 'vibestudio/core')
install_headers('core/audio_delivery.h', subdir: 'vibestudio/core')
install_headers('core/audio_level.h', subdir: 'vibestudio/core')
install_headers('core/audio_analysis.h', subdir: 'vibestudio/core')

install_headers('core/project_text_search.h', subdir: 'vibestudio/core')
install_headers('core/text_document.h', subdir: 'vibestudio/core')
install_headers('core/text_recovery.h', subdir: 'vibestudio/core')
install_headers('core/code_files.h', subdir: 'vibestudio/core')
install_headers('core/language_server.h', subdir: 'vibestudio/core')
install_headers('core/language_references.h', subdir: 'vibestudio/core')
install_headers('core/language_completion.h', subdir: 'vibestudio/core')
install_headers('core/completion_snippet.h', subdir: 'vibestudio/core')
install_headers('core/language_hover.h', subdir: 'vibestudio/core')
install_headers('core/language_signature.h', subdir: 'vibestudio/core')
install_headers('core/language_formatting.h', subdir: 'vibestudio/core')
install_headers('core/language_rename.h', subdir: 'vibestudio/core')
install_headers('core/language_workspace_edit.h', 'core/language_code_actions.h', subdir: 'vibestudio/core')

install_headers('core/box_resize.h', subdir: 'vibestudio/core')
install_headers('core/box_draw.h', subdir: 'vibestudio/core')
install_headers('core/level_navigation.h', subdir: 'vibestudio/core')
install_headers('core/level_placement.h', subdir: 'vibestudio/core')
install_headers('core/level_build_workspace.h', subdir: 'vibestudio/core')
install_headers('core/level_quake_assets.h', subdir: 'vibestudio/core')
install_headers('core/model_fingerprint.h', subdir: 'vibestudio/core')
install_headers('core/level_build_artifacts.h', subdir: 'vibestudio/core')
install_headers('core/level_build_deployment.h', subdir: 'vibestudio/core')
install_headers('core/level_doom_selection.h', subdir: 'vibestudio/core')
install_headers('core/level_doom_nodes.h', subdir: 'vibestudio/core')
install_headers('core/level_udmf.h', subdir: 'vibestudio/core')
install_headers('core/package_index_p.h', subdir: 'vibestudio/core')
install_headers('core/package_protection_p.h', subdir: 'vibestudio/core')
install_headers('core/level_build_pak_deployment.h', subdir: 'vibestudio/core')
install_headers('core/level_scene.h', 'core/level_scene_types.h', 'core/level_scene_locks.h', subdir: 'vibestudio/core')
install_headers('core/map_geometry_cache.h', subdir: 'vibestudio/core')
install_headers('core/level_material_paint.h', subdir: 'vibestudio/core')
install_headers('core/level_surface_clipboard.h', subdir: 'vibestudio/core')
install_headers('core/doom_preview_geometry.h', 'core/doom_preview_materials.h', subdir: 'vibestudio/core')

install_headers('core/extra_image.h', subdir: 'vibestudio/core')

install_headers('core/asset_formats.h', 'core/workspace_document.h', subdir: 'vibestudio/core')

core_sources = files(
  'core/level_gestures.cpp',
  'core/level_camera_keys.cpp',
  'core/studio_settings_gestures.cpp',
  'core/asset_formats.cpp',
  'core/workspace_document.cpp',
  'core/extra_image.cpp',
  'core/dds_image.cpp',
  'core/level_build_workspace.cpp',
  'core/level_quake_assets.cpp',
  'core/level_build_artifacts.cpp',
  'core/level_build_deployment.cpp',
  'core/level_doom_selection.cpp',
  'core/level_doom_nodes.cpp',
  'core/level_udmf.cpp',
  'core/level_build_pak_deployment.cpp',
  'core/level_placement.cpp',
  # Internal transaction controls are build inputs, not installed public API.
  'core/level_placement_control_p.h',
  'core/level_scene.cpp',
  'core/level_scene_locks.cpp',
  'core/doom_preview_geometry.cpp',
  'core/doom_preview_materials.cpp',
  'core/level_material_paint.cpp',
  'core/level_surface_clipboard.cpp',
  'core/level_navigation.cpp',
  'core/language_completion.cpp',
  'core/completion_snippet.cpp',
  'core/language_hover.cpp',
  'core/language_signature.cpp',
  'core/language_formatting.cpp',
  'core/language_rename.cpp',
  'core/language_workspace_edit.cpp',
  'core/language_code_actions.cpp',
  'core/language_server.cpp',
  'core/language_diagnostics.cpp',
  'core/language_references.cpp',
  'core/code_files.cpp',
  'core/advanced_studio.cpp',
  'core/ai_connectors.cpp',
  'core/ai_transport.cpp',
  'core/ai_image_transport.cpp',
  'core/texture_generation.cpp',
  'core/level_generation.cpp',
  'core/level_generation_build.cpp',
  'core/level_ai_edit.cpp',
  'core/ai_audio_transport.cpp',
  'core/sound_generation.cpp',
  # Internal planner/builder helpers, not installed public API.
  'core/level_generation_p.h',
  'core/ai_workflows.cpp',
  'core/asset_tools.cpp',
  'core/audio_clip.cpp',
  'core/audio_decode.cpp',
  'core/audio_markers.cpp',
  'core/audio_project.cpp',
  'core/audio_session.cpp',
  'core/audio_arrangement.cpp',
  'core/audio_range.cpp',
  'core/audio_media.cpp',
  'core/audio_meter.cpp',
  'core/audio_duplex.cpp',
  'core/audio_loop.h',
  'core/audio_duplex_queue.cpp',
  'core/audio_recording.cpp',
  'core/audio_recording_import.cpp',
  'core/audio_recording_review.cpp',
  'core/audio_take_lanes.cpp',
  'core/audio_routing.cpp',
  'core/audio_effects.cpp',
  'core/audio_automation.cpp',
  'core/audio_effect_automation.cpp',
  'core/audio_reverb.cpp',
  'core/audio_publication.cpp',
  'core/audio_effect_preset.cpp',
  'core/audio_transport.cpp',
  'core/audio_take.cpp',
  'core/audio_session_io.cpp',
  'core/audio_stems.cpp',
  'core/audio_latency.cpp',
  'core/audio_tempo.cpp',
  'core/audio_lookahead.cpp',
  'core/audio_recovery_store.cpp',
  'core/audio_waveform.cpp',
  'core/audio_resample.cpp',
  'core/audio_export.cpp',
  'core/audio_delivery.cpp',
  'core/audio_level.cpp',
  'core/audio_analysis.cpp',
  'core/texture_document.cpp',
  'core/texture_paint.cpp',
  'core/texture_output.cpp',
  'core/texture_transform.cpp',
  'core/texture_export.cpp',
  'core/texture_handoff.cpp',
  'core/texture_project.cpp',
  'core/texture_recovery.cpp',
  'core/bsp_inspect.cpp',
  'core/build_pipeline.cpp',
  'core/compiler_artifact_validation.cpp',
  'core/compiler_known_issues.cpp',
  'core/compiler_profiles.cpp',
  'core/compiler_registry.cpp',
  'core/compiler_runner.cpp',
  'core/deflate.cpp',
  'core/document_watch.cpp',
  'core/editor_profiles.cpp',
  'core/entity_definitions.cpp',
  'core/ericw_map_preflight.cpp',
  'core/game_installation.cpp',
  'core/idtech_image.cpp',
  'core/level_editor_controls.cpp',
  'core/level_editor_familiar_controls.cpp',
  'core/level_map.cpp',
  'core/level_patch.cpp',
  'core/level_patch_stitch.cpp',
  'core/level_patch_cap.cpp',
  'core/level_brush.cpp',
  'core/level_primitive.cpp',
  'core/level_merge.cpp',
  'core/level_texture_mapping.cpp',
  'core/level_surface.cpp',
  'core/level_document.cpp',
  'core/level_prefab.cpp',
  'core/level_dependencies.cpp',
  'core/localization.cpp',
  'core/map_assets.cpp',
  'core/map_geometry.cpp',
  'core/map_geometry_cache.cpp',
  'core/map_preview_mesh.cpp',
  'core/level_materials.cpp',
  'core/level_model_appearance.cpp',
  'core/map_render.cpp',
  'core/model_mesh.cpp',
  'core/model_archive.cpp',
  'core/model_obj.cpp',
  'core/model_frame_export.cpp',
  'core/model_appearance.cpp',
  'core/model_mdl.cpp',
  'core/model_mdl_json.cpp',
  'core/model_mdl_export.cpp',
  'core/model_mdl_edit.cpp',
  'core/model_mdl_playback.cpp',
  'core/model_skin_source.cpp',
  'core/model_skin_bindings.cpp',
  'core/model_design.cpp',
  'core/model_document.cpp',
  'core/model_import_repair.cpp',
  'core/model_source_decode.h',
  'core/model_collision.cpp',
  'core/model_assembly.cpp',
  'core/model_assembly_skin.cpp',
  'core/model_player_bundle.cpp',
  'core/model_assembly_q3_animation.cpp',
  'core/model_q3_animation.cpp',
  'core/model_assembly_animation.cpp',
  'core/model_assembly_document.cpp',
  'core/model_assembly_recovery.cpp',
  'core/model_fingerprint.cpp',
  'core/model_topology.cpp',
  'core/model_topology_health.cpp',
  'core/model_intersections.cpp',
  'core/model_triangle_contact.cpp',
  'core/model_geometry_helpers.h',
  'core/model_boundary_fill.cpp',
  'core/model_boundary_bridge.cpp',
  # Shared boundary algorithms remain private to the model implementation.
  'core/model_boundary_helpers.h',
  'core/model_surfaces.cpp',
  'core/model_surface_selection.cpp',
  'core/model_material_slots.cpp',
  'core/model_animation.cpp',
  'core/model_pose.cpp',
  'core/model_transform.cpp',
  'core/model_trackball.cpp',
  'core/box_resize.cpp',
  'core/box_draw.cpp',
  'core/model_transform_axes.cpp',
  'core/model_tags.cpp',
  'core/model_uv.cpp',
  'core/model_uv_transform.cpp',
  'core/model_uv_atlas.cpp',
  'core/model_uv_mapping.cpp',
  'core/model_uv_obstacles.cpp',
  'core/model_uv_atlas_memory.cpp',
  'core/model_file_io.cpp',
  'core/model_work.cpp',
  'core/model_export.cpp',
  'core/model_recovery.cpp',
  'core/operation_state.cpp',
  'core/package_archive.cpp',
  'core/package_zip.cpp',
  'core/package_zip_p.h',
  'core/package_browser.cpp',
  'core/package_index.cpp',
  'core/package_snapshot.cpp',
  'core/package_snapshot_p.h',
  'core/package_plan_p.h',
  'core/package_content.cpp',
  'core/package_protection.cpp',
  'core/package_staging_protection.cpp',
  'core/package_import_store.cpp',
  'core/package_import_locks.cpp',
  'core/package_draft.cpp',
  'core/package_draft_access.cpp',
  'core/package_draft_storage.cpp',
  'core/package_recovery.cpp',
  'core/package_storage.cpp',
  'core/package_extraction.cpp',
  'core/package_copy.cpp',
  'core/package_copy_budget.cpp',
  'core/package_copy_store.cpp',
  'core/package_compare.cpp',
  'core/package_preview.cpp',
  'core/package_selection.cpp',
  'core/package_subset.cpp',
  'core/package_wad_groups.cpp',
  'core/package_staging.cpp',
  'core/package_staging_content.cpp',
  'core/package_staging_metadata.cpp',
  'core/package_directory.cpp',
  'core/package_publication.cpp',
  'core/package_publication_inventory.cpp',
  'core/package_validation.cpp',
  'core/parser_fuzz.cpp',
  'core/project_manifest.cpp',
  'core/project_text_search.cpp',
  'core/text_document.cpp',
  'core/text_recovery.cpp',
  'core/studio_semantics.cpp',
  'core/studio_manifest.cpp',
  'core/studio_query.cpp',
  'core/studio_settings.cpp',
)

audio_threads_dep = dependency('threads')

vibestudio_core = static_library(
  'vibestudio_core',
  core_sources,
  include_directories: [vibestudio_includes, include_directories('../external/audio/r8brain-free-src', '../external/audio/dr_libs', is_system: true)],
  dependencies: [qt6_core_dep, audio_threads_dep, xiph_decoder_dep, audio_meter_dep, xatlas_dep],
  install: true,
)

vibestudio_core_dep = declare_dependency(
  include_directories: vibestudio_includes,
  dependencies: [qt6_core_dep, audio_threads_dep, xiph_decoder_dep, audio_meter_dep, xatlas_dep],
  link_with: [vibestudio_core],
)

install_headers('core/level_dependencies.h', 'core/level_document.h', 'core/package_selection.h', 'core/model_design.h', subdir: 'vibestudio/core')
install_headers('core/level_patch.h', subdir: 'vibestudio/core')
install_headers('core/level_patch_stitch.h', subdir: 'vibestudio/core')
install_headers('core/level_patch_cap.h', subdir: 'vibestudio/core')
install_headers('core/level_brush.h', subdir: 'vibestudio/core')
install_headers('core/level_primitive.h', subdir: 'vibestudio/core')
install_headers('core/level_prefab.h', subdir: 'vibestudio/core')
install_headers('core/level_merge.h', subdir: 'vibestudio/core')
install_headers('core/level_surface.h', subdir: 'vibestudio/core')
install_headers('core/level_texture_mapping.h', subdir: 'vibestudio/core')
install_headers('core/model_document.h', 'core/model_topology.h', subdir: 'vibestudio/core')
install_headers('core/model_assembly.h', subdir: 'vibestudio/core')
install_headers('core/model_player_bundle.h', subdir: 'vibestudio/core')
install_headers('core/model_q3_animation.h', subdir: 'vibestudio/core')
install_headers('core/model_assembly_animation.h', subdir: 'vibestudio/core')
install_headers('core/model_collision.h', subdir: 'vibestudio/core')
install_headers('core/model_assembly_recovery.h', subdir: 'vibestudio/core')
install_headers('core/model_topology_health.h', subdir: 'vibestudio/core')
install_headers('core/model_boundary_fill.h', subdir: 'vibestudio/core')
install_headers('core/model_intersections.h', 'core/model_triangle_contact.h', subdir: 'vibestudio/core')
install_headers('core/model_import_repair.h', subdir: 'vibestudio/core')
install_headers('core/model_boundary_bridge.h', subdir: 'vibestudio/core')
install_headers('core/model_surfaces.h', subdir: 'vibestudio/core')
install_headers('core/model_surface_selection.h', subdir: 'vibestudio/core')
install_headers('core/model_material_slots.h', subdir: 'vibestudio/core')
install_headers('core/model_animation.h', subdir: 'vibestudio/core')
install_headers('core/model_pose.h', subdir: 'vibestudio/core')
install_headers('core/model_mdl.h', subdir: 'vibestudio/core')
install_headers('core/model_skin_source.h', subdir: 'vibestudio/core')
install_headers('core/model_skin_bindings.h', subdir: 'vibestudio/core')
install_headers('core/model_obj.h', subdir: 'vibestudio/core')
install_headers('core/model_archive.h', subdir: 'vibestudio/core')
install_headers('core/model_frame_export.h', subdir: 'vibestudio/core')
install_headers('core/model_appearance.h', subdir: 'vibestudio/core')
install_headers('core/package_wad_groups.h', subdir: 'vibestudio/core')
install_headers('core/model_transform.h', subdir: 'vibestudio/core')
install_headers('core/model_trackball.h', subdir: 'vibestudio/core')
install_headers('core/model_transform_axes.h', subdir: 'vibestudio/core')
install_headers('core/model_tags.h', subdir: 'vibestudio/core')
install_headers('core/model_uv.h', subdir: 'vibestudio/core')
install_headers('core/model_uv_mapping.h', subdir: 'vibestudio/core')
install_headers('core/model_uv_atlas.h', subdir: 'vibestudio/core')
install_headers('core/model_uv_obstacles.h', subdir: 'vibestudio/core')
install_headers('core/model_uv_atlas_memory.h', subdir: 'vibestudio/core')
install_headers('core/model_file_io.h', 'core/model_work.h', subdir: 'vibestudio/core')
install_headers('core/package_validation.h', subdir: 'vibestudio/core')
install_headers('core/package_publication.h', subdir: 'vibestudio/core')
install_headers('core/package_content.h', subdir: 'vibestudio/core')
install_headers('core/package_import_store.h', subdir: 'vibestudio/core')
install_headers('core/package_draft.h', 'core/package_draft_access.h', 'core/package_draft_storage.h', subdir: 'vibestudio/core')
install_headers('core/package_recovery.h', subdir: 'vibestudio/core')
install_headers('core/package_storage.h', 'core/package_copy.h', 'core/package_copy_budget.h', 'core/package_copy_store.h', subdir: 'vibestudio/core')

install_headers(files('core/advanced_studio.h', 'core/ai_connectors.h', 'core/ai_transport.h', 'core/ai_workflows.h', 'core/asset_tools.h', 'core/bsp_inspect.h', 'core/build_pipeline.h', 'core/compiler_artifact_validation.h', 'core/compiler_known_issues.h', 'core/compiler_profiles.h', 'core/compiler_registry.h', 'core/compiler_runner.h', 'core/deflate.h', 'core/document_watch.h', 'core/ericw_map_preflight.h', 'core/editor_profiles.h', 'core/entity_definitions.h', 'core/game_installation.h', 'core/idtech_image.h', 'core/level_editor_controls.h', 'core/level_gestures.h', 'core/level_camera_keys.h', 'core/level_map.h', 'core/localization.h', 'core/map_assets.h', 'core/map_geometry.h', 'core/map_preview_mesh.h', 'core/map_render.h', 'core/model_mesh.h', 'core/operation_state.h', 'core/package_archive.h', 'core/package_compare.h', 'core/package_preview.h', 'core/package_staging.h', 'core/package_directory.h', 'core/package_browser.h', 'core/parser_fuzz.h', 'core/project_manifest.h', 'core/studio_semantics.h', 'core/studio_manifest.h', 'core/studio_query.h', 'core/studio_settings.h'), subdir: 'vibestudio/core')

install_headers('core/ai_image_transport.h', subdir: 'vibestudio/core')
install_headers('core/texture_generation.h', subdir: 'vibestudio/core')
install_headers('core/level_generation.h', subdir: 'vibestudio/core')
install_headers('core/level_ai_edit.h', subdir: 'vibestudio/core')
install_headers('core/ai_audio_transport.h', subdir: 'vibestudio/core')
install_headers('core/sound_generation.h', subdir: 'vibestudio/core')
install_headers('core/level_materials.h', subdir: 'vibestudio/core')
install_headers('core/level_model_appearance.h', subdir: 'vibestudio/core')

app_sources = files(
  'app/asset_workbench_actions.cpp',
  'app/ai_generation_actions.cpp',
  'app/level_ai_edit_dialog.cpp',
  'app/level_generation_dialog.cpp',
  'app/sound_generation_dialog.cpp',
  'app/texture_generation_dialog.cpp',
  'app/workspace_actions.cpp',
  'app/level_scene_panel.cpp',
  'app/level_object_model.cpp',
  'app/level_object_list.cpp',
  'app/application_shell.cpp',
  'app/asset_views.cpp',
  'app/audio_editor_dialog.cpp',
  'app/audio_session_dialog.cpp',
  'app/audio_tempo_dialog.cpp',
  'app/audio_arrangement_dialog.cpp',
  'app/audio_range_dialog.cpp',
  'app/audio_media_dialog.cpp',
  'app/audio_meter_dialog.cpp',
  'app/audio_meter_widgets.cpp',
  'app/audio_recording_meters.cpp',
  'app/audio_session_playback.cpp',
  'app/audio_capture.cpp',
  'app/audio_recording.cpp',
  'app/audio_recording_dialog.cpp',
  'app/audio_input_device.cpp',
  'app/audio_take_dialog.cpp',
  'app/audio_routing_dialog.cpp',
  'app/audio_stem_export_dialog.cpp',
  'app/audio_effects_dialog.cpp',
  'app/audio_automation_editor.cpp',
  'app/audio_stream_device.cpp',
  'app/audio_session_timeline.cpp',
  'app/audio_session_actions.cpp',
  'app/audio_playback.cpp',
  'app/audio_duplex_device.cpp',
  'app/audio_browser_actions.cpp',
  'app/audio_browser_worker.cpp',
  'app/audio_placement_dialog.cpp',
  'app/audio_analysis_dialog.cpp',
  'app/audio_markers_dialog.cpp',
  'app/audio_recovery.cpp',
  'app/audio_recovery_actions.cpp',
  'app/audio_recovery_dialog.cpp',
  'app/audio_waveform_view.cpp',
  'app/texture_canvas.cpp',
  'app/texture_editor_dialog.cpp',
  'app/texture_preview_worker.cpp',
  'app/texture_png_export_dialog.cpp',
  'app/texture_thumbnail_cache.cpp',
  'app/texture_export_panel.cpp',
  'app/texture_recovery.cpp',
  'app/code_editor.cpp',
  'app/code_completion.cpp',
  'app/code_snippet.cpp',
  'app/code_snippet_bar.cpp',
  'app/code_document_actions.cpp',
  'app/code_search_actions.cpp',
  'app/code_language_panel.cpp',
  'app/code_language_actions.cpp',
  'app/code_quick_info.cpp',
  'app/language_documentation.cpp',
  'app/code_signature_panel.cpp',
  'app/code_signature_actions.cpp',
  'app/code_formatting.cpp',
  'app/code_rename.cpp',
  'app/code_actions.cpp',
  'app/code_actions_dialog.cpp',
  'app/code_recovery.cpp',
  'app/code_index_worker.cpp',
  'app/code_index_actions.cpp',
  'app/code_files_worker.cpp',
  'app/quick_open_catalog.cpp',
  'app/code_files_panel.cpp',
  'app/project_search_panel.cpp',
  'app/level_dependency_dialog.cpp',
  'app/package_subset_dialog.cpp',
  'app/package_wad_groups_dialog.cpp',
  'app/level_document_dialog.cpp',
  'app/level_document_actions.cpp',
  'app/level_view_actions.cpp',
  'app/level_bookmark_actions.cpp',
  'app/level_bookmarks_dialog.cpp',
  'app/level_build_workspace_dialog.cpp',
  'app/level_build_package_dialog.cpp',
  'app/level_build_workspace_actions.cpp',
  'app/patch_editor_dialog.cpp',
  'app/patch_editor_actions.cpp',
  'app/patch_stitch_dialog.cpp',
  'app/patch_stitch_actions.cpp',
  'app/patch_cap_actions.cpp',
  'app/patch_cap_dialog.cpp',
  'app/brush_editor_dialog.cpp',
  'app/brush_editor_actions.cpp',
  'app/level_primitive_dialog.cpp',
  'app/level_primitive_actions.cpp',
  'app/level_prefab_dialog.cpp',
  'app/level_prefab_actions.cpp',
  'app/level_merge_dialog.cpp',
  'app/level_merge_actions.cpp',
  'app/level_rotation_dialog.cpp',
  'app/level_udmf_dialog.cpp',
  'app/level_udmf_actions.cpp',
  'app/level_rotation_actions.cpp',
  'app/level_placement_dialog.cpp',
  'app/level_placement_task_dialog.cpp',
  'app/game_launch_task_dialog.cpp',
  'app/level_placement_actions.cpp',
  'app/level_surface_dialog.cpp',
  'app/level_surface_actions.cpp',
  'app/level_surface_quick_actions.cpp',
  'app/level_surface_tools.cpp',
  'app/level_surface_worker.cpp',
  'app/level_material_paint_actions.cpp',
  'app/level_surface_stroke_actions.cpp',
  'app/level_preview_worker.cpp',
  'app/level_load_dialog.cpp',
  'app/level_preview_actions.cpp',
  'app/model_design_dialog.cpp',
  'app/model_assembly_dialog.cpp',
  'app/model_assembly_skin.cpp',
  'app/model_player_bundle.cpp',
  'app/model_assembly_q3_animation.cpp',
  'app/model_q3_animation_dialog.cpp',
  'app/model_assembly_animation_dialog.cpp',
  'app/model_assembly_recovery_writer.cpp',
  'app/model_assembly_recovery_dialog.cpp',
  'app/model_editor_dialog.cpp',
  'app/model_document_work.cpp',
  'app/model_material_worker.cpp',
  'app/model_browser_appearance.cpp',
  'app/model_recovery_writer.cpp',
  'app/model_recovery_dialog.cpp',
  'app/package_operation_dialog.cpp',
  'app/package_progress.cpp',
  'app/package_preview_worker.cpp',
  'app/package_entry_view.cpp',
  'app/package_staging_view.cpp',
  'app/package_folder_view.cpp',
  'app/package_extraction_paths.cpp',
  'app/package_document_actions.cpp',
  'app/package_context_actions.cpp',
  'app/level_texture_audit_panel.cpp',
  'app/package_recovery_actions.cpp',
  'app/package_recovery_writer.cpp',
  'app/package_recovery_dialog.cpp',
  'app/package_publication_dialog.cpp',
  'app/package_import_dialog.cpp',
  'app/package_draft_storage_dialog.cpp',
  'app/package_copy_budget_dialog.cpp',
  'app/package_copy_sessions_dialog.cpp',
  'app/map_viewport.cpp',
  'app/map_viewport_scene_index.cpp',
  'app/map_viewport_projection.cpp',
  'app/map_viewport_overlay.cpp',
  'app/map_viewport_overlay_worker.cpp',
  'app/map_plan_wires.cpp',
  'app/map_plan_renderer.cpp',
  'app/map_plan_render_worker.cpp',
  'app/level_gestures_dialog.cpp',
  'app/level_gesture_actions.cpp',
  'app/viewport_hud.cpp',
  'app/viewport_image.cpp',
  'app/viewport_ring_cache.cpp',
  'app/map_grid.cpp',
  'app/model_viewport.cpp',
  'app/model_viewport_resize.cpp',
  'app/model_viewport_brush.cpp',
  'app/level_camera_brush_actions.cpp',
  'app/level_camera_resize_actions.cpp',
  'app/model_viewport_collision.cpp',
  'app/model_viewport_edit.cpp',
  'app/model_viewport_animation.cpp',
  'app/model_viewport_surface.cpp',
  'app/model_viewport_drive.cpp',
  'app/model_uv_view.cpp',
  'app/model_editor_uv.cpp',
  'app/model_editor_tags.cpp',
  'app/model_editor_collision.cpp',
  'app/model_editor_health.cpp',
  'app/model_editor_intersections.cpp',
  'app/model_editor_import_repair.cpp',
  'app/model_import_repair_dialog.cpp',
  'app/model_editor_animation.cpp',
  'app/model_editor_mdl.cpp',
  'app/model_editor_skin_source.cpp',
  'app/model_skin_source_dialog.cpp',
  'app/model_editor_skin_bindings.cpp',
  'app/model_editor_surfaces.cpp',
  'app/model_surface_dialog.cpp',
  'app/model_material_slots_dialog.cpp',
  'app/model_editor_material_slots.cpp',
  'app/model_preview_worker.cpp',
  'app/model_uv_render.cpp',
  'app/model_uv_fill.cpp',
  'app/model_rasterizer.cpp',
  'app/model_vertex_overlay.cpp',
  'app/studio_actions.cpp',
  'app/studio_charts.cpp',
  'app/studio_icons.cpp',
  'app/studio_layout.cpp',
  'app/studio_runtime.cpp',
  'app/studio_theme.cpp',
  'app/syntax_highlight.cpp',
  'app/ui_primitives.cpp',
  'app/property_grid.cpp',
)

app_moc_headers = files(
  'app/level_gestures_dialog.h',
  'app/package_entry_view.h',
  'app/package_staging_view.h',
  'app/package_folder_view.h',
  'app/level_scene_panel.h',
  'app/level_object_list.h',
  'app/level_load_dialog.h',
  'app/code_files_panel.h',
  'app/code_language_panel.h',
  'app/code_quick_info.h',
  'app/code_signature_panel.h',
  'app/brush_editor_dialog.h',
  'app/patch_editor_dialog.h',
  'app/texture_canvas.h',
  'app/texture_editor_dialog.h',
  'app/application_shell.h',
  'app/project_search_panel.h',
  'app/asset_views.h',
  'app/audio_editor_dialog.h',
  'app/audio_session_dialog.h',
  'app/audio_tempo_dialog.h',
  'app/audio_arrangement_dialog.h',
  'app/audio_range_dialog.h',
  'app/audio_media_dialog.h',
  'app/audio_meter_dialog.h',
  'app/audio_recording_meters.h',
  'app/audio_session_playback.h',
  'app/audio_capture.h',
  'app/audio_recording.h',
  'app/audio_recording_dialog.h',
  'app/audio_take_dialog.h',
  'app/audio_routing_dialog.h',
  'app/audio_stem_export_dialog.h',
  'app/audio_effects_dialog.h',
  'app/audio_automation_editor.h',
  'app/audio_session_timeline.h',
  'app/audio_playback.h',
  'app/audio_placement_dialog.h',
  'app/audio_analysis_dialog.h',
  'app/audio_markers_dialog.h',
  'app/audio_recovery_dialog.h',
  'app/audio_waveform_view.h',
  'app/map_viewport.h',
  'app/map_viewport_scene_index.h',
  'app/map_plan_wires.h',
  'app/model_viewport.h',
  'app/model_uv_view.h',
  'app/studio_actions.h',
  'app/studio_charts.h',
  'app/studio_layout.h',
  'app/syntax_highlight.h',
)

app_moc_sources = qt6.preprocess(
  moc_headers: app_moc_headers,
  include_directories: vibestudio_includes,
  dependencies: qt6_dep,
)

# The shell and its widgets as a library, so GUI tests can build and drive the
# real window without the executable's main().
audio_capture_platform_dep = dependency('', required: false)
if host_machine.system() == 'darwin'
  audio_capture_platform_dep = dependency('appleframeworks', modules: ['CoreFoundation'])
endif
vibestudio_app = static_library(
  'vibestudio_app',
  app_sources,
  app_moc_sources,
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, qt6_multimedia_dep, audio_capture_platform_dep, audio_duplex_dep, vibestudio_core_dep],
  install: false,
)

vibestudio_app_dep = declare_dependency(
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, qt6_multimedia_dep, audio_capture_platform_dep, audio_duplex_dep, vibestudio_core_dep],
  link_with: [vibestudio_app],
)

vibestudio = executable(
  'vibestudio',
  files('cli/audio_take.cpp'),
  files('cli/audio_recording.cpp'),
  files('cli/audio_session.cpp'),
  files('cli/level_gestures.cpp'),
  files('cli/model_import_repair.cpp'),
  files('cli/ai_generation.cpp'),
  ['main.cpp', 'cli/cli.cpp', 'cli/audio_session.cpp', 'cli/level_scene.cpp', 'cli/level_bookmarks.cpp', 'cli/workspace.cpp', 'cli/level_build_workspace.cpp', 'cli/level_build_artifacts.cpp', 'cli/level_build_deployment.cpp', 'cli/level_material_paint.cpp', 'cli/level_surface_clipboard.cpp', 'cli/model_mdl.cpp', 'cli/model_appearance.cpp', 'cli/model_assembly.cpp', 'cli/model_collision.cpp', 'cli/model_skin_bindings.cpp', 'cli/model_surfaces.cpp', 'cli/model_material_slots.cpp', 'cli/model_intersections.cpp', 'cli/package_copy_limits.cpp', 'cli/package_copy_store_limits.cpp', 'cli/package_copy_sessions.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: true,
)

deflate_smoke_test = executable(
  'deflate_smoke_test',
  ['tests/deflate_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('deflate-smoke', deflate_smoke_test)

idtech_image_smoke_test = executable(
  'idtech_image_smoke_test',
  ['tests/idtech_image_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('idtech-image-smoke', idtech_image_smoke_test)

texture_document_smoke_test = executable(
  'texture_document_smoke_test',
  ['tests/texture_document_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-document-smoke', texture_document_smoke_test, args: [vibestudio], timeout: 90)

texture_paint_smoke_test = executable(
  'texture_paint_smoke_test',
  ['tests/texture_paint_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-paint-smoke', texture_paint_smoke_test, timeout: 90)

texture_transform_smoke_test = executable(
  'texture_transform_smoke_test',
  ['tests/texture_transform_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-transform-smoke', texture_transform_smoke_test, timeout: 90)

texture_resource_smoke_test = executable(
  'texture_resource_smoke_test',
  ['tests/texture_resource_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-resource-smoke', texture_resource_smoke_test, timeout: 90)
test('texture-performance-smoke', texture_resource_smoke_test, args: ['--benchmark'], timeout: 180, is_parallel: false)

texture_preview_worker_smoke_test = executable(
  'texture_preview_worker_smoke_test',
  ['tests/texture_preview_worker_smoke_test.cpp', 'app/texture_preview_worker.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-preview-worker-smoke', texture_preview_worker_smoke_test, timeout: 90)

texture_palette_ui_smoke_test = executable(
  'texture_palette_ui_smoke_test',
  ['tests/texture_palette_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('texture-palette-ui-smoke', texture_palette_ui_smoke_test, timeout: 90)

texture_thumbnail_cache_smoke_test = executable(
  'texture_thumbnail_cache_smoke_test',
  ['tests/texture_thumbnail_cache_smoke_test.cpp', 'app/texture_thumbnail_cache.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep],
  install: false,
)
test('texture-thumbnail-cache-smoke', texture_thumbnail_cache_smoke_test, timeout: 90)

texture_png_export_ui_smoke_test = executable(
  'texture_png_export_ui_smoke_test',
  ['tests/texture_png_export_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('texture-png-export-ui-smoke', texture_png_export_ui_smoke_test, timeout: 90)

texture_export_smoke_test = executable(
  'texture_export_smoke_test',
  ['tests/texture_export_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-export-smoke', texture_export_smoke_test, args: [vibestudio], timeout: 90)

texture_staging_smoke_test = executable(
  'texture_staging_smoke_test',
  ['tests/texture_staging_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-staging-smoke', texture_staging_smoke_test, args: [vibestudio], timeout: 90)

texture_handoff_smoke_test = executable(
  'texture_handoff_smoke_test',
  ['tests/texture_handoff_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-handoff-smoke', texture_handoff_smoke_test, timeout: 90)

texture_project_smoke_test = executable(
  'texture_project_smoke_test',
  ['tests/texture_project_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('texture-project-smoke', texture_project_smoke_test, timeout: 90)

texture_recovery_smoke_test = executable(
  'texture_recovery_smoke_test',
  ['tests/texture_recovery_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('texture-recovery-smoke', texture_recovery_smoke_test, args: [vibestudio], timeout: 90)

texture_editor_ui_smoke_test = executable(
  'texture_editor_ui_smoke_test',
  ['tests/texture_editor_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
# These suites construct the complete shell; serialize them on shared runners.
test('texture-editor-ui-smoke', texture_editor_ui_smoke_test, timeout: 90, is_parallel: false)

texture_editor_latency_smoke_test = executable(
  'texture_editor_latency_smoke_test',
  ['tests/texture_editor_latency_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('texture-editor-latency-smoke', texture_editor_latency_smoke_test, timeout: 180, is_parallel: false)

texture_accessibility_smoke_test = executable(
  'texture_accessibility_smoke_test',
  ['tests/texture_accessibility_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('texture-accessibility-smoke', texture_accessibility_smoke_test, timeout: 60, is_parallel: false)

texture_recovery_ui_smoke_test = executable(
  'texture_recovery_ui_smoke_test',
  ['tests/texture_recovery_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('texture-recovery-ui-smoke', texture_recovery_ui_smoke_test, timeout: 90, is_parallel: false)

texture_export_ui_smoke_test = executable(
  'texture_export_ui_smoke_test',
  ['tests/texture_export_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('texture-export-ui-smoke', texture_export_ui_smoke_test, timeout: 90, is_parallel: false)

map_assets_smoke_test = executable(
  'map_assets_smoke_test',
  ['tests/map_assets_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('map-assets-smoke', map_assets_smoke_test)

package_context_ui_smoke_test = executable(
  'package_context_ui_smoke_test',
  ['tests/package_context_ui_smoke_test.cpp'],
  dependencies: vibestudio_app_dep,
  install: false,
)
test('package-context-ui-smoke', package_context_ui_smoke_test, timeout: 120)

package_context_smoke_test = executable(
  'package_context_smoke_test', ['tests/package_context_smoke_test.cpp'],
  dependencies: vibestudio_core_dep, install: false,
)
test('package-context-smoke', package_context_smoke_test, args: [vibestudio], timeout: 90)

level_texture_audit_ui_smoke_test = executable(
  'level_texture_audit_ui_smoke_test', ['tests/level_texture_audit_ui_smoke_test.cpp'],
  dependencies: vibestudio_app_dep, install: false,
)
test('level-texture-audit-ui-smoke', level_texture_audit_ui_smoke_test, timeout: 90)

level_package_workflow_smoke_test = executable(
  'level_package_workflow_smoke_test',
  ['tests/level_package_workflow_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('level-package-workflow-smoke', level_package_workflow_smoke_test, args: [vibestudio], timeout: 90)

level_dependency_ui_smoke_test = executable(
  'level_dependency_ui_smoke_test',
  ['tests/level_dependency_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('level-dependency-ui-smoke', level_dependency_ui_smoke_test, timeout: 60)

map_geometry_smoke_test = executable(
  'map_geometry_smoke_test',
  ['tests/map_geometry_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('map-geometry-smoke', map_geometry_smoke_test)

map_geometry_cache_smoke_test = executable('map_geometry_cache_smoke_test', ['tests/map_geometry_cache_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('map-geometry-cache-smoke', map_geometry_cache_smoke_test)

level_geometry_cache_ui_smoke_test = executable('level_geometry_cache_ui_smoke_test', ['tests/level_geometry_cache_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-geometry-cache-ui-smoke', level_geometry_cache_ui_smoke_test, timeout: 90)

level_plan_selection_ui_smoke_test = executable('level_plan_selection_ui_smoke_test', ['tests/level_plan_selection_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-plan-selection-ui-smoke', level_plan_selection_ui_smoke_test, timeout: 120)

level_plan_wires_ui_smoke_test = executable('level_plan_wires_ui_smoke_test', ['tests/level_plan_wires_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-plan-wires-ui-smoke', level_plan_wires_ui_smoke_test, timeout: 120)

level_selection_outlines_ui_smoke_test = executable('level_selection_outlines_ui_smoke_test', ['tests/level_selection_outlines_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-selection-outlines-ui-smoke', level_selection_outlines_ui_smoke_test, timeout: 120)

level_plan_worker_ui_smoke_test = executable('level_plan_worker_ui_smoke_test', ['tests/level_plan_worker_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-plan-worker-ui-smoke', level_plan_worker_ui_smoke_test, timeout: 120)

level_plan_scale_ui_smoke_test = executable('level_plan_scale_ui_smoke_test', ['tests/level_plan_scale_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-plan-scale-ui-smoke', level_plan_scale_ui_smoke_test, timeout: 60)

level_viewport_overlay_ui_smoke_test = executable('level_viewport_overlay_ui_smoke_test', ['tests/level_viewport_overlay_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-viewport-overlay-ui-smoke', level_viewport_overlay_ui_smoke_test, timeout: 60)

level_overlay_worker_ui_smoke_test = executable('level_overlay_worker_ui_smoke_test', ['tests/level_overlay_worker_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-overlay-worker-ui-smoke', level_overlay_worker_ui_smoke_test, timeout: 90)

level_pane_phase_ui_smoke_test = executable('level_pane_phase_ui_smoke_test', ['tests/level_pane_phase_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-pane-phase-ui-smoke', level_pane_phase_ui_smoke_test, timeout: 90)

box_resize_smoke_test = executable('box_resize_smoke_test', ['tests/box_resize_smoke_test.cpp', 'core/box_resize.cpp'],
  include_directories: vibestudio_includes, install: false)
test('box-resize-smoke', box_resize_smoke_test)

box_draw_smoke_test = executable('box_draw_smoke_test', ['tests/box_draw_smoke_test.cpp', 'core/box_draw.cpp'],
  include_directories: vibestudio_includes, install: false)
test('box-draw-smoke', box_draw_smoke_test)

level_camera_brush_ui_smoke_test = executable('level_camera_brush_ui_smoke_test', ['tests/level_camera_brush_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-camera-brush-ui-smoke', level_camera_brush_ui_smoke_test, timeout: 90)
test('level-camera-brush-latency-smoke', level_camera_brush_ui_smoke_test, args: ['--latency'], timeout: 180)

level_camera_brush_shell_smoke_test = executable('level_camera_brush_shell_smoke_test', ['tests/level_camera_brush_shell_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-camera-brush-shell-smoke', level_camera_brush_shell_smoke_test, timeout: 120)

level_camera_resize_ui_smoke_test = executable('level_camera_resize_ui_smoke_test', ['tests/level_camera_resize_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-camera-resize-ui-smoke', level_camera_resize_ui_smoke_test, timeout: 90)
test('level-camera-resize-latency-smoke', level_camera_resize_ui_smoke_test, args: ['--latency'], timeout: 180)

level_camera_resize_shell_smoke_test = executable('level_camera_resize_shell_smoke_test', ['tests/level_camera_resize_shell_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-camera-resize-shell-smoke', level_camera_resize_shell_smoke_test, timeout: 120)

level_map_load_smoke_test = executable('level_map_load_smoke_test', ['tests/level_map_load_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-map-load-smoke', level_map_load_smoke_test)

level_scene_smoke_test = executable('level_scene_smoke_test', ['tests/level_scene_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-scene-smoke', level_scene_smoke_test)

level_scene_locks_smoke_test = executable('level_scene_locks_smoke_test', ['tests/level_scene_locks_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-scene-locks-smoke', level_scene_locks_smoke_test, timeout: 120)

level_scene_locks_benchmark = executable('level_scene_locks_benchmark', ['tests/level_scene_locks_benchmark.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)

level_scene_ui_smoke_test = executable('level_scene_ui_smoke_test', ['tests/level_scene_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-scene-ui-smoke', level_scene_ui_smoke_test, timeout: 120)

level_scene_cli_smoke_test = executable('level_scene_cli_smoke_test', ['tests/level_scene_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-scene-cli-smoke', level_scene_cli_smoke_test, args: [vibestudio], timeout: 120)

level_map_load_ui_smoke_test = executable('level_map_load_ui_smoke_test', ['tests/level_map_load_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-map-load-ui-smoke', level_map_load_ui_smoke_test, timeout: 90)

level_map_load_benchmark = executable('level_map_load_benchmark', ['tests/level_map_load_benchmark.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)

# Explicit performance harness: timings are evidence, not portable CI thresholds.
level_geometry_benchmark = executable('level_geometry_benchmark', ['tests/level_geometry_benchmark.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)

level_plan_selection_benchmark = executable('level_plan_selection_benchmark', ['tests/level_plan_selection_benchmark.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)

map_render_smoke_test = executable(
  'map_render_smoke_test',
  ['tests/map_render_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('map-render-smoke', map_render_smoke_test)

bsp_inspect_smoke_test = executable(
  'bsp_inspect_smoke_test',
  ['tests/bsp_inspect_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('bsp-inspect-smoke', bsp_inspect_smoke_test)

build_pipeline_smoke_test = executable(
  'build_pipeline_smoke_test',
  ['tests/build_pipeline_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('build-pipeline-smoke', build_pipeline_smoke_test)

core_smoke_test = executable(
  'core_smoke_test',
  ['tests/core_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('core-smoke', core_smoke_test)

ai_connectors_smoke_test = executable(
  'ai_connectors_smoke_test',
  ['tests/ai_connectors_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ai-connectors-smoke', ai_connectors_smoke_test)

# Every provider's request and answer shape, then the client end to end
# against a fake provider on 127.0.0.1: no network, no credentials.
ai_transport_smoke_test = executable(
  'ai_transport_smoke_test',
  ['tests/ai_transport_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ai-transport-smoke', ai_transport_smoke_test)

# Image requests for OpenAI, Gemini, and the Stable Diffusion web UI, then
# the client against a fake provider: batches, links, cancel, timeout.
ai_image_transport_smoke_test = executable(
  'ai_image_transport_smoke_test',
  ['tests/ai_image_transport_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ai-image-transport-smoke', ai_image_transport_smoke_test)

# Generated-texture pipeline without a model: prompts, seams, resampling,
# palettes, companion maps, and writing WAD2, WAL, TGA, PWAD, and PNG outputs.
texture_generation_smoke_test = executable(
  'texture_generation_smoke_test',
  ['tests/texture_generation_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('texture-generation-smoke', texture_generation_smoke_test)

# Generated levels: prompt specs, rules and model plans with repair, layouts
# built for Quake, Quake II, Quake III and Doom and read back by the editor.
level_generation_smoke_test = executable(
  'level_generation_smoke_test',
  ['tests/level_generation_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('level-generation-smoke', level_generation_smoke_test, timeout: 120)

# Map edits a model proposes: the schema, the map summary it sees, checks
# against the map, applying through undoable edits, and a fake model.
level_ai_edit_smoke_test = executable(
  'level_ai_edit_smoke_test',
  ['tests/level_ai_edit_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('level-ai-edit-smoke', level_ai_edit_smoke_test, timeout: 120)

# Generated sounds: names and prompts per game, the synthesizer's kinds,
# trimming, loops and delivery, writing WAVs and Doom lumps, and the
# sound-effects request against a fake provider.
sound_generation_smoke_test = executable(
  'sound_generation_smoke_test',
  ['tests/sound_generation_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('sound-generation-smoke', sound_generation_smoke_test, timeout: 120)

# The generative commands through the executable: map generate, plan and
# ai-edit, asset audio-generate, ai status, and the AI-free refusals.
ai_generation_cli_smoke_test = executable(
  'ai_generation_cli_smoke_test',
  ['tests/ai_generation_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ai-generation-cli-smoke', ai_generation_cli_smoke_test, args: [vibestudio], timeout: 180)

ai_workflows_smoke_test = executable(
  'ai_workflows_smoke_test',
  ['tests/ai_workflows_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ai-workflows-smoke', ai_workflows_smoke_test)

advanced_studio_smoke_test = executable(
  'advanced_studio_smoke_test',
  ['tests/advanced_studio_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('advanced-studio-smoke', advanced_studio_smoke_test)

compiler_registry_smoke_test = executable(
  'compiler_registry_smoke_test',
  ['tests/compiler_registry_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('compiler-registry-smoke', compiler_registry_smoke_test)

compiler_known_issues_smoke_test = executable(
  'compiler_known_issues_smoke_test',
  ['tests/compiler_known_issues_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('compiler-known-issues-smoke', compiler_known_issues_smoke_test)

compiler_profiles_smoke_test = executable(
  'compiler_profiles_smoke_test',
  ['tests/compiler_profiles_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('compiler-profiles-smoke', compiler_profiles_smoke_test)

compiler_artifact_validation_smoke_test = executable(
  'compiler_artifact_validation_smoke_test',
  ['tests/compiler_artifact_validation_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('compiler-artifact-validation-smoke', compiler_artifact_validation_smoke_test)

ericw_map_preflight_smoke_test = executable(
  'ericw_map_preflight_smoke_test',
  ['tests/ericw_map_preflight_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ericw-map-preflight-smoke', ericw_map_preflight_smoke_test)

level_map_smoke_test = executable(
  'level_map_smoke_test',
  ['tests/level_map_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

# This cumulative suite covers 47 authoring and file-format scenarios. The
# instrumented Windows debug run takes over two minutes under concurrent load.
test('level-map-smoke', level_map_smoke_test, timeout: 240)

level_patch_smoke_test = executable(
  'level_patch_smoke_test',
  ['tests/level_patch_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('level-patch-smoke', level_patch_smoke_test, args: [vibestudio], timeout: 90)

level_primitive_smoke_test = executable('level_primitive_smoke_test', ['tests/level_primitive_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-primitive-smoke', level_primitive_smoke_test, args: [vibestudio], timeout: 120)

level_brush_jobs_smoke_test = executable('level_brush_jobs_smoke_test', ['tests/level_brush_jobs_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-brush-jobs-smoke', level_brush_jobs_smoke_test, timeout: 120)

level_object_model_smoke_test = executable('level_object_model_smoke_test', ['tests/level_object_model_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-object-model-smoke', level_object_model_smoke_test, timeout: 180)

level_object_shell_smoke_test = executable('level_object_shell_smoke_test', ['tests/level_object_shell_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-object-shell-smoke', level_object_shell_smoke_test, timeout: 180)

level_texture_summary_smoke_test = executable('level_texture_summary_smoke_test', ['tests/level_texture_summary_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-texture-summary-smoke', level_texture_summary_smoke_test, timeout: 120)

level_brush_jobs_ui_smoke_test = executable('level_brush_jobs_ui_smoke_test', ['tests/level_brush_jobs_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-brush-jobs-ui-smoke', level_brush_jobs_ui_smoke_test, timeout: 180)
test('level-brush-jobs-latency-smoke', level_brush_jobs_ui_smoke_test, args: ['--latency'], timeout: 240)

level_prefab_smoke_test = executable('level_prefab_smoke_test', ['tests/level_prefab_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-prefab-smoke', level_prefab_smoke_test, timeout: 120)

level_prefab_ui_smoke_test = executable('level_prefab_ui_smoke_test', ['tests/level_prefab_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-prefab-ui-smoke', level_prefab_ui_smoke_test, timeout: 240)

level_views_ui_smoke_test = executable('level_views_ui_smoke_test', ['tests/level_views_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-views-ui-smoke', level_views_ui_smoke_test, args: [vibestudio], timeout: 240)

level_view_workspace_ui_smoke_test = executable('level_view_workspace_ui_smoke_test', ['tests/level_view_workspace_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-view-workspace-ui-smoke', level_view_workspace_ui_smoke_test, timeout: 240)

viewport_hud_smoke_test = executable('viewport_hud_smoke_test', ['tests/viewport_hud_smoke_test.cpp', 'app/viewport_hud.cpp'],
  include_directories: vibestudio_includes, dependencies: [qt6_dep], install: false)
test('viewport-hud-smoke', viewport_hud_smoke_test, timeout: 30)

viewport_overlay_smoke_test = executable('viewport_overlay_smoke_test',
  ['tests/viewport_overlay_smoke_test.cpp', 'app/map_grid.cpp', 'app/viewport_ring_cache.cpp', 'app/viewport_image.cpp'],
  include_directories: vibestudio_includes, dependencies: [qt6_dep], install: false)
test('viewport-overlay-smoke', viewport_overlay_smoke_test, timeout: 30)

level_gestures_smoke_test = executable('level_gestures_smoke_test', ['tests/level_gestures_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-gestures-smoke', level_gestures_smoke_test, args: [vibestudio], timeout: 120)

level_gestures_ui_smoke_test = executable('level_gestures_ui_smoke_test', ['tests/level_gestures_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-gestures-ui-smoke', level_gestures_ui_smoke_test, timeout: 240)

level_profiles_ui_smoke_test = executable('level_profiles_ui_smoke_test', ['tests/level_profiles_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-profiles-ui-smoke', level_profiles_ui_smoke_test, args: [vibestudio], timeout: 240)

level_view_links_ui_smoke_test = executable('level_view_links_ui_smoke_test', ['tests/level_view_links_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-view-links-ui-smoke', level_view_links_ui_smoke_test, timeout: 240)

level_navigation_smoke_test = executable('level_navigation_smoke_test', ['tests/level_navigation_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-navigation-smoke', level_navigation_smoke_test, args: [vibestudio], timeout: 120)

doom_preview_smoke_test = executable('doom_preview_smoke_test', ['tests/doom_preview_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('doom-preview-smoke', doom_preview_smoke_test, args: [vibestudio], timeout: 120)

doom_preview_ui_smoke_test = executable('doom_preview_ui_smoke_test', ['tests/doom_preview_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('doom-preview-ui-smoke', doom_preview_ui_smoke_test, timeout: 240)

level_doom_nodes_smoke_test = executable('level_doom_nodes_smoke_test', ['tests/level_doom_nodes_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-doom-nodes-smoke', level_doom_nodes_smoke_test, timeout: 120)

level_udmf_smoke_test = executable('level_udmf_smoke_test', ['tests/level_udmf_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-udmf-smoke', level_udmf_smoke_test, args: [vibestudio], timeout: 120)
level_udmf_ui_smoke_test = executable('level_udmf_ui_smoke_test', ['tests/level_udmf_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-udmf-ui-smoke', level_udmf_ui_smoke_test, timeout: 180)
level_udmf_transform_smoke_test = executable('level_udmf_transform_smoke_test', ['tests/level_udmf_transform_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-udmf-transform-smoke', level_udmf_transform_smoke_test, timeout: 120)
level_udmf_transform_ui_smoke_test = executable('level_udmf_transform_ui_smoke_test', ['tests/level_udmf_transform_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-udmf-transform-ui-smoke', level_udmf_transform_ui_smoke_test, timeout: 180)
level_doom_nodes_ui_smoke_test = executable('level_doom_nodes_ui_smoke_test', ['tests/level_doom_nodes_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-doom-nodes-ui-smoke', level_doom_nodes_ui_smoke_test, timeout: 120)

level_doom_mirror_smoke_test = executable('level_doom_mirror_smoke_test', ['tests/level_doom_mirror_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-doom-mirror-smoke', level_doom_mirror_smoke_test, args: [vibestudio], timeout: 120)

level_doom_mirror_ui_smoke_test = executable('level_doom_mirror_ui_smoke_test', ['tests/level_doom_mirror_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-doom-mirror-ui-smoke', level_doom_mirror_ui_smoke_test, timeout: 240)

level_material_paint_smoke_test = executable('level_material_paint_smoke_test', ['tests/level_material_paint_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-material-paint-smoke', level_material_paint_smoke_test, args: [vibestudio], timeout: 120)

level_material_paint_ui_smoke_test = executable('level_material_paint_ui_smoke_test', ['tests/level_material_paint_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-material-paint-ui-smoke', level_material_paint_ui_smoke_test, timeout: 240)

level_material_gestures_ui_smoke_test = executable('level_material_gestures_ui_smoke_test', ['tests/level_material_gestures_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-material-gestures-ui-smoke', level_material_gestures_ui_smoke_test, timeout: 240)

level_camera_drive_ui_smoke_test = executable('level_camera_drive_ui_smoke_test', ['tests/level_camera_drive_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-camera-drive-ui-smoke', level_camera_drive_ui_smoke_test, timeout: 120)

level_bookmarks_ui_smoke_test = executable('level_bookmarks_ui_smoke_test', ['tests/level_bookmarks_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-bookmarks-ui-smoke', level_bookmarks_ui_smoke_test, timeout: 240)

level_primitive_ui_smoke_test = executable('level_primitive_ui_smoke_test', ['tests/level_primitive_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-primitive-ui-smoke', level_primitive_ui_smoke_test, timeout: 180)

level_brush_smoke_test = executable('level_brush_smoke_test', ['tests/level_brush_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-brush-smoke', level_brush_smoke_test, args: [vibestudio], timeout: 90)

level_rotation_smoke_test = executable('level_rotation_smoke_test', ['tests/level_rotation_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-rotation-smoke', level_rotation_smoke_test, args: [vibestudio], timeout: 120)

level_placement_smoke_test = executable('level_placement_smoke_test', ['tests/level_placement_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-placement-smoke', level_placement_smoke_test, timeout: 120)

level_placement_jobs_smoke_test = executable('level_placement_jobs_smoke_test', ['tests/level_placement_jobs_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-placement-jobs-smoke', level_placement_jobs_smoke_test, timeout: 120)

level_build_workspace_smoke_test = executable('level_build_workspace_smoke_test', ['tests/level_build_workspace_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], include_directories: vibestudio_includes)
test('level-build-workspace-smoke', level_build_workspace_smoke_test, timeout: 120)

level_build_workspace_cli_smoke_test = executable('level_build_workspace_cli_smoke_test', ['tests/level_build_workspace_cli_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], include_directories: vibestudio_includes)
test('level-build-workspace-cli-smoke', level_build_workspace_cli_smoke_test, args: [vibestudio], timeout: 120)

level_build_engines_smoke_test = executable('level_build_engines_smoke_test', ['tests/level_build_engines_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], include_directories: vibestudio_includes)
test('level-build-engines-smoke', level_build_engines_smoke_test, args: [vibestudio], timeout: 180)

level_build_artifacts_smoke_test = executable('level_build_artifacts_smoke_test', ['tests/level_build_artifacts_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], include_directories: vibestudio_includes)
test('level-build-artifacts-smoke', level_build_artifacts_smoke_test, args: [vibestudio], timeout: 120)

level_build_deployment_smoke_test = executable('level_build_deployment_smoke_test', ['tests/level_build_deployment_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], include_directories: vibestudio_includes)
test('level-build-deployment-smoke', level_build_deployment_smoke_test, args: [level_build_artifacts_smoke_test, vibestudio], timeout: 180)

level_classic_deployment_smoke_test = executable('level_classic_deployment_smoke_test', ['tests/level_classic_deployment_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], include_directories: vibestudio_includes)
test('level-classic-deployment-smoke', level_classic_deployment_smoke_test, args: [level_build_engines_smoke_test, vibestudio], timeout: 240)

level_classic_deployment_ui_smoke_test = executable('level_classic_deployment_ui_smoke_test', ['tests/level_classic_deployment_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], include_directories: vibestudio_includes)
test('level-classic-deployment-ui-smoke', level_classic_deployment_ui_smoke_test, args: [level_build_engines_smoke_test, level_classic_deployment_smoke_test], timeout: 240)

level_build_deployment_ui_smoke_test = executable('level_build_deployment_ui_smoke_test', ['tests/level_build_deployment_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], include_directories: vibestudio_includes)
test('level-build-deployment-ui-smoke', level_build_deployment_ui_smoke_test, args: [level_build_artifacts_smoke_test, level_build_deployment_smoke_test], timeout: 240)

level_build_package_ui_smoke_test = executable('level_build_package_ui_smoke_test', ['tests/level_build_package_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], include_directories: vibestudio_includes)
test('level-build-package-ui-smoke', level_build_package_ui_smoke_test, args: [level_build_artifacts_smoke_test], timeout: 240)

level_build_workspace_ui_smoke_test = executable('level_build_workspace_ui_smoke_test', ['tests/level_build_workspace_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], include_directories: vibestudio_includes)
test('level-build-workspace-ui-smoke', level_build_workspace_ui_smoke_test, args: [level_build_engines_smoke_test], timeout: 240)

level_placement_jobs_ui_smoke_test = executable('level_placement_jobs_ui_smoke_test', ['tests/level_placement_jobs_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-placement-jobs-ui-smoke', level_placement_jobs_ui_smoke_test, timeout: 180)

level_placement_cli_smoke_test = executable('level_placement_cli_smoke_test', ['tests/level_placement_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-placement-cli-smoke', level_placement_cli_smoke_test, args: [vibestudio], timeout: 120)

level_placement_ui_smoke_test = executable('level_placement_ui_smoke_test', ['tests/level_placement_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-placement-ui-smoke', level_placement_ui_smoke_test, timeout: 240)

level_placement_benchmark = executable('level_placement_benchmark', ['tests/level_placement_benchmark.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)

level_rotation_ui_smoke_test = executable('level_rotation_ui_smoke_test', ['tests/level_rotation_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-rotation-ui-smoke', level_rotation_ui_smoke_test, timeout: 180)

level_transform_smoke_test = executable('level_transform_smoke_test', ['tests/level_transform_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-transform-smoke', level_transform_smoke_test, args: [vibestudio], timeout: 120)

level_transform_ui_smoke_test = executable('level_transform_ui_smoke_test', ['tests/level_transform_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-transform-ui-smoke', level_transform_ui_smoke_test, timeout: 180)

level_materials_smoke_test = executable('level_materials_smoke_test', ['tests/level_materials_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-materials-smoke', level_materials_smoke_test, args: [vibestudio], timeout: 120)

level_materials_ui_smoke_test = executable('level_materials_ui_smoke_test', ['tests/level_materials_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-materials-ui-smoke', level_materials_ui_smoke_test, timeout: 180)

level_surface_smoke_test = executable('level_surface_smoke_test', ['tests/level_surface_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-surface-smoke', level_surface_smoke_test, args: [vibestudio], timeout: 120)

level_merge_smoke_test = executable('level_merge_smoke_test', ['tests/level_merge_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-merge-smoke', level_merge_smoke_test, args: [vibestudio], timeout: 120)

level_merge_ui_smoke_test = executable('level_merge_ui_smoke_test', ['tests/level_merge_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-merge-ui-smoke', level_merge_ui_smoke_test, timeout: 180)

patch_stitch_smoke_test = executable('patch_stitch_smoke_test', ['tests/patch_stitch_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('patch-stitch-smoke', patch_stitch_smoke_test, args: [vibestudio], timeout: 120)

patch_stitch_ui_smoke_test = executable('patch_stitch_ui_smoke_test', ['tests/patch_stitch_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('patch-stitch-ui-smoke', patch_stitch_ui_smoke_test, timeout: 180)

patch_cap_smoke_test = executable('patch_cap_smoke_test', ['tests/patch_cap_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('patch-cap-smoke', patch_cap_smoke_test, args: [vibestudio], timeout: 120)

patch_cap_ui_smoke_test = executable('patch_cap_ui_smoke_test', ['tests/patch_cap_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('patch-cap-ui-smoke', patch_cap_ui_smoke_test, timeout: 180)

level_surface_ui_smoke_test = executable('level_surface_ui_smoke_test', ['tests/level_surface_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-surface-ui-smoke', level_surface_ui_smoke_test, timeout: 180)

level_surface_batch_smoke_test = executable('level_surface_batch_smoke_test', ['tests/level_surface_batch_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-surface-batch-smoke', level_surface_batch_smoke_test, timeout: 120)

level_surface_clipboard_smoke_test = executable('level_surface_clipboard_smoke_test', ['tests/level_surface_clipboard_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-surface-clipboard-smoke', level_surface_clipboard_smoke_test, args: [vibestudio], timeout: 120)

level_surface_wrap_smoke_test = executable('level_surface_wrap_smoke_test', ['tests/level_surface_wrap_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-surface-wrap-smoke', level_surface_wrap_smoke_test, timeout: 120)

level_surface_transfer_smoke_test = executable('level_surface_transfer_smoke_test', ['tests/level_surface_transfer_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-surface-transfer-smoke', level_surface_transfer_smoke_test, args: [vibestudio], timeout: 120)

level_surface_project_smoke_test = executable('level_surface_project_smoke_test', ['tests/level_surface_project_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-surface-project-smoke', level_surface_project_smoke_test, args: [vibestudio], timeout: 120)

level_surface_stroke_smoke_test = executable('level_surface_stroke_smoke_test', ['tests/level_surface_stroke_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('level-surface-stroke-smoke', level_surface_stroke_smoke_test, args: [vibestudio], timeout: 120)

level_surface_stroke_ui_smoke_test = executable('level_surface_stroke_ui_smoke_test', ['tests/level_surface_stroke_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-surface-stroke-ui-smoke', level_surface_stroke_ui_smoke_test, timeout: 240)

level_surface_clipboard_ui_smoke_test = executable('level_surface_clipboard_ui_smoke_test', ['tests/level_surface_clipboard_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-surface-clipboard-ui-smoke', level_surface_clipboard_ui_smoke_test, timeout: 240)

level_surface_quick_ui_smoke_test = executable('level_surface_quick_ui_smoke_test', ['tests/level_surface_quick_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('level-surface-quick-ui-smoke', level_surface_quick_ui_smoke_test, timeout: 180)

brush_editor_ui_smoke_test = executable('brush_editor_ui_smoke_test', ['tests/brush_editor_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
# Includes the full shell workflow and two scaled/translated dialog renders.
test('brush-editor-ui-smoke', brush_editor_ui_smoke_test, timeout: 180)

patch_editor_ui_smoke_test = executable(
  'patch_editor_ui_smoke_test',
  ['tests/patch_editor_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('patch-editor-ui-smoke', patch_editor_ui_smoke_test, timeout: 90)

level_document_smoke_test = executable(
  'level_document_smoke_test',
  ['tests/level_document_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
)
test('level-document-smoke', level_document_smoke_test, args: [vibestudio], timeout: 90)

level_document_ui_smoke_test = executable(
  'level_document_ui_smoke_test',
  ['tests/level_document_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
)
test('level-document-ui-smoke', level_document_ui_smoke_test, timeout: 90)

localization_smoke_test = executable(
  'localization_smoke_test',
  ['tests/localization_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('localization-smoke', localization_smoke_test)

studio_semantics_smoke_test = executable(
  'studio_semantics_smoke_test',
  ['tests/studio_semantics_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('studio-semantics-smoke', studio_semantics_smoke_test)

editor_profiles_smoke_test = executable(
  'editor_profiles_smoke_test',
  ['tests/editor_profiles_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('editor-profiles-smoke', editor_profiles_smoke_test)

settings_smoke_test = executable(
  'settings_smoke_test',
  ['tests/settings_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('settings-smoke', settings_smoke_test)

operation_state_smoke_test = executable(
  'operation_state_smoke_test',
  ['tests/operation_state_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('operation-state-smoke', operation_state_smoke_test)

game_installation_smoke_test = executable(
  'game_installation_smoke_test',
  ['tests/game_installation_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('game-installation-smoke', game_installation_smoke_test)

package_archive_smoke_test = executable(
  'package_archive_smoke_test',
  ['tests/package_archive_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('package-archive-smoke', package_archive_smoke_test)

package_zip_metadata_smoke_test = executable(
  'package_zip_metadata_smoke_test', ['tests/package_zip_metadata_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-zip-metadata-smoke', package_zip_metadata_smoke_test, timeout: 90)

package_base_identity_smoke_test = executable(
  'package_base_identity_smoke_test', ['tests/package_base_identity_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-base-identity-smoke', package_base_identity_smoke_test, timeout: 90)
package_protection_smoke_test = executable(
  'package_protection_smoke_test', ['tests/package_protection_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-protection-smoke', package_protection_smoke_test, timeout: 90)

package_protection_work_smoke_test = executable(
  'package_protection_work_smoke_test', ['tests/package_protection_work_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep],
)
test('package-protection-work-smoke', package_protection_work_smoke_test, timeout: 90)

package_base_admission_smoke_test = executable(
  'package_base_admission_smoke_test', ['tests/package_base_admission_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-base-admission-smoke', package_base_admission_smoke_test, timeout: 90)

package_index_smoke_test = executable(
  'package_index_smoke_test',
  ['tests/package_index_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-index-smoke', package_index_smoke_test, timeout: 90)

ogg_preview_smoke_test = executable(
  'ogg_preview_smoke_test', ['tests/ogg_preview_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('ogg-preview-smoke', ogg_preview_smoke_test, args: [vibestudio], timeout: 90)

package_preview_smoke_test = executable(
  'package_preview_smoke_test',
  ['tests/package_preview_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('package-preview-smoke', package_preview_smoke_test)

package_preview_worker_smoke_test = executable(
  'package_preview_worker_smoke_test',
  ['tests/package_preview_worker_smoke_test.cpp', 'app/package_preview_worker.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-preview-worker-smoke', package_preview_worker_smoke_test, timeout: 90)

package_preview_ui_smoke_test = executable(
  'package_preview_ui_smoke_test',
  ['tests/package_preview_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('package-preview-ui-smoke', package_preview_ui_smoke_test, timeout: 180)

asset_tools_smoke_test = executable(
  'asset_tools_smoke_test',
  ['tests/asset_tools_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('asset-tools-smoke', asset_tools_smoke_test)

project_text_search_smoke_test = executable(
  'project_text_search_smoke_test',
  ['tests/project_text_search_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('project-text-search-smoke', project_text_search_smoke_test, args: [vibestudio], timeout: 60)

language_completion_smoke_test = executable(
  'language_completion_smoke_test',
  ['tests/language_completion_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('language-completion-smoke', language_completion_smoke_test, timeout: 30)

language_diagnostics_smoke_test = executable(
  'language_diagnostics_smoke_test', ['tests/language_diagnostics_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], install: false,
)
test('language-diagnostics-smoke', language_diagnostics_smoke_test, args: [vibestudio], timeout: 90)

language_diagnostics_ui_smoke_test = executable(
  'language_diagnostics_ui_smoke_test', ['tests/language_diagnostics_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], install: false,
)
test('language-diagnostics-ui-smoke', language_diagnostics_ui_smoke_test, timeout: 180)

completion_snippet_smoke_test = executable(
  'completion_snippet_smoke_test', ['tests/completion_snippet_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], install: false,
)
test('completion-snippet-smoke', completion_snippet_smoke_test, args: [vibestudio], timeout: 60)

completion_snippet_ui_smoke_test = executable(
  'completion_snippet_ui_smoke_test', ['tests/completion_snippet_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], install: false,
)
test('completion-snippet-ui-smoke', completion_snippet_ui_smoke_test, timeout: 180)

language_completion_resolve_smoke_test = executable(
  'language_completion_resolve_smoke_test', ['tests/language_completion_resolve_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], install: false,
)
test('language-completion-resolve-smoke', language_completion_resolve_smoke_test, args: [vibestudio], timeout: 90)

language_completion_resolve_ui_smoke_test = executable(
  'language_completion_resolve_ui_smoke_test', ['tests/language_completion_resolve_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], install: false,
)
test('language-completion-resolve-ui-smoke', language_completion_resolve_ui_smoke_test, timeout: 180)

language_completion_ui_smoke_test = executable(
  'language_completion_ui_smoke_test',
  ['tests/language_completion_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('language-completion-ui-smoke', language_completion_ui_smoke_test, timeout: 120)

language_server_smoke_test = executable(
  'language_server_smoke_test',
  ['tests/language_server_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('language-server-smoke', language_server_smoke_test, args: [vibestudio], timeout: 60)

language_references_smoke_test = executable(
  'language_references_smoke_test',
  ['tests/language_references_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('language-references-smoke', language_references_smoke_test, timeout: 30)

language_references_ui_smoke_test = executable(
  'language_references_ui_smoke_test',
  ['tests/language_references_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('language-references-ui-smoke', language_references_ui_smoke_test, timeout: 120)

language_hover_smoke_test = executable(
  'language_hover_smoke_test',
  ['tests/language_hover_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('language-hover-smoke', language_hover_smoke_test, timeout: 30)

language_signature_smoke_test = executable('language_signature_smoke_test', ['tests/language_signature_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], install: false)
test('language-signature-smoke', language_signature_smoke_test, args: [vibestudio], timeout: 90)

language_signature_ui_smoke_test = executable('language_signature_ui_smoke_test', ['tests/language_signature_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], install: false)
test('language-signature-ui-smoke', language_signature_ui_smoke_test, timeout: 180)

language_formatting_smoke_test = executable(
  'language_formatting_smoke_test', ['tests/language_formatting_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], install: false,
)
test('language-formatting-smoke', language_formatting_smoke_test, args: [vibestudio], timeout: 90)

language_formatting_ui_smoke_test = executable(
  'language_formatting_ui_smoke_test', ['tests/language_formatting_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], install: false,
)
test('language-formatting-ui-smoke', language_formatting_ui_smoke_test, timeout: 120)

language_rename_smoke_test = executable(
  'language_rename_smoke_test', ['tests/language_rename_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], install: false,
)
test('language-rename-smoke', language_rename_smoke_test, args: [vibestudio], timeout: 90)

language_rename_ui_smoke_test = executable(
  'language_rename_ui_smoke_test', ['tests/language_rename_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], install: false,
)
test('language-rename-ui-smoke', language_rename_ui_smoke_test, timeout: 120)

language_code_actions_smoke_test = executable(
  'language_code_actions_smoke_test', ['tests/language_code_actions_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep], install: false,
)
test('language-code-actions-smoke', language_code_actions_smoke_test, args: [vibestudio], timeout: 90)

language_code_actions_ui_smoke_test = executable(
  'language_code_actions_ui_smoke_test', ['tests/language_code_actions_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep], install: false,
)
test('language-code-actions-ui-smoke', language_code_actions_ui_smoke_test, timeout: 180)

language_hover_ui_smoke_test = executable(
  'language_hover_ui_smoke_test',
  ['tests/language_hover_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('language-hover-ui-smoke', language_hover_ui_smoke_test, timeout: 120)

language_server_ui_smoke_test = executable(
  'language_server_ui_smoke_test',
  ['tests/language_server_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('language-server-ui-smoke', language_server_ui_smoke_test, timeout: 120)

project_search_ui_smoke_test = executable(
  'project_search_ui_smoke_test',
  ['tests/project_search_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('project-search-ui-smoke', project_search_ui_smoke_test, timeout: 90)

text_document_smoke_test = executable(
  'text_document_smoke_test',
  ['tests/text_document_smoke_test.cpp'],
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('text-document-smoke', text_document_smoke_test, args: [vibestudio], timeout: 90)

text_document_ui_smoke_test = executable(
  'text_document_ui_smoke_test',
  ['tests/text_document_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('text-document-ui-smoke', text_document_ui_smoke_test, timeout: 90)

code_document_ui_smoke_test = executable(
  'code_document_ui_smoke_test',
  ['tests/code_document_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
)
test('code-document-ui-smoke', code_document_ui_smoke_test, timeout: 120)

text_recovery_smoke_test = executable(
  'text_recovery_smoke_test',
  ['tests/text_recovery_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('text-recovery-smoke', text_recovery_smoke_test, args: [vibestudio], timeout: 90)

code_index_smoke_test = executable(
  'code_index_smoke_test',
  ['tests/code_index_smoke_test.cpp', 'app/code_index_worker.cpp'],
  dependencies: [vibestudio_core_dep],
)
test('code-index-smoke', code_index_smoke_test, args: [vibestudio], timeout: 90)

code_index_ui_smoke_test = executable(
  'code_index_ui_smoke_test',
  ['tests/code_index_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
)
test('code-index-ui-smoke', code_index_ui_smoke_test, timeout: 120)

code_files_smoke_test = executable(
  'code_files_smoke_test',
  ['tests/code_files_smoke_test.cpp', 'app/code_files_worker.cpp'],
  dependencies: [vibestudio_core_dep],
)
test('code-files-smoke', code_files_smoke_test, args: [vibestudio], timeout: 90)

code_files_ui_smoke_test = executable(
  'code_files_ui_smoke_test',
  ['tests/code_files_ui_smoke_test.cpp'],
  dependencies: [vibestudio_app_dep],
)
test('code-files-ui-smoke', code_files_ui_smoke_test, timeout: 120)

quick_open_ui_smoke_test = executable(
  'quick_open_ui_smoke_test',
  ['tests/quick_open_ui_smoke_test.cpp'],
  dependencies: vibestudio_app_dep,
)
test('quick-open-ui-smoke', quick_open_ui_smoke_test, timeout: 120)

package_staging_smoke_test = executable(
  'package_staging_smoke_test',
  ['tests/package_staging_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

# Includes two 65k-entry ZIP64 round trips; debug builds under CI load need
# more than the default 30 seconds. Performance acceptance is measured separately.
test('package-staging-smoke', package_staging_smoke_test, timeout: 90)

package_plan_smoke_test = executable(
  'package_plan_smoke_test',
  ['tests/package_plan_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-plan-smoke', package_plan_smoke_test, timeout: 90)

package_plan_limits_smoke_test = executable(
  'package_plan_limits_smoke_test',
  ['tests/package_plan_limits_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-plan-limits-smoke', package_plan_limits_smoke_test, timeout: 90)

package_view_admission_smoke_test = executable(
  'package_view_admission_smoke_test',
  ['tests/package_view_admission_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-view-admission-smoke', package_view_admission_smoke_test, timeout: 90)

# Opt-in process-memory/cancellation workload; not a timing-sensitive test.
package_plan_scale_probe = executable(
  'package_plan_scale_probe',
  ['tests/package_plan_scale_probe.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

package_write_index_smoke_test = executable(
  'package_write_index_smoke_test',
  ['tests/package_write_index_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('package-write-index-smoke', package_write_index_smoke_test, timeout: 90)

package_snapshot_smoke_test = executable(
  'package_snapshot_smoke_test',
  ['tests/package_snapshot_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-snapshot-smoke', package_snapshot_smoke_test, timeout: 90)

package_summary_smoke_test = executable(
  'package_summary_smoke_test', ['tests/package_summary_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-summary-smoke', package_summary_smoke_test, timeout: 90)

package_summary_cli_smoke_test = executable(
  'package_summary_cli_smoke_test', ['tests/package_summary_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-summary-cli-smoke', package_summary_cli_smoke_test, args: [vibestudio], timeout: 90)

package_summary_ui_smoke_test = executable(
  'package_summary_ui_smoke_test', ['tests/package_summary_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-summary-ui-smoke', package_summary_ui_smoke_test, timeout: 120)


package_staging_metadata_smoke_test = executable(
  'package_staging_metadata_smoke_test',
  ['tests/package_staging_metadata_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-staging-metadata-smoke', package_staging_metadata_smoke_test, timeout: 90)

package_staging_content_smoke_test = executable(
  'package_staging_content_smoke_test',
  ['tests/package_staging_content_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-staging-content-smoke', package_staging_content_smoke_test, timeout: 90)

project_manifest_smoke_test = executable(
  'project_manifest_smoke_test',
  ['tests/project_manifest_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('project-manifest-smoke', project_manifest_smoke_test)

inspector_layout_smoke_test = executable('inspector_layout_smoke_test', ['tests/inspector_layout_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('inspector-layout-smoke', inspector_layout_smoke_test, timeout: 90)

ui_primitives_smoke_test = executable(
  'ui_primitives_smoke_test',
  ['tests/ui_primitives_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)

ui_primitives_test_env = []
if host_machine.system() != 'windows'
  ui_primitives_test_env += 'QT_QPA_PLATFORM=offscreen'
endif

test(
  'ui-primitives-smoke',
  ui_primitives_smoke_test,
  env: ui_primitives_test_env,
)

document_watch_smoke_test = executable(
  'document_watch_smoke_test',
  ['tests/document_watch_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('document-watch-smoke', document_watch_smoke_test)

entity_definitions_smoke_test = executable(
  'entity_definitions_smoke_test',
  ['tests/entity_definitions_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('entity-definitions-smoke', entity_definitions_smoke_test)

model_mesh_smoke_test = executable(
  'model_mesh_smoke_test',
  ['tests/model_mesh_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('model-mesh-smoke', model_mesh_smoke_test)

model_design_smoke_test = executable(
  'model_design_smoke_test',
  ['tests/model_design_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
# CLI fixtures change their child working directory. Pass an absolute program
# path on every platform and retain the explicit application build dependency.
test('model-design-smoke', model_design_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, timeout: 90)

model_design_ui_smoke_test = executable(
  'model_design_ui_smoke_test',
  ['tests/model_design_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('model-design-ui-smoke', model_design_ui_smoke_test, timeout: 60)

model_document_test_env = environment()
model_document_test_env.set('VIBESTUDIO_TEST_TMP_ROOT', join_paths(meson.project_source_root(), '.agents', 'tmp', 'modeller-rc', 'runtime'))

model_mdl_smoke_test = executable(
  'model_mdl_smoke_test', ['tests/model_mdl_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep], install: false,
)
test('model-mdl-smoke', model_mdl_smoke_test, env: model_document_test_env, timeout: 90)
model_mdl_export_smoke_test = executable(
  'model_mdl_export_smoke_test', ['tests/model_mdl_export_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep], install: false,
)
test('model-mdl-export-smoke', model_mdl_export_smoke_test, env: model_document_test_env, timeout: 90)
model_mdl_edit_smoke_test = executable(
  'model_mdl_edit_smoke_test', ['tests/model_mdl_edit_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep], install: false,
)
test('model-mdl-edit-smoke', model_mdl_edit_smoke_test, env: model_document_test_env, timeout: 90)
model_mdl_cli_smoke_test = executable(
  'model_mdl_cli_smoke_test', ['tests/model_mdl_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep], install: false,
)
test('model-mdl-cli-smoke', model_mdl_cli_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)
model_mdl_ui_smoke_test = executable(
  'model_mdl_ui_smoke_test', ['tests/model_mdl_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep], install: false,
)
test('model-mdl-ui-smoke', model_mdl_ui_smoke_test, env: model_document_test_env, timeout: 90)
model_mdl_playback_smoke_test = executable(
  'model_mdl_playback_smoke_test', ['tests/model_mdl_playback_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep], install: false,
)
test('model-mdl-playback-smoke', model_mdl_playback_smoke_test, env: model_document_test_env, timeout: 90)
model_skin_source_smoke_test = executable(
  'model_skin_source_smoke_test', ['tests/model_skin_source_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep], install: false,
)
test('model-skin-source-smoke', model_skin_source_smoke_test, env: model_document_test_env, timeout: 90)
model_skin_source_ui_smoke_test = executable(
  'model_skin_source_ui_smoke_test', ['tests/model_skin_source_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep], install: false,
)
test('model-skin-source-ui-smoke', model_skin_source_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_skin_bindings_smoke_test = executable(
  'model_skin_bindings_smoke_test', ['tests/model_skin_bindings_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-skin-bindings-smoke', model_skin_bindings_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)
model_skin_bindings_ui_smoke_test = executable(
  'model_skin_bindings_ui_smoke_test', ['tests/model_skin_bindings_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-skin-bindings-ui-smoke', model_skin_bindings_ui_smoke_test, env: model_document_test_env, timeout: 120)
model_document_smoke_test = executable(
  'model_document_smoke_test',
  ['tests/model_document_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('model-document-smoke', model_document_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_obj_smoke_test = executable(
  'model_obj_smoke_test', ['tests/model_obj_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-obj-smoke', model_obj_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_obj_ui_smoke_test = executable(
  'model_obj_ui_smoke_test', ['tests/model_obj_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-obj-ui-smoke', model_obj_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_preview_smoke_test = executable(
  'model_preview_smoke_test', ['tests/model_preview_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-preview-smoke', model_preview_smoke_test, env: model_document_test_env, timeout: 90)

model_frame_export_smoke_test = executable(
  'model_frame_export_smoke_test', ['tests/model_frame_export_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-frame-export-smoke', model_frame_export_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_frame_export_ui_smoke_test = executable(
  'model_frame_export_ui_smoke_test', ['tests/model_frame_export_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-frame-export-ui-smoke', model_frame_export_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_appearance_smoke_test = executable(
  'model_appearance_smoke_test', ['tests/model_appearance_smoke_test.cpp', 'cli/model_appearance.cpp', 'app/model_preview_worker.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-appearance-smoke', model_appearance_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

level_model_appearance_smoke_test = executable(
  'level_model_appearance_smoke_test', ['tests/level_model_appearance_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('level-model-appearance-smoke', level_model_appearance_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

level_model_appearance_ui_smoke_test = executable(
  'level_model_appearance_ui_smoke_test', ['tests/level_model_appearance_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('level-model-appearance-ui-smoke', level_model_appearance_ui_smoke_test, env: model_document_test_env, timeout: 180)

model_appearance_ui_smoke_test = executable(
  'model_appearance_ui_smoke_test', ['tests/model_appearance_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-appearance-ui-smoke', model_appearance_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_md2_smoke_test = executable(
  'model_md2_smoke_test', ['tests/model_md2_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-md2-smoke', model_md2_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_winding_smoke_test = executable(
  'model_winding_smoke_test', ['tests/model_winding_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-winding-smoke', model_winding_smoke_test, env: model_document_test_env, timeout: 90)

model_topology_smoke_test = executable(
  'model_topology_smoke_test',
  ['tests/model_topology_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('model-topology-smoke', model_topology_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_health_smoke_test = executable(
  'model_health_smoke_test', ['tests/model_health_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-health-smoke', model_health_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_nonmanifold_smoke_test = executable(
  'model_nonmanifold_smoke_test', ['tests/model_nonmanifold_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-nonmanifold-smoke', model_nonmanifold_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_intersections_smoke_test = executable(
  'model_intersections_smoke_test', ['tests/model_intersections_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-intersections-smoke', model_intersections_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_intersections_ui_smoke_test = executable(
  'model_intersections_ui_smoke_test', ['tests/model_intersections_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-intersections-ui-smoke', model_intersections_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_import_repair_smoke_test = executable(
  'model_import_repair_smoke_test', ['tests/model_import_repair_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep],
)
test('model-import-repair-smoke', model_import_repair_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_import_repair_ui_smoke_test = executable(
  'model_import_repair_ui_smoke_test', ['tests/model_import_repair_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep],
)
test('model-import-repair-ui-smoke', model_import_repair_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_health_ui_smoke_test = executable(
  'model_health_ui_smoke_test', ['tests/model_health_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-health-ui-smoke', model_health_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_surface_selection_smoke_test = executable(
  'model_surface_selection_smoke_test', ['tests/model_surface_selection_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-surface-selection-smoke', model_surface_selection_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_surface_selection_ui_smoke_test = executable(
  'model_surface_selection_ui_smoke_test', ['tests/model_surface_selection_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-surface-selection-ui-smoke', model_surface_selection_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_material_slots_smoke_test = executable(
  'model_material_slots_smoke_test', ['tests/model_material_slots_smoke_test.cpp', 'cli/model_material_slots.cpp', 'app/model_material_worker.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-material-slots-smoke', model_material_slots_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_material_slots_ui_smoke_test = executable(
  'model_material_slots_ui_smoke_test', ['tests/model_material_slots_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-material-slots-ui-smoke', model_material_slots_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_surfaces_smoke_test = executable(
  'model_surfaces_smoke_test', ['tests/model_surfaces_smoke_test.cpp', 'cli/model_surfaces.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-surfaces-smoke', model_surfaces_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_surfaces_ui_smoke_test = executable(
  'model_surfaces_ui_smoke_test', ['tests/model_surfaces_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-surfaces-ui-smoke', model_surfaces_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_boundary_fill_smoke_test = executable(
  'model_boundary_fill_smoke_test', ['tests/model_boundary_fill_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-boundary-fill-smoke', model_boundary_fill_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_boundary_fill_ui_smoke_test = executable(
  'model_boundary_fill_ui_smoke_test', ['tests/model_boundary_fill_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-boundary-fill-ui-smoke', model_boundary_fill_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_boundary_bridge_smoke_test = executable(
  'model_boundary_bridge_smoke_test', ['tests/model_boundary_bridge_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-boundary-bridge-smoke', model_boundary_bridge_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_boundary_bridge_ui_smoke_test = executable(
  'model_boundary_bridge_ui_smoke_test', ['tests/model_boundary_bridge_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-boundary-bridge-ui-smoke', model_boundary_bridge_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_transform_smoke_test = executable(
  'model_transform_smoke_test', ['tests/model_transform_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-transform-smoke', model_transform_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_transform_axes_smoke_test = executable(
  'model_transform_axes_smoke_test', ['tests/model_transform_axes_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-transform-axes-smoke', model_transform_axes_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_transform_axes_ui_smoke_test = executable(
  'model_transform_axes_ui_smoke_test', ['tests/model_transform_axes_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-transform-axes-ui-smoke', model_transform_axes_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_trackball_smoke_test = executable(
  'model_trackball_smoke_test', ['tests/model_trackball_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-trackball-smoke', model_trackball_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_trackball_ui_smoke_test = executable(
  'model_trackball_ui_smoke_test', ['tests/model_trackball_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-trackball-ui-smoke', model_trackball_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_tags_smoke_test = executable(
  'model_tags_smoke_test', ['tests/model_tags_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-tags-smoke', model_tags_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_tags_ui_smoke_test = executable(
  'model_tags_ui_smoke_test', ['tests/model_tags_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-tags-ui-smoke', model_tags_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_animation_smoke_test = executable(
  'model_animation_smoke_test', ['tests/model_animation_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-animation-smoke', model_animation_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_animation_ui_smoke_test = executable(
  'model_animation_ui_smoke_test', ['tests/model_animation_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-animation-ui-smoke', model_animation_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_playback_smoke_test = executable(
  'model_playback_smoke_test', ['tests/model_playback_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-playback-smoke', model_playback_smoke_test, env: model_document_test_env, timeout: 90)

model_uv_smoke_test = executable(
  'model_uv_smoke_test', ['tests/model_uv_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-uv-smoke', model_uv_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_uv_islands_smoke_test = executable(
  'model_uv_islands_smoke_test', ['tests/model_uv_islands_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-uv-islands-smoke', model_uv_islands_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_uv_islands_ui_smoke_test = executable(
  'model_uv_islands_ui_smoke_test', ['tests/model_uv_islands_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-uv-islands-ui-smoke', model_uv_islands_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_uv_atlas_smoke_test = executable(
  'model_uv_atlas_smoke_test', ['tests/model_uv_atlas_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-uv-atlas-smoke', model_uv_atlas_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_uv_atlas_ui_smoke_test = executable(
  'model_uv_atlas_ui_smoke_test', ['tests/model_uv_atlas_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-uv-atlas-ui-smoke', model_uv_atlas_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_uv_rect_smoke_test = executable(
  'model_uv_rect_smoke_test', ['tests/model_uv_rect_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-uv-rect-smoke', model_uv_rect_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_uv_rect_ui_smoke_test = executable(
  'model_uv_rect_ui_smoke_test', ['tests/model_uv_rect_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-uv-rect-ui-smoke', model_uv_rect_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_uv_obstacles_smoke_test = executable(
  'model_uv_obstacles_smoke_test', ['tests/model_uv_obstacles_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-uv-obstacles-smoke', model_uv_obstacles_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_uv_obstacles_ui_smoke_test = executable(
  'model_uv_obstacles_ui_smoke_test', ['tests/model_uv_obstacles_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-uv-obstacles-ui-smoke', model_uv_obstacles_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_uv_ui_smoke_test = executable(
  'model_uv_ui_smoke_test', ['tests/model_uv_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-uv-ui-smoke', model_uv_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_uv_render_smoke_test = executable(
  'model_uv_render_smoke_test', ['tests/model_uv_render_smoke_test.cpp', 'app/model_uv_render.cpp', 'app/model_uv_fill.cpp', 'app/model_rasterizer.cpp', 'core/model_uv.cpp', 'core/model_work.cpp'],
  include_directories: vibestudio_includes, dependencies: [qt6_core_dep], install: false,
)
test('model-uv-render-smoke', model_uv_render_smoke_test, env: model_document_test_env, timeout: 240)

model_manipulation_ui_smoke_test = executable(
  'model_manipulation_ui_smoke_test', ['tests/model_manipulation_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-manipulation-ui-smoke', model_manipulation_ui_smoke_test, env: model_document_test_env, timeout: 90)

install_headers('core/model_recovery.h', subdir: 'vibestudio/core')
model_recovery_smoke_test = executable(
  'model_recovery_smoke_test',
  ['tests/model_recovery_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('model-recovery-smoke', model_recovery_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_recovery_writer_smoke_test = executable(
  'model_recovery_writer_smoke_test',
  ['tests/model_recovery_writer_smoke_test.cpp', 'app/model_recovery_writer.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('model-recovery-writer-smoke', model_recovery_writer_smoke_test, env: model_document_test_env, timeout: 90)

model_file_io_smoke_test = executable(
  'model_file_io_smoke_test',
  ['tests/model_file_io_smoke_test.cpp', 'core/model_file_io.cpp', 'core/model_work.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [qt6_core_dep],
  install: false,
)
test('model-file-io-smoke', model_file_io_smoke_test, env: model_document_test_env, timeout: 90)

model_document_work_smoke_test = executable(
  'model_document_work_smoke_test',
  ['tests/model_document_work_smoke_test.cpp', 'app/model_document_work.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep, qt6_dep],
  install: false,
)
test('model-document-work-smoke', model_document_work_smoke_test, env: model_document_test_env, timeout: 90)

model_materials_smoke_test = executable(
  'model_materials_smoke_test',
  ['tests/model_materials_smoke_test.cpp', 'app/model_material_worker.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('model-materials-smoke', model_materials_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_materials_ui_smoke_test = executable(
  'model_materials_ui_smoke_test',
  ['tests/model_materials_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('model-materials-ui-smoke', model_materials_ui_smoke_test, env: model_document_test_env, timeout: 120)


model_recovery_ui_smoke_test = executable(
  'model_recovery_ui_smoke_test',
  ['tests/model_recovery_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('model-recovery-ui-smoke', model_recovery_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_editor_ui_smoke_test = executable(
  'model_editor_ui_smoke_test',
  ['tests/model_editor_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('model-editor-ui-smoke', model_editor_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_shell_smoke_test = executable(
  'model_shell_smoke_test', ['tests/model_shell_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-shell-smoke', model_shell_smoke_test, env: model_document_test_env, timeout: 90)

model_rasterizer_smoke_test = executable(
  'model_rasterizer_smoke_test',
  ['tests/model_rasterizer_smoke_test.cpp', 'app/model_rasterizer.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [qt6_core_dep],
  install: false,
)
test('model-rasterizer-smoke', model_rasterizer_smoke_test, timeout: 90)

model_wireframe_smoke_test = executable(
  'model_wireframe_smoke_test', ['tests/model_wireframe_smoke_test.cpp', 'app/model_rasterizer.cpp'],
  include_directories: vibestudio_includes, dependencies: [qt6_core_dep], install: false,
)
test('model-wireframe-smoke', model_wireframe_smoke_test, timeout: 90)

model_vertex_overlay_smoke_test = executable(
  'model_vertex_overlay_smoke_test', ['tests/model_vertex_overlay_smoke_test.cpp', 'app/model_vertex_overlay.cpp'],
  include_directories: vibestudio_includes, dependencies: [qt6_core_dep], install: false,
)
test('model-vertex-overlay-smoke', model_vertex_overlay_smoke_test, timeout: 90)

model_viewport_smoke_test = executable(
  'model_viewport_smoke_test',
  ['tests/model_viewport_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('model-viewport-smoke', model_viewport_smoke_test, timeout: 90)

model_viewport_latency_smoke_test = executable(
  'model_viewport_latency_smoke_test', ['tests/model_viewport_latency_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-viewport-latency-smoke', model_viewport_latency_smoke_test, timeout: 180)

model_editor_latency_smoke_test = executable(
  'model_editor_latency_smoke_test', ['tests/model_editor_latency_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-editor-latency-smoke', model_editor_latency_smoke_test, env: model_document_test_env, timeout: 180)

model_document_latency_smoke_test = executable(
  'model_document_latency_smoke_test', ['tests/model_document_latency_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-document-latency-smoke', model_document_latency_smoke_test, env: model_document_test_env, timeout: 240)

model_assembly_smoke_test = executable(
  'model_assembly_smoke_test', ['tests/model_assembly_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-assembly-smoke', model_assembly_smoke_test, env: model_document_test_env, timeout: 90)

model_q3_animation_smoke_test = executable(
  'model_q3_animation_smoke_test', ['tests/model_q3_animation_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-q3-animation-smoke', model_q3_animation_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_q3_animation_ui_smoke_test = executable(
  'model_q3_animation_ui_smoke_test', ['tests/model_q3_animation_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-q3-animation-ui-smoke', model_q3_animation_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_assembly_skin_smoke_test = executable(
  'model_assembly_skin_smoke_test', ['tests/model_assembly_skin_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-assembly-skin-smoke', model_assembly_skin_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_assembly_skin_ui_smoke_test = executable(
  'model_assembly_skin_ui_smoke_test', ['tests/model_assembly_skin_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-assembly-skin-ui-smoke', model_assembly_skin_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_player_bundle_smoke_test = executable(
  'model_player_bundle_smoke_test', ['tests/model_player_bundle_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-player-bundle-smoke', model_player_bundle_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_player_bundle_ui_smoke_test = executable(
  'model_player_bundle_ui_smoke_test', ['tests/model_player_bundle_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-player-bundle-ui-smoke', model_player_bundle_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_assembly_animation_smoke_test = executable(
  'model_assembly_animation_smoke_test', ['tests/model_assembly_animation_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-assembly-animation-smoke', model_assembly_animation_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_assembly_animation_ui_smoke_test = executable(
  'model_assembly_animation_ui_smoke_test', ['tests/model_assembly_animation_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-assembly-animation-ui-smoke', model_assembly_animation_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_assembly_cli_smoke_test = executable(
  'model_assembly_cli_smoke_test', ['tests/model_assembly_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-assembly-cli-smoke', model_assembly_cli_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_assembly_ui_smoke_test = executable(
  'model_assembly_ui_smoke_test', ['tests/model_assembly_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-assembly-ui-smoke', model_assembly_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_assembly_recovery_smoke_test = executable(
  'model_assembly_recovery_smoke_test', ['tests/model_assembly_recovery_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-assembly-recovery-smoke', model_assembly_recovery_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_assembly_recovery_ui_smoke_test = executable(
  'model_assembly_recovery_ui_smoke_test', ['tests/model_assembly_recovery_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-assembly-recovery-ui-smoke', model_assembly_recovery_ui_smoke_test, env: model_document_test_env, timeout: 90)

model_collision_smoke_test = executable(
  'model_collision_smoke_test', ['tests/model_collision_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-collision-smoke', model_collision_smoke_test, env: model_document_test_env, timeout: 90)

model_collision_cli_smoke_test = executable(
  'model_collision_cli_smoke_test', ['tests/model_collision_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-collision-cli-smoke', model_collision_cli_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 90)

model_collision_ui_smoke_test = executable(
  'model_collision_ui_smoke_test', ['tests/model_collision_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-collision-ui-smoke', model_collision_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_collision_animation_smoke_test = executable(
  'model_collision_animation_smoke_test', ['tests/model_collision_animation_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-collision-animation-smoke', model_collision_animation_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, env: model_document_test_env, timeout: 120)

model_collision_animation_ui_smoke_test = executable(
  'model_collision_animation_ui_smoke_test', ['tests/model_collision_animation_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('model-collision-animation-ui-smoke', model_collision_animation_ui_smoke_test, env: model_document_test_env, timeout: 120)

model_fingerprint_smoke_test = executable(
  'model_fingerprint_smoke_test', ['tests/model_fingerprint_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('model-fingerprint-smoke', model_fingerprint_smoke_test, timeout: 90)

audio_test_env = environment()
audio_test_env.set('VIBESTUDIO_TEST_TMP_ROOT', join_paths(meson.project_source_root(), '.agents', 'tmp', 'audio-editor'))
audio_test_env.set('VIBESTUDIO_AUDIO_FIXTURES', meson.current_source_dir() / 'tests/fixtures/audio_decode')
audio_clip_smoke_test = executable(
  'audio_clip_smoke_test',
  ['tests/audio_clip_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-clip-smoke', audio_clip_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 90)

audio_take_smoke_test = executable('audio_take_smoke_test', ['tests/audio_take_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-take-smoke', audio_take_smoke_test, env: audio_test_env, timeout: 120)

audio_capture_smoke_test = executable('audio_capture_smoke_test', ['tests/audio_capture_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-capture-smoke', audio_capture_smoke_test, env: audio_test_env, timeout: 120)

audio_recording_smoke_test = executable('audio_recording_smoke_test', ['tests/audio_recording_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-recording-smoke', audio_recording_smoke_test, env: audio_test_env, timeout: 120)

audio_recording_import_smoke_test = executable('audio_recording_import_smoke_test', ['tests/audio_recording_import_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-recording-import-smoke', audio_recording_import_smoke_test, env: audio_test_env, timeout: 120)

audio_recording_comp_smoke_test = executable('audio_recording_comp_smoke_test', ['tests/audio_recording_comp_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-recording-comp-smoke', audio_recording_comp_smoke_test, env: audio_test_env, timeout: 120)

audio_recording_review_smoke_test = executable('audio_recording_review_smoke_test', ['tests/audio_recording_review_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-recording-review-smoke', audio_recording_review_smoke_test, env: audio_test_env, timeout: 120)

audio_recording_worker_smoke_test = executable('audio_recording_worker_smoke_test', ['tests/audio_recording_worker_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-recording-worker-smoke', audio_recording_worker_smoke_test, env: audio_test_env, timeout: 120)

audio_recording_ui_smoke_test = executable('audio_recording_ui_smoke_test', ['tests/audio_recording_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-recording-ui-smoke', audio_recording_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_recording_audition_smoke_test = executable('audio_recording_audition_smoke_test', ['tests/audio_recording_audition_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-recording-audition-smoke', audio_recording_audition_smoke_test, env: audio_test_env, timeout: 180)

audio_recording_cli_smoke_test = executable('audio_recording_cli_smoke_test', ['tests/audio_recording_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-recording-cli-smoke', audio_recording_cli_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 120)

audio_take_cli_smoke_test = executable('audio_take_cli_smoke_test', ['tests/audio_take_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-take-cli-smoke', audio_take_cli_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 180)

audio_take_ui_smoke_test = executable('audio_take_ui_smoke_test', ['tests/audio_take_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-take-ui-smoke', audio_take_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_session_transport_smoke_test = executable('audio_session_transport_smoke_test', ['tests/audio_session_transport_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-session-transport-smoke', audio_session_transport_smoke_test, env: audio_test_env, timeout: 120)

audio_playback_loop_smoke_test = executable('audio_playback_loop_smoke_test', ['tests/audio_playback_loop_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-playback-loop-smoke', audio_playback_loop_smoke_test, env: audio_test_env, timeout: 180)

audio_session_playback_smoke_test = executable('audio_session_playback_smoke_test', ['tests/audio_session_playback_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-session-playback-smoke', audio_session_playback_smoke_test, env: audio_test_env, timeout: 120)

audio_session_smoke_test = executable('audio_session_smoke_test', ['tests/audio_session_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-session-smoke', audio_session_smoke_test, env: audio_test_env, timeout: 120)

audio_routing_smoke_test = executable('audio_routing_smoke_test', ['tests/audio_routing_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-routing-smoke', audio_routing_smoke_test, env: audio_test_env, timeout: 120)

audio_stems_smoke_test = executable('audio_stems_smoke_test', ['tests/audio_stems_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-stems-smoke', audio_stems_smoke_test, env: audio_test_env, timeout: 120)

audio_latency_smoke_test = executable('audio_latency_smoke_test', ['tests/audio_latency_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-latency-smoke', audio_latency_smoke_test, env: audio_test_env, timeout: 120)

audio_live_smoke_test = executable('audio_live_smoke_test', ['tests/audio_live_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-live-smoke', audio_live_smoke_test, env: audio_test_env, timeout: 180)

audio_duplex_smoke_test = executable('audio_duplex_smoke_test', ['tests/audio_duplex_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-duplex-smoke', audio_duplex_smoke_test, env: audio_test_env, timeout: 180)

audio_duplex_meter_smoke_test = executable('audio_duplex_meter_smoke_test', ['tests/audio_duplex_meter_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-duplex-meter-smoke', audio_duplex_meter_smoke_test, env: audio_test_env, timeout: 120)

audio_duplex_loop_smoke_test = executable('audio_duplex_loop_smoke_test', ['tests/audio_duplex_loop_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-duplex-loop-smoke', audio_duplex_loop_smoke_test, env: audio_test_env, timeout: 180)

audio_duplex_queue_smoke_test = executable('audio_duplex_queue_smoke_test', ['tests/audio_duplex_queue_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-duplex-queue-smoke', audio_duplex_queue_smoke_test, env: audio_test_env, timeout: 180)

audio_duplex_device_smoke_test = executable('audio_duplex_device_smoke_test',
  ['tests/audio_duplex_device_smoke_test.cpp', 'app/audio_duplex_device.cpp'],
  include_directories: [vibestudio_includes, pa_common_headers],
  dependencies: [vibestudio_core_dep, audio_duplex_dep], install: false)
test('audio-duplex-device-smoke', audio_duplex_device_smoke_test, env: audio_test_env, timeout: 120)

audio_meter_smoke_test = executable('audio_meter_smoke_test', ['tests/audio_meter_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-meter-smoke', audio_meter_smoke_test, env: audio_test_env, timeout: 180)

audio_meter_cli_smoke_test = executable('audio_meter_cli_smoke_test', ['tests/audio_meter_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-meter-cli-smoke', audio_meter_cli_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 180)

audio_meter_ui_smoke_test = executable('audio_meter_ui_smoke_test', ['tests/audio_meter_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-meter-ui-smoke', audio_meter_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_media_smoke_test = executable('audio_media_smoke_test', ['tests/audio_media_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-media-smoke', audio_media_smoke_test, env: audio_test_env, timeout: 180)

audio_media_cli_smoke_test = executable('audio_media_cli_smoke_test', ['tests/audio_media_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-media-cli-smoke', audio_media_cli_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 180)

audio_media_ui_smoke_test = executable('audio_media_ui_smoke_test', ['tests/audio_media_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-media-ui-smoke', audio_media_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_range_smoke_test = executable('audio_range_smoke_test', ['tests/audio_range_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-range-smoke', audio_range_smoke_test, env: audio_test_env, timeout: 180)

audio_range_cli_smoke_test = executable('audio_range_cli_smoke_test', ['tests/audio_range_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-range-cli-smoke', audio_range_cli_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 180)

audio_range_ui_smoke_test = executable('audio_range_ui_smoke_test', ['tests/audio_range_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-range-ui-smoke', audio_range_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_arrangement_smoke_test = executable('audio_arrangement_smoke_test', ['tests/audio_arrangement_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-arrangement-smoke', audio_arrangement_smoke_test, env: audio_test_env, timeout: 180)

audio_arrangement_cli_smoke_test = executable('audio_arrangement_cli_smoke_test', ['tests/audio_arrangement_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-arrangement-cli-smoke', audio_arrangement_cli_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 180)

audio_arrangement_ui_smoke_test = executable('audio_arrangement_ui_smoke_test', ['tests/audio_arrangement_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-arrangement-ui-smoke', audio_arrangement_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_tempo_smoke_test = executable('audio_tempo_smoke_test', ['tests/audio_tempo_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-tempo-smoke', audio_tempo_smoke_test, env: audio_test_env, timeout: 120)

# Keep musical arithmetic executable without linking the GUI or unrelated core
# modules, including in device-disabled portability builds.
audio_tempo_timing_smoke_test = executable('audio_tempo_timing_smoke_test',
  ['tests/audio_tempo_timing_smoke_test.cpp', 'core/audio_tempo.cpp'],
  include_directories: vibestudio_includes, dependencies: [qt6_core_dep], install: false)
test('audio-tempo-timing-smoke', audio_tempo_timing_smoke_test, env: audio_test_env, timeout: 120)

audio_tempo_cli_smoke_test = executable('audio_tempo_cli_smoke_test', ['tests/audio_tempo_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-tempo-cli-smoke', audio_tempo_cli_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 120)

audio_tempo_ui_smoke_test = executable('audio_tempo_ui_smoke_test', ['tests/audio_tempo_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-tempo-ui-smoke', audio_tempo_ui_smoke_test, env: audio_test_env, timeout: 120)

audio_stems_ui_smoke_test = executable('audio_stems_ui_smoke_test', ['tests/audio_stems_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-stems-ui-smoke', audio_stems_ui_smoke_test, env: audio_test_env, timeout: 120)

audio_effects_smoke_test = executable('audio_effects_smoke_test', ['tests/audio_effects_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-effects-smoke', audio_effects_smoke_test, env: audio_test_env, timeout: 120)

audio_modulation_smoke_test = executable('audio_modulation_smoke_test', ['tests/audio_modulation_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-modulation-smoke', audio_modulation_smoke_test, env: audio_test_env, timeout: 120)

audio_effect_automation_smoke_test = executable('audio_effect_automation_smoke_test', ['tests/audio_effect_automation_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-effect-automation-smoke', audio_effect_automation_smoke_test, env: audio_test_env, timeout: 120)

audio_effect_preset_smoke_test = executable('audio_effect_preset_smoke_test', ['tests/audio_effect_preset_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-effect-preset-smoke', audio_effect_preset_smoke_test, env: audio_test_env, timeout: 120)

audio_routing_ui_smoke_test = executable('audio_routing_ui_smoke_test', ['tests/audio_routing_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-routing-ui-smoke', audio_routing_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_effects_ui_smoke_test = executable('audio_effects_ui_smoke_test', ['tests/audio_effects_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-effects-ui-smoke', audio_effects_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_automation_ui_smoke_test = executable('audio_automation_ui_smoke_test', ['tests/audio_automation_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-automation-ui-smoke', audio_automation_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_session_cli_smoke_test = executable('audio_session_cli_smoke_test', ['tests/audio_session_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-session-cli-smoke', audio_session_cli_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 180)

audio_session_ui_smoke_test = executable('audio_session_ui_smoke_test', ['tests/audio_session_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-session-ui-smoke', audio_session_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_session_recovery_smoke_test = executable('audio_session_recovery_smoke_test', ['tests/audio_session_recovery_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('audio-session-recovery-smoke', audio_session_recovery_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 180)

audio_session_recovery_ui_smoke_test = executable('audio_session_recovery_ui_smoke_test', ['tests/audio_session_recovery_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('audio-session-recovery-ui-smoke', audio_session_recovery_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_project_smoke_test = executable(
  'audio_project_smoke_test',
  ['tests/audio_project_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-project-smoke', audio_project_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 90)

audio_recovery_smoke_test = executable(
  'audio_recovery_smoke_test',
  ['tests/audio_recovery_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-recovery-smoke', audio_recovery_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 90)

audio_markers_smoke_test = executable(
  'audio_markers_smoke_test',
  ['tests/audio_markers_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-markers-smoke', audio_markers_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 90)

audio_decode_smoke_test = executable(
  'audio_decode_smoke_test',
  ['tests/audio_decode_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-decode-smoke', audio_decode_smoke_test, args: [meson.current_source_dir() / 'tests/fixtures/audio_decode', vibestudio], env: audio_test_env, timeout: 90)

audio_resample_smoke_test = executable(
  'audio_resample_smoke_test',
  ['tests/audio_resample_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-resample-smoke', audio_resample_smoke_test, timeout: 90)

audio_export_smoke_test = executable(
  'audio_export_smoke_test',
  ['tests/audio_export_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-export-smoke', audio_export_smoke_test, env: audio_test_env, timeout: 90)

audio_delivery_smoke_test = executable(
  'audio_delivery_smoke_test',
  ['tests/audio_delivery_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-delivery-smoke', audio_delivery_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 90)

audio_level_smoke_test = executable(
  'audio_level_smoke_test',
  ['tests/audio_level_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-level-smoke', audio_level_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 90)

audio_analysis_smoke_test = executable(
  'audio_analysis_smoke_test',
  ['tests/audio_analysis_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('audio-analysis-smoke', audio_analysis_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 90)

audio_loudness_smoke_test = executable(
  'audio_loudness_smoke_test', 'tests/audio_loudness_smoke_test.cpp',
  include_directories: vibestudio_includes, dependencies: vibestudio_core_dep,
)
test('audio-loudness-smoke', audio_loudness_smoke_test, args: [vibestudio], env: audio_test_env, timeout: 90)

# Run explicitly against staged application, Qt, and SDK catalog directories.
qt_runtime_catalog_probe = executable(
  'qt_runtime_catalog_probe', 'tests/qt_runtime_catalog_probe.cpp',
  dependencies: qt6_core_dep, install: false,
)

audio_transport_smoke_test = executable(
  'audio_transport_smoke_test',
  ['tests/audio_transport_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('audio-transport-smoke', audio_transport_smoke_test, env: audio_test_env, timeout: 90)

audio_browser_smoke_test = executable(
  'audio_browser_smoke_test',
  ['tests/audio_browser_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('audio-browser-smoke', audio_browser_smoke_test, env: audio_test_env, timeout: 180)

audio_editor_ui_smoke_test = executable(
  'audio_editor_ui_smoke_test',
  ['tests/audio_editor_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('audio-editor-ui-smoke', audio_editor_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_history_smoke_test = executable(
  'audio_history_smoke_test',
  ['tests/audio_history_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('audio-history-smoke', audio_history_smoke_test, env: audio_test_env, timeout: 180)

audio_level_ui_smoke_test = executable(
  'audio_level_ui_smoke_test',
  ['tests/audio_level_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('audio-level-ui-smoke', audio_level_ui_smoke_test, env: audio_test_env, timeout: 120)

audio_startup_ui_smoke_test = executable(
  'audio_startup_ui_smoke_test',
  ['tests/audio_startup_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('audio-startup-ui-smoke', audio_startup_ui_smoke_test, env: audio_test_env, timeout: 180)

audio_import_ui_smoke_test = executable(
  'audio_import_ui_smoke_test',
  ['tests/audio_import_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('audio-import-ui-smoke', audio_import_ui_smoke_test, env: audio_test_env, timeout: 90)

package_compare_smoke_test = executable(
  'package_compare_smoke_test',
  ['tests/package_compare_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('package-compare-smoke', package_compare_smoke_test)

package_validation_smoke_test = executable(
  'package_validation_smoke_test',
  ['tests/package_validation_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-validation-smoke', package_validation_smoke_test)

package_publication_smoke_test = executable(
  'package_publication_smoke_test',
  ['tests/package_publication_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-publication-smoke', package_publication_smoke_test, timeout: 90)

package_publication_inventory_smoke_test = executable(
  'package_publication_inventory_smoke_test', ['tests/package_publication_inventory_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-publication-inventory-smoke', package_publication_inventory_smoke_test, timeout: 90)

package_wad_groups_smoke_test = executable(
  'package_wad_groups_smoke_test', ['tests/package_wad_groups_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-wad-groups-smoke', package_wad_groups_smoke_test, timeout: 90)

package_wad_groups_ui_smoke_test = executable(
  'package_wad_groups_ui_smoke_test', ['tests/package_wad_groups_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-wad-groups-ui-smoke', package_wad_groups_ui_smoke_test, timeout: 120)

package_wad_groups_cli_smoke_test = executable(
  'package_wad_groups_cli_smoke_test', ['tests/package_wad_groups_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-wad-groups-cli-smoke', package_wad_groups_cli_smoke_test, args: [vibestudio], timeout: 90)

package_subset_smoke_test = executable(
  'package_subset_smoke_test', ['tests/package_subset_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-subset-smoke', package_subset_smoke_test, timeout: 90)

package_subset_cli_smoke_test = executable(
  'package_subset_cli_smoke_test', ['tests/package_subset_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-subset-cli-smoke', package_subset_cli_smoke_test, args: [vibestudio.full_path()], depends: vibestudio, timeout: 90)

package_subset_ui_smoke_test = executable(
  'package_subset_ui_smoke_test', ['tests/package_subset_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-subset-ui-smoke', package_subset_ui_smoke_test, timeout: 90)

package_publication_cli_smoke_test = executable(
  'package_publication_cli_smoke_test', ['tests/package_publication_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-publication-cli-smoke', package_publication_cli_smoke_test, args: [vibestudio], timeout: 90)

package_publication_ui_smoke_test = executable(
  'package_publication_ui_smoke_test', ['tests/package_publication_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-publication-ui-smoke', package_publication_ui_smoke_test, timeout: 90)

package_import_smoke_test = executable(
  'package_import_smoke_test',
  ['tests/package_import_smoke_test.cpp'],
  dependencies: vibestudio_core_dep,
  install: false,
)
test('package-import-smoke', package_import_smoke_test, timeout: 90)

package_draft_storage_cli_smoke_test = executable(
  'package_draft_storage_cli_smoke_test', ['tests/package_draft_storage_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-draft-storage-cli-smoke', package_draft_storage_cli_smoke_test, args: [vibestudio], timeout: 90)

package_draft_storage_ui_smoke_test = executable(
  'package_draft_storage_ui_smoke_test', ['tests/package_draft_storage_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-draft-storage-ui-smoke', package_draft_storage_ui_smoke_test, timeout: 90)

package_draft_quota_smoke_test = executable(
  'package_draft_quota_smoke_test',
  ['tests/package_draft_quota_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-draft-quota-smoke', package_draft_quota_smoke_test, timeout: 90)

package_draft_storage_smoke_test = executable(
  'package_draft_storage_smoke_test',
  ['tests/package_draft_storage_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-draft-storage-smoke', package_draft_storage_smoke_test, timeout: 90)

package_draft_access_smoke_test = executable(
  'package_draft_access_smoke_test',
  ['tests/package_draft_access_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-draft-access-smoke', package_draft_access_smoke_test, timeout: 90)

package_import_storage_smoke_test = executable(
  'package_import_storage_smoke_test',
  ['tests/package_import_storage_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-import-storage-smoke', package_import_storage_smoke_test, args: [vibestudio], timeout: 90)

package_import_locks_smoke_test = executable(
  'package_import_locks_smoke_test', ['tests/package_import_locks_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('package-import-locks-smoke', package_import_locks_smoke_test, args: [vibestudio], timeout: 90)

package_import_ui_smoke_test = executable(
  'package_import_ui_smoke_test',
  ['tests/package_import_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('package-import-ui-smoke', package_import_ui_smoke_test, timeout: 90)

package_content_smoke_test = executable(
  'package_content_smoke_test',
  ['tests/package_content_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-content-smoke', package_content_smoke_test, timeout: 90)

package_document_smoke_test = executable(
  'package_document_smoke_test',
  ['tests/package_document_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-document-smoke', package_document_smoke_test, timeout: 90)

package_document_cli_smoke_test = executable(
  'package_document_cli_smoke_test', ['tests/package_document_cli_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-document-cli-smoke', package_document_cli_smoke_test, args: [vibestudio], timeout: 90)

package_document_ui_smoke_test = executable(
  'package_document_ui_smoke_test', ['tests/package_document_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-document-ui-smoke', package_document_ui_smoke_test, timeout: 120)

package_recovery_ui_smoke_test = executable(
  'package_recovery_ui_smoke_test',
  ['tests/package_recovery_ui_smoke_test.cpp'],
  dependencies: vibestudio_app_dep,
  install: false,
)
test('package-recovery-ui-smoke', package_recovery_ui_smoke_test, timeout: 180)

package_recovery_writer_smoke_test = executable(
  'package_recovery_writer_smoke_test',
  ['tests/package_recovery_writer_smoke_test.cpp', 'app/package_recovery_writer.cpp'],
  dependencies: vibestudio_core_dep,
  install: false,
)
test('package-recovery-writer-smoke', package_recovery_writer_smoke_test, timeout: 90)
package_recovery_cli_smoke_test = executable(
  'package_recovery_cli_smoke_test',
  ['tests/package_recovery_cli_smoke_test.cpp'],
  dependencies: vibestudio_core_dep,
  install: false,
)
test('package-recovery-cli-smoke', package_recovery_cli_smoke_test, args: [vibestudio], timeout: 90)

package_storage_smoke_test = executable(
  'package_storage_smoke_test',
  ['tests/package_storage_smoke_test.cpp'],
  dependencies: vibestudio_core_dep,
  install: false,
)
test('package-storage-smoke', package_storage_smoke_test, timeout: 90)

package_recovery_smoke_test = executable(
  'package_recovery_smoke_test',
  ['tests/package_recovery_smoke_test.cpp'],
  dependencies: vibestudio_core_dep,
  install: false,
)
test('package-recovery-smoke', package_recovery_smoke_test, timeout: 90)

package_draft_smoke_test = executable(
  'package_draft_smoke_test',
  ['tests/package_draft_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-draft-smoke', package_draft_smoke_test, timeout: 90)

package_damaged_history_smoke_test = executable('package_damaged_history_smoke_test',
  ['tests/package_damaged_history_smoke_test.cpp'], include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep], install: false)
test('package-damaged-history-smoke', package_damaged_history_smoke_test, timeout: 90)

package_copy_store_smoke_test = executable('package_copy_store_smoke_test', ['tests/package_copy_store_smoke_test.cpp'],
  dependencies: vibestudio_core_dep)
test('package-copy-store-smoke', package_copy_store_smoke_test, timeout: 90)

package_copy_quota_smoke_test = executable('package_copy_quota_smoke_test', ['tests/package_copy_quota_smoke_test.cpp'],
  dependencies: vibestudio_core_dep, install: false)
test('package-copy-quota-smoke', package_copy_quota_smoke_test, timeout: 90)

package_copy_sessions_cli_smoke_test = executable('package_copy_sessions_cli_smoke_test', ['tests/package_copy_sessions_cli_smoke_test.cpp'],
  dependencies: vibestudio_core_dep, install: false)
test('package-copy-sessions-cli-smoke', package_copy_sessions_cli_smoke_test, args: [vibestudio], timeout: 90)

package_copy_sessions_ui_smoke_test = executable('package_copy_sessions_ui_smoke_test', ['tests/package_copy_sessions_ui_smoke_test.cpp'],
  dependencies: vibestudio_app_dep, install: false)
test('package-copy-sessions-ui-smoke', package_copy_sessions_ui_smoke_test, timeout: 90)

package_copy_budget_smoke_test = executable('package_copy_budget_smoke_test', ['tests/package_copy_budget_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-copy-budget-smoke', package_copy_budget_smoke_test, args: [vibestudio], timeout: 90)

package_copy_budget_ui_smoke_test = executable('package_copy_budget_ui_smoke_test', ['tests/package_copy_budget_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-copy-budget-ui-smoke', package_copy_budget_ui_smoke_test, timeout: 60)

package_copy_scale_probe = executable('package_copy_scale_probe', ['tests/package_copy_scale_probe.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)

package_copy_smoke_test = executable('package_copy_smoke_test', ['tests/package_copy_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false)
test('package-copy-smoke', package_copy_smoke_test, timeout: 90)

package_copy_ui_smoke_test = executable('package_copy_ui_smoke_test', ['tests/package_copy_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-copy-ui-smoke', package_copy_ui_smoke_test, timeout: 180)

package_extraction_smoke_test = executable(
  'package_extraction_smoke_test',
  ['tests/package_extraction_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-extraction-smoke', package_extraction_smoke_test, timeout: 90)

package_protection_ui_smoke_test = executable('package_protection_ui_smoke_test', ['tests/package_protection_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false)
test('package-protection-ui-smoke', package_protection_ui_smoke_test, timeout: 90)

package_operation_ui_smoke_test = executable(
  'package_operation_ui_smoke_test',
  ['tests/package_operation_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
# Covers modal workers, scaled/translated widgets, shell handoffs and many CLI
# subprocesses. Individual operation checks retain their own bounded waits.
test('package-operation-ui-smoke', package_operation_ui_smoke_test, args: [vibestudio], timeout: 180)

package_handoff_ui_smoke_test = executable(
  'package_handoff_ui_smoke_test',
  ['tests/package_handoff_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('package-handoff-ui-smoke', package_handoff_ui_smoke_test, timeout: 120)

package_browser_model_smoke_test = executable(
  'package_browser_model_smoke_test',
  ['tests/package_browser_model_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)
test('package-browser-model-smoke', package_browser_model_smoke_test, timeout: 90)

package_entry_view_smoke_test = executable(
  'package_entry_view_smoke_test',
  ['tests/package_entry_view_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('package-entry-view-smoke', package_entry_view_smoke_test, timeout: 120)

package_staging_view_smoke_test = executable(
  'package_staging_view_smoke_test',
  ['tests/package_staging_view_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('package-staging-view-smoke', package_staging_view_smoke_test, timeout: 90)

package_folder_view_smoke_test = executable(
  'package_folder_view_smoke_test',
  ['tests/package_folder_view_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('package-folder-view-smoke', package_folder_view_smoke_test, timeout: 120)

package_browser_ui_smoke_test = executable(
  'package_browser_ui_smoke_test',
  ['tests/package_browser_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('package-browser-ui-smoke', package_browser_ui_smoke_test, timeout: 90)

package_occurrence_ui_smoke_test = executable(
  'package_occurrence_ui_smoke_test',
  ['tests/package_occurrence_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('package-occurrence-ui-smoke', package_occurrence_ui_smoke_test, args: [vibestudio], timeout: 90)

parser_fuzz_smoke_test = executable(
  'parser_fuzz_smoke_test',
  ['tests/parser_fuzz_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('parser-fuzz-smoke', parser_fuzz_smoke_test)

corrupt_fixture_smoke_test = executable(
  'corrupt_fixture_smoke_test',
  ['tests/corrupt_fixture_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('corrupt-fixture-smoke', corrupt_fixture_smoke_test)

studio_charts_smoke_test = executable(
  'studio_charts_smoke_test',
  ['tests/studio_charts_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_app_dep],
  install: false,
)
test('studio-charts-smoke', studio_charts_smoke_test, timeout: 30)

package_progress_smoke_test = executable('package_progress_smoke_test',
  ['tests/package_progress_smoke_test.cpp', 'app/package_progress.cpp'],
  include_directories: vibestudio_includes, dependencies: [qt6_dep], install: false)
test('package-progress-smoke', package_progress_smoke_test, timeout: 30)

progress_theme_smoke_test = executable('progress_theme_smoke_test',
  ['tests/progress_theme_smoke_test.cpp', 'app/studio_theme.cpp', 'app/studio_icons.cpp'],
  include_directories: vibestudio_includes, dependencies: [qt6_dep, vibestudio_core_dep], install: false)
test('progress-theme-smoke', progress_theme_smoke_test, timeout: 90)

studio_theme_smoke_test = executable(
  'studio_theme_smoke_test',
  ['tests/studio_theme_smoke_test.cpp', 'app/studio_theme.cpp', 'app/studio_icons.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, vibestudio_core_dep],
  install: false,
)

test(
  'studio-theme-smoke',
  studio_theme_smoke_test,
  env: ui_primitives_test_env,
)

studio_runtime_smoke_test = executable(
  'studio_runtime_smoke_test',
  ['tests/studio_runtime_smoke_test.cpp', 'app/studio_runtime.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, vibestudio_core_dep],
  install: false,
)

test(
  'studio-runtime-smoke',
  studio_runtime_smoke_test,
  env: ui_primitives_test_env,
)

# Builds the real window from the app library and drives it with Qt Test:
# fixtures open, rows get selected, keys get pressed, and the checks assert
# that the shell responds. It forces the offscreen platform itself.
if qt6_test_dep.found()
  shell_interaction_smoke_test = executable(
    'shell_interaction_smoke_test',
    ['tests/shell_interaction_smoke_test.cpp'],
    include_directories: vibestudio_includes,
    dependencies: [vibestudio_app_dep, qt6_test_dep],
    install: false,
  )

  # The whole studio, driven check by check through focus and window
  # activation: run alone, since sharing the CPU with the other tests made it
  # three times slower. About 70 seconds on its own in a debug build.
  test(
    'shell-interaction-smoke',
    shell_interaction_smoke_test,
    is_parallel: false,
    timeout: 600,
  )
endif

workspace_smoke_test = executable(
  'workspace_smoke_test', ['tests/workspace_smoke_test.cpp', 'cli/workspace.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('workspace-smoke', workspace_smoke_test)
extra_image_smoke_test = executable(
  'extra_image_smoke_test', ['tests/extra_image_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_core_dep], install: false,
)
test('extra-image-smoke', extra_image_smoke_test)
workspace_ui_smoke_test = executable(
  'workspace_ui_smoke_test', ['tests/workspace_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('workspace-ui-smoke', workspace_ui_smoke_test, env: ['QT_QPA_PLATFORM=offscreen'], timeout: 120)

asset_workbench_ui_smoke_test = executable(
  'asset_workbench_ui_smoke_test', ['tests/asset_workbench_ui_smoke_test.cpp'],
  include_directories: vibestudio_includes, dependencies: [vibestudio_app_dep], install: false,
)
test('asset-workbench-ui-smoke', asset_workbench_ui_smoke_test, env: audio_test_env, timeout: 240, is_parallel: false)
