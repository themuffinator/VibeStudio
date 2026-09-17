// VibeStudio sample shader script.
//
// Hand-authored placeholder shaders for the Quake III sample project. No
// content is taken from a commercial game; these exist so shader parsing,
// stage editing, and the map texture audit have something real to resolve
// against.

textures/vibestudio/sample
{
  surfaceparm nolightmap
}

// Referenced by maps/sample_q3.map. A shader declared here counts as resolved
// even though no image file sits beside it, which is exactly how a Quake III
// engine treats it.
textures/sample/wall
{
  qer_editorimage textures/sample/wall
  surfaceparm nolightmap
  {
    map $lightmap
    rgbGen identity
  }
  {
    map textures/sample/wall
    blendFunc GL_DST_COLOR GL_ZERO
    rgbGen identity
  }
}

textures/sample/arch
{
  qer_editorimage textures/sample/arch
  cull none
  {
    map textures/sample/arch
    blendFunc GL_ONE GL_ZERO
    rgbGen identityLighting
    tcMod scroll 0 0.05
  }
}
