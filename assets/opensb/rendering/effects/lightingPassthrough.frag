#version 140

// Slice-1 spike: identity. Sample the uploaded input lightmap and write it unchanged into
// the float target. If the screen is visually identical with /lighting gpu on, the whole
// upload -> off-screen float pass -> consume-as-lightMap chain is proven on the GPU.
// Slices 2-4 replace this body with the real spread / point / compose passes.
uniform sampler2D inputTexture;
uniform vec2 inputTextureSize;

in vec2 fragTexCoord;

out vec4 fragColor;

void main() {
  fragColor = texture(inputTexture, fragTexCoord);
}
