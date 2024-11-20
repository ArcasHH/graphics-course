#ifndef UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED
#define UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED

// NOTE: .glsl extension is used for helper files with shader code

vec3 decode_normal(uint a_data)
{
  const uint x = ((a_data & 0x000000FFu) >> 0);
  const uint y = ((a_data & 0x0000FF00u) >> 8);
  const uint z = ((a_data & 0x00FF0000u) >> 16);
  ivec3 i_enc = ivec3(x, y, z);
  vec3 enc = vec3((i_enc + 128) % 256 - 128);
  
  return max(enc / 127.0, -1.0);
}

#endif // UNPACK_ATTRIBUTES_BAKED_GLSL_INCLUDED
